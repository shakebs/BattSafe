#!/usr/bin/env python3
"""Minimal log viewer for simulation and early hardware logs."""

import csv
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
LOG_PATH = REPO_ROOT / "5_Data" / "logs" / "sim_transition_log.csv"


def main() -> None:
    if not LOG_PATH.exists():
        print(f"Log not found: {LOG_PATH}")
        return

    with LOG_PATH.open("r", encoding="utf-8") as f:
        rows = list(csv.DictReader(f))

    if not rows:
        print("Log is empty")
        return

    print("t_ms | state | categories | V | I | T | gas_ratio | dP")
    print("-" * 85)
    for r in rows:
        print(
            f"{r['t_ms']:>5} | {r['state']:<9} | {r['categories']:<24} | "
            f"{float(r['voltage_v']):>4.1f} | {float(r['current_a']):>4.1f} | "
            f"{float(r['temp_c']):>4.1f} | {float(r['gas_ratio']):>8.2f} | "
            f"{float(r['pressure_delta_hpa']):>4.1f}"
        )


if __name__ == "__main__":
    main()
