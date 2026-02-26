# EV Battery Thermal Anomaly Detection — Complete Parameter Reference

> **Purpose:** This document is the master reference for all thermal anomaly indicators, their detection priority, sensor requirements, sampling strategies, and correlation with external factors. It is designed to inform the next phase of algorithm and logic development for the VSDSquadron Ultra–based BMS.

---

## 1. Thermal Runaway — Event Sequence (Chronological Priority)

Understanding the **exact order** in which events manifest is critical. A thermal runaway is not instantaneous — it follows a predictable cascade. Detecting the earliest precursors buys the most response time.

```
┌─────────────────────────────────────────────────────────────────────────┐
│  STAGE 0 — NORMAL OPERATION                                           │
│  Cell within nominal V, I, T envelope                                 │
├───────────────────────────── ▼ ─────────────────────────────────────────┤
│  STAGE 1 — EARLY WARNING  (minutes to hours before runaway)           │
│  ➊ Internal resistance rise (subtle, < 5 % drift)                    │
│  ➋ Voltage deviation from expected (mV-level drop under load)        │
│  ➌ Micro-current anomalies (self-discharge increase)                 │
├───────────────────────────── ▼ ─────────────────────────────────────────┤
│  STAGE 2 — SEI DECOMPOSITION  (~80–120 °C internally)                │
│  ➍ dT/dt starts rising (self-heating begins, >0.2 °C/min)           │
│  ➎ Cell-to-cell temperature imbalance grows                          │
│  ➏ Gas generation begins inside cell (CO₂, CO first)                │
├───────────────────────────── ▼ ─────────────────────────────────────────┤
│  STAGE 3 — PRESSURE BUILD-UP & VENTING  (~120–150 °C)               │
│  ➐ Internal pressure rises → cell swelling / bulging                 │
│  ➑ Safety vent opens → gas release (H₂, CO, CO₂, HF, CH₄, C₂H₄)   │
│  ➒ Pressure spike detectable by sensor (<2 ms response)              │
├───────────────────────────── ▼ ─────────────────────────────────────────┤
│  STAGE 4 — SEPARATOR FAILURE & ISC  (~150–200+ °C)                  │
│  ➓ Sharp voltage drop toward 0 V (internal short circuit)            │
│  ⓫ Current spike (massive, uncontrolled)                             │
│  ⓬ Rapid temperature rise (>10 °C/min, self-accelerating)           │
├───────────────────────────── ▼ ─────────────────────────────────────────┤
│  STAGE 5 — THERMAL RUNAWAY  (~200 °C+)                              │
│  ⓭ Fire / explosion / full cell failure                              │
│  ⓮ Propagation to adjacent cells                                     │
└─────────────────────────────────────────────────────────────────────────┘
```

> **Key takeaway:** The system must catch anomalies at **Stage 1–2** (electrical + early thermal) to have any meaningful response window. Stage 3 (gas/pressure) is the **last reliable pre-runaway indicator**. By Stage 4, damage is already happening.

---

## 2. Complete Anomaly List — Prioritized

### PRIORITY 1 — Continuous Fast-Loop Monitoring (Core Safety)

These are the **electrical and thermal parameters** that change first and are cheapest to sample. They form the always-on detection backbone.

| # | Anomaly | What to Measure | Detection Threshold / Logic | Sampling Rate | Sensor Type |
|---|---------|----------------|----------------------------|---------------|-------------|
| 1.1 | **Voltage drop from expected** | Individual cell voltage | >50 mV deviation from model-predicted V under same SOC/load/temp | **100 ms** (10 Hz) | Voltage divider → ADC |
| 1.2 | **Internal resistance rise** | V–I relationship (ΔV/ΔI during load steps) | >10% rise from baseline R₀ at same temp and SOH | **100 ms** (computed per load cycle) | Derived from V + I sensors |
| 1.3 | **Current spike / anomaly** | Pack current (charge & discharge) | Sudden >20% deviation from expected C-rate, or any current when contactors should be open | **100 ms** (10 Hz) | Hall-effect or shunt current sensor → ADC |
| 1.4 | **Surface temperature** | Cell surface temp | Absolute >55 °C warning, >65 °C critical | **500 ms** (2 Hz) | NTC thermistor (10kΩ) or thermocouple |
| 1.5 | **dT/dt (rate of temp rise)** | Derivative of temperature over time | >1 °C/min warning, >5 °C/min critical, >10 °C/min = runaway onset | **500 ms** (computed from temp samples) | Derived from temperature sensors |
| 1.6 | **Cell-to-cell temp imbalance** | ΔT between cells | >5 °C warning, >10 °C critical between any two cells | **1 s** (1 Hz) | Multiple NTC thermistors |
| 1.7 | **Short circuit detection** | Sudden V drop + current surge simultaneously | V drops > 200 mV AND current exceeds 2× expected within 10 ms | **100 ms** (hardware interrupt preferred) | Combined V + I threshold with comparator |

