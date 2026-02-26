# Detection Logic

`3_Firmware/src/anomaly_eval.c` maps incoming pack data to five anomaly categories.

## Category Triggers (Current Defaults)
| Category | Primary Signals | Warning Trigger |
| --- | --- | --- |
| Electrical | Pack voltage, group deviation, spread, current, internal resistance | Voltage outside 260-380 V, spread > 50 mV, group deviation > 15 mV, current > 180 A, or Rint > 0.55 mOhm |
| Thermal | NTC temperatures, dT/dt, module and pack delta-T | Temp > 55 C, dT/dt > 0.5 C/min, or thermal imbalance checks |
| Gas | Dual gas ratio inputs | Worst gas ratio < 0.70 |
| Pressure | Dual pressure delta inputs | Worst pressure delta > 2.0 hPa |
| Swelling | Module swelling input | Swelling > 3% |

## Direct Emergency Bypass
| Trigger Type | Condition |
| --- | --- |
| Thermal absolute | Temperature > 80 C |
| Thermal rate | dT/dt > 5 C/min |
| Electrical spike | Current > 500 A |
| Fast fault | Short-circuit flag set |

Any direct emergency trigger bypasses normal category counting and enters `EMERGENCY` immediately.

## Correlation State Machine
| Active Categories | State |
| --- | --- |
| 0 | `NORMAL` |
| 1 | `WARNING` |
| 2 | `CRITICAL` |
| 3 or more | `EMERGENCY` |

Emergency state is latched and released only after sustained nominal conditions.
