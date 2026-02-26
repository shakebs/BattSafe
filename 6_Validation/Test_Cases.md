# Prototype Validation Test Plan

## Objective

Demonstrate that multi-modal correlation improves safety and reduces false positives.

## Test Matrix

| ID | Test | Stimulus | Expected Result | Evidence |
|---|---|---|---|---|
| T1 | Normal stability | No fault injection for 20+ min | State remains `NORMAL` | CSV log + short video |
| T2 | Thermal-only anomaly | Local heat input on one cell | `WARNING` only, no emergency | Dashboard video + state log |
| T3 | Gas-only anomaly | IPA vapor near BME680 | Gas flag active; no forced emergency if single mode | Sensor log |
| T4 | Pressure-only anomaly | Controlled pressure rise in enclosure | Pressure anomaly flag set | Sensor log |
| T5 | Dual anomaly | Heat + gas combined | State escalates to `CRITICAL` then policy action | Video + CSV + transition timestamps |
| T6 | Multi anomaly | Heat + gas + pressure | `EMERGENCY`, relay disconnect | Video + relay proof |
| T7 | Electrical fast event | Load step / short-circuit simulation | Fast-loop response under 100 ms path | Timing log |
| T8 | Recovery behavior | Remove fault conditions | Expected de-escalation behavior or latch rule | State trace |

## Logging Format

Each record should include:

1. Timestamp (ms)
2. Pack voltage/current
3. Cell temperature summary (max, mean, dT/dt)
4. Gas ratio
5. Pressure delta
6. Swelling estimate
7. Active anomaly bitmask
8. State
9. Relay state

## Acceptance Criteria

1. Single-mode disturbances do not directly trigger emergency cutoff
2. Correlated multi-mode events trigger escalation correctly
3. Emergency action path is deterministic and logged
4. Demo flow is repeatable with clear evidence
