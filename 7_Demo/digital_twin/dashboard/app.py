"""
Battery Pack Digital Twin — Dashboard Backend
===============================================
Flask-SocketIO server for real-time dashboard.
Uses a shared threading lock for thread-safe pack access.
"""

import threading
from flask import Flask, render_template, jsonify
from flask_socketio import SocketIO, emit

from digital_twin.config import (
    FAULT_CATALOG, DASHBOARD_UPDATE_INTERVAL,
    SIM_SPEED_OPTIONS, TIME_JUMP_OPTIONS, SIM_DT,
    THERMAL_CASCADE,
)

app = Flask(__name__)
app.config['SECRET_KEY'] = 'ev-bic-digital-twin'
socketio = SocketIO(app, cors_allowed_origins="*", async_mode='threading')

# Global references (set by main.py)
pack = None
fault_engine = None
pack_lock = None   # threading.Lock from main.py


def init_dashboard(battery_pack, fault_injection_engine, lock=None):
    global pack, fault_engine, pack_lock
    pack = battery_pack
    fault_engine = fault_injection_engine
    pack_lock = lock or threading.Lock()


# ── Routes ──────────────────────────────────────────────────

@app.route('/')
def index():
    return render_template('index.html')


@app.route('/api/catalog')
def api_catalog():
    return jsonify(FAULT_CATALOG)


@app.route('/api/status')
def api_status():
    if pack:
        with pack_lock:
            return jsonify(pack.get_snapshot())
    return jsonify({'error': 'Pack not initialized'})


@app.route('/api/cascade')
def api_cascade():
    return jsonify(THERMAL_CASCADE)


# ── WebSocket Events ─────────────────────────────────────────

@socketio.on('connect')
def on_connect():
    if pack:
        with pack_lock:
            emit('pack_data', pack.get_snapshot())
    emit('catalog', FAULT_CATALOG)
    emit('sim_config', {
        'speed_options': SIM_SPEED_OPTIONS,
        'time_jump_options': TIME_JUMP_OPTIONS,
        'cascade_stages': THERMAL_CASCADE,
    })


@socketio.on('inject_fault')
def on_inject_fault(data):
    if not fault_engine:
        return
    with pack_lock:
        result = fault_engine.inject_fault(
            fault_type=data.get('fault_type', ''),
            module=int(data.get('module', 1)),
            group=int(data.get('group', 1)),
            severity=float(data.get('severity', 0.5)),
            duration=float(data.get('duration', 0)),
        )
    emit('fault_injected', result)
    _broadcast_faults()


@socketio.on('clear_fault')
def on_clear_fault(data):
    if not fault_engine:
        return
    fault_id = data.get('fault_id', '')
    with pack_lock:
        success = fault_engine.clear_fault(fault_id)
    emit('fault_cleared', {'success': success, 'fault_id': fault_id})
    _broadcast_faults()


@socketio.on('clear_all_faults')
def on_clear_all_faults(data=None):
    if fault_engine:
        with pack_lock:
            fault_engine.clear_all_faults()
    _broadcast_faults()
    emit('all_faults_cleared', {'success': True})


@socketio.on('set_operating_mode')
def on_set_operating_mode(data):
    if not pack:
        return
    is_charging = data.get('charging', False)
    c_rate = float(data.get('c_rate', 0.5))
    with pack_lock:
        pack.set_operating_mode(charging=is_charging, c_rate=c_rate)
    emit('operating_mode_set', {'charging': is_charging, 'c_rate': c_rate})


@socketio.on('set_soc')
def on_set_soc(data):
    if not pack:
        return
    soc_pct = float(data.get('soc', 50))
    with pack_lock:
        pack.set_soc(soc_pct / 100.0)
    emit('soc_set', {'soc': soc_pct})


@socketio.on('set_ambient')
def on_set_ambient(data):
    if not pack:
        return
    temp = float(data.get('temp', 30.0))
    with pack_lock:
        pack.set_ambient_temp(temp)
    emit('ambient_set', {'temp': temp})


@socketio.on('set_sim_speed')
def on_set_sim_speed(data):
    """Set simulation speed multiplier."""
    if not pack:
        return
    speed = int(data.get('speed', 1))
    if speed in SIM_SPEED_OPTIONS:
        with pack_lock:
            pack.sim_speed = speed
        emit('sim_speed_set', {'speed': speed})


@socketio.on('time_jump')
def on_time_jump(data):
    """
    Jump simulation forward by N seconds.
    Uses a coarser dt (2.0s) to avoid lag — thermal physics is
    linear so total temperature change is identical.
    +1min = 30 steps, +5min = 150, +30min = 900.
    """
    if not pack or not fault_engine:
        return
    seconds = float(data.get('seconds', 60))
    seconds = min(seconds, 1800)

    JUMP_DT = 2.0  # Coarser than real-time (SIM_DT=0.1s) but same physics
    steps = int(seconds / JUMP_DT)

    with pack_lock:
        for _ in range(steps):
            fault_engine.apply_faults(dt=JUMP_DT)
            pack.step(dt=JUMP_DT)

    emit('time_jumped', {
        'seconds': seconds,
        'new_sim_time': round(pack.sim_time, 2),
    })
    with pack_lock:
        socketio.emit('pack_data', pack.get_snapshot())


@socketio.on('reset_system')
def on_reset_system(data=None):
    if fault_engine:
        with pack_lock:
            fault_engine.clear_all_faults()
    if pack:
        with pack_lock:
            pack.full_reset()
    emit('system_reset', {'success': True})
    _broadcast_faults()


def _broadcast_faults():
    if fault_engine:
        with pack_lock:
            socketio.emit('active_faults', fault_engine.get_active_faults_summary())
            socketio.emit('fault_log', fault_engine.get_recent_log())


# ── Background Data Broadcast ────────────────────────────────

def start_broadcast_thread():
    def broadcast_loop():
        while True:
            if pack:
                with pack_lock:
                    socketio.emit('pack_data', pack.get_snapshot())
                    if fault_engine:
                        socketio.emit('active_faults', fault_engine.get_active_faults_summary())
            socketio.sleep(DASHBOARD_UPDATE_INTERVAL)

    socketio.start_background_task(broadcast_loop)


def run_dashboard(host='0.0.0.0', port=5001):
    start_broadcast_thread()
    socketio.run(app, host=host, port=port, debug=False, allow_unsafe_werkzeug=True)
