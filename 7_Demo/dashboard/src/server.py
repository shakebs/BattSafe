#!/usr/bin/env python3
"""
Dashboard Server — Flask + SocketIO backend (Full Pack Edition)
================================================================

Reads multi-frame telemetry from VSDSquadron ULTRA (104S8P pack)
or virtual-board pipeline, then pushes to the web frontend via WebSockets.

Usage:
  python3 dashboard/src/server.py --virtual-board  # Twin → virtual board → dashboard
  python3 dashboard/src/server.py --port COM5      # Live board
  python3 dashboard/src/server.py --twin-bridge --port COM5  # Twin → board → dashboard
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
twin_path = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..',
                                         'digital_twin'))
sys.path.insert(0, twin_path)
repo_root = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..', '..'))
sys.path.insert(0, repo_root)

from flask import Flask, render_template, send_from_directory, jsonify
from flask_socketio import SocketIO
from virtual_board import VirtualVsdsquadron

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
mode = "virtual-board"
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
VIRTUAL_BOARD_LOOP_SLEEP_S = 0.1
virtual_board = VirtualVsdsquadron()


# ---------------------------------------------------------------------------
# Text line parser — parses [TEL] lines from firmware UART output
# New format: [TEL] t=Xms V=N I=N Tmax=N dT/dt=N gas=[N,N] dP=[N,N] state=X cats=N hot=MN risk=N% stg=X
# ---------------------------------------------------------------------------
TEL_PATTERN = re.compile(
    r'\[TEL\]\s*t=(\d+)ms\s+V=([\d.\-]+)\s+I=([\d.\-]+)\s+'
    r'Tmax=([\d.\-]+)\s+dT/dt=([\d.\-]+)\s+'
    r'gas=\[([\d.\-]+),([\d.\-]+)\]\s+dP=\[([\d.\-]+),([\d.\-]+)\]\s+'
    r'state=(\w+)\s+cats=(\d+)\s+'
    r'hot=M(\d+)\s+risk=(\d+)%\s+stg=(\w+)'
)

STATE_PATTERN = re.compile(
    r'\[STATE\]\s+(\w+)\s*->\s*(\w+)\s*\(cats=(\d+)'
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
        "max_temp": float(m.group(4)),
        "dt_dt_max": float(m.group(5)),
        "gas_ratio_1": float(m.group(6)),
        "gas_ratio_2": float(m.group(7)),
        "pressure_delta_1": float(m.group(8)),
        "pressure_delta_2": float(m.group(9)),
        "system_state": state_name,
        "state_num": STATE_TO_NUM.get(state_name, 0),
        "anomaly_count": int(m.group(11)),
        "hotspot_module": int(m.group(12)),
        "risk_pct": int(m.group(13)),
        "cascade_stage": m.group(14),
    }


def reading_to_dict(r):
    """Convert SensorReading to dict for JSON (full-pack data)."""
    d = {
        "timestamp_ms": r.timestamp_ms,
        "voltage_v": r.voltage_v,
        "current_a": r.current_a,
        "temp_ambient": getattr(r, 'temp_ambient_c', 25.0),
        "gas_ratio_1": getattr(r, 'gas_ratio_1', getattr(r, 'gas_ratio', 1.0)),
        "gas_ratio_2": getattr(r, 'gas_ratio_2', getattr(r, 'gas_ratio', 1.0)),
        "pressure_delta_1": getattr(r, 'pressure_delta_1', getattr(r, 'pressure_delta_hpa', 0)),
        "pressure_delta_2": getattr(r, 'pressure_delta_2', 0),
        "dt_dt_max": getattr(r, 'dt_dt_max', 0),
        "system_state": r.system_state,
        "state_num": STATE_TO_NUM.get(r.system_state, 0),
        "anomaly_count": len(getattr(r, 'active_categories', [])),
        "categories": getattr(r, 'active_categories', []),
        "hotspot_module": getattr(r, 'hotspot_module', 0),
        "risk_pct": getattr(r, 'risk_pct', 0),
        "cascade_stage": getattr(r, 'cascade_stage', 'Normal'),
        "modules": getattr(r, 'modules', []),
    }
    # Backward compat: generate temp_cells from module NTCs if available
    mods = getattr(r, 'modules', [])
    if mods:
        temps = []
        for m in mods[:4]:
            temps.append(m.get('ntc1', 25) if isinstance(m, dict) else 25)
        d["temp_cells"] = temps
    else:
        d["temp_cells"] = [
            getattr(r, 'temp_cell1_c', 25), getattr(r, 'temp_cell2_c', 25),
            getattr(r, 'temp_cell3_c', 25), getattr(r, 'temp_cell4_c', 25),
        ]
    return d


def board_reading_to_dict(r):
    """Convert BoardReading to dict for JSON (full-pack data)."""
    cats = active_categories(r.anomaly_mask) if HAS_SERIAL else []
    d = {
        "timestamp_ms": r.timestamp_ms,
        "voltage_v": r.voltage_v,
        "current_a": r.current_a,
        "temp_ambient": getattr(r, 'temp_ambient_c', 25.0),
        "max_temp": getattr(r, 'max_temp_c', 30.0),
        "core_temp_est": getattr(r, 'core_temp_est_c', 30.0),
        "gas_ratio_1": getattr(r, 'gas_ratio_1', getattr(r, 'gas_ratio', 1.0)),
        "gas_ratio_2": getattr(r, 'gas_ratio_2', 1.0),
        "pressure_delta_1": getattr(r, 'pressure_delta_1_hpa', getattr(r, 'pressure_delta_hpa', 0)),
        "pressure_delta_2": getattr(r, 'pressure_delta_2_hpa', 0),
        "v_spread_mv": getattr(r, 'v_spread_mv', 0),
        "temp_spread": getattr(r, 'temp_spread_c', 0),
        "dt_dt_max": getattr(r, 'dt_dt_max', 0.0),
        "system_state": r.system_state,
        "state_num": STATE_TO_NUM.get(r.system_state, 0),
        "anomaly_count": r.anomaly_count,
        "anomaly_modules": getattr(r, 'anomaly_modules', 0),
        "hotspot_module": getattr(r, 'hotspot_module', 0),
        "risk_pct": getattr(r, 'risk_pct', 0),
        "cascade_stage": getattr(r, 'cascade_stage', 0),
        "categories": cats,
        "emergency_direct": getattr(r, 'emergency_direct', False),
        "modules": getattr(r, 'module_data', []),
    }
    # Backward compat
    d["temp_cells"] = [
        getattr(r, 'temp_cell1_c', 25), getattr(r, 'temp_cell2_c', 25),
        getattr(r, 'temp_cell3_c', 25), getattr(r, 'temp_cell4_c', 25),
    ]
    return d


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


def pulse_board_reset(port_name: str) -> bool:
    """Best-effort board reset via USB-serial DTR/RTS toggling."""
    if not port_name:
        return False
    try:
        import serial  # type: ignore
        ser = serial.Serial(port=port_name, baudrate=115200, timeout=1)
    except Exception:
        return False

    try:
        ser.dtr = False
        ser.rts = False
        time.sleep(0.05)
        ser.dtr = True
        ser.rts = True
        time.sleep(0.20)
        ser.dtr = False
        ser.rts = False
        time.sleep(0.20)
        return True
    except Exception:
        return False
    finally:
        try:
            ser.close()
        except Exception:
            pass


class RescanRequested(Exception):
    """Internal signal to force serial reconnect."""


def _start_data_thread_locked():
    """Start the active streaming thread. Caller must hold thread_lock."""
    global data_thread, is_running
    is_running = True
    if mode == "virtual-board":
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
    """Stream digital-twin snapshots through virtual board processing."""
    global latest_reading, is_running, board_connected
    import requests

    prev_state = None
    virtual_board.reset()
    emit_board_status("connecting", "VIRTUAL-USB", "Starting virtual VSDSquadron pipeline")
    emit_bridge_status("idle", "Waiting for digital twin source")

    while is_running and expected_epoch == stream_epoch:
        try:
            t0 = time.perf_counter()
            resp = requests.get(f"{twin_source_url}/api/status", timeout=1.0)
            twin_snapshot = resp.json()
            if not isinstance(twin_snapshot, dict):
                raise ValueError("Invalid twin payload")

            d = virtual_board.process_snapshot(twin_snapshot)
            latency_ms = max(0.0, (time.perf_counter() - t0) * 1000.0)
            d["bridge_status"] = {"state": "received", "latency_ms": latency_ms}
            d["mode"] = "virtual-board"

            board_connected = True
            latest_reading = d
            socketio.emit("telemetry", d)
            emit_board_status(
                "connected",
                "VIRTUAL-USB",
                "Digital twin -> virtual VSDSquadron -> dashboard",
            )
            emit_bridge_status("received", "Virtual board output emitted", latency_ms=latency_ms)

            if prev_state and d["system_state"] != prev_state:
                socketio.emit(
                    "state_change",
                    {
                        "from": prev_state,
                        "to": d["system_state"],
                        "timestamp_ms": d["timestamp_ms"],
                    },
                )
            prev_state = d["system_state"]
        except Exception as e:
            board_connected = False
            emit_board_status(
                "waiting",
                "VIRTUAL-USB",
                f"Waiting for digital twin at {twin_source_url}",
            )
            emit_bridge_status("idle", f"Twin source unavailable: {e}")

        socketio.sleep(VIRTUAL_BOARD_LOOP_SLEEP_S)


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
    """Encode a digital twin snapshot into multi-frame binary for the board.

    Uses the SerialBridge from the digital twin to produce 9 frames
    (1 pack + 8 module frames) containing all 139 sensor channels.
    """
    try:
        from digital_twin.serial_bridge import SerialBridge
        bridge = SerialBridge.__new__(SerialBridge)
        return bridge.encode_all_frames(snapshot)
    except ImportError:
        # Fallback: import from path
        import importlib.util
        bridge_path = os.path.join(twin_path, 'serial_bridge.py')
        if os.path.exists(bridge_path):
            spec = importlib.util.spec_from_file_location('serial_bridge', bridge_path)
            mod = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(mod)
            bridge = mod.SerialBridge.__new__(mod.SerialBridge)
            return bridge.encode_all_frames(snapshot)
        # Last resort: return empty (board will use internal sim)
        return b''


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

    # Detect button is allowed to switch from virtual pipeline -> board mode.
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


@app.route('/api/reset_logic', methods=['POST'])
def api_reset_logic():
    """Reset processing logic and restart the active data pipeline."""
    global latest_reading

    reset_triggered = False
    if mode == "virtual-board":
        virtual_board.reset()
    else:
        target_port = serial_port if serial_port else (find_board_port() if HAS_SERIAL else None)
        if target_port:
            reset_triggered = pulse_board_reset(target_port)

    latest_reading = None
    restart_data_pipeline()
    socketio.emit('scenario_restart')
    socketio.emit('config', {'mode': mode, 'port': serial_port})

    port_label = "VIRTUAL-USB" if mode == "virtual-board" else serial_port
    if mode == "virtual-board":
        emit_board_status("reconnecting", port_label, "Logic reset requested")
    elif reset_triggered:
        emit_board_status("reconnecting", port_label, "Board reset pulse sent; reevaluating")
    else:
        emit_board_status("reconnecting", port_label, "Logic restart requested (reset pulse unavailable)")

    return jsonify({
        "ok": True,
        "mode": mode,
        "port": serial_port,
        "board_reset_pulse": reset_triggered,
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
    parser.add_argument(
        "--virtual-board",
        action="store_true",
        help="Digital twin -> virtual VSDSquadron -> dashboard",
    )
    parser.add_argument(
        "--sim",
        action="store_true",
        help="Alias for --virtual-board (legacy flag)",
    )
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
    elif args.virtual_board or args.sim:
        mode = "virtual-board"
        serial_port = None
        port_locked = False
    else:
        # Auto-detect: prefer live board only when a port is actually present.
        detected_port = find_board_port() if HAS_SERIAL else None
        if detected_port:
            mode = "board"
            serial_port = detected_port
        else:
            mode = "virtual-board"
            serial_port = None
        port_locked = False

    print("=" * 60)
    print("  EV Battery Intelligence — Dashboard Server")
    if mode == "virtual-board":
        mode_text = f"Twin({twin_source_url}) -> Virtual VSDSquadron -> :{args.web_port}"
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
