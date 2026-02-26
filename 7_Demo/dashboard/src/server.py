#!/usr/bin/env python3
"""
Dashboard Server — Flask + SocketIO backend
=============================================

Reads telemetry from VSDSquadron ULTRA or simulated data,
pushes to the web frontend via WebSockets.

Usage:
  python3 dashboard/src/server.py --sim          # Simulated data
  python3 dashboard/src/server.py --port /dev/cu.usbserial-110  # Live board

  python3 dashboard/src/server.py --twin-bridge   # Digital twin → board → dashboard
"""

import sys
import struct
import os
import re
import time
import json
import argparse
import threading

# Add parent directories for imports
sys.path.insert(0, os.path.dirname(__file__))

from flask import Flask, render_template, send_from_directory, jsonify
from flask_socketio import SocketIO
from sim_data_generator import generate_full_demo, SensorReading

try:
    from serial_reader import SerialReader, find_board_port, active_categories, BoardReading
    HAS_SERIAL = True
except ImportError:
    HAS_SERIAL = False

# ---------------------------------------------------------------------------
# Flask app setup
# ---------------------------------------------------------------------------
WEB_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..', 'website'))
app = Flask(__name__, template_folder=WEB_DIR)
app.config['SECRET_KEY'] = 'ev-battery-intel'
socketio = SocketIO(app, cors_allowed_origins="*")

# Global state
data_thread = None
thread_lock = threading.Lock()
is_running = False
mode = "sim"
serial_port = None
latest_reading = None
board_connected = False
port_locked = False
rescan_requested = False
stream_epoch = 0
twin_source_url = "http://127.0.0.1:5001"
TWIN_BRIDGE_LOOP_SLEEP_S = 0.05  # 20Hz bridge loop
BRIDGE_AWAITING_EMIT_MIN_INTERVAL_S = 0.25
BRIDGE_AWAITING_AFTER_SEND_S = 0.06


# ---------------------------------------------------------------------------
# Text line parser — parses [TEL] lines from firmware UART output
# ---------------------------------------------------------------------------
TEL_PATTERN = re.compile(
    r'\[TEL\]\s*t=(\d+)ms\s+V=([\d.\-]+)\s+I=([\d.\-]+)\s+'
    r'T=\[([\d.\-]+),([\d.\-]+),([\d.\-]+),([\d.\-]+)\]\s+'
    r'gas=([\d.\-]+)\s+dP=([\d.\-]+)\s+'
    r'state=(\w+)\s+cats=(\d+)'
)

STATE_PATTERN = re.compile(
    r'\[STATE\]\s+(\w+)\s*->\s*(\w+)\s*\(cats=(\d+)\)'
)

STATE_TO_NUM = {"NORMAL": 0, "WARNING": 1, "CRITICAL": 2, "EMERGENCY": 3}
SERIAL_TEXT_FALLBACK = False


def parse_tel_line(line):
    """Parse a [TEL] text line into a dict for the frontend."""
    m = TEL_PATTERN.search(line)
    if not m:
        return None

    state_name = m.group(10)
    return {
        "timestamp_ms": int(m.group(1)),
        "voltage_v": float(m.group(2)),
        "current_a": float(m.group(3)),
        "temp_cells": [
            float(m.group(4)), float(m.group(5)),
            float(m.group(6)), float(m.group(7))
        ],
        "gas_ratio": float(m.group(8)),
        "pressure_delta": float(m.group(9)),
        "system_state": state_name,
        "state_num": STATE_TO_NUM.get(state_name, 0),
        "anomaly_count": int(m.group(11)),
    }


def reading_to_dict(r):
    """Convert SensorReading to dict for JSON."""
    return {
        "timestamp_ms": r.timestamp_ms,
        "voltage_v": r.voltage_v,
        "current_a": r.current_a,
        "temp_cells": [
            r.temp_cells_c[0] if hasattr(r, 'temp_cells_c') else getattr(r, 'temp_cell1_c', 25),
            r.temp_cells_c[1] if hasattr(r, 'temp_cells_c') else getattr(r, 'temp_cell2_c', 25),
            r.temp_cells_c[2] if hasattr(r, 'temp_cells_c') else getattr(r, 'temp_cell3_c', 25),
            r.temp_cells_c[3] if hasattr(r, 'temp_cells_c') else getattr(r, 'temp_cell4_c', 25),
        ],
        "temp_ambient": getattr(r, 'temp_ambient_c', 25.0),
        "gas_ratio": r.gas_ratio,
        "pressure_delta": getattr(r, 'pressure_delta_hpa', 0),
        "swelling_pct": getattr(r, 'swelling_pct', 0),
        "dt_dt_max": getattr(r, 'dt_dt_max', 0),
        "system_state": r.system_state,
        "state_num": STATE_TO_NUM.get(r.system_state, 0),
        "anomaly_count": len(getattr(r, 'active_categories', [])),
        "categories": getattr(r, 'active_categories', []),
    }


