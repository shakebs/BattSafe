# Hardware Handoff Runbook (Friend Setup)

Goal: plug one VSDSquadron board into a PC and run full end-to-end demo with both dashboards.

Pipeline used:
- Digital Twin generates 139-channel sensor stream.
- Stream goes to VSDSquadron over USB UART.
- Board runs anomaly/correlation logic.
- Board telemetry is read back over same USB and shown on output dashboard.

## 1) Environment Setup

From repo root:

```powershell
python -m venv .venv
.venv\Scripts\Activate.ps1
python -m pip install --upgrade pip
pip install -r 7_Demo\digital_twin\requirements.txt
pip install -r 7_Demo\dashboard\requirements.txt
pip install pyserial
```

## 2) Optional Pre-Flight Validation (No Board Needed)

```powershell
python -m unittest discover -s 7_Demo\dashboard\tests -v
3_Firmware\test_runner.exe
```

## 3) Build + Flash Board Firmware

Build:

```powershell
powershell -ExecutionPolicy Bypass -File 3_Firmware\target\build_target.ps1
```

Flash:

```powershell
python 3_Firmware\target\upload.py 3_Firmware\build\user.bin --port COM5
```

Replace `COM5` with actual board port.

## 4) Launch Full System

Recommended:

```powershell
powershell -ExecutionPolicy Bypass -File 7_Demo\run_twin_bridge.ps1 -BoardPort COM5
```

Opens:
- Input dashboard: `http://127.0.0.1:5001`
- Output dashboard: `http://127.0.0.1:5000`

## 5) Demo Flow for Validation

1. Confirm output dashboard shows `LIVE` and `Twin -> Board -> Dashboard` pipeline.
2. In input dashboard, inject one fault (for example `Thermal Runaway`, module/group specific).
3. Confirm output dashboard rises through warning/critical/emergency based on board logic.
4. Click input dashboard `Reset System to Normal`.
5. Confirm output dashboard de-escalates back to `NORMAL` after recovery window.
6. Click output dashboard `Reset`:
  - board logic restarts and reevaluates from fresh state.

## 6) Done Criteria

- Board connected and auto-detected (or explicit COM port works).
- Both dashboards visible and updating.
- Fault injection on input side changes output side state.
- Clearing/resetting input returns output to `NORMAL`.
- Output reset button triggers reevaluation and does not leave stale state.

## 7) Common Failure Fixes

- Output dashboard stuck on waiting:
  - check bridge server terminal for serial errors.
  - ensure COM port is not occupied.
- Upload fails handshake:
  - verify boot mode/jumper and try manual reset + rerun upload.
- Wrong mode on output server:
  - launch with `--twin-bridge`.