### PRIORITY 2 — Medium-Loop Monitoring (Gas & Pressure — Confirmatory Indicators)

These parameters change at **Stage 3** and confirm that internal damage has occurred. They are slower-onset but extremely high-confidence indicators.

| # | Anomaly | What to Measure | Detection Threshold / Logic | Sampling Rate | Sensor Type |
|---|---------|----------------|----------------------------|---------------|-------------|
| 2.1 | **Gas release — H₂** | Hydrogen concentration in pack enclosure | >50 ppm = warning, >200 ppm = critical | **1 s** (1 Hz) | MQ-8 or electrochemical H₂ sensor |
| 2.2 | **Gas release — CO** | Carbon monoxide concentration | >10 ppm = warning, >50 ppm = critical | **1 s** (1 Hz) | MQ-7 or electrochemical CO sensor |
| 2.3 | **Gas release — CO₂** | Carbon dioxide concentration | >1000 ppm above baseline = warning | **2 s** (0.5 Hz) | MQ-135 or NDIR CO₂ sensor |
| 2.4 | **Gas release — HF** | Hydrogen fluoride (extremely toxic) | ANY detection = critical alarm | **2 s** (0.5 Hz) | Electrochemical HF sensor |
| 2.5 | **Gas release — VOCs / hydrocarbons** | CH₄, C₂H₄, electrolyte vapors | >100 ppm combined = warning | **2 s** (0.5 Hz) | MQ-2 or MQ-4 (flammable gas) |
| 2.6 | **Internal pressure build-up** | Pack enclosure pressure or cell-level pressure | >5% rise from baseline = warning, >15% = critical | **500 ms** (2 Hz) | BMP280/BMP388 barometric or strain gauge |
| 2.7 | **Cell swelling / bulging** | Physical deformation / displacement | Any measurable displacement > 0.5 mm | **5 s** (0.2 Hz) | Strain gauge or flex sensor on cell surface |

#### Complete Gas Emissions During Thermal Runaway

| Gas | Formula | Source | Hazard | Detectable Before Vent? |
|-----|---------|--------|--------|------------------------|
| Hydrogen | H₂ | Electrolyte decomposition | Highly flammable, explosive | Yes (early at ~100 °C) |
| Carbon Monoxide | CO | Electrolyte oxidation | Toxic + flammable | Yes |
| Carbon Dioxide | CO₂ | Cathode decomposition, combustion | Asphyxiant at high conc. | Yes |
| Hydrogen Fluoride | HF | LiPF₆ salt decomposition | Extremely toxic (lethal) | At vent stage |
| Methane | CH₄ | Electrolyte decomposition | Flammable | At vent stage |
| Ethylene | C₂H₄ | Electrolyte decomposition | Flammable | At vent stage |
| Ethane | C₂H₆ | Electrolyte decomposition | Flammable | At vent stage |
| Acetylene | C₂H₂ | High-temp decomposition | Flammable | Late stage |
| Propylene | C₃H₆ | Electrolyte decomposition | Flammable | Late stage |
| Phosphorous Oxyfluoride | POF₃ | LiPF₆ + moisture | Toxic | At vent stage |
| Hydrogen Chloride | HCl | Plastic/separator decomposition | Toxic, corrosive | Late stage |
| Hydrogen Cyanide | HCN | Nitrogen-containing components | Extremely toxic | Late stage |
| Ammonia | NH₃ | Nitrogen-containing components | Toxic, irritant | Late stage |