def board_reading_to_dict(r):
    """Convert BoardReading to dict for JSON."""
    cats = active_categories(r.anomaly_mask) if HAS_SERIAL else []
    return {
        "timestamp_ms": r.timestamp_ms,
        "voltage_v": r.voltage_v,
        "current_a": r.current_a,
        "temp_cells": [r.temp_cell1_c, r.temp_cell2_c, r.temp_cell3_c, r.temp_cell4_c],
        "temp_ambient": getattr(r, 'temp_ambient_c', 25.0),
        "gas_ratio": r.gas_ratio,
        "pressure_delta": r.pressure_delta_hpa,
        "swelling_pct": r.swelling_pct,
        "dt_dt_max": getattr(r, 'dt_dt_max', 0.0),
        "system_state": r.system_state,
        "state_num": STATE_TO_NUM.get(r.system_state, 0),
        "anomaly_count": r.anomaly_count,
        "categories": cats,
        "emergency_direct": getattr(r, 'emergency_direct', False),
    }


def emit_board_status(status, port=None, message=None):
    payload = {"status": status, "port": port}
    if message:
        payload["message"] = message
    socketio.emit("board_status", payload)


def emit_bridge_status(state, message=None, latency_ms=None, wait_ms=None, input_seq=None):
    payload = {
        "state": state,
        "timestamp_ms": int(time.time() * 1000),
    }
    if message:
        payload["message"] = message
    if latency_ms is not None:
        payload["latency_ms"] = float(latency_ms)
    if wait_ms is not None:
        payload["wait_ms"] = float(wait_ms)
    if input_seq is not None:
        payload["input_seq"] = int(input_seq)
    socketio.emit("bridge_status", payload)


class RescanRequested(Exception):
    """Internal signal to force serial reconnect."""


def _start_data_thread_locked():
    """Start the active streaming thread. Caller must hold thread_lock."""
    global data_thread, is_running
    is_running = True
    if mode == "sim":
        data_thread = socketio.start_background_task(sim_data_thread, stream_epoch)
    elif mode == "twin-bridge":
        data_thread = socketio.start_background_task(twin_bridge_data_thread, stream_epoch)
    else:
        data_thread = socketio.start_background_task(serial_data_thread, stream_epoch)


def restart_data_pipeline():
    """Stop current streamer and start one that matches the current mode."""
    global data_thread, is_running, board_connected, rescan_requested, stream_epoch

    with thread_lock:
        # Ask current thread to exit, then start a fresh one for the new mode.
        if data_thread is not None and data_thread.is_alive():
            is_running = False
            stream_epoch += 1
            time.sleep(0.2)

        board_connected = False
        rescan_requested = False
        if data_thread is None or not data_thread.is_alive():
            stream_epoch += 1
        _start_data_thread_locked()


# ---------------------------------------------------------------------------
# Data streaming threads
# ---------------------------------------------------------------------------

def sim_data_thread(expected_epoch):
    """Stream simulated data to the frontend."""
    global latest_reading, is_running
    data = generate_full_demo()
    idx = 0

    while is_running and expected_epoch == stream_epoch:
        r = data[idx]
        d = reading_to_dict(r)
        latest_reading = d
        socketio.emit('telemetry', d)

        # Also emit state transitions
        if idx > 0:
            prev = data[idx - 1]
            if r.system_state != prev.system_state:
                socketio.emit('state_change', {
                    'from': prev.system_state,
                    'to': r.system_state,
                    'timestamp_ms': r.timestamp_ms,
                })

        idx = (idx + 1) % len(data)
        if idx == 0:
            socketio.emit('scenario_restart', {})
        socketio.sleep(0.5)  # 2Hz


