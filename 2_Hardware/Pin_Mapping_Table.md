# Pin Mapping Table

This file documents the full-pack interface plan for the 104S8P architecture. Firmware uses HAL abstraction, so exact physical pad numbers can be finalized per board revision without changing core logic.

## Full-Pack Interface Allocation (139 Channels)

| Function | Interface | Direction | Notes |
| --- | --- | --- | --- |
| Twin/telemetry transport | UART0 | RX/TX | `0xBB` input frames and `0xAA` telemetry output |
| Cell-voltage AFE chain | SPI / isolated bridge | Bidirectional | 9 x LTC6811-class AFEs for 104 group voltage channels |
| Pack voltage transducer | ADC0 | Input | Isolated HV voltage mirror channel |
| Pack current transducer | ADC1 | Input | Hall current channel (pack-level) |
| NTC network | ADC + MUX banks | Input | 16 cell-surface NTC + ambient + coolant channels |
| Swelling network | ADC + MUX banks | Input | Up to 8 module end-plate force channels |
| Optional hydrogen channel | ADC2 | Input | MQ-8 optional dedicated H2 channel |
| Dual gas/pressure sensors | I2C0 | Bidirectional | 2 x BME680 (VOC + pressure + humidity) |
| Isolation monitor input | GPIO / ADC | Input | Isolation fault status/measurement channel |
| Relay/contactor control | GPIO | Output | Emergency disconnect path |
| Buzzer control | GPIO | Output | Audible critical/emergency indication |
| Status LEDs | GPIO | Output | System state indication |
| Fast fault interrupt | GPIO/IRQ | Input | Optional hardware comparator interrupt path |

## Firmware References

- `3_Firmware/src/main.c`
- `3_Firmware/src/anomaly_eval.c`
- `3_Firmware/src/correlation_engine.c`
- `3_Firmware/src/hal_gpio.c`
- `3_Firmware/src/hal_adc.c`
- `3_Firmware/src/hal_i2c.c`
- `3_Firmware/src/hal_uart.c`

## Notes

- Keep logic I/O in 3.3V domain.
- Do not drive relay coils directly from MCU pins.
- Freeze exact pad numbers and schematic net names before hardware handoff.
- Sensor count reference: `2_Hardware/Sensor_Count_Summary.md`.
