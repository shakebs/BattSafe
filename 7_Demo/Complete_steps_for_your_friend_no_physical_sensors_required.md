# Complete Board-in-Loop Setup Steps (no external sensor rig required)

1. Open PowerShell at repo root.

```powershell
cd C:\Users\Shakeb\BattSafe\BattSafe
```

2. Create env and install Python deps.

```powershell
python -m venv .venv
.venv\Scripts\Activate.ps1
python -m pip install --upgrade pip
pip install -r 7_Demo\digital_twin\requirements.txt
pip install -r 7_Demo\dashboard\requirements.txt
pip install pyserial
```

3. Install RISC-V GCC toolchain and ensure these commands exist:
- `riscv64-unknown-elf-gcc`
- `riscv64-unknown-elf-objcopy`
- `riscv64-unknown-elf-size`

4. Build firmware.

```powershell
powershell -ExecutionPolicy Bypass -File 3_Firmware\target\build_target.ps1
```

5. Find board COM port (after plugging VSDSquadron).

```powershell
Get-CimInstance Win32_SerialPort | Select-Object DeviceID,Name
```

6. Flash firmware.

```powershell
python 3_Firmware\target\upload.py 3_Firmware\build\user.bin --port COM5
```

Replace `COM5`.

7. Launch full twin->board->dashboard pipeline.

```powershell
powershell -ExecutionPolicy Bypass -File 7_Demo\run_twin_bridge.ps1 -BoardPort COM5
```

8. Open dashboards:
- Input: `http://127.0.0.1:5001`
- Output: `http://127.0.0.1:5000`

9. Validation flow (board-tested path):
- Inject fault in input dashboard -> output should escalate.
- Click input `Reset System to Normal` -> output should return to `NORMAL` after recovery hold.
- Click output `Reset` -> board logic restart + reevaluation.
