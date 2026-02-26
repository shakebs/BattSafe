# Prototype Round Playbook (Feb 16, 2026 to Feb 26, 2026)

This playbook is optimized for your current situation: new to embedded systems, 10-day prototype window, and VSDSquadron ULTRA arriving soon.

## 1. Competition Deliverables (Non-Negotiable)

By **February 26, 2026**, prepare:
1. A **working prototype**
2. A **prototype demonstration video**
3. A **GitHub repository** with code + documentation

## 2. Scope Lock (Critical)

Your latest submitted proposal (`proposal_v2.pdf`) is a **4S prototype**. Earlier docs mention 12S.

Lock the prototype scope now to avoid failure from overreach:
1. **4S architecture only** for this round
2. **Core sensors**: NTC, INA219, BME680, FSR (or simulated FSR if unavailable)
3. **Core feature to prove**: multi-modal correlation engine prevents false positives

## 3. Definition of Done (What judges must see)

A successful demo must visibly prove:
1. Real-time sensing on VSDSquadron ULTRA
2. 3-speed loop execution (fast/medium/slow)
3. Correlation logic:
- 1 anomaly category => warning
- 2 categories => critical
- 3+ categories => emergency and relay cut-off
4. False-positive resistance (single-mode anomaly does not trigger emergency)
5. Live telemetry on dashboard + clean GitHub docs

## 4. Date-Wise Execution Plan

## Feb 16 (Today)
1. Freeze architecture and pin map
2. Create GitHub repo structure
3. Write interface contracts (`sensor_read`, `state_update`, `packet_encode`)
4. Build a host-side simulator for the correlation engine with synthetic sensor data

## Feb 17
1. Implement state machine logic (Normal/Warning/Critical/Emergency)
2. Implement anomaly category evaluator (Electrical/Thermal/Gas/Pressure/Swelling)
3. Create 6 test scenarios from proposal (normal, heat-only, gas-only, heat+gas, pressure, short-circuit)

## Feb 18
1. Implement UART packet format and parser
2. Build minimal Python dashboard (live values + state + anomaly flags)
3. Record baseline simulation logs and expected outputs

## Feb 19 (Hardware arrival target)
1. Board bring-up: blink LED, UART print, I2C scan, ADC read
2. Integrate INA219 and BME680 first
3. Keep simulator fallback for missing sensors

## Feb 20
1. Add NTC via MUX and validate conversion
2. Add relay control path with safe defaults (boot = relay OFF/open)
3. Integrate full anomaly evaluator on real inputs

## Feb 21
1. End-to-end loop timing validation (100 ms / 500 ms / 5 s)
2. Run correlation tests on hardware
3. Tune thresholds once using controlled experiments

## Feb 22
1. Execute validation matrix and log evidence (CSV + timestamped notes)
2. Capture first demo video draft
3. Fix reliability issues only (no new features)

## Feb 23
1. Refine dashboard readability
2. Finalize wiring photos, block diagram, and architecture figure
3. Improve README with setup and reproducible test instructions

## Feb 24
1. Re-run all tests and record final demo footage
2. Prepare "single anomaly vs multi anomaly" side-by-side evidence
3. Prepare quick backup demo (simulated input stream) in case sensor fails

## Feb 25
1. Documentation freeze
2. Final repo audit (clean run instructions, no broken links, tags/releases optional)
3. Export final video + upload backup copies

## Feb 26 (Submission Day)
1. Final dry run on full flow (hardware + dashboard + narration)
2. Submit all required links/assets before deadline cutoff
3. Keep a short fallback clip + zipped evidence pack ready

## 5. Hardware Bring-Up Checklist (First 2 Hours)

1. Power rail check (multimeter, no sensor connected)
2. Flash test firmware and print boot banner on UART
3. I2C scan should detect INA219 and BME680 addresses
4. ADC raw read should change when touching NTC/FSR line
5. Relay pin toggles only when commanded (default safe state)
6. Watchdog or fail-safe reset path enabled

## 6. Minimum Firmware Module Breakdown

1. `hal/`:
- I2C, ADC, GPIO, UART wrappers

2. `drivers/`:
- `ina219.c`
- `bme680.c`
- `ntc_mux.c`
- `fsr.c`

3. `core/`:
- `anomaly_eval.c`
- `correlation_engine.c`
- `state_machine.c`
- `loop_scheduler.c`

4. `app/`:
- `main.c`
- `packet_format.c`

5. `tests/`:
- Host simulation tests for state transitions and edge cases

## 7. Validation Matrix You Should Actually Run

1. Normal operation for 20+ minutes => remains Normal
2. Heat only => Warning only
3. Gas only => Warning or Critical based on threshold
4. Heat + Gas => Critical/Emergency transition
5. Pressure spike + any other anomaly => Emergency path check
6. Fast electrical event (short/load step) => sub-100 ms detection path
7. Relay disconnect command => physically verified response

## 8. Demo Video Script (6 to 8 minutes)

1. Problem and approach (30s)
2. Hardware overview and sensor stack (60s)
3. Firmware architecture and loop rates (60s)
4. Live normal operation dashboard (45s)
5. Single-anomaly test (false-positive rejection) (60s)
6. Multi-anomaly test (critical/emergency + relay cut-off) (90s)
7. GitHub walkthrough (code, docs, test logs) (45s)
8. Closing summary and future scope (30s)

## 9. GitHub Repository Checklist

Create this structure:

```
firmware/
  core/
  drivers/
  hal/
  app/
  tests/
dashboard/
  src/
  requirements.txt
hardware/
  wiring_diagram.png
  bom.csv
docs/
  architecture.md
  test_plan.md
  test_results.md
  video_script.md
data/
  logs/
README.md
LICENSE
```

README must include:
1. What problem is solved
2. Hardware list and wiring
3. Build/flash steps
4. How to run dashboard
5. How to reproduce each validation test
6. Known limitations

## 10. Risk Controls (Do These Early)

1. Keep one **simulation mode** that feeds prerecorded sensor values
2. Use feature flags for optional sensors
3. Avoid adding new sensors after Feb 22
4. Record every test as short clips + CSV logs immediately
5. Keep backup power supply, jumper wires, and spare sensor modules

## 11. If VSDSquadron ULTRA Arrives Late

Still stay in the competition with a staged fallback:
1. Run full logic engine and dashboard on simulated stream
2. Show at least one live sensor on substitute MCU if needed
3. Once board arrives, move same logic with minimal API changes

## 12. Next Actions (Do Immediately)

1. Create the GitHub repo today and push docs + simulator skeleton
2. Freeze to 4S scope and align all docs to avoid judge confusion
3. Start with host-side correlation engine tests before hardware arrives
4. Share one daily progress post in your team (what works, what broke, next day target)
