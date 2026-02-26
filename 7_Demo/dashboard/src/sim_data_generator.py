#!/usr/bin/env python3
"""
Generates realistic simulated sensor data for dashboard testing.

This module produces continuous time-series data that mimics what the
VSDSquadron ULTRA would send over UART. It runs through 7 test scenarios
from the proposal's validation matrix:

  1. Normal operation          - everything stable
  2. Thermal anomaly only      - temperature rises, nothing else
  3. Gas anomaly only          - VOC detected, temperature normal
  4. Multi-fault (heat + gas)  - two categories trigger CRITICAL
  5. Full emergency            - heat + gas + pressure → EMERGENCY
  6. Short circuit event       - sudden current spike
  7. Ambient compensation       - same absolute temp, different outcomes

Each scenario runs for a set number of seconds of simulated time,
with data points generated at 500ms intervals (matching the medium loop).
"""

import math
import random
from dataclasses import dataclass, field
from typing import Iterator


# ---------------------------------------------------------------------------
# Data structures
# ---------------------------------------------------------------------------

@dataclass
class SensorReading:
    """One snapshot of all sensor values at a point in time.
    
    Full-pack edition: carries data for 8 modules (16 NTCs, 104 groups)
    plus pack-level sensors. Legacy 4-cell fields still present for
    backward compat with the existing dashboard frontend.
    """
    timestamp_ms: int       # milliseconds since boot
    
    # Electrical — full pack (104S8P, ~332.8V, 120Ah)
    voltage_v: float        # pack voltage (~332.8V)
    current_a: float        # pack current in amps
    r_internal_mohm: float  # group internal resistance in milliohms
    
    # Legacy Thermal (kept for backward compat — first 4 module NTC1s)
    temp_cell1_c: float     # Module 1 NTC1
    temp_cell2_c: float     # Module 2 NTC1
    temp_cell3_c: float     # Module 3 NTC1
    temp_cell4_c: float     # Module 4 NTC1
    temp_ambient_c: float   # ambient temperature
    
    # Gas & Pressure — single-value legacy (worst-case of dual sensors)
    gas_ratio: float        # min(gas1, gas2)
    pressure_delta_hpa: float  # max(p1, p2)
    humidity_pct: float
    
    # Mechanical — single-value legacy (max across 8 modules)
    swelling_pct: float
    
    # Derived flags
    short_circuit: bool
    dt_dt_max: float = 0.0
    
    # State machine output
    active_categories: list = field(default_factory=list)
    system_state: str = "NORMAL"
    
    # Full-pack data (new)
    modules: list = field(default_factory=list)  # list of dicts with ntc1, ntc2, swelling_pct, etc.
    gas_ratio_1: float = 1.0
    gas_ratio_2: float = 1.0
    pressure_delta_1: float = 0.0
    pressure_delta_2: float = 0.0
    hotspot_module: int = 0     # 1-based module index
    risk_pct: int = 0           # 0-100
    cascade_stage: str = 'Normal'
    temp_cells_c: list = field(default_factory=lambda: [25.0]*4)  # backward compat


@dataclass
class CorrelationEngineState:
    """Tracks stateful correlation behavior (matches firmware engine)."""
    state: str = "NORMAL"
    critical_countdown: int = 0
    critical_countdown_limit: int = 20   # 10s at 500ms
    deescalation_counter: int = 0
    deescalation_limit: int = 10         # 5s at 500ms
    emergency_latched: bool = False


# ---------------------------------------------------------------------------
# Thresholds (must match firmware/core thresholds exactly)
# ---------------------------------------------------------------------------

