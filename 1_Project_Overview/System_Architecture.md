# System Architecture

## End-to-End Runtime Flow
| Step | Module | Output |
| --- | --- | --- |
| 1 | Input stream from digital twin or sensors | Full pack snapshot (139 channels) |
| 2 | `3_Firmware/src/main.c` scheduler | Fast/medium/slow loop execution |
| 3 | `3_Firmware/src/anomaly_eval.c` | Active anomaly categories and derived metrics |
| 4 | `3_Firmware/src/correlation_engine.c` | `NORMAL` / `WARNING` / `CRITICAL` / `EMERGENCY` |
| 5 | Safety output control | Relay, buzzer, status LED actions |
| 6 | `3_Firmware/src/packet_format.c` | UART telemetry frames |
| 7 | `7_Demo/dashboard/src/server.py` | Live visualization and logs |

## Loop Design
| Loop | Normal Period | Alert Period | Purpose |
| --- | --- | --- | --- |
| Fast | 100 ms | 20 ms | Rapid electrical and short-circuit checks |
| Medium | 500 ms | 100 ms | Category evaluation and state transitions |
| Slow | 5000 ms | 1000 ms | Pack and module telemetry transmission |

## Runtime Block Diagram
```text
Digital Twin / Sensors
        |
        v
[Input Frames / HAL]
        |
        v
[Fast Loop] -> [Medium Loop] -> [Correlation Engine]
                                |            |
                                |            +-> [Safety Outputs]
                                v
                          [Slow Loop Telemetry]
                                |
                                v
                          [Dashboard + Logs]
```

## Core Implementation Files
- `3_Firmware/src/main.c`
- `3_Firmware/src/anomaly_eval.c`
- `3_Firmware/src/correlation_engine.c`
- `3_Firmware/src/packet_format.c`
- `7_Demo/dashboard/src/server.py`
- `7_Demo/digital_twin/main.py`
