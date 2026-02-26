# Validation Results Summary

## Scope

Results below are from the current reproducible simulation track and firmware unit tests in this repository.

## Result Table

| ID | Result | Observation | Evidence |
| --- | --- | --- | --- |
| T1 | Pass | No categories active in baseline window, state remained `NORMAL` | `3_Firmware/tests/test_main.c`, `5_Data/Processed_Data_Output.csv` |
| T2 | Pass | Thermal-only anomaly produced `WARNING` without emergency jump | `3_Firmware/tests/test_main.c` |
| T3 | Pass | Gas-only anomaly produced `WARNING` | `3_Firmware/tests/test_main.c` |
| T4 | Pass | Two categories produced `CRITICAL` | `3_Firmware/tests/test_main.c` |
| T5 | Pass | Three categories produced immediate `EMERGENCY` | `3_Firmware/tests/test_main.c`, `5_Data/Fault_Test_Data.csv` |
| T6 | Pass | Short-circuit path triggered direct emergency | `3_Firmware/tests/test_main.c` |
| T7 | Pass | Emergency latch held until recovery window conditions were met | `3_Firmware/tests/test_main.c` |
| T8 | Pass | Ambient compensation changed decision for identical cell temperature | `3_Firmware/tests/test_main.c` |

## Honest Status

- Current submission evidence is simulation and software-in-the-loop focused.
- Firmware, state machine, and telemetry are ready for board path.
- Full live-sensor hardware evidence can be expanded in next iteration.
