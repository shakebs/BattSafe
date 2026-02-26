#!/usr/bin/env python3
"""
Real-Time Battery Monitor Dashboard
====================================

A live-updating dashboard that visualizes sensor data from the
EV Battery Thermal Runaway Prevention System.

MODES:
  --sim       Replay simulated data (default, no hardware needed)
  --serial    Read from hardware via serial/UART (when board is connected)
  --csv FILE  Replay a previously recorded CSV log

Run:
  python3 dashboard/src/dashboard.py --sim
  python3 dashboard/src/dashboard.py --serial --port /dev/tty.usbserial-XXX
  python3 dashboard/src/dashboard.py --csv /path/to/log.csv
"""

import argparse
import csv
import os
import sys
from collections import deque
from pathlib import Path

# Ensure matplotlib/font caches are writable in restricted environments.
os.environ.setdefault("MPLCONFIGDIR", "/tmp/mplconfig")
os.environ.setdefault("XDG_CACHE_HOME", "/tmp")

import matplotlib
if sys.platform == "darwin":
    # Prefer native macOS backend, but allow fallback when unavailable.
    try:
        matplotlib.use("macosx")
    except Exception:
        pass
elif not os.environ.get("DISPLAY"):
    # Allow non-interactive environments (CI/headless) to import this module.
    matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.patches as mpatches
from matplotlib.animation import FuncAnimation

# Add parent directory to path so we can import our modules
sys.path.insert(0, str(Path(__file__).resolve().parent))
from sim_data_generator import generate_full_demo, SensorReading


# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------

# How many data points to show on screen at once (rolling window)
WINDOW_SIZE = 120  # 60 seconds at 2Hz

# How fast to replay simulation data (ms between frames)
REPLAY_SPEED_MS = 100  # 100ms = 5x real speed for demo

# Colors matching alert severity
COLORS = {
    "NORMAL":    "#22c55e",  # green
    "WARNING":   "#eab308",  # yellow
    "CRITICAL":  "#f97316",  # orange
    "EMERGENCY": "#ef4444",  # red
}

# Dark theme colors
BG_COLOR = "#0f172a"       # dark navy background
PANEL_COLOR = "#1e293b"    # slightly lighter panel
TEXT_COLOR = "#e2e8f0"     # light gray text
GRID_COLOR = "#334155"     # subtle grid lines
ACCENT_BLUE = "#38bdf8"   # sky blue accent
ACCENT_CYAN = "#22d3ee"   # cyan accent
ACCENT_AMBER = "#fbbf24"  # amber accent
ACCENT_ROSE = "#fb7185"   # rose accent
ACCENT_GREEN = "#4ade80"  # green accent


def _safe_float(row: dict, key: str, default: float = 0.0) -> float:
    raw = row.get(key, "")
    if raw in ("", None):
        return default
    try:
        return float(raw)
    except ValueError:
        return default


def _parse_categories(raw_value: str) -> list[str]:
    if not raw_value:
        return []
    return [part.strip().lower() for part in raw_value.split("|") if part.strip()]