def serial_data_thread(expected_epoch):
    """Stream live data from the board to the frontend."""
    global latest_reading, is_running, serial_port, board_connected, rescan_requested

    prev_state = None

    while is_running and expected_epoch == stream_epoch:
        target_port = serial_port if port_locked else None
        if not target_port and HAS_SERIAL:
            target_port = find_board_port()

        if not target_port:
            board_connected = False
            emit_board_status("waiting", None, "No serial port detected")
            socketio.sleep(1.0)
            continue

        reader = SerialReader(target_port)
        try:
            emit_board_status("connecting", target_port)
            reader.open()
            serial_port = target_port
            board_connected = True
            emit_board_status("connected", target_port)

            while is_running and expected_epoch == stream_epoch:
                if rescan_requested:
                    rescan_requested = False
                    raise RescanRequested()

                # Try binary packet first
                reading = reader.read_latest_packet()
                if reading:
                    d = board_reading_to_dict(reading)
                    latest_reading = d
                    socketio.emit('telemetry', d)

                    if prev_state and d['system_state'] != prev_state:
                        socketio.emit('state_change', {
                            'from': prev_state,
                            'to': d['system_state'],
                            'timestamp_ms': d['timestamp_ms'],
                        })
                    prev_state = d['system_state']
                elif SERIAL_TEXT_FALLBACK:
                    # Try text lines
                    line = reader.read_text_line()
                    if line:
                        tel = parse_tel_line(line)
                        if tel:
                            latest_reading = tel
                            socketio.emit('telemetry', tel)

                            if prev_state and tel['system_state'] != prev_state:
                                socketio.emit('state_change', {
                                    'from': prev_state,
                                    'to': tel['system_state'],
                                    'timestamp_ms': tel['timestamp_ms'],
                                })
                            prev_state = tel['system_state']

                        # Forward state change messages
                        sm = STATE_PATTERN.search(line) if line else None
                        if sm:
                            socketio.emit('state_change', {
                                'from': sm.group(1),
                                'to': sm.group(2),
                                'timestamp_ms': latest_reading.get('timestamp_ms', 0) if latest_reading else 0,
                            })

                socketio.sleep(0.05)
        except RescanRequested:
            board_connected = False
            emit_board_status("reconnecting", target_port, "Manual rescan requested")
        except Exception as e:
            board_connected = False
            emit_board_status("disconnected", target_port, str(e))
            socketio.emit('error', {'message': str(e)})
        finally:
            reader.close()

        if not is_running:
            break

        board_connected = False
        emit_board_status("reconnecting", target_port)
        socketio.sleep(1.0)


# ---------------------------------------------------------------------------
# Twin Bridge Mode — Digital Twin → Board → Dashboard
# ---------------------------------------------------------------------------

def encode_twin_input_packet(snapshot):
    """Encode a digital twin snapshot into a 20-byte 0xBB input packet for the board."""
    INPUT_SYNC = 0xBB
    INPUT_LEN = 20

    # Voltage: scale 104S pack to 4S prototype
    pack_v = snapshot.get('pack_voltage', 341.0)
    proto_v = pack_v * 4.0 / 104.0
    voltage_cv = int(proto_v * 100)
    current_ca = int(snapshot.get('pack_current', 0.0) * 100)

    # Temps: use module 1 and 2 NTC readings
    modules = snapshot.get('modules', [])
    if modules:
        m0 = modules[0]
        temp1 = int(m0.get('temp_ntc1', 30.0) * 10)
        temp2 = int(m0.get('temp_ntc2', 30.0) * 10)
        m1 = modules[1] if len(modules) > 1 else m0
        temp3 = int(m1.get('temp_ntc1', 30.0) * 10)
        temp4 = int(m1.get('temp_ntc2', 30.0) * 10)
    else:
        temp1 = temp2 = temp3 = temp4 = 300

    gas1 = snapshot.get('gas_ratio_1', 1.0)
    gas2 = snapshot.get('gas_ratio_2', 1.0)
    gas_cp = int(((gas1 + gas2) / 2.0) * 100)

    p1 = snapshot.get('pressure_delta_1', 0.0)
    p2 = snapshot.get('pressure_delta_2', 0.0)
    press_chpa = int(((p1 + p2) / 2.0) * 100)

    swelling = min(int(modules[0].get('swelling_pct', 0)) if modules else 0, 100)

    payload = struct.pack('<HhhhhhHhB',
        voltage_cv, current_ca,
        temp1, temp2, temp3, temp4,
        gas_cp, press_chpa, swelling)

    frame_no_csum = struct.pack('BB', INPUT_SYNC, INPUT_LEN) + payload
    checksum = 0
    for b in frame_no_csum:
        checksum ^= b
    return frame_no_csum + struct.pack('B', checksum)