THRESHOLDS = {
    "temp_warning_c": 55.0,          # NTC temp above this = thermal anomaly
    "temp_critical_c": 65.0,         # NTC temp critical
    "dt_dt_warning_c_per_s": 0.5/60, # 0.5°C/min warning
    "delta_t_ambient_warning": 20.0,  # ΔT above ambient >= this = thermal
    "inter_module_dt_warn": 5.0,      # ΔT between modules warning
    "intra_module_dt_warn": 3.0,      # ΔT within module (NTC1 vs NTC2)
    "gas_warning_ratio": 0.70,       # gas ratio below this = gas anomaly
    "gas_critical_ratio": 0.40,      # gas ratio critical
    "pressure_warning_hpa": 2.0,     # pressure rise warning
    "pressure_critical_hpa": 5.0,    # pressure rise critical
    "swelling_warning_pct": 3.0,     # swelling above this = anomaly
    "current_warning_a": 180.0,      # >1.5C = 180A warning (full pack)
    "current_short_a": 350.0,        # short circuit
    "voltage_low_v": 260.0,          # pack voltage low (full pack)
    "voltage_high_v": 380.0,         # pack voltage high
    "r_int_warning_mohm": 0.55,      # group R_int warning
    "temp_emergency_c": 80.0,        # direct emergency
    "dt_dt_emergency_c_per_s": 5.0 / 60.0,  # 5°C/min
    "current_emergency_a": 500.0,    # full-pack direct emergency
}


# ---------------------------------------------------------------------------
# Helper: add realistic sensor noise
# ---------------------------------------------------------------------------

def _noise(base: float, noise_pct: float = 0.5) -> float:
    """Add small random noise to a sensor value (simulates ADC jitter)."""
    delta = base * (noise_pct / 100.0)
    return base + random.uniform(-delta, delta)


# ---------------------------------------------------------------------------
# Correlation engine (mirrors C firmware logic)
# ---------------------------------------------------------------------------

def evaluate_categories(reading: SensorReading) -> list:
    """Determine which anomaly categories are active.
    
    Full-pack version: checks 8 modules' NTCs, dual gas sensors,
    dual pressure sensors, and 8 swelling sensors.
    """
    cats = []
    
    # Electrical: high current, voltage bounds, or high R_int
    abs_current = abs(reading.current_a)
    if (abs_current > THRESHOLDS["current_warning_a"] or
        reading.voltage_v < THRESHOLDS["voltage_low_v"] or
        reading.voltage_v > THRESHOLDS.get("voltage_high_v", 999) or
        reading.r_internal_mohm > THRESHOLDS["r_int_warning_mohm"]):
        cats.append("electrical")
    
    # Thermal: check NTCs from modules if available, else legacy
    all_ntcs = []
    if reading.modules:
        for m in reading.modules:
            ntc1 = m.get('ntc1', 25)
            ntc2 = m.get('ntc2', 25)
            all_ntcs.extend([ntc1, ntc2])
    else:
        all_ntcs = [reading.temp_cell1_c, reading.temp_cell2_c,
                    reading.temp_cell3_c, reading.temp_cell4_c]
    
    if any(t > THRESHOLDS["temp_warning_c"] for t in all_ntcs):
        cats.append("thermal")
    if reading.dt_dt_max > THRESHOLDS["dt_dt_warning_c_per_s"]:
        if "thermal" not in cats:
            cats.append("thermal")
    
    # Ambient-compensated thermal check
    max_cell = max(all_ntcs) if all_ntcs else 25.0
    delta_t = max_cell - reading.temp_ambient_c
    if delta_t >= THRESHOLDS["delta_t_ambient_warning"]:
        if "thermal" not in cats:
            cats.append("thermal")
    
    # Inter-module ΔT detection
    if len(all_ntcs) >= 4:
        temp_spread = max(all_ntcs) - min(all_ntcs)
        if temp_spread > THRESHOLDS.get("inter_module_dt_warn", 5.0):
            if "thermal" not in cats:
                cats.append("thermal")
    
    # Gas: worst-case of dual sensors
    worst_gas = min(reading.gas_ratio_1, reading.gas_ratio_2) if reading.modules else reading.gas_ratio
    if worst_gas < THRESHOLDS["gas_warning_ratio"]:
        cats.append("gas")
    
    # Pressure: worst-case of dual sensors
    worst_pressure = max(reading.pressure_delta_1, reading.pressure_delta_2) if reading.modules else reading.pressure_delta_hpa
    if worst_pressure > THRESHOLDS["pressure_warning_hpa"]:
        cats.append("pressure")
    
    # Swelling: any module above threshold
    if reading.modules:
        for m in reading.modules:
            if m.get('swelling_pct', 0) > THRESHOLDS["swelling_warning_pct"]:
                cats.append("swelling")
                break
    elif reading.swelling_pct > THRESHOLDS["swelling_warning_pct"]:
        cats.append("swelling")
    
    return cats


