# Validation Test Cases

Validation is run against the same firmware logic used in deployment (`anomaly_eval` + `correlation_engine`).

## Core Cases

| ID | Test Case | Stimulus | Expected Outcome |
| --- | --- | --- | --- |
| T1 | Normal baseline | Nominal pack conditions | Stay in `NORMAL` |
| T2 | Thermal-only anomaly | Local hotspot in one module | `WARNING` only |
| T3 | Gas-only anomaly | Gas ratio drop without thermal rise | `WARNING` only |
| T4 | Dual anomaly | Thermal + gas | `CRITICAL` |
| T5 | Triple anomaly | Thermal + gas + pressure | Immediate `EMERGENCY` |
| T6 | Fast electrical fault | Short-circuit signature | Immediate `EMERGENCY` bypass |
| T7 | Emergency recovery behavior | Return to nominal after emergency | Latch holds, then controlled release |
| T8 | Ambient compensation | Same cell temp with different ambient values | Different decisions (`WARNING` vs normal) |

## Evidence Sources

- `3_Firmware/tests/test_main.c`
- `5_Data/logs/sim_transition_log.csv`
- `5_Data/Raw_Data_Sample.csv`
- `5_Data/Processed_Data_Output.csv`
