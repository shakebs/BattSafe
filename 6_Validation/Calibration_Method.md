# Calibration Method

## Calibration Workflow
| Step | Scope | Method | Output |
| --- | --- | --- | --- |
| 1 | Electrical baseline | Validate voltage/current scaling and idle spread | Stable nominal electrical reference |
| 2 | Thermal baseline | Capture steady NTC values and initialize history | Reliable delta-T and dT/dt baseline |
| 3 | Gas and pressure baseline | Record clean-air ratios and enclosure pressure deltas | Baseline for anomaly thresholds |
| 4 | Correlation timing | Validate state thresholds, holds, and latch behavior | Deterministic transition behavior |
| 5 | End-to-end verification | Run tests and dashboard pipeline checks | Submission-ready validation evidence |

## Verification Targets
- `3_Firmware/tests/test_main.c`
- `7_Demo/dashboard/tests/test_virtual_board.py`
- `5_Data/logs/sim_transition_log.csv`