def is_emergency_direct(reading: SensorReading) -> bool:
    """Single-parameter direct emergency bypass (firmware parity)."""
    all_ntcs = []
    if reading.modules:
        for m in reading.modules:
            all_ntcs.extend([m.get('ntc1', 25), m.get('ntc2', 25)])
    else:
        all_ntcs = [reading.temp_cell1_c, reading.temp_cell2_c,
                    reading.temp_cell3_c, reading.temp_cell4_c]
    max_temp = max(all_ntcs) if all_ntcs else 25.0
    return (
        max_temp > THRESHOLDS["temp_emergency_c"]
        or reading.dt_dt_max > THRESHOLDS["dt_dt_emergency_c_per_s"]
        or abs(reading.current_a) > THRESHOLDS["current_emergency_a"]
    )


def resolve_state(
    categories: list,
    short_circuit: bool,
    emergency_direct: bool = False,
    engine: CorrelationEngineState | None = None,
) -> str:
    """Determine system state from active categories.
    
    If `engine` is provided, uses stateful latch/countdown/de-escalation
    behavior to match firmware. Without `engine`, falls back to stateless
    mapping for ad-hoc callers.
    """
    count = len(categories)

    if engine is None:
        if short_circuit or emergency_direct:
            return "EMERGENCY"
        if count >= 3:
            return "EMERGENCY"
        if count >= 2:
            return "CRITICAL"
        if count == 1:
            return "WARNING"
        return "NORMAL"

    if engine.emergency_latched:
        return "EMERGENCY"

    if short_circuit or emergency_direct or count >= 3:
        engine.state = "EMERGENCY"
        engine.emergency_latched = True
        return engine.state

    if count >= 2:
        if engine.state != "CRITICAL":
            engine.state = "CRITICAL"
            engine.critical_countdown = 0
        engine.critical_countdown += 1
        engine.deescalation_counter = 0
        if engine.critical_countdown >= engine.critical_countdown_limit:
            engine.state = "EMERGENCY"
            engine.emergency_latched = True
        return engine.state

    if count == 1:
        engine.state = "WARNING"
        engine.critical_countdown = 0
        engine.deescalation_counter = 0
        return engine.state

    if engine.state != "NORMAL":
        engine.deescalation_counter += 1
        if engine.deescalation_counter >= engine.deescalation_limit:
            engine.state = "NORMAL"
            engine.deescalation_counter = 0
    engine.critical_countdown = 0
    return engine.state


# ---------------------------------------------------------------------------
# Scenario generators
# ---------------------------------------------------------------------------

def _scenario_normal(start_ms: int, duration_s: int = 30) -> list:
    """Scenario 1: Normal operation — everything within safe limits."""
    readings = []
    for i in range(0, duration_s * 2):  # 500ms steps
        t = start_ms + i * 500
        r = SensorReading(
            timestamp_ms=t,
            voltage_v=_noise(14.8, 0.3),
            current_a=_noise(2.0, 2.0),
            r_internal_mohm=_noise(45.0, 3.0),
            temp_cell1_c=_noise(28.0, 1.0),
            temp_cell2_c=_noise(28.5, 1.0),
            temp_cell3_c=_noise(27.8, 1.0),
            temp_cell4_c=_noise(28.2, 1.0),
            temp_ambient_c=_noise(25.0, 0.5),
            gas_ratio=_noise(0.98, 1.0),
            pressure_delta_hpa=_noise(0.2, 20.0),
            humidity_pct=_noise(45.0, 2.0),
            swelling_pct=_noise(2.0, 10.0),
            short_circuit=False,
        )
        r.active_categories = evaluate_categories(r)
        r.system_state = resolve_state(r.active_categories, r.short_circuit)
        readings.append(r)
    return readings


