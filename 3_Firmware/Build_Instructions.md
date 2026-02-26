# VSDSquadron Firmware Build + Flash Guide

This guide is for flashing the firmware to a real VSDSquadron ULTRA board and running the full
Digital Twin -> USB -> Board -> USB -> Output Dashboard pipeline.

## 1) Prerequisites

### Software
- Python 3.10+ (`python --version`)
- `pip install pyserial`
- RISC-V bare-metal toolchain in `PATH`:
  - `riscv64-unknown-elf-gcc`
  - `riscv64-unknown-elf-objcopy`
  - `riscv64-unknown-elf-size`

### Hardware
- VSDSquadron ULTRA board
- USB cable (data-capable)
- Board visible as serial port (`COMx` on Windows)

## 2) Build Firmware for THEJAS32

From repository root:

```powershell
powershell -ExecutionPolicy Bypass -File 3_Firmware\target\build_target.ps1
```

If your toolchain prefix differs, pass it explicitly:

```powershell
powershell -ExecutionPolicy Bypass -File 3_Firmware\target\build_target.ps1 -ToolPrefix riscv64-unknown-elf-
```

Build outputs:
- `3_Firmware/build/user.elf`
- `3_Firmware/build/user.bin`
- `3_Firmware/build/user.map`

## 3) Flash Firmware to Board

```powershell
python 3_Firmware\target\upload.py 3_Firmware\build\user.bin --port COM5
```

Notes:
- Omit `--port` to auto-detect.
- Uploader performs DTR/RTS reset + XMODEM transfer.
- Keep board in UART boot mode (as required by your board jumper settings).

## 4) Quick Firmware Health Check

After upload, you should see board UART output including startup/self-check lines.

Expected behavior:
- Self-check passes.
- Telemetry frames stream continuously.
- If no external input arrives, firmware falls back to internal simulation.

## 5) Run Full System with Real Board (No Physical Sensors Needed)

Use digital twin as input source and board as real inference target.

### Option A: one launcher script

```powershell
powershell -ExecutionPolicy Bypass -File 7_Demo\run_twin_bridge.ps1 -BoardPort COM5
```

### Option B: manual (2 terminals)

Terminal 1 (Digital Twin input dashboard):

```powershell
cd 7_Demo
python -m digital_twin.main --no-serial
```

Terminal 2 (bridge to board + output dashboard):

```powershell
python 7_Demo\dashboard\src\server.py --twin-bridge --port COM5 --host 127.0.0.1 --web-port 5000 --twin-url http://127.0.0.1:5001
```

Open:
- Input dashboard: `http://127.0.0.1:5001`
- Output dashboard: `http://127.0.0.1:5000`

## 6) Runtime Reset Behavior

- Clearing faults/resetting the twin to nominal makes output return to `NORMAL` after recovery hold.
- Output dashboard `Reset` triggers logic restart.
- In board/twin-bridge mode, reset also attempts USB DTR/RTS pulse reset of firmware.

## 7) Troubleshooting

- No board detected:
  - Check Device Manager for COM port.
  - Re-run with explicit `--port COMx`.
- Upload times out waiting for handshake:
  - Verify UART boot mode/jumper state.
  - Try `--no-reset` and manual board reset.
- Output dashboard stuck waiting:
  - Confirm twin at `:5001` and bridge server at `:5000`.
  - Confirm board serial port is not opened by another app.