> **Practical note:** For an embedded BMS, monitoring **H₂ + CO + CO₂** gives the best coverage-to-complexity ratio. HF is critical for safety alarms but sensors are expensive. VOC/hydrocarbon sensor (MQ-2) provides cheap broad-spectrum backup.

### PRIORITY 3 — Slow-Loop / Background Monitoring (External Factors & Context)

These factors don't directly indicate runaway but **modulate all thresholds** and provide context for the algorithm to reduce false positives and improve accuracy.

| # | Factor | What to Measure | Why It Matters | Sampling Rate | Sensor Type |
|---|--------|----------------|----------------|---------------|-------------|
| 3.1 | **Ambient temperature** | Environment temp around pack | Shifts all thermal thresholds; hot ambient = lower margin | **5 s** (0.2 Hz) | DHT22 / DS18B20 / NTC |
| 3.2 | **C-rate (charge/discharge rate)** | Current magnitude relative to capacity | High C-rate = expected higher temps, adjust thresholds | **Derived from I sensor** (continuous) | Computed |
| 3.3 | **Charging vs discharging state** | Direction of current flow | Different anomaly profiles; charging = more sensitive to overcharge | **Continuous** (from I sensor polarity) | Computed |
| 3.4 | **SOH (State of Health)** | Capacity fade, resistance trend over lifetime | Aged cells have higher R₀, narrower safe operating window | **Every charge cycle** (background) | Computed from V/I history |
| 3.5 | **Battery age / cycle count** | Number of charge-discharge cycles | Adjusts all baselines; older = higher risk | **Per cycle** (stored in memory) | Computed / stored |
| 3.6 | **Cooling system efficiency** | Coolant flow rate or fan speed + resulting ΔT | Cooling failure = thermal anomaly even without cell fault | **2 s** (0.5 Hz) | Flow sensor or tachometer |
| 3.7 | **Vibration / impact detection** | Acceleration / shock | Mechanical damage → internal short circuit risk | **Interrupt-driven** + **10 ms** burst capture | ADXL345 / MPU6050 accelerometer |
| 3.8 | **Operating conditions** | Speed, load demand, terrain | Correlate thermal events with driving patterns | **1 s** (1 Hz) | CAN bus / vehicle data |

---

## 3. Sampling Strategy — Loop Architecture

The system should run **three nested monitoring loops** to balance responsiveness vs. computational load:

```
╔══════════════════════════════════════════════════════════════════════╗
║  FAST LOOP — 100 ms (10 Hz)                                        ║
║  ┌──────────────────────────────────────────────────────────────┐   ║
║  │  • Cell voltages (all cells via multiplexer)                │   ║
║  │  • Pack current (charge/discharge)                          │   ║
║  │  • Short circuit detection (V + I cross-check)              │   ║
║  │  • Internal resistance estimation (during load transients)  │   ║
║  └──────────────────────────────────────────────────────────────┘   ║
╠══════════════════════════════════════════════════════════════════════╣
║  MEDIUM LOOP — 500 ms to 1 s (1–2 Hz)                              ║
║  ┌──────────────────────────────────────────────────────────────┐   ║
║  │  • All temperature sensors (surface, ambient)               │   ║
║  │  • dT/dt computation                                        │   ║
║  │  • Cell-to-cell ΔT comparison                               │   ║
║  │  • Gas sensors (H₂, CO, CO₂, VOCs)                         │   ║
║  │  • Pressure sensor                                          │   ║
║  │  • Cooling system status                                    │   ║
║  └──────────────────────────────────────────────────────────────┘   ║
╠══════════════════════════════════════════════════════════════════════╣
║  SLOW LOOP — 5 s to per-cycle                                      ║
║  ┌──────────────────────────────────────────────────────────────┐   ║
║  │  • Ambient temperature                                      │   ║
║  │  • Cell swelling / strain gauges                             │   ║
║  │  • SOH computation                                          │   ║
║  │  • Cycle count update                                       │   ║
║  │  • Vibration baseline                                       │   ║
║  │  • Threshold adaptation (based on SOH, ambient, C-rate)     │   ║
║  └──────────────────────────────────────────────────────────────┘   ║
╠══════════════════════════════════════════════════════════════════════╣
║  INTERRUPT-DRIVEN (asynchronous, immediate)                         ║
║  ┌──────────────────────────────────────────────────────────────┐   ║
║  │  • Impact / high-vibration shock (accelerometer interrupt)  │   ║
║  │  • Hardware comparator: V drops below critical threshold    │   ║
║  │  • Hardware comparator: I exceeds absolute max              │   ║
║  └──────────────────────────────────────────────────────────────┘   ║
╚══════════════════════════════════════════════════════════════════════╝
```

