# Prototype Demo Video Script (6-8 minutes)

## Segment 1: Problem + Approach (0:00-0:30)

- Thermal runaway precursors appear before temperature spikes.
- Our edge system correlates electrical, thermal, gas, pressure, and swelling signals.

## Segment 2: Hardware Overview (0:30-1:30)

- Show VSDSquadron ULTRA, sensor wiring, relay path, and power setup.
- Mention safety default (relay off/open on boot).

## Segment 3: Firmware Architecture (1:30-2:30)

- Explain fast/medium/slow loops.
- Explain anomaly categories and state machine transitions.

## Segment 4: Normal Operation (2:30-3:15)

- Dashboard live stream in stable condition.
- Confirm `NORMAL` state and baseline behavior.

## Segment 5: False-Positive Demonstration (3:15-4:15)

- Inject single-mode fault (heat only or gas only).
- Show it does not jump directly to emergency.

## Segment 6: Multi-Modal Fault Demonstration (4:15-5:45)

- Inject dual or triple anomalies.
- Show `CRITICAL/EMERGENCY` transition and relay cutoff.

## Segment 7: GitHub Repository Walkthrough (5:45-6:45)

- Show firmware modules, dashboard, test plan, and test results.
- Show reproducible run instructions.

## Segment 8: Close (6:45-7:15)

- Summary: edge-only safety logic, reduced false positives, real-time action.
- Future scope: scaling and TinyML.
