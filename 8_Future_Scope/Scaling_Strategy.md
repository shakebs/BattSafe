# Scaling Strategy

## 1) Hardware Scale-Up

- Move from prototype sensor count to full pack instrumentation.
- Add production-grade AFEs and isolated sensing front-end.
- Harden relay/contactor stage for automotive current levels.

## 2) Validation Scale-Up

- Add hardware-in-loop and controlled abuse tests.
- Capture long-duration logs for drift and false-positive analysis.
- Build threshold tuning dataset from real drive and charge cycles.

## 3) Algorithm Scale-Up

- Keep deterministic correlation as safety baseline.
- Add optional anomaly scoring layer for pre-warning ranking.
- Maintain explainable outputs for service diagnostics.

## 4) System Integration

- Integrate with BMS/VCU over CAN for vehicle-level actions.
- Add cloud reporting only for observability, not safety dependence.
- Package as module-level building block for larger EV platforms.