### Adaptive Sampling (Smart Approach)

Don't oversample when everything is normal. **Escalate sampling when anomalies are detected:**

| System State | Fast Loop | Medium Loop | Slow Loop |
|--------------|-----------|-------------|-----------|
| **Normal** | 10 Hz | 1 Hz | 0.2 Hz |
| **Warning** (1 anomaly detected) | 20 Hz | 5 Hz | 1 Hz |
| **Critical** (2+ anomalies OR any gas/pressure) | 50 Hz | 10 Hz | 2 Hz |
| **Emergency** (confirmed runaway indicators) | Max rate | Max rate | Max rate |

> This adaptive approach keeps the system lightweight during normal operation (99.9% of the time) while providing maximum resolution during critical events.

---

## 4. Correlation Matrix — Cross-Referencing Anomalies with External Factors

Each anomaly must be **contextually validated** against external factors to minimize false positives:

| Anomaly | Modulated By | How to Adjust |
|---------|-------------|---------------|
| **Voltage drop** | SOC, C-rate, temperature, SOH | Compare against model: V = f(SOC, I, T, SOH). Only flag if residual > threshold |
| **Resistance rise** | Temperature, SOH, age | Normalize R₀ to 25°C reference. Compare against aging curve |
| **Current spike** | Operating condition, C-rate | Compare against expected profile. Regen braking creates legitimate current spikes |
| **Temperature rise** | Ambient temp, C-rate, cooling efficiency | ΔT from ambient matters more than absolute T. Adjust for coolant status |
| **dT/dt** | C-rate, ambient temp | Normalize: higher C-rate permits higher dT/dt. Flag only excessive rates |
| **Temp imbalance** | Cell position, cooling geometry | Some imbalance is normal (edge cells cooler). Track deviation from learned pattern |
| **Gas detection** | Ventilation, ambient air quality | Baseline the enclosure. Flag only rises above baseline |
| **Pressure** | Temperature, altitude | Compensate for altitude and ambient temp changes |
| **Cell swelling** | Temperature (thermal expansion) | Small expansion at high temp is normal. Track trend over time |

---

## 5. Sensor Requirements & Count

### Recommended Sensor Suite (per battery pack)

| Sensor | Quantity | Interface | Purpose | Estimated Cost |
|--------|----------|-----------|---------|---------------|
| **NTC Thermistor (10kΩ)** | 1 per cell + 2 ambient = **N+2** (e.g., 6 for 4-cell pack) | ADC (via mux) | Cell surface temp + ambient | ~$0.50 each |
| **Voltage sense** | 1 per cell = **N** (e.g., 4) | ADC (via mux + voltage divider) | Individual cell voltage | ~$0.30 each |
| **Current sensor (ACS712 / INA219)** | **1** (pack-level) | ADC or I2C | Pack current | ~$3–5 |
| **H₂ gas sensor (MQ-8)** | **1** | ADC | Hydrogen detection | ~$3 |
| **CO gas sensor (MQ-7)** | **1** | ADC | Carbon monoxide detection | ~$3 |
| **VOC/multi-gas sensor (MQ-2)** | **1** | ADC | Broad flammable gas detection | ~$2 |
| **Pressure sensor (BMP280)** | **1** | I2C | Enclosure pressure | ~$2 |
| **Accelerometer (ADXL345)** | **1** | I2C/SPI | Impact & vibration detection | ~$3 |
| **Strain gauge / flex sensor** | **1–2** (most vulnerable cells) | ADC | Cell swelling detection | ~$2–5 each |

### Minimum Viable Sensor Set (if budget/pins constrained)

