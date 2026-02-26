# Bill of Materials (INR)

Pricing basis: India market web prices captured on **2026-02-26**.

## A) Full-Pack Sensor Architecture BOM (104S8P, 139 channels)

| Category | Component | Qty | Unit Cost (INR) | Subtotal (INR) | Notes |
| --- | --- | ---: | ---: | ---: | --- |
| Controller | VSDSquadron ULTRA (THEJAS32) | 1 | 3999.00 | 3999.00 | Main edge compute board |
| Voltage Monitoring | LTC6811 multi-cell AFE IC (equivalent) | 9 | 2215.19 | 19936.71 | 9 x 12 channels -> 108 channels for 104 group voltage taps |
| Pack Voltage Sensing | LEM LV-25P voltage transducer | 1 | 5664.32 | 5664.32 | Pack-level HV voltage sensing |
| Pack Current Sensing | LEM HAS-500S hall current transducer | 1 | 4699.83 | 4699.83 | Pack-level current sensing |
| Temperature Sensing | NTC 10k 3950 thermistor | 19 | 10.00 | 190.00 | 16 cell-surface + 1 ambient + 2 coolant points |
| Gas + Pressure Sensing | BME680 module | 2 | 729.00 | 1458.00 | Dual-end VOC + pressure + humidity |
| Gas Sensing (Optional) | MQ-8 hydrogen gas sensor | 1 | 89.00 | 89.00 | Optional dedicated H2 channel |
| Swelling Sensing | FSR402 force sensing resistor | 8 | 160.00 | 1280.00 | 1 per module end-plate (ideal full-pack) |
| Analog Expansion | CD74HC4067 analog multiplexer | 2 | 46.00 | 92.00 | Auxiliary analog expansion |
| Auxiliary ADC | ADS1115 16-bit ADC module | 2 | 208.00 | 416.00 | Higher-resolution aux acquisition |
| Safety Actuation | 5V single-channel relay module | 1 | 35.00 | 35.00 | Disconnect control stage driver |
| Alerting | Active buzzer module | 1 | 27.00 | 27.00 | Audible warning path |
| Integration Materials | Wiring/connectors/protoboard | 1 | 500.00 | 500.00 | Bench integration materials |
| Isolation Monitoring (Industrial) | Siemens 3UG4582 insulation relay | 1 | 45357.00 | 45357.00 | Optional industrial-grade insulation monitoring |

### Totals

| Total Type | Amount (INR) |
| --- | ---: |
| Total with industrial isolation monitor | **83743.86** |
| Total without industrial isolation monitor | **38386.86** |

## B) Sensor Count Alignment (Current Repo Architecture)

| Sensor/Channel Group | Count |
| --- | ---: |
| Parallel-group voltage channels | 104 |
| Pack voltage | 1 |
| Pack current | 1 |
| Isolation | 1 |
| Cell NTC channels | 16 |
| Ambient + coolant temperature channels | 3 |
| Gas channels | 2 |
| Pressure channels | 2 |
| Humidity channel | 1 |
| Swelling channels | 8 |
| **Total active channels** | **139** |

## C) Notes

- This BOM is in **INR only**.
- It reflects the full 104S8P architecture used by the firmware logic and telemetry model.
- The board has been run in board-in-loop mode (`twin -> board -> dashboard`) and not only in host simulation mode.