def twin_bridge_data_thread(expected_epoch):
    """Bridge: Digital Twin → Serial → Board → Dashboard."""
    global latest_reading, is_running, serial_port, board_connected, twin_source_url
    import requests

    prev_state = None
    bridge_seq = 0
    last_await_emit_at = 0.0

    while is_running and expected_epoch == stream_epoch:
        # Find serial port
        target_port = serial_port if port_locked else None
        if not target_port and HAS_SERIAL:
            target_port = find_board_port()
        if not target_port:
            emit_board_status("waiting", None, "No serial port — connect VSDSquadron")
            emit_bridge_status("idle", "Waiting for board serial port")
            socketio.sleep(1.0)
            continue

        reader = SerialReader(target_port)
        try:
            emit_board_status("connecting", target_port, "Twin-bridge mode")
            reader.open()
            serial_port = target_port
            board_connected = True
            emit_board_status("connected", target_port, "Twin-bridge active")
            emit_bridge_status("idle", "Bridge active")

            while is_running and expected_epoch == stream_epoch:
                input_seq = None
                input_sent_at = None

                # 1. Fetch latest snapshot from digital twin API
                try:
                    resp = requests.get(f"{twin_source_url}/api/status", timeout=1.0)
                    twin_data = resp.json()
                except Exception:
                    twin_data = None

                if twin_data:
                    # 2. Encode as 0xBB input packet and send to board
                    input_pkt = encode_twin_input_packet(twin_data)
                    if reader.ser and reader.ser.is_open:
                        try:
                            reader.ser.write(input_pkt)
                            bridge_seq += 1
                            input_seq = bridge_seq
                            input_sent_at = time.perf_counter()
                        except Exception:
                            pass

                # 3. Read board's 0xAA telemetry output
                reading = reader.read_latest_packet()
                if reading:
                    d = board_reading_to_dict(reading)
                    latency_ms = None
                    if input_sent_at is not None:
                        latency_ms = max(0.0, (time.perf_counter() - input_sent_at) * 1000.0)

                    # 4. Merge twin data (raw + prediction) with board output (detection)
                    d["raw_data"] = None
                    d["intelligent_detection"] = {
                        "system_state": d.get("system_state"),
                        "state_num": d.get("state_num"),
                        "anomaly_count": d.get("anomaly_count"),
                        "categories": d.get("categories", []),
                        "emergency_direct": d.get("emergency_direct", False),
                    }
                    d["thermal_runaway_prediction"] = {}

                    if twin_data:
                        d["raw_data"] = {
                            "pack_voltage": twin_data.get("pack_voltage"),
                            "pack_current": twin_data.get("pack_current"),
                            "pack_soc": twin_data.get("pack_soc"),
                            "modules": twin_data.get("modules", []),
                            "gas_ratio_1": twin_data.get("gas_ratio_1"),
                            "gas_ratio_2": twin_data.get("gas_ratio_2"),
                            "pressure_delta_1": twin_data.get("pressure_delta_1"),
                            "pressure_delta_2": twin_data.get("pressure_delta_2"),
                            "total_channels": twin_data.get("total_channels", 139),
                            "sim_time": twin_data.get("sim_time"),
                        }
                        d["thermal_runaway_prediction"] = twin_data.get("thermal_risk", {})

                    # Backward-compatible aliases
                    d["twin_raw"] = d["raw_data"]
                    d["prediction"] = d["thermal_runaway_prediction"]
                    d["bridge_status"] = {
                        "state": "received",
                        "latency_ms": latency_ms,
                        "input_seq": input_seq,
                    }

                    d['mode'] = 'twin-bridge'
                    latest_reading = d
                    socketio.emit('telemetry', d)

                    if prev_state and d['system_state'] != prev_state:
                        socketio.emit('state_change', {
                            'from': prev_state,
                            'to': d['system_state'],
                            'timestamp_ms': d['timestamp_ms'],
                        })
                    prev_state = d['system_state']
                elif input_sent_at is not None:
                    now = time.perf_counter()
                    wait_ms = max(0.0, (now - input_sent_at) * 1000.0)
                    if (wait_ms / 1000.0) >= BRIDGE_AWAITING_AFTER_SEND_S and \
                       (now - last_await_emit_at) >= BRIDGE_AWAITING_EMIT_MIN_INTERVAL_S:
                        emit_bridge_status(
                            "awaiting",
                            "Input received, awaiting board response",
                            wait_ms=wait_ms,
                            input_seq=input_seq,
                        )
                        last_await_emit_at = now

                socketio.sleep(TWIN_BRIDGE_LOOP_SLEEP_S)

        except Exception as e:
            board_connected = False
            emit_board_status("disconnected", target_port, str(e))
            emit_bridge_status("error", f"Bridge error: {e}")
        finally:
            reader.close()

        if not is_running:
            break
        board_connected = False
        emit_board_status("reconnecting", target_port)
        emit_bridge_status("idle", "Reconnecting bridge")
        socketio.sleep(1.0)


