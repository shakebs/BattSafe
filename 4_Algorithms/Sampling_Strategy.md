# Sampling Strategy

## Adaptive Scheduler
| System State | Fast Loop | Medium Loop | Slow Loop |
| --- | --- | --- | --- |
| Normal | 100 ms | 500 ms | 5000 ms |
| Alert (`WARNING` and above) | 20 ms | 100 ms | 1000 ms |

## Loop Responsibilities
| Loop | Responsibilities |
| --- | --- |
| Fast | High-current detection, short-circuit handling, emergency pre-check |
| Medium | Derived metric computation, anomaly category evaluation, state update |
| Slow | Telemetry framing and transmission for dashboard/logging |

## Design Rationale
| Goal | Approach |
| --- | --- |
| Low baseline compute load | Slower loops during stable operation |
| Fast reaction in anomalies | Automatic loop acceleration in alert states |
| Deterministic behavior | Fixed-rate scheduler with explicit state policy |
