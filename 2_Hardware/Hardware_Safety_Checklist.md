# Hardware Safety Checklist (Before Live EV Demo)

Use this checklist before connecting real sensors, cells, or load hardware.

## 1) Power and Voltage Domains

- Keep all THEJAS32 GPIO/I2C/UART/SPI signals in the 3.3V domain only.
- Do not apply 5V directly to any THEJAS32 I/O pin.
- Maintain board rails within datasheet ranges:
  - VDD (core): 1.08V to 1.32V operating range
  - VDDIO (I/O): 2.97V to 3.63V operating range
- Never exceed absolute limits:
  - VDD <= 1.5V
  - VDDIO <= 3.9V

## 2) GPIO Current and Driver Protection

- Do not drive relay coils or buzzers directly from MCU pins.
- Use a transistor/MOSFET driver stage for relay and buzzer outputs.
- Add a flyback diode across relay coils.
- Respect I/O current limits (datasheet):
  - 8mA per I/O for I2C/PWM/UART-class pins
  - 12mA per I/O for PROC_HB/GPIO/SPI-class pins

## 3) Boot and Fault Safety Behavior

- Relay must default to OPEN (battery disconnected) at boot.
- Allow manual/explicit enable to close relay only after sanity checks.
- Verify emergency path always forces relay OPEN.
- Verify emergency latch requires deliberate reset/acknowledgment.

## 4) Sensor and Bus Wiring

- I2C pull-ups must be tied to 3.3V (not 5V).
- Confirm shared ground between controller, sensors, and power stage.
- Verify polarity and orientation for INA219, BME680, and ADC inputs.
- Keep analog sensor lines away from switching/high-current wiring.

## 5) Pack and High-Current Path

- Use fuse/current limiting on battery input during bench testing.
- Keep contactor/relay current rating above worst-case pack current.
- Use proper wire gauge and insulated terminals for pack path.
- Keep high-current return path physically separated from logic ground where possible.

## 6) Thermal and Physical Safety

- Keep board operating environment within 0C to +70C.
- Keep hot-cell/heat-gun region physically isolated from controller board.
- Mount sensors and relay so accidental shorting is not possible.
- Keep Class C fire extinguisher and insulated gloves available during tests.

## 7) ESD and Handling

- Use ESD-safe handling during wiring/rework.
- Power down and discharge before rewiring.
- Avoid touching exposed pins while powered.

## 8) Pre-Demo Quick Checks

- Confirm relay logic with no battery connected first.
- Confirm `NORMAL -> WARNING -> CRITICAL -> EMERGENCY` transitions on simulation.
- Confirm UART telemetry packets decode correctly in dashboard.
- Confirm no component exceeds safe touch temperature before live test.

## Datasheet Sources

- `/Users/mohammedomer/Docs/EV/THEJAS32_Datasheet.pdf` (electrical limits, current, temperature, I/O characteristics)
- `/Users/mohammedomer/Docs/EV/datasheet_ultra.pdf` (board-level interface/power overview)
- `/Users/mohammedomer/Docs/EV/DS-VEGA_ET1031 V1.0.pdf` (core architecture context)
