# System Architecture (Prototype Round)

## Scope

This prototype implements the proposal-v2 safety logic on VSDSquadron ULTRA using simulated sensor inputs in firmware, with the same data flow and interfaces that real sensors will use.

## End-to-End Flow

1. Sensor snapshot is produced each scheduler cycle.
2. `anomaly_eval` converts raw readings into a 5-category anomaly bitmask.
3. `correlation_engine` maps active category count to state:
   - `NORMAL` (0)
   - `WARNING` (1)
   - `CRITICAL` (2)
   - `EMERGENCY` (3+ or short-circuit)
4. Safety outputs are driven by state (LED/buzzer and relay disconnect in emergency).
5. A fixed 32-byte telemetry packet is emitted on UART.
6. Dashboard visualizes electrical, thermal, gas/pressure, and state history.

## Scheduler Design

The firmware uses a 3-speed cooperative scheduler:

- Normal mode:
  - Fast loop: 100 ms
  - Medium loop: 500 ms
  - Slow loop: 5000 ms
- Alert mode (any anomaly/state above normal):
  - Fast loop: 20 ms
  - Medium loop: 100 ms
  - Slow loop: 1000 ms

This keeps baseline compute usage low and increases resolution only during events.

## Firmware Modules

- `/Users/mohammedomer/Docs/EV/firmware/app/main.c`
  - Scheduler, simulation input injection, control loop coordination.
- `/Users/mohammedomer/Docs/EV/firmware/core/anomaly_eval.c`
  - Category-level anomaly detection thresholds and bitmask generation.
- `/Users/mohammedomer/Docs/EV/firmware/core/correlation_engine.c`
  - State-machine transitions, countdown behavior, emergency latch.
- `/Users/mohammedomer/Docs/EV/firmware/app/packet_format.c`
  - 32-byte UART packet encoding/checksum.

## Dashboard Modes

- `--sim`: replay full 7-scenario synthetic run.
- `--serial`: parse live board UART packets.
- `--csv FILE`: replay previously logged data.

## Current Limitation

Physical sensors are not connected in this round, so firmware input comes from deterministic simulation profiles. Driver interfaces are kept in the repo for hardware integration in the next phase.