| Sensor | Qty | Justification |
|--------|-----|---------------|
| NTC Thermistors | N+1 (cells + 1 ambient) | Temperature is the most fundamental indicator |
| Voltage sense | N (per cell) | Voltage anomaly is the earliest detectable sign |
| Current sensor | 1 | Essential for R calculation, C-rate, short circuit detection |
| H₂ gas sensor | 1 | Earliest gas released; single sensor covers the most critical gas |
| Pressure sensor | 1 | Fast, reliable, low-cost confirmation of venting |

---

## 6. VSDSquadron Ultra — Board Capabilities & Limitations

### Board Specifications

| Feature | Specification |
|---------|--------------|
| **SoC** | THEJAS32 (C-DAC VEGA ET1031) |
| **Clock** | 100 MHz |
| **GPIO** | 32 pins (3.3V tolerant) |
| **ADC** | 4-channel, 12-bit (ADS1015 integrated) |
| **I2C** | 3 ports |
| **SPI** | 4 ports |
| **UART** | 3 ports |
| **Connectivity** | ESP32-C3 module (WiFi + BLE) |
| **Flash** | 8 Mbit SPI NOR Flash |

### Limitations & Mitigations

| Limitation | Impact | Mitigation |
|------------|--------|------------|
| **Only 4 ADC channels** | Cannot directly read all analog sensors simultaneously (thermistors, voltage dividers, gas sensors, strain gauges) | Use **analog multiplexers** (CD4051 = 8:1, CD4067 = 16:1) to expand ADC channels. One mux per ADC channel = up to 64 analog inputs |
| **12-bit ADC resolution** | 3.3V / 4096 = ~0.8 mV per step. Sufficient for temperature (NTC) and gas sensors, but marginal for precise voltage measurement across high-voltage cells | Use **external high-resolution ADC** (ADS1115, 16-bit, I2C) for cell voltage measurement. Keep onboard ADC for temperature and gas sensors |
| **3.3V GPIO tolerance** | Cannot directly interface with 5V sensors (many MQ-series gas sensors run at 5V) | Use **level shifters** (BSS138-based bidirectional) for 5V sensor digital outputs. For analog, use voltage divider at ADC input |
| **Processing power (100 MHz)** | Sufficient for real-time monitoring loops but may struggle with complex ML inference | Keep algorithms **rule-based + lightweight statistical** (moving averages, Z-scores, threshold logic). Offload heavy computation to ESP32-C3 or cloud |
| **Limited SRAM** | Limits the size of data buffers and rolling windows | Use circular buffers with minimum required history. Compute metrics incrementally (running averages) instead of storing large arrays |
| **32 GPIO pins** | With muxes and I2C/SPI devices, pin allocation must be carefully planned | Use I2C bus for digital sensors (BMP280, ADXL345, INA219) to minimize GPIO usage. SPI for high-speed ADC if needed |

### Recommended Pin Allocation Plan

```
VSDSquadron Ultra Pin Map (Draft)

ADC Channels (4):
  ADC0 → MUX_A (CD4067) → 16 thermistors + voltage dividers
  ADC1 → MUX_B (CD4067) → Gas sensors (MQ-7, MQ-8, MQ-2) + strain gauges + spares
  ADC2 → High-priority direct: Pack current sensor (if ACS712 analog)
  ADC3 → Spare / calibration reference

I2C Port 1:
  → ADS1115 (16-bit ADC for precise cell voltage)
  → BMP280 (pressure sensor)
  → ADXL345 (accelerometer)

I2C Port 2:
  → INA219 (current sensor, if using I2C version)
  → Future expansion

I2C Port 3:
  → ESP32-C3 communication (if I2C bridge)

GPIO (Digital):
  3 pins → MUX_A select lines (S0, S1, S2, S3 → 4 pins for 16:1)
  4 pins → MUX_B select lines
  2 pins → Relay/contactor control (disconnect battery)
  1 pin  → Buzzer / alarm output
  1 pin  → Status LED
  2 pins → Hardware comparator interrupt inputs (over-current, under-voltage)
  Remaining → Spare

UART:
  UART0 → Debug / programming
  UART1 → ESP32-C3 communication (WiFi/BLE data offload)
  UART2 → Spare / external display
```

---

## 7. Detection Priority Flow — Algorithm Decision Tree

