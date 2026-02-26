# Edge vs Cloud Architecture

## Edge (Mandatory Safety Path)

All safety decisions are made on VSDSquadron ULTRA:
- anomaly category evaluation
- state machine transitions
- relay and buzzer actuation
- emergency latch handling

If network or dashboard fails, edge safety logic still runs.

## Cloud/Dashboard (Observability Path)

The dashboard is used for:
- visualization
- operator awareness
- replay and debugging
- demo evidence capture

No cloud dependency is required for emergency action.

## Practical Outcome

This split keeps response latency low and keeps safety deterministic.
