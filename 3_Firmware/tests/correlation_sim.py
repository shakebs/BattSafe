#!/usr/bin/env python3
"""Host-side correlation engine simulation for rapid logic validation.

This script does not require hardware. It helps verify state transitions before
board bring-up.
"""

from dataclasses import dataclass
from enum import Enum, auto
from pathlib import Path
import csv


class State(Enum):
    NORMAL = auto()
    WARNING = auto()
    CRITICAL = auto()
    EMERGENCY = auto()


@dataclass
class Sample:
    t_ms: int
    voltage_v: float
    current_a: float
    temp_c: float
    dt_dt_max: float
    gas_ratio: float      # <1 means higher VOC activity
    pressure_delta_hpa: float
    swelling_pct: float
    short_event: bool = False


THRESHOLDS = {
    "temp_warning_c": 55.0,
    "dt_dt_warning_c_per_s": 2.0,
    "gas_warning_ratio": 0.70,
    "pressure_warning_hpa": 5.0,
    "swelling_warning_pct": 30.0,
    "current_warning_a": 8.0,
    "current_short_a": 15.0,
    "voltage_low_v": 12.0,
    "temp_emergency_c": 80.0,
    "dt_dt_emergency_c_per_s": 5.0 / 60.0,
    "current_emergency_a": 20.0,
}


def detect_categories(s: Sample) -> set[str]:
    cats: set[str] = set()

    electrical = s.current_a > THRESHOLDS["current_warning_a"] or s.voltage_v < THRESHOLDS["voltage_low_v"]
    thermal = (
        s.temp_c > THRESHOLDS["temp_warning_c"]
        or s.dt_dt_max > THRESHOLDS["dt_dt_warning_c_per_s"]
    )
    gas = s.gas_ratio < THRESHOLDS["gas_warning_ratio"]
    pressure = s.pressure_delta_hpa > THRESHOLDS["pressure_warning_hpa"]
    swelling = s.swelling_pct > THRESHOLDS["swelling_warning_pct"]

    if electrical:
        cats.add("electrical")
    if thermal:
        cats.add("thermal")
    if gas:
        cats.add("gas")
    if pressure:
        cats.add("pressure")
    if swelling:
        cats.add("swelling")

    return cats


def is_emergency_direct(s: Sample) -> bool:
    return (
        s.temp_c > THRESHOLDS["temp_emergency_c"]
        or s.dt_dt_max > THRESHOLDS["dt_dt_emergency_c_per_s"]
        or s.current_a > THRESHOLDS["current_emergency_a"]
    )


class CorrelationEngine:
    def __init__(self) -> None:
        self.state = State.NORMAL
        self.emergency_latched = False
        self.critical_countdown = 0
        self.critical_countdown_limit = 20
        self.deescalation_counter = 0
        self.deescalation_limit = 10

    def update(self, categories: set[str], short_event: bool, emergency_direct: bool) -> State:
        if self.emergency_latched:
            return State.EMERGENCY

        count = len(categories)
        if short_event or emergency_direct or count >= 3:
            self.state = State.EMERGENCY
            self.emergency_latched = True
            return self.state

        if count >= 2:
            if self.state != State.CRITICAL:
                self.state = State.CRITICAL
                self.critical_countdown = 0
            self.critical_countdown += 1
            self.deescalation_counter = 0
            if self.critical_countdown >= self.critical_countdown_limit:
                self.state = State.EMERGENCY
                self.emergency_latched = True
            return self.state

        if count == 1:
            self.state = State.WARNING
            self.critical_countdown = 0
            self.deescalation_counter = 0
            return self.state

        if self.state != State.NORMAL:
            self.deescalation_counter += 1
            if self.deescalation_counter >= self.deescalation_limit:
                self.state = State.NORMAL
                self.deescalation_counter = 0
        self.critical_countdown = 0
        return self.state


def build_demo_samples() -> list[Sample]:
    return [
        Sample(0, 14.8, 2.0, 31.0, 0.01, 1.00, 0.0, 0.0),
        Sample(5_000, 14.7, 2.2, 31.5, 0.02, 0.99, 0.1, 0.0),
        # Heat-only disturbance: should go warning
        Sample(10_000, 14.6, 2.5, 58.2, 0.03, 0.98, 0.2, 0.0),
        # Heat + gas: should go critical
        Sample(15_000, 14.5, 2.7, 59.0, 0.04, 0.55, 0.8, 0.0),
        # Heat + gas + pressure: should go emergency
        Sample(20_000, 14.3, 3.0, 61.0, 0.05, 0.42, 8.7, 0.0),
        # Electrical event (state remains latched emergency after prior event)
        Sample(25_000, 11.5, 9.4, 50.0, 0.02, 0.95, 0.6, 0.0),
        # Short event hard-emergency
        Sample(30_000, 9.8, 18.0, 53.0, 0.02, 0.92, 0.5, 0.0, short_event=True),
    ]


def main() -> None:
    samples = build_demo_samples()
    out_rows = []

    engine = CorrelationEngine()
    prev_state = State.NORMAL
    print("t_ms, categories, state")

    for s in samples:
        cats = detect_categories(s)
        emergency_direct = is_emergency_direct(s)
        state = engine.update(cats, s.short_event, emergency_direct)

        if state != prev_state:
            print(f"{s.t_ms}, {sorted(cats)}, {state.name} <- transition")
        else:
            print(f"{s.t_ms}, {sorted(cats)}, {state.name}")

        out_rows.append(
            {
                "t_ms": s.t_ms,
                "voltage_v": s.voltage_v,
                "current_a": s.current_a,
                "temp_c": s.temp_c,
                "dt_dt_max": s.dt_dt_max,
                "gas_ratio": s.gas_ratio,
                "pressure_delta_hpa": s.pressure_delta_hpa,
                "swelling_pct": s.swelling_pct,
                "short_event": int(s.short_event),
                "categories": "|".join(sorted(cats)),
                "state": state.name,
            }
        )
        prev_state = state

    out_path = Path("/Users/mohammedomer/Docs/EV/data/logs/sim_transition_log.csv")
    out_path.parent.mkdir(parents=True, exist_ok=True)
    with out_path.open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=list(out_rows[0].keys()))
        writer.writeheader()
        writer.writerows(out_rows)

    print(f"\nWrote {out_path}")


if __name__ == "__main__":
    main()