def _scenario_thermal_only(start_ms: int, duration_s: int = 40) -> list:
    """Scenario 2: Heat-only anomaly — temperature rises on cell 3.
    
    Should trigger WARNING but NOT emergency (single category).
    This proves the false-positive resistance of the correlation engine.
    """
    readings = []
    for i in range(0, duration_s * 2):
        t = start_ms + i * 500
        progress = i / (duration_s * 2)  # 0.0 to 1.0
        
        # Cell 3 heats up gradually, others stay normal
        cell3_temp = 28.0 + progress * 40.0  # rises to ~68°C
        
        r = SensorReading(
            timestamp_ms=t,
            voltage_v=_noise(14.7, 0.3),
            current_a=_noise(2.5, 2.0),
            r_internal_mohm=_noise(48.0, 3.0),
            temp_cell1_c=_noise(29.0, 1.0),
            temp_cell2_c=_noise(29.5, 1.0),
            temp_cell3_c=_noise(cell3_temp, 0.5),
            temp_cell4_c=_noise(29.2, 1.0),
            temp_ambient_c=_noise(25.5, 0.5),
            gas_ratio=_noise(0.95, 1.5),  # gas stays normal
            pressure_delta_hpa=_noise(0.3, 20.0),  # pressure stays normal
            humidity_pct=_noise(46.0, 2.0),
            swelling_pct=_noise(3.0, 10.0),
            short_circuit=False,
        )
        r.active_categories = evaluate_categories(r)
        r.system_state = resolve_state(r.active_categories, r.short_circuit)
        readings.append(r)
    return readings


def _scenario_gas_only(start_ms: int, duration_s: int = 30) -> list:
    """Scenario 3: Gas anomaly only — VOC detected, temp normal.
    
    Should trigger WARNING (single category).
    """
    readings = []
    for i in range(0, duration_s * 2):
        t = start_ms + i * 500
        progress = i / (duration_s * 2)
        
        # Gas ratio drops (simulating IPA vapor near BME680)
        gas = max(0.3, 0.98 - progress * 0.65)
        
        r = SensorReading(
            timestamp_ms=t,
            voltage_v=_noise(14.8, 0.3),
            current_a=_noise(2.0, 2.0),
            r_internal_mohm=_noise(45.0, 3.0),
            temp_cell1_c=_noise(28.0, 1.0),
            temp_cell2_c=_noise(28.0, 1.0),
            temp_cell3_c=_noise(28.0, 1.0),
            temp_cell4_c=_noise(28.0, 1.0),
            temp_ambient_c=_noise(25.0, 0.5),
            gas_ratio=_noise(gas, 1.0),
            pressure_delta_hpa=_noise(0.5, 20.0),
            humidity_pct=_noise(48.0, 2.0),
            swelling_pct=_noise(2.0, 10.0),
            short_circuit=False,
        )
        r.active_categories = evaluate_categories(r)
        r.system_state = resolve_state(r.active_categories, r.short_circuit)
        readings.append(r)
    return readings


