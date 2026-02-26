# Problem Statement

Battery thermal runaway does not start with a single high temperature event. In practical packs, it is usually preceded by a sequence of weak signals: subtle voltage spread, internal resistance drift, local heating, gas evolution, and pressure rise.

Most low-cost monitoring setups react only when absolute temperature is already high. That reduces available response time and increases both fire risk and false alarms.

This project implements a correlation-based edge monitor on VSDSquadron ULTRA. The system tracks five independent anomaly categories (electrical, thermal, gas, pressure, swelling) and escalates action only when multi-domain evidence is present. This improves early detection while keeping false positives under control.

Scope for this submission:
- Full-pack logic modeled for a 104S8P LFP architecture
- Firmware state machine running on VSDSquadron ULTRA
- Digital twin and dashboard used for repeatable validation and demo