# ---------------------------------------------------------------------------
# Routes
# ---------------------------------------------------------------------------

@app.route('/')
def index():
    return send_from_directory(WEB_DIR, 'index.html')


@app.route('/<path:filename>')
def static_files(filename):
    return send_from_directory(WEB_DIR, filename)


@app.route('/api/status')
def api_status():
    return jsonify({
        'mode': mode,
        'port': serial_port,
        'connected': is_running,
        'board_connected': board_connected,
        'port_locked': port_locked,
        'latest': latest_reading,
    })


@app.route('/api/rescan', methods=['POST'])
def api_rescan():
    global mode, serial_port, port_locked

    if not HAS_SERIAL:
        return jsonify({"ok": False, "error": "pyserial not installed on server"}), 400

    # Detect button is allowed to switch from sim -> board mode.
    mode = "board"
    port_locked = False

    detected = find_board_port()
    serial_port = detected if detected else None

    restart_data_pipeline()
    socketio.emit('config', {'mode': mode, 'port': serial_port})
    emit_board_status("reconnecting", serial_port, "Manual detect triggered")
    return jsonify({
        "ok": True,
        "mode": mode,
        "detected_port": detected,
        "active_port": serial_port,
        "port_locked": port_locked,
    })


# ---------------------------------------------------------------------------
# SocketIO events
# ---------------------------------------------------------------------------

@socketio.on('connect')
def on_connect():
    global data_thread
    print(f"Client connected")

    with thread_lock:
        if data_thread is None or not data_thread.is_alive():
            _start_data_thread_locked()

    socketio.emit('config', {'mode': mode, 'port': serial_port})


@socketio.on('disconnect')
def on_disconnect():
    print("Client disconnected")


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    global mode, serial_port, port_locked, twin_source_url

    parser = argparse.ArgumentParser(description="EV Battery Dashboard Server")
    parser.add_argument("--sim", action="store_true", help="Use simulated data")
    parser.add_argument("--twin-bridge", action="store_true", help="Digital twin → board → dashboard")
    parser.add_argument("--twin-url", type=str, default="http://127.0.0.1:5001",
                        help="Digital twin base URL (used in --twin-bridge mode)")
    parser.add_argument("--port", type=str, help="Serial port for live board")
    parser.add_argument("--host", default="127.0.0.1", help="Server host")
    parser.add_argument("--web-port", type=int, default=5000, help="Web server port")
    args = parser.parse_args()
    twin_source_url = args.twin_url.rstrip("/")

    if args.twin_bridge:
        mode = "twin-bridge"
        serial_port = args.port
        port_locked = bool(args.port)
    elif args.port:
        mode = "board"
        serial_port = args.port
        port_locked = True
    elif args.sim:
        mode = "sim"
        port_locked = False
    else:
        # Auto-detect: prefer live board only when a port is actually present.
        detected_port = find_board_port() if HAS_SERIAL else None
        if detected_port:
            mode = "board"
            serial_port = detected_port
        else:
            mode = "sim"
            serial_port = None
        port_locked = False

    print("=" * 60)
    print("  EV Battery Intelligence — Dashboard Server")
    if mode == "sim":
        mode_text = "Simulation"
    elif mode == "twin-bridge":
        mode_text = f"Twin Bridge ({twin_source_url} -> board -> :{args.web_port})"
    else:
        mode_text = f"Live Board ({serial_port})" if serial_port else "Live Board (auto-detect)"
    print(f"  Mode: {mode_text}")
    print(f"  URL:  http://{args.host}:{args.web_port}")
    print("=" * 60)
    print()

    with thread_lock:
        if data_thread is None or not data_thread.is_alive():
            _start_data_thread_locked()

    socketio.run(app, host=args.host, port=args.web_port, debug=False,
                 allow_unsafe_werkzeug=True)


if __name__ == "__main__":
    main()
