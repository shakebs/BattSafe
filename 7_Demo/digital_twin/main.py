"""
Battery Pack Digital Twin — Main Entry Point
===============================================
Orchestrates physics simulation, fault injection,
serial bridge, and dashboard.

Uses a threading lock to ensure pack state is not
modified by sim loop and time-jump simultaneously.
"""

import argparse
import threading
import time
import signal
import sys
import os

from digital_twin.config import SIM_DT, TOTAL_SENSOR_CHANNELS, SAMPLING_RATE_HZ
from digital_twin.physics_engine import BatteryPack
from digital_twin.fault_injection import FaultInjectionEngine
from digital_twin.dashboard.app import init_dashboard, run_dashboard


class DigitalTwinSimulator:
    """Main simulator orchestrator with thread-safe pack access."""

    def __init__(self, use_serial=False, serial_port=None):
        self.pack = BatteryPack()
        self.fault_engine = FaultInjectionEngine(self.pack)
        self.lock = threading.Lock()   # Protects pack state

        self.serial_bridge = None
        if use_serial:
            try:
                from digital_twin.serial_bridge import SerialBridge
                self.serial_bridge = SerialBridge(port=serial_port)
                print(f"[Serial] Bridge initialized on {self.serial_bridge.port}")
            except Exception as e:
                print(f"[Serial] Could not initialize: {e}")
                self.serial_bridge = None

        init_dashboard(self.pack, self.fault_engine, self.lock)

        self._running = False
        self._sim_thread = None

    def start(self):
        """Start the simulation loop and dashboard."""
        self._running = True

        self._sim_thread = threading.Thread(target=self._simulation_loop, daemon=True)
        self._sim_thread.start()

        print()
        print("=" * 60)
        print("  EV Battery Digital Twin — Simplified Input Simulator")
        print(f"  Pack: 104S8P | 832 cells | 332.8V nominal | 120Ah")
        print(f"  Sensor Channels: {TOTAL_SENSOR_CHANNELS} | Rate: {SAMPLING_RATE_HZ} Hz")
        print("  Dashboard: http://localhost:5001")
        print()
        print("  Press CTRL+C to stop the server.")
        print("=" * 60)
        print()

        run_dashboard()

    def _simulation_loop(self):
        """Main physics simulation loop with speed control."""
        print("[Sim] Simulation loop started")
        while self._running:
            loop_start = time.time()

            with self.lock:
                # Run sim_speed steps per tick
                steps = max(1, self.pack.sim_speed)
                for _ in range(steps):
                    self.fault_engine.apply_faults(dt=SIM_DT)
                    self.pack.step(dt=SIM_DT)

            # Send to serial if connected
            if self.serial_bridge and self.serial_bridge.is_connected:
                try:
                    with self.lock:
                        self.serial_bridge.send_data(self.pack.get_snapshot())
                except Exception:
                    pass

            # Maintain wall-clock timing
            elapsed = time.time() - loop_start
            sleep_time = SIM_DT - elapsed
            if sleep_time > 0:
                time.sleep(sleep_time)

    def stop(self):
        self._running = False
        if self._sim_thread:
            self._sim_thread.join(timeout=2.0)
        if self.serial_bridge:
            self.serial_bridge.close()
        print("[Sim] Stopped.")


def main():
    parser = argparse.ArgumentParser(description='EV Battery Digital Twin Simulator')
    parser.add_argument('--no-serial', action='store_true',
                        help='Run without serial bridge')
    parser.add_argument('--port', type=str, default=None,
                        help='Serial port (e.g., COM3)')
    args = parser.parse_args()

    sim = DigitalTwinSimulator(
        use_serial=not args.no_serial,
        serial_port=args.port,
    )

    def signal_handler(sig, frame):
        print("\n[Sim] Shutting down (CTRL+C)...")
        sim.stop()
        os._exit(0)

    signal.signal(signal.SIGINT, signal_handler)
    signal.signal(signal.SIGTERM, signal_handler)

    try:
        sim.start()
    except KeyboardInterrupt:
        print("\n[Sim] Shutting down...")
        sim.stop()
        os._exit(0)


if __name__ == '__main__':
    main()
