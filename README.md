# BattSafe: Intelligent Thermal Anomaly Detection (Theme 2)

## Demo Video
https://drive.google.com/file/d/1m627qJw2Xilpu_7p-FZGwTNUh165RZEK/view?usp=drive_link

## Project Snapshot
| Item | Value |
| --- | --- |
| Theme | Theme 2: Thermal anomaly detection |
| Compute Platform | VSDSquadron ULTRA (THEJAS32) |
| Pack Architecture Modeled | 104S8P LFP reference pack (832 cells) |
| Active Sensor/Signal Channels | 139 |
| Detection Categories | Electrical, Thermal, Gas, Pressure, Swelling |
| Runtime Validation | Board-in-loop tested (twin -> board -> dashboard) |
| Safety Action | Relay disconnect + alarm on emergency |

## Sensor Count Summary (Current Architecture)
| Sensor/Channel Group | Count |
| --- | ---: |
| Parallel-group voltage channels | 104 |
| Pack voltage | 1 |
| Pack current | 1 |
| Isolation | 1 |
| Cell NTC channels | 16 |
| Ambient + coolant temperature channels | 3 |
| Gas channels | 2 |
| Pressure channels | 2 |
| Humidity channel | 1 |
| Swelling channels | 8 |
| **Total active channels** | **139** |

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

## Hardware Cost (INR)
- Full-pack architecture BOM: `2_Hardware/Bill_of_Materials.md`
- Machine-readable BOM: `2_Hardware/Bill_of_Materials.csv`
- Price source log: `2_Hardware/Cost_References_2026-02-26.md`
- Estimated full architecture total (with industrial isolation monitor): **INR 83743.86**
- Estimated full architecture total (without industrial isolation monitor): **INR 38386.86**

## Validation Snapshot
| Test Case | Expected Behavior | Result |
| --- | --- | --- |
| Normal operation | Stay in `NORMAL` | Pass |
| Single anomaly | Escalate only to `WARNING` | Pass |
| Dual anomaly | Escalate to `CRITICAL` | Pass |
| Triple anomaly | Immediate `EMERGENCY` | Pass |
| Fast short-circuit | Emergency bypass path | Pass |
| Board-in-loop run | Firmware executes on board and returns telemetry over UART | Pass |

## Repository Guide
| Path | Contents |
| --- | --- |
| `1_Project_Overview/` | Problem statement, architecture, block diagram |
| `2_Hardware/` | BOM, cost references, circuit diagram, pin mapping, safety checklist |
| `3_Firmware/` | Embedded C firmware, drivers, target tools, tests |
| `4_Algorithms/` | Sampling, filtering, detection logic, edge architecture |
| `5_Data/` | Raw/processed/fault CSV data and data format |
| `6_Validation/` | Test cases, calibration method, results, error analysis |
| `7_Demo/` | Video link, runbook, dashboard, digital twin |
| `8_Future_Scope/` | Scale-up strategy |
| `archive/reference_docs/` | Archived planning/reference docs |

## Quick Start (Board-In-Loop)
```powershell
# Terminal 1
python -m digital_twin.main --no-serial

# Terminal 2
python 7_Demo\dashboard\src\server.py --twin-bridge --port COM5 --host 127.0.0.1 --web-port 5000 --twin-url http://127.0.0.1:5001
```

Board build/flash instructions:
- `3_Firmware/Build_Instructions.md`
