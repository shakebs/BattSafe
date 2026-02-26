# Hardware Safety Checklist

Use this checklist before connecting live cells or running fault-injection demos.

## 1) Power Domain and Electrical Limits

- Keep MCU I/O in 3.3V domain only.
- Never apply 5V directly to THEJAS32 GPIO.
- Verify board supply rails are within datasheet limits.
- Add fuse or current-limited supply during bench bring-up.

## 2) Driver and Protection

- Use transistor or MOSFET stage for relay and buzzer.
- Add flyback diode for relay coils.
- Verify relay defaults to open (disconnect) on boot.
- Keep emergency path independent from UI/dashboard path.

## 3) Wiring and Grounding

- Confirm common ground across controller and sensors.
- Keep analog lines away from high-current switching traces.
- Validate polarity for INA219 and sensor connectors.
- Ensure I2C pull-ups are to 3.3V.

## 4) Thermal and Mechanical Safety

- Isolate heat source from controller PCB.
- Secure cells and wiring to prevent shorts from vibration.
- Keep extinguisher and PPE available during tests.

## 5) Pre-Demo Functional Checks

- Confirm state transitions in safe simulated mode first.
- Confirm relay disconnect action on emergency condition.
- Confirm UART telemetry is decoded correctly by dashboard.
- Confirm no component exceeds safe temperature during dry run.

## Datasheet References

- `2_Hardware/Sensor_Datasheets/datasheet_ultra.pdf`
- `2_Hardware/Sensor_Datasheets/DS-VEGA_ET1031 V1.0.pdf`
- `2_Hardware/Sensor_Datasheets/THEJAS32_Datasheet.pdf`
