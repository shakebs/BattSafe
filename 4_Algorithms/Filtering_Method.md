# Filtering Method

The current implementation uses deterministic feature extraction and threshold logic.

## Signal Processing Summary
| Domain | Method | Purpose |
| --- | --- | --- |
| Electrical | Per-module mean and per-group deviation, pack-wide spread | Detect voltage imbalance and masked faults |
| Thermal | NTC history and absolute dT/dt | Detect fast thermal rise and local hotspots |
| Thermal Gradient | Intra-module and inter-module delta-T | Detect localized vs pack-wide thermal behavior |
| Gas | Worst-case ratio across dual sensors | Preserve sensitivity to localized venting |
| Pressure | Worst-case delta across dual sensors | Confirm enclosure pressure events |
| Context | Ambient-compensated thermal delta | Reduce false positives in high ambient conditions |

## Output to Detection Stage
The filtering stage outputs derived metrics that directly drive category flags:
- electrical
- thermal
- gas
- pressure
- swelling