def _scenario_multi_fault(start_ms: int, duration_s: int = 50) -> list:
    """Scenario 4: Heat + Gas — two categories → CRITICAL.
    
    Then adds pressure → three categories → EMERGENCY + disconnect.
    This is the most important scenario for the demo.
    """
    readings = []
    for i in range(0, duration_s * 2):
        t = start_ms + i * 500
        progress = i / (duration_s * 2)
        
        # Phase 1 (0-40%): Normal
        # Phase 2 (40-60%): Temperature rises
        # Phase 3 (60-80%): Gas also drops → CRITICAL
        # Phase 4 (80-100%): Pressure also rises → EMERGENCY
        
        if progress < 0.4:
            temp3 = 28.0
            gas = 0.97
            pressure = 0.3
        elif progress < 0.6:
            phase = (progress - 0.4) / 0.2  # 0 to 1 within this phase
            temp3 = 28.0 + phase * 35.0
            gas = 0.95
            pressure = 0.4
        elif progress < 0.8:
            phase = (progress - 0.6) / 0.2
            temp3 = 63.0 + phase * 5.0
            gas = 0.95 - phase * 0.60  # drops to ~0.35
            pressure = 0.5 + phase * 2.0
        else:
            phase = (progress - 0.8) / 0.2
            temp3 = 68.0
            gas = 0.35 - phase * 0.05
            pressure = 2.5 + phase * 15.0  # rises to ~17.5 hPa
        
        r = SensorReading(
            timestamp_ms=t,
            voltage_v=_noise(14.6 - progress * 0.5, 0.3),
            current_a=_noise(2.5 + progress * 1.0, 2.0),
            r_internal_mohm=_noise(50.0 + progress * 20.0, 3.0),
            temp_cell1_c=_noise(29.0, 1.0),
            temp_cell2_c=_noise(29.5, 1.0),
            temp_cell3_c=_noise(temp3, 0.5),
            temp_cell4_c=_noise(29.2, 1.0),
            temp_ambient_c=_noise(26.0, 0.5),
            gas_ratio=_noise(gas, 1.0),
            pressure_delta_hpa=_noise(pressure, 5.0),
            humidity_pct=_noise(50.0, 2.0),
            swelling_pct=_noise(5.0, 10.0),
            short_circuit=False,
        )
        r.active_categories = evaluate_categories(r)
        r.system_state = resolve_state(r.active_categories, r.short_circuit)
        readings.append(r)
    return readings


def _scenario_short_circuit(start_ms: int, duration_s: int = 15) -> list:
    """Scenario 5: Short circuit event — sudden current spike.
    
    Current jumps from 2A to 18A+ in one step. Should trigger
    EMERGENCY immediately via the fast loop (<100ms detection).
    """
    readings = []
    for i in range(0, duration_s * 2):
        t = start_ms + i * 500
        progress = i / (duration_s * 2)
        
        # Normal for first 60%, then sudden spike
        if progress < 0.6:
            current = 2.0
            voltage = 14.8
            short = False
        else:
            current = 18.0 + random.uniform(0, 3.0)
            voltage = 9.5 - random.uniform(0, 1.0)
            short = True
        
        r = SensorReading(
            timestamp_ms=t,
            voltage_v=_noise(voltage, 0.3),
            current_a=current,
            r_internal_mohm=_noise(45.0 if not short else 200.0, 3.0),
            temp_cell1_c=_noise(28.0, 1.0),
            temp_cell2_c=_noise(28.0, 1.0),
            temp_cell3_c=_noise(28.0, 1.0),
            temp_cell4_c=_noise(28.0, 1.0),
            temp_ambient_c=_noise(25.0, 0.5),
            gas_ratio=_noise(0.96, 1.0),
            pressure_delta_hpa=_noise(0.3, 20.0),
            humidity_pct=_noise(45.0, 2.0),
            swelling_pct=_noise(2.0, 10.0),
            short_circuit=short,
        )
        r.active_categories = evaluate_categories(r)
        r.system_state = resolve_state(r.active_categories, r.short_circuit)
        readings.append(r)
    return readings


def _scenario_recovery(start_ms: int, duration_s: int = 20) -> list:
    """Scenario 6: Recovery — system returns to NORMAL after a warning.
    
    Shows that the system correctly de-escalates when conditions improve.
    """
    readings = []
    for i in range(0, duration_s * 2):
        t = start_ms + i * 500
        progress = i / (duration_s * 2)
        
        # Warm at start (from previous scenario), cooling down
        temp3 = 60.0 - progress * 35.0  # cools from 60 to 25
        
        r = SensorReading(
            timestamp_ms=t,
            voltage_v=_noise(14.5 + progress * 0.3, 0.3),
            current_a=_noise(2.5, 2.0),
            r_internal_mohm=_noise(50.0 - progress * 10.0, 3.0),
            temp_cell1_c=_noise(29.0, 1.0),
            temp_cell2_c=_noise(29.0, 1.0),
            temp_cell3_c=_noise(temp3, 0.5),
            temp_cell4_c=_noise(29.0, 1.0),
            temp_ambient_c=_noise(25.0, 0.5),
            gas_ratio=_noise(0.90 + progress * 0.08, 1.0),
            pressure_delta_hpa=_noise(0.5, 20.0),
            humidity_pct=_noise(45.0, 2.0),
            swelling_pct=_noise(3.0, 10.0),
            short_circuit=False,
        )
        r.active_categories = evaluate_categories(r)
        r.system_state = resolve_state(r.active_categories, r.short_circuit)
        readings.append(r)
    return readings


