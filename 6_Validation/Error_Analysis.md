# Error Analysis

## False-Positive Sources and Mitigations
| Risk Source | Typical Failure Mode | Current Mitigation |
| --- | --- | --- |
| High ambient temperature | Thermal flags during hot weather | Ambient-compensated thermal checks |
| Current transients | Electrical spikes interpreted as faults | Multi-category correlation before escalation |
| Sensor drift | Slow offset in gas/NTC readings | Dual-sensor worst-case logic + threshold margins |
| Localized hidden cell fault in 8P | Voltage masking by parallel group behavior | Combined thermal + gas + pressure confirmation |

## Current Limits
| Limitation | Impact |
| --- | --- |
| Simulation-heavy evidence | Real sensor drift behavior not fully covered |
| Pack-specific thresholds | May need retuning for other chemistries/form factors |

## Next Improvement Actions
| Priority | Action |
| --- | --- |
| High | Hardware-in-loop long-duration runs |
| High | Controlled thermal and venting proxy experiments |
| Medium | Threshold tuning with expanded field dataset |