def load_csv_data(csv_path: Path) -> list[SensorReading]:
    """Load a CSV log into dashboard-native SensorReading records."""
    readings: list[SensorReading] = []
    with csv_path.open("r", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        for row in reader:
            t_ms = int(_safe_float(row, "t_ms", _safe_float(row, "timestamp_ms", 0.0)))
            temp_common = _safe_float(row, "temp_c", 28.0)
            temp1 = _safe_float(row, "temp_cell1_c", temp_common)
            temp2 = _safe_float(row, "temp_cell2_c", temp_common)
            temp3 = _safe_float(row, "temp_cell3_c", temp_common)
            temp4 = _safe_float(row, "temp_cell4_c", temp_common)
            state = (row.get("state") or row.get("system_state") or "NORMAL").upper()
            categories = _parse_categories(row.get("categories", ""))

            reading = SensorReading(
                timestamp_ms=t_ms,
                voltage_v=_safe_float(row, "voltage_v", 0.0),
                current_a=_safe_float(row, "current_a", 0.0),
                r_internal_mohm=_safe_float(row, "r_internal_mohm", 0.0),
                temp_cell1_c=temp1,
                temp_cell2_c=temp2,
                temp_cell3_c=temp3,
                temp_cell4_c=temp4,
                temp_ambient_c=_safe_float(row, "temp_ambient_c", min(temp1, temp2, temp3, temp4)),
                gas_ratio=_safe_float(row, "gas_ratio", 1.0),
                pressure_delta_hpa=_safe_float(row, "pressure_delta_hpa", 0.0),
                humidity_pct=_safe_float(row, "humidity_pct", 45.0),
                swelling_pct=_safe_float(row, "swelling_pct", 0.0),
                short_circuit=bool(int(_safe_float(row, "short_event", 0.0))),
                active_categories=categories,
                system_state=state,
            )
            readings.append(reading)
    return readings


# ---------------------------------------------------------------------------
# Dashboard class
# ---------------------------------------------------------------------------

class BatteryDashboard:
    """Live-updating dashboard for battery monitoring system.
    
    The dashboard has 5 panels:
      1. Top-left:     Voltage & Current (electrical health)
      2. Top-right:    Cell Temperatures (thermal monitoring)
      3. Middle-left:  Gas Ratio & Pressure (pre-runaway indicators)
      4. Middle-right: System State & Correlation Engine
      5. Bottom:       Alert Timeline (state history)
    """
    
    def __init__(
        self,
        data_source: list[SensorReading] | None = None,
        replay_speed_ms: int = 100,
        stream_getter=None,
        loop_playback: bool = True,
    ):
        """Initialize dashboard with a data source.
        
        Args:
            data_source: List of SensorReading objects to display.
            replay_speed_ms: Milliseconds between frames.
            stream_getter: Callable returning one SensorReading (or None).
            loop_playback: If True, replay finite sources in a loop.
        """
        self.data_source = data_source or []
        self.replay_speed_ms = replay_speed_ms
        self.data_index = 0
        self.stream_getter = stream_getter
        self.loop_playback = loop_playback
        
        # Rolling buffers for each chart (stores last WINDOW_SIZE points)
        self.timestamps = deque(maxlen=WINDOW_SIZE)
        self.voltages = deque(maxlen=WINDOW_SIZE)
        self.currents = deque(maxlen=WINDOW_SIZE)
        self.temp1 = deque(maxlen=WINDOW_SIZE)
        self.temp2 = deque(maxlen=WINDOW_SIZE)
        self.temp3 = deque(maxlen=WINDOW_SIZE)
        self.temp4 = deque(maxlen=WINDOW_SIZE)
        self.temp_amb = deque(maxlen=WINDOW_SIZE)
        self.gas_ratios = deque(maxlen=WINDOW_SIZE)
        self.pressures = deque(maxlen=WINDOW_SIZE)
        self.states = deque(maxlen=WINDOW_SIZE)
        self.state_colors = deque(maxlen=WINDOW_SIZE)
        self.categories_history = deque(maxlen=WINDOW_SIZE)
        
        # Set up the figure and subplots
        self._setup_figure()
    
    def _setup_figure(self):
        """Create the matplotlib figure with dark theme and 5 panels."""
        plt.style.use("dark_background")
        
        self.fig = plt.figure(
            figsize=(16, 10),
            facecolor=BG_COLOR,
        )
        self.fig.canvas.manager.set_window_title(
            "EV Battery Intelligence — Thermal Runaway Prevention Dashboard"
        )
        
        # Create grid: 3 rows, 2 columns
        # Row 1: Electrical | Thermal
        # Row 2: Gas/Pressure | State Machine
        # Row 3: Alert Timeline (full width)
        gs = self.fig.add_gridspec(
            3, 2,
            hspace=0.35, wspace=0.25,
            left=0.07, right=0.97, top=0.92, bottom=0.06,
        )
        
        self.ax_electrical = self.fig.add_subplot(gs[0, 0])
        self.ax_thermal = self.fig.add_subplot(gs[0, 1])
        self.ax_gas = self.fig.add_subplot(gs[1, 0])
        self.ax_state = self.fig.add_subplot(gs[1, 1])
        self.ax_timeline = self.fig.add_subplot(gs[2, :])
        
        # Style each axes
        for ax in [self.ax_electrical, self.ax_thermal, self.ax_gas,
                    self.ax_state, self.ax_timeline]:
            ax.set_facecolor(PANEL_COLOR)
            ax.tick_params(colors=TEXT_COLOR, labelsize=8)
            ax.grid(True, alpha=0.15, color=GRID_COLOR)
            for spine in ax.spines.values():
                spine.set_color(GRID_COLOR)
        
        # Title
        self.fig.suptitle(
            "EV Battery Thermal Runaway Prevention  --  Live Monitor",
            fontsize=15, fontweight="bold", color=TEXT_COLOR,
            y=0.97,
        )
        
        # Subtitle with scenario name (updated dynamically)
        self.scenario_text = self.fig.text(
            0.5, 0.935, "Scenario: Normal Operation",
            ha="center", fontsize=10, color=ACCENT_CYAN,
            fontstyle="italic",
        )
        
        # --- Panel 1: Electrical ---
        self.ax_electrical.set_title("[E] Voltage & Current", color=TEXT_COLOR,
                                      fontsize=10, fontweight="bold", pad=8)
        self.ax_electrical.set_ylabel("Voltage (V)", color=ACCENT_BLUE, fontsize=9)
        self.ax_elec_twin = self.ax_electrical.twinx()
        self.ax_elec_twin.set_ylabel("Current (A)", color=ACCENT_AMBER, fontsize=9)
        self.ax_elec_twin.tick_params(colors=TEXT_COLOR, labelsize=8)
        
        # Initialize line objects (empty data)
        self.line_voltage, = self.ax_electrical.plot(
            [], [], color=ACCENT_BLUE, linewidth=1.5, label="Voltage"
        )
        self.line_current, = self.ax_elec_twin.plot(
            [], [], color=ACCENT_AMBER, linewidth=1.5, label="Current"
        )
        
        # --- Panel 2: Thermal ---
        self.ax_thermal.set_title("[T] Cell Temperatures", color=TEXT_COLOR,
                                   fontsize=10, fontweight="bold", pad=8)
        self.ax_thermal.set_ylabel("Temperature (°C)", color=ACCENT_ROSE, fontsize=9)
        
        cell_colors = [ACCENT_ROSE, "#c084fc", ACCENT_AMBER, ACCENT_GREEN]
        self.line_temps = []
        for i, c in enumerate(cell_colors):
            line, = self.ax_thermal.plot([], [], color=c, linewidth=1.2,
                                          label=f"Cell {i+1}", alpha=0.9)
            self.line_temps.append(line)
        self.line_amb, = self.ax_thermal.plot(
            [], [], color=TEXT_COLOR, linewidth=1, linestyle="--",
            label="Ambient", alpha=0.5
        )
        # Threshold line
        self.ax_thermal.axhline(y=55, color=ACCENT_AMBER, linewidth=0.8,
                                 linestyle=":", alpha=0.6, label="Warning (55°C)")
        self.ax_thermal.legend(loc="upper left", fontsize=7, framealpha=0.3)
        
        # --- Panel 3: Gas & Pressure ---
        self.ax_gas.set_title("[G] Gas (VOC) & Pressure", color=TEXT_COLOR,
                               fontsize=10, fontweight="bold", pad=8)
        self.ax_gas.set_ylabel("Gas Ratio", color=ACCENT_CYAN, fontsize=9)
        self.ax_gas_twin = self.ax_gas.twinx()
        self.ax_gas_twin.set_ylabel("ΔPressure (hPa)", color=ACCENT_ROSE, fontsize=9)
        self.ax_gas_twin.tick_params(colors=TEXT_COLOR, labelsize=8)
        
        self.line_gas, = self.ax_gas.plot(
            [], [], color=ACCENT_CYAN, linewidth=1.5, label="Gas Ratio"
        )
        self.line_pressure, = self.ax_gas_twin.plot(
            [], [], color=ACCENT_ROSE, linewidth=1.5, label="ΔPressure"
        )
        # Threshold lines for gas
        self.ax_gas.axhline(y=0.70, color=ACCENT_AMBER, linewidth=0.8,
                             linestyle=":", alpha=0.6)
        self.ax_gas.axhline(y=0.40, color=COLORS["EMERGENCY"], linewidth=0.8,
                             linestyle=":", alpha=0.6)
        
        # --- Panel 4: State Machine ---
        self.ax_state.set_title("[CE] Correlation Engine State", color=TEXT_COLOR,
                                 fontsize=10, fontweight="bold", pad=8)
        self.ax_state.set_xlim(0, 1)
        self.ax_state.set_ylim(0, 1)
        self.ax_state.set_xticks([])
        self.ax_state.set_yticks([])
        
        # State indicator (large colored circle + text)
        self.state_circle = mpatches.FancyBboxPatch(
            (0.15, 0.45), 0.7, 0.45,
            boxstyle="round,pad=0.05",
            facecolor=COLORS["NORMAL"], alpha=0.9,
            edgecolor="white", linewidth=2,
        )
        self.ax_state.add_patch(self.state_circle)
        self.state_label = self.ax_state.text(
            0.5, 0.68, "NORMAL", ha="center", va="center",
            fontsize=22, fontweight="bold", color="white",
        )
        
        # Category indicators
        self.category_labels = {}
        categories = ["electrical", "thermal", "gas", "pressure", "swelling"]
        for i, cat in enumerate(categories):
            x = 0.1 + i * 0.18
            self.category_labels[cat] = self.ax_state.text(
                x, 0.18, f"● {cat.upper()[:5]}",
                ha="center", va="center", fontsize=8,
                color=GRID_COLOR, fontweight="bold",
            )
        
        # Active count text
        self.active_count_text = self.ax_state.text(
            0.5, 0.35, "Active categories: 0/5",
            ha="center", va="center", fontsize=9, color=TEXT_COLOR,
        )
        
        # --- Panel 5: Alert Timeline ---
        self.ax_timeline.set_title("📊 State History", color=TEXT_COLOR,
                                    fontsize=10, fontweight="bold", pad=8)
        self.ax_timeline.set_ylabel("Alert Level", color=TEXT_COLOR, fontsize=9)
        self.ax_timeline.set_yticks([0, 1, 2, 3])
        self.ax_timeline.set_yticklabels(
            ["NORMAL", "WARNING", "CRITICAL", "EMERGENCY"],
            fontsize=8,
        )
        self.ax_timeline.set_ylim(-0.5, 3.5)
        
        # Map state names to numeric values for timeline
        self.state_to_num = {
            "NORMAL": 0, "WARNING": 1, "CRITICAL": 2, "EMERGENCY": 3
        }
        self.timeline_nums = deque(maxlen=WINDOW_SIZE)
        self.timeline_ts = deque(maxlen=WINDOW_SIZE)

    def _clear_history_buffers(self):
        for buf in [self.timestamps, self.voltages, self.currents,
                    self.temp1, self.temp2, self.temp3, self.temp4,
                    self.temp_amb, self.gas_ratios, self.pressures,
                    self.states, self.state_colors,
                    self.categories_history, self.timeline_nums,
                    self.timeline_ts]:
            buf.clear()
        
    def _update(self, frame):
        """Called every frame — push new data and update all panels."""
        if self.stream_getter is not None:
            r = self.stream_getter()
            if r is None:
                return []
        else:
            if not self.data_source:
                return []
            if self.data_index >= len(self.data_source):
                if not self.loop_playback:
                    return []
                self.data_index = 0
                self._clear_history_buffers()

            r = self.data_source[self.data_index]
            self.data_index += 1
        
        # Convert timestamp to seconds for x-axis
        t_s = r.timestamp_ms / 1000.0

        # Board demo wraps timestamp to 0 at scenario restart.
        if self.timestamps and t_s < self.timestamps[-1]:
            self._clear_history_buffers()
        
        # Push data into rolling buffers
        self.timestamps.append(t_s)
        self.voltages.append(r.voltage_v)
        self.currents.append(r.current_a)
        self.temp1.append(r.temp_cell1_c)
        self.temp2.append(r.temp_cell2_c)
        self.temp3.append(r.temp_cell3_c)
        self.temp4.append(r.temp_cell4_c)
        self.temp_amb.append(r.temp_ambient_c)
        self.gas_ratios.append(r.gas_ratio)
        self.pressures.append(r.pressure_delta_hpa)
        self.states.append(r.system_state)
        self.state_colors.append(COLORS.get(r.system_state, COLORS["NORMAL"]))
        self.categories_history.append(r.active_categories)
        self.timeline_nums.append(self.state_to_num.get(r.system_state, 0))
        self.timeline_ts.append(t_s)
        
        ts = list(self.timestamps)
        
        # --- Update Panel 1: Electrical ---
        self.line_voltage.set_data(ts, list(self.voltages))
        self.line_current.set_data(ts, list(self.currents))
        if ts:
            self.ax_electrical.set_xlim(ts[0], ts[-1] + 0.5)
            self.ax_electrical.set_ylim(8, 16)
            self.ax_elec_twin.set_xlim(ts[0], ts[-1] + 0.5)
            self.ax_elec_twin.set_ylim(0, max(25, max(self.currents) + 2))
        
        # --- Update Panel 2: Thermal ---
        temps_data = [list(self.temp1), list(self.temp2),
                      list(self.temp3), list(self.temp4)]
        for i, line in enumerate(self.line_temps):
            line.set_data(ts, temps_data[i])
        self.line_amb.set_data(ts, list(self.temp_amb))
        if ts:
            self.ax_thermal.set_xlim(ts[0], ts[-1] + 0.5)
            all_temps = sum(temps_data, []) + list(self.temp_amb)
            self.ax_thermal.set_ylim(
                max(15, min(all_temps) - 3),
                max(all_temps) + 5,
            )
        
        # --- Update Panel 3: Gas & Pressure ---
        self.line_gas.set_data(ts, list(self.gas_ratios))
        self.line_pressure.set_data(ts, list(self.pressures))
        if ts:
            self.ax_gas.set_xlim(ts[0], ts[-1] + 0.5)
            self.ax_gas.set_ylim(0, 1.2)
            self.ax_gas_twin.set_xlim(ts[0], ts[-1] + 0.5)
            self.ax_gas_twin.set_ylim(
                -1,
                max(3, max(self.pressures) + 2),
            )
        
        # --- Update Panel 4: State Machine ---
        state = r.system_state
        color = COLORS.get(state, COLORS["NORMAL"])
        self.state_circle.set_facecolor(color)
        self.state_label.set_text(state)
        
        # Update category indicators
        active_cats = r.active_categories
        for cat, label in self.category_labels.items():
            if cat in active_cats:
                label.set_color(COLORS["EMERGENCY"] if len(active_cats) >= 3
                                else COLORS["CRITICAL"] if len(active_cats) >= 2
                                else COLORS["WARNING"])
            else:
                label.set_color(GRID_COLOR)
        
        self.active_count_text.set_text(
            f"Active categories: {len(active_cats)}/5"
        )
        
        # --- Update Panel 5: Timeline ---
        if ts:
            self.ax_timeline.clear()
            self.ax_timeline.set_facecolor(PANEL_COLOR)
            self.ax_timeline.grid(True, alpha=0.15, color=GRID_COLOR)
            self.ax_timeline.set_ylabel("Alert Level", color=TEXT_COLOR, fontsize=9)
            self.ax_timeline.set_yticks([0, 1, 2, 3])
            self.ax_timeline.set_yticklabels(
                ["NORMAL", "WARNING", "CRITICAL", "EMERGENCY"], fontsize=8,
            )
            self.ax_timeline.set_ylim(-0.5, 3.5)
            self.ax_timeline.tick_params(colors=TEXT_COLOR, labelsize=8)
            self.ax_timeline.set_xlabel("Time (s)", color=TEXT_COLOR, fontsize=9)
            for spine in self.ax_timeline.spines.values():
                spine.set_color(GRID_COLOR)
            
            tl_ts = list(self.timeline_ts)
            tl_nums = list(self.timeline_nums)
            
            # Color each point by its state
            for i in range(len(tl_ts)):
                c = list(self.state_colors)[i] if i < len(self.state_colors) else COLORS["NORMAL"]
                self.ax_timeline.bar(
                    tl_ts[i], tl_nums[i] + 0.4,
                    width=0.4, bottom=-0.2,
                    color=c, alpha=0.7,
                )
            
            self.ax_timeline.set_xlim(tl_ts[0], tl_ts[-1] + 0.5)
            self.ax_timeline.set_title("State History", color=TEXT_COLOR,
                                        fontsize=10, fontweight="bold", pad=8)
        
        # Update scenario label
        scenario_name = getattr(r, "_scenario_name", "")
        if scenario_name:
            self.scenario_text.set_text(f"Scenario: {scenario_name}")
        
        return []  # Return empty list (blit=False)
    
    def run(self):
        """Start the live dashboard animation."""
        self.anim = FuncAnimation(
            self.fig,
            self._update,
            interval=self.replay_speed_ms,
            blit=False,
            cache_frame_data=False,
        )
        plt.show()


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

def _board_to_sensor_reading(board_reading, categories_fn) -> SensorReading:
    categories = categories_fn(board_reading.anomaly_mask)
    modules = getattr(board_reading, "module_data", []) or []

    module_temps = []
    for m in modules:
        if not isinstance(m, dict):
            continue
        t1 = m.get("ntc1")
        t2 = m.get("ntc2")
        if isinstance(t1, (int, float)):
            module_temps.append(float(t1))
        if isinstance(t2, (int, float)):
            module_temps.append(float(t2))

    # Matplotlib dashboard expects 4 preview channels; derive from full module data.
    while len(module_temps) < 4:
        module_temps.append(getattr(board_reading, "max_temp_c", 25.0))

    gas_ratio_1 = getattr(board_reading, "gas_ratio_1", 1.0)
    gas_ratio_2 = getattr(board_reading, "gas_ratio_2", gas_ratio_1)
    pressure_1 = getattr(board_reading, "pressure_delta_1_hpa", 0.0)
    pressure_2 = getattr(board_reading, "pressure_delta_2_hpa", pressure_1)

    swelling_pct = 0.0
    for m in modules:
        if not isinstance(m, dict):
            continue
        v = m.get("swelling_pct")
        if isinstance(v, (int, float)):
            swelling_pct = max(swelling_pct, float(v))

    return SensorReading(
        timestamp_ms=board_reading.timestamp_ms,
        voltage_v=board_reading.voltage_v,
        current_a=board_reading.current_a,
        r_internal_mohm=board_reading.r_internal_mohm,
        temp_cell1_c=module_temps[0],
        temp_cell2_c=module_temps[1],
        temp_cell3_c=module_temps[2],
        temp_cell4_c=module_temps[3],
        temp_ambient_c=board_reading.temp_ambient_c,
        gas_ratio=min(gas_ratio_1, gas_ratio_2),
        pressure_delta_hpa=max(pressure_1, pressure_2),
        humidity_pct=45.0,
        swelling_pct=swelling_pct,
        short_circuit=board_reading.current_a >= 15.0,
        dt_dt_max=board_reading.dt_dt_max,
        active_categories=categories,
        system_state=board_reading.system_state,
    )


def main():
    parser = argparse.ArgumentParser(
        description="EV Battery Intelligence — Real-Time Dashboard"
    )
    mode_group = parser.add_mutually_exclusive_group()
    mode_group.add_argument(
        "--sim", action="store_true",
        help="Use simulated data replay"
    )
    mode_group.add_argument(
        "--serial", action="store_true",
        help="Read live telemetry from board UART"
    )
    mode_group.add_argument(
        "--csv", type=str, metavar="FILE",
        help="Replay readings from a CSV log file"
    )
    parser.add_argument(
        "--port", type=str, default=None,
        help="Serial port path (used with --serial)"
    )
    parser.add_argument(
        "--baud", type=int, default=115200,
        help="Serial baud rate (used with --serial)"
    )
    parser.add_argument(
        "--speed", type=int, default=REPLAY_SPEED_MS,
        help=f"Replay speed in ms between frames (default: {REPLAY_SPEED_MS})"
    )
    args = parser.parse_args()

    if not args.sim and not args.serial and not args.csv:
        args.sim = True

    replay_speed = args.speed

    print("=" * 60)
    print("  EV Battery Intelligence — Live Dashboard")
    if args.serial:
        print("  Mode: Serial UART")
    elif args.csv:
        print("  Mode: CSV Replay")
    else:
        print("  Mode: Simulation Replay")
    print(f"  Refresh speed: {replay_speed}ms per frame")
    print("=" * 60)
    print()

    if args.serial:
        from serial_reader import SerialReader, active_categories, find_board_port

        port = args.port or find_board_port()
        if not port:
            print("ERROR: No serial port found. Use --port to specify one.")
            sys.exit(1)

        reader = SerialReader(port=port, baud=args.baud)
        reader.open()

        print(f"Streaming from {port} at {args.baud} baud")
        print("Launching dashboard... (close window to exit)")
        print()

        def stream_getter():
            board_reading = reader.read_latest_packet()
            if board_reading is None:
                return None
            reading = _board_to_sensor_reading(board_reading, active_categories)
            reading._scenario_name = f"Serial UART ({port})"
            return reading

        dashboard = BatteryDashboard(
            data_source=[],
            replay_speed_ms=replay_speed,
            stream_getter=stream_getter,
            loop_playback=False,
        )
        try:
            dashboard.run()
        finally:
            reader.close()
        return

    if args.csv:
        csv_path = Path(args.csv).expanduser().resolve()
        if not csv_path.exists():
            print(f"ERROR: CSV file not found: {csv_path}")
            sys.exit(1)
        data = load_csv_data(csv_path)
        if not data:
            print(f"ERROR: CSV file contains no rows: {csv_path}")
            sys.exit(1)
        print(f"Loaded {len(data)} points from {csv_path}")
        print("Launching dashboard... (close window to exit)")
        print()
        dashboard = BatteryDashboard(data, replay_speed_ms=replay_speed)
        dashboard.run()
        return

    print("Generating simulation data (7 scenarios)...")
    data = generate_full_demo()
    print(f"Generated {len(data)} data points ({data[-1].timestamp_ms/1000:.0f}s)")
    print("Launching dashboard... (close window to exit)")
    print()

    dashboard = BatteryDashboard(data, replay_speed_ms=replay_speed)
    dashboard.run()


if __name__ == "__main__":
    main()
