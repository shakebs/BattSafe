# Pin Mapping Table

The firmware uses logical HAL mapping so it can run on host simulation and board targets. Exact pad numbers depend on the board revision and final wiring harness.

## Interface Allocation

| Function | Interface | Direction | Notes |
| --- | --- | --- | --- |
| Twin/telemetry link | UART0 | RX/TX | Input and output packet transport |
| Current/voltage sensor | I2C0 | Bidirectional | INA219 class interface |
| Gas + pressure sensor | I2C0 | Bidirectional | BME680 class interface |
| Analog thermal channels | ADC0 + MUX | Input | NTC channels via multiplexer |
| Analog gas/swelling channels | ADC1 + MUX | Input | Optional expansion inputs |
| Relay control | GPIO | Output | Emergency disconnect control |
| Buzzer control | GPIO | Output | Critical and emergency alarm |
| Status LEDs | GPIO | Output | State visualization |
| Comparator interrupts | GPIO/IRQ | Input | Optional hard fault interrupt path |

## Firmware References

- `3_Firmware/src/hal_gpio.c`
- `3_Firmware/src/hal_adc.c`
- `3_Firmware/src/hal_i2c.c`
- `3_Firmware/src/hal_uart.c`

## Notes

- Keep all logic lines in 3.3V domain.
- Do not drive relay coils directly from MCU pins.
- For final hardware, freeze exact pin numbers in this file and matching schematic revision.
