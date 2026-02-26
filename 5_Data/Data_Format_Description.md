# Data Format Description

## UART Telemetry Frames (Board to Dashboard)
| Field | Value |
| --- | --- |
| Sync byte | `0xAA` |
| Frame types | `0x01` pack summary, `0x02` module detail |
| Integrity | XOR checksum per frame |
| Slow-loop payload | 1 pack frame + 8 module frames |

Reference files:
- `3_Firmware/src/packet_format.h`
- `3_Firmware/src/packet_format.c`

## Input Frames (Twin to Board)
| Field | Value |
| --- | --- |
| Sync byte | `0xBB` |
| Source | `7_Demo/digital_twin/serial_bridge.py` |
| Purpose | Stream pack and module input data into board firmware |

Reference files:
- `7_Demo/digital_twin/serial_bridge.py`
- `3_Firmware/src/input_packet.h`

## CSV Data Files
| File | Purpose |
| --- | --- |
| `Raw_Data_Sample.csv` | Representative unprocessed time-series sample |
| `Processed_Data_Output.csv` | Derived features, decisions, and states |
| `Fault_Test_Data.csv` | Fault-case expected vs observed outcomes |

## Units
| Signal | Unit |
| --- | --- |
| Voltage | V |
| Current | A |
| Temperature | C |
| Pressure delta | hPa |
| Gas ratio | unitless normalized ratio |
| dT/dt | C/min |