```
Every 100 ms (FAST LOOP):
│
├─► Read all cell voltages (via mux + ADC)
│   ├─ Any cell V < threshold?  ──► ALERT: Voltage anomaly
│   ├─ ΔV from model > 50 mV?  ──► ALERT: Voltage deviation
│   └─ V dropped + I spiked?   ──► ALERT: Possible short circuit ──► EMERGENCY
│
├─► Read pack current
│   ├─ I > max expected?        ──► ALERT: Current anomaly
│   └─ Compute R_int = ΔV/ΔI   ──► R > 110% baseline? ──► ALERT: Resistance rise
│
Every 500 ms (MEDIUM LOOP):
│
├─► Read all temperatures
│   ├─ Any T > 55°C?            ──► WARNING: Elevated temperature
│   ├─ Any T > 65°C?            ──► CRITICAL: High temperature
│   ├─ dT/dt > 1°C/min?        ──► WARNING: Rising too fast
│   ├─ dT/dt > 5°C/min?        ──► CRITICAL: Rapid thermal rise
│   └─ ΔT between cells > 5°C? ──► WARNING: Imbalanced heating
│
├─► Read gas sensors
│   ├─ H₂ > 50 ppm?            ──► WARNING: Hydrogen detected
│   ├─ CO > 10 ppm?            ──► WARNING: CO detected
│   ├─ Any gas > critical?      ──► CRITICAL: Gas venting confirmed
│   └─ Multiple gases rising?   ──► EMERGENCY: Thermal runaway likely
│
├─► Read pressure sensor
│   └─ ΔP > 5% from baseline?  ──► WARNING: Pressure building
│       └─ ΔP > 15%?           ──► CRITICAL: Cell venting
│
CORRELATION ENGINE (runs after each medium loop):
│
├─► Cross-check: Is V anomaly + T anomaly + gas present?
│   └─ YES (2+ categories)     ──► CONFIRMED THERMAL EVENT
│       ├─ Escalate all sampling to max rate
│       ├─ Activate cooling at maximum
│       ├─ Send alert via ESP32 (WiFi/BLE)
│       └─ If 3+ categories    ──► DISCONNECT BATTERY (open contactors)
│
└─► Context adjustment:
    ├─ High ambient T?          ──► Relax temp thresholds by ΔT_ambient
    ├─ High C-rate?             ──► Relax dT/dt threshold proportionally
    ├─ Low SOH?                 ──► Tighten all thresholds by 10–20%
    └─ Recent vibration/impact? ──► Tighten thresholds + increase sampling for 60s
```

---

## 8. Summary — What Makes This System Smart, Not Overkill

| Design Principle | How We Achieve It |
|-----------------|-------------------|
| **Catch earliest signs** | Electrical anomalies (V, I, R) sampled at 10 Hz — they change first |
| **Confirm before acting** | Gas + pressure provide independent physical confirmation |
| **Context-aware** | All thresholds adapt to ambient temp, C-rate, SOH, cooling status |
| **Adaptive sampling** | Normal = lightweight. Anomaly detected = sampling escalates automatically |
| **Multi-layer correlation** | No single-sensor false alarm triggers emergency. Need 2+ independent anomaly categories |
| **Hardware-assisted safety** | Comparator interrupts for critical V/I thresholds — zero latency, no software dependency |
| **Computationally efficient** | Rule-based + statistical (moving average, Z-score). No heavy ML models on main MCU |
| **Expandable** | I2C/SPI buses + mux architecture allows adding sensors without redesign |

> **Target:** Detect thermal runaway precursors **within 30 seconds** of first anomaly appearance, with **<1% false positive rate** under normal operating conditions.

---

## 9. Next Steps for Algorithm Development

1. **Define baseline profiles** — Normal V(SOC, I, T), R₀(T, SOH), and T(I, T_ambient) curves
2. **Implement FAST LOOP** first — V + I + R monitoring with threshold logic
3. **Add MEDIUM LOOP** — Temperature + gas + pressure with dT/dt computation
4. **Build correlation engine** — Multi-parameter cross-check with severity escalation
5. **Add context adaptation** — Integrate ambient temp, C-rate, SOH into threshold adjustment
6. **Test with simulated faults** — Inject known anomaly patterns to validate detection
7. **Tune thresholds** — Optimize warning/critical levels to minimize false positives while ensuring zero missed detections
