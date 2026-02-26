---
description: Run the live EV Battery Intelligence demo (board + dashboard + digital twin)
---

# Live Demo Workflow

## Two Websites

| Website | URL | Backing Process | Purpose |
|--------|-----|------------------|---------|
| Dashboard (board + twin bridge) | http://127.0.0.1:5050 | `dashboard/src/server.py` | Live board telemetry and web UI |
| Digital Twin | http://127.0.0.1:5001 | `digital_twin.main` | Full-scale 104S8P twin and `/api/status` source |

## Start Both Websites

### Terminal 1: Digital Twin
// turbo
```bash
cd /Users/mohammedomer/Docs/EV && source .venv/bin/activate && python3 -m digital_twin.main --no-serial
```

### Terminal 2: Dashboard (Twin -> Board -> Web)
// turbo
```bash
cd /Users/mohammedomer/Docs/EV && source .venv/bin/activate && python3 dashboard/src/server.py --twin-bridge --port /dev/cu.usbserial-110 --web-port 5050
```

Notes:
- Port `5000` may already be used by another app on this machine. Use `5050`.
- If your board port is different, run `python3 dashboard/src/serial_reader.py --list` and replace `/dev/cu.usbserial-110`.

## How Data Enters the Board (No Ambiguity)

When you run:

```bash
python3 dashboard/src/server.py --twin-bridge --port /dev/cu.usbserial-110 --web-port 5050
```

the server does this continuously (automatic loop):

1. Reads latest raw snapshot from Twin (`5001/api/status`)
2. Encodes it into board input packet (`0xBB`)
3. Sends packets to board over serial (continuous stream)
4. Reads board telemetry output (`0xAA`)
5. Pushes merged output to dashboard (`5050`)

So in twin-bridge mode:
- You are **not** manually injecting one value each time.
- A live stream is already running once server + serial are healthy.
- Digital Twin controls/faults change the upstream stream, and board processes that stream.
- Bridge loop is tuned for responsiveness (~20 Hz server bridge cycle).

## What Those Input Controls Actually Do

This is the exact behavior of the left-side controls in the Twin UI (`5001`):

| Control in Twin | What it changes in Twin | Does it affect board input stream? |
|---|---|---|
| **Discharge / Charge** | Sets operating mode (`set_operating_mode`) and current sign/magnitude from C-rate | **Yes** (via `pack_current`, then sent to board) |
| **C-Rate** | Sets target current magnitude | **Yes** (through `pack_current`) |
| **SOC slider** | Changes cell SOC and OCV baseline | **Indirectly yes** (affects pack voltage/current over time) |
| **Ambient slider** | Changes ambient thermal boundary | **Indirectly yes** (affects temps/gas/pressure evolution) |
| **Fault Injection buttons** | Applies physics-based anomalies in twin | **Yes** (results propagate into sent measurements) |
| **Speed / Time Jump** | Advances twin simulation faster | **Yes** (same stream, just time-evolved faster) |
| **Reset System** (Twin UI) | Resets twin state/faults | **Yes** (stream returns to nominal) |

Important detail:
- The bridge sends a compact subset to board every cycle: voltage/current, 4 temps, gas, pressure, swelling.
- Full 104S8P state is **not** sent byte-for-byte to board; it is compressed/scaled for the 4S prototype firmware.
- Example: pack voltage from 104S is scaled to 4S-equivalent before send.

## Health Check (Everything Working Fine)

### Browser checks
1. Open http://127.0.0.1:5001 and confirm the digital twin page loads.
2. Open http://127.0.0.1:5050 and confirm the web dashboard loads.
3. On `5050`, mode should move from `WAIT` to `LIVE` after telemetry arrives.
4. On `5050`, the top `Bridge:` status should move through:
   - `input received, awaiting board response` (pending)
   - then `board response received` with latency in ms.

### API checks
// turbo
```bash
curl -s http://127.0.0.1:5001/api/status | head -c 200
curl -s http://127.0.0.1:5050/api/status
```

Expected from `5050/api/status`:
- `"mode":"twin-bridge"`
- `"board_connected":true`
- `"latest"` object present
- `"latest.raw_data"` present (from twin `5001`)
- `"latest.intelligent_detection"` present (from board output)

## Exact Board Handling Sequence

Use this exact physical flow during demo runs:

1. Connect board via USB and leave it connected.
2. Start Twin (`5001`) and Dashboard server (`5050`) as above.
3. Wait ~3-8 seconds for serial open/reconnect.
4. If dashboard becomes `LIVE`, do nothing else. Let it run.
5. If dashboard is still `WAIT/NO DATA`, click **Detect Board** once.
6. If still no telemetry, press physical **RESET** button once.
7. During normal demo playback, keep board connected and untouched.
8. To restart scenario from beginning, press physical **RESET** once.

Important:
- You do **not** need to keep pressing reset during normal streaming.
- You do **not** need reflashing for every run.

## Reset vs Flashing (Important)

This is the most common confusion in live demos.

| Action | What it actually does | When to use |
|-------|-------------------------|-------------|
| Web **Reset** button (on dashboard UI) | Clears web charts/state in browser only. Does **not** reboot board. Does **not** flash firmware. | Clean up graphs before recording or restarting visualization |
| Web **Detect Board** button | Calls `/api/rescan` to reconnect serial port. Does **not** flash firmware. | UI shows `NO DATA` or board was unplugged/replugged |
| Physical **RESET** button on board | Hardware reboot of firmware already stored in flash | Restart the board scenario cycle from time zero |
| **Flashing firmware** (`upload.py`) | Rewrites program in board flash memory | Only when firmware changed, board has old build, or board is stuck/broken |

## Flashing Firmware Clearly

Use flashing only when needed.

### Flash command
// turbo
```bash
cd /Users/mohammedomer/Docs/EV && .venv/bin/python firmware/target/upload.py firmware/build/user.bin --port /dev/cu.usbserial-110 --baud 115200 --no-reset
```

### What to do during flashing
1. Start uploader command above.
2. Press physical **RESET** on board once (uploader catches bootloader handshake).
3. Wait for `Upload complete!` and `Sent ENTER`.
4. Confirm board output appears (uploader listens for output).

If no output appears after flash:
1. Press physical **RESET** once more.
2. Click **Detect Board** in web dashboard.

## Quick Troubleshooting

1. `5050` page loads but says `NO DATA`: click **Detect Board**, then press physical **RESET**.
2. Board still not detected: verify serial device path and cable, then retry.
3. Still failing after retries: reflash firmware once, then reset board.
