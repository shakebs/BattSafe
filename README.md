# BattSafe: Intelligent Thermal Anomaly Detection (Theme 2)

## Demo Video
https://drive.google.com/file/d/1m627qJw2Xilpu_7p-FZGwTNUh165RZEK/view?usp=drive_link

## Project Snapshot
| Item | Value |
| --- | --- |
| Theme | Theme 2: Thermal anomaly detection |
| Compute Platform | VSDSquadron ULTRA (THEJAS32) |
| Target Pack Model | 104S8P LFP reference architecture (832 cells) |
| Active Sensor/Signal Channels | 139 |
| Detection Categories | Electrical, Thermal, Gas, Pressure, Swelling |
| Safety Action | Relay disconnect + alarm on emergency |

## Problem Statement
Conventional temperature-only protection reacts late in the thermal runaway chain. BattSafe improves response time by correlating electrical, thermal, gas, pressure, and swelling evidence at the edge before catastrophic escalation.

## System Overview
| Block | Implementation |
| --- | --- |
| Firmware Core | `3_Firmware/src/main.c`, `anomaly_eval.c`, `correlation_engine.c` |
| Data Path | Twin/sensors -> VSDSquadron -> UART telemetry -> dashboard |
| Board Interfaces | ADC, I2C, UART, GPIO |
| Dashboard | `7_Demo/dashboard/src/server.py` |
| Digital Twin | `7_Demo/digital_twin/main.py` |

## What Runs on VSDSquadron ULTRA
| Loop | Normal Rate | Alert Rate | Core Responsibility |
| --- | --- | --- | --- |
| Fast | 100 ms | 20 ms | Short-circuit and fast electrical checks |
| Medium | 500 ms | 100 ms | Category detection + state transition |
| Slow | 5000 ms | 1000 ms | Pack/module telemetry emission |

State policy:
- `NORMAL`: 0 categories active
- `WARNING`: 1 category active
- `CRITICAL`: 2 categories active
- `EMERGENCY`: 3+ categories, short-circuit, or direct emergency trigger

## Validation Snapshot
| Test Case | Expected Behavior | Result |
| --- | --- | --- |
| Normal operation | Stay in `NORMAL` | Pass |
| Single anomaly | Escalate only to `WARNING` | Pass |
| Dual anomaly | Escalate to `CRITICAL` | Pass |
| Triple anomaly | Immediate `EMERGENCY` | Pass |
| Fast short-circuit | Emergency bypass path | Pass |

## Hardware Cost (INR)
- Detailed BOM (table): `2_Hardware/Bill_of_Materials.md`
- Raw BOM (CSV): `2_Hardware/Bill_of_Materials.csv`
- Estimated prototype total: **INR 2824**

## Repository Guide
| Path | Contents |
| --- | --- |
| `1_Project_Overview/` | Problem statement, architecture, block diagram |
| `2_Hardware/` | BOM, circuit diagram, pin mapping, safety checklist |
| `3_Firmware/` | Embedded C firmware, drivers, target tools, tests |
| `4_Algorithms/` | Sampling, filtering, detection logic, edge architecture |
| `5_Data/` | Raw/processed/fault CSV data and data format |
| `6_Validation/` | Test cases, calibration method, results, error analysis |
| `7_Demo/` | Video link, runbook, dashboard, digital twin |
| `8_Future_Scope/` | Scale-up strategy |
| `archive/reference_docs/` | Archived planning/reference docs (non-submission core) |

## Quick Start (Software-Only)
```powershell
python -m unittest discover -s 7_Demo\dashboard\tests -v
python 3_Firmware\tests\correlation_sim.py
python 7_Demo\dashboard\src\server.py --sim --host 127.0.0.1 --web-port 5000
```




