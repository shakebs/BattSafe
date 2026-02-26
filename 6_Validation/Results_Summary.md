# Validation Results Summary

## Scope

Validation in this repository includes both:
- host-side firmware logic tests
- board-in-loop execution on VSDSquadron ULTRA using twin-fed input over UART

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
| T9 | Pass | Board path executed full detection loop and streamed telemetry to dashboard | `3_Firmware/Build_Instructions.md`, `7_Demo/Hardware_Handoff_Runbook.md` |

## Current Status

- Firmware anomaly and correlation engine are board-runnable and tested in board-in-loop mode.
- Dashboard and twin-bridge pipeline validate real board telemetry flow.
- Additional physical-sensor expansion can be layered on top of this board-validated baseline.