# ---------------------------------------------------------------------------
# Public API: generate full demo sequence
# ---------------------------------------------------------------------------

def _scenario_ambient_compensation(start_ms: int, duration_s: int = 30) -> list:
    """Scenario 7: Ambient compensation — same temp, different outcomes.
    
    Phase A (0-15s): Cell=45°C, Ambient=25°C → ΔT=20 → WARNING
    Phase B (15-30s): Cell=45°C, Ambient=38°C → ΔT=7 → NORMAL
    
    This demonstrates the spec §3.3 ambient-compensated thresholds:
    the same absolute temperature is suspicious in cold ambient but
    expected in hot ambient (Indian summer conditions).
    """
    readings = []
    for i in range(0, duration_s * 2):
        t = start_ms + i * 500
        progress = i / (duration_s * 2)
        
        # All cells at ~45°C
        cell_temp = _noise(45.0, 0.3)
        
        if progress < 0.5:
            # Phase A: Cold ambient
            ambient = _noise(25.0, 0.5)
        else:
            # Phase B: Hot ambient (Indian summer)
            ambient = _noise(38.0, 0.5)
        
        r = SensorReading(
            timestamp_ms=t,
            voltage_v=_noise(14.8, 0.3),
            current_a=_noise(2.0, 2.0),
            r_internal_mohm=_noise(45.0, 3.0),
            temp_cell1_c=_noise(cell_temp, 0.3),
            temp_cell2_c=_noise(cell_temp, 0.3),
            temp_cell3_c=_noise(cell_temp, 0.3),
            temp_cell4_c=_noise(cell_temp, 0.3),
            temp_ambient_c=ambient,
            gas_ratio=_noise(0.98, 1.0),
            pressure_delta_hpa=_noise(0.2, 20.0),
            humidity_pct=_noise(45.0, 2.0),
            swelling_pct=_noise(2.0, 10.0),
            short_circuit=False,
        )
        r.active_categories = evaluate_categories(r)
        r.system_state = resolve_state(r.active_categories, r.short_circuit)
        readings.append(r)
    return readings


def generate_full_demo() -> list:
    """Generate the complete demo data sequence through all 7 scenarios.
    
    Returns a list of SensorReading objects with 500ms spacing.
    Total duration: ~215 seconds (~430 data points).
    """
    all_readings = []
    t = 0
    
    # Run each scenario in sequence
    scenarios = [
        ("Normal Operation", _scenario_normal, 30),
        ("Thermal Anomaly Only", _scenario_thermal_only, 40),
        ("Gas Anomaly Only", _scenario_gas_only, 30),
        ("Multi-Fault Escalation", _scenario_multi_fault, 50),
        ("Short Circuit", _scenario_short_circuit, 15),
        ("Recovery to Normal", _scenario_recovery, 20),
        ("Ambient Compensation", _scenario_ambient_compensation, 30),
    ]
    
    for name, func, duration in scenarios:
        readings = func(t, duration)
        # Tag each reading with scenario name for the dashboard
        for r in readings:
            r._scenario_name = name
        all_readings.extend(readings)
        t = readings[-1].timestamp_ms + 500

    # Recompute state for full-sequence parity with firmware latch/countdown logic.
    engine = CorrelationEngineState()
    for r in all_readings:
        r.active_categories = evaluate_categories(r)
        r.system_state = resolve_state(
            r.active_categories,
            r.short_circuit,
            emergency_direct=is_emergency_direct(r),
            engine=engine,
        )

    return all_readings


def generate_continuous(interval_ms: int = 500) -> Iterator[SensorReading]:
    """Yield sensor readings continuously, looping through the full demo.
    
    This is used by the live dashboard for continuous playback.
    """
    while True:
        for reading in generate_full_demo():
            yield reading
