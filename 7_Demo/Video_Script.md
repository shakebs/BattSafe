# Demo Video Script (Max 5 Minutes)

## 0:00 - 0:30 Problem and Theme

- Theme 2: thermal anomaly detection.
- Explain why temperature-only protection is late.
- State goal: early multi-signal detection on VSDSquadron ULTRA.

## 0:30 - 1:20 Architecture Walkthrough

- Show firmware loop structure (fast, medium, slow).
- Show five anomaly categories and state machine.
- Show emergency action path (relay disconnect).

## 1:20 - 2:30 Live Demo: Normal then Single Anomaly

- Run baseline and show `NORMAL` stability.
- Inject one disturbance (thermal or gas only).
- Show controlled `WARNING` without emergency overreaction.

## 2:30 - 3:40 Live Demo: Correlated Fault

- Inject dual and triple anomalies.
- Show `CRITICAL` then `EMERGENCY` transition.
- Show alarm/disconnect behavior and telemetry evidence.

## 3:40 - 4:30 Repository Proof

- Show structure from `1_Project_Overview` to `8_Future_Scope`.
- Open firmware detection files and validation results.
- Show data samples used for evidence.

## 4:30 - 5:00 Close

- Summarize: edge-first, correlation-based, safety-focused.
- Mention immediate next step: expanded hardware validation.
