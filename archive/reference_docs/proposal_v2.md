# EV Battery Intelligence Challenge — Proposal Submission

**Submission format:** PDF  
**Length:** 2-3 pages (max)  
**Team size:** 1-2 members

---

## 1. Project Title & Theme Selection

**Project Title:**  
Edge-Intelligence Thermal Runaway Prevention Using Multi-Modal Sensor Fusion on VSDSquadron ULTRA

**Selected Theme:**  
☐ Theme 1: Predictive Battery Health Analytics  
☑ **Theme 2: Intelligent Thermal Anomaly Detection**  
☐ Theme 3: Fleet-Level Battery Performance Dashboard

**Problem Statement:**  
Most Battery Management Systems (BMS) rely only on temperature sensors for thermal protection. By the time a temperature alarm fires, the battery is already seconds away from failure. Research shows that thermal runaway follows a predictable cascade—gas venting and pressure changes occur **minutes before** the temperature spike. Our system catches these earlier precursors at the edge, using a VSDSquadron ULTRA as a real-time multi-modal sensor fusion engine, to **prevent** thermal runaway, not just detect it.

---

## 2. System Overview

We are building an **edge-based thermal runaway prevention system** that monitors a 4S Li-Ion battery module using four distinct physical phenomena, not just temperature:

- **What is sensed:**
  - Cell surface temperatures (NTC thermistors via analog MUX)
  - Volatile Organic Compounds (VOCs) and enclosure pressure (Bosch BME680)
  - Battery voltage, current, and internal resistance (INA219)
  - Cell swelling force (FSR402 force-sensitive resistor)

- **What processing happens at the edge (VSDSquadron ULTRA):**
  - A **3-speed monitoring loop** runs entirely on the THEJAS32 RISC-V core
  - A **Correlation Engine** cross-references anomalies from different physical domains (electrical + thermal + gas + pressure)
  - Alerts are raised only when **2 or more independent anomaly categories** align—drastically reducing false positives

- **Output:**  
  - Tiered alert system: Normal → Warning → Critical → Emergency
  - Automatic battery disconnect (relay cutoff) on confirmed multi-parameter fault
  - Telemetry data streamed to ESP32-C3 via UART for cloud/dashboard

---

## 3. Block Diagram

```
╔═══════════════════════════════════════════════════════════════════════════╗
║                         SYSTEM ARCHITECTURE                             ║
╠═══════════════════════════════════════════════════════════════════════════╣
║                                                                         ║
║  ┌───────────────────────────────────────────────────────────────────┐   ║
║  │         🔋 Battery Module (4S Li-Ion Prototype)                  │   ║
║  └───────┬──────────────┬──────────────┬──────────────┬─────────────┘   ║
║          │              │              │              │                  ║
║    ┌─────▼─────┐  ┌─────▼─────┐  ┌────▼────┐  ┌─────▼─────┐           ║
║    │   NTC     │  │  BME680   │  │ INA219  │  │  FSR402   │           ║
║    │Thermistor │  │ Gas+Press │  │  V+I    │  │ Swelling  │           ║
║    │ (×4+1)   │  │  (I2C)    │  │  (I2C)  │  │ (Analog)  │           ║
║    └─────┬─────┘  └─────┬─────┘  └────┬────┘  └─────┬─────┘           ║
║          │ Analog        │ I2C         │ I2C         │ Analog           ║
║      ┌───▼───┐           │             │             │                  ║
║      │CD4051 │           │             │             │                  ║
║      │  MUX  │           │             │             │                  ║
║      └───┬───┘           │             │             │                  ║
║          │ ADC           │             │             │                  ║
║  ╔═══════▼═══════════════▼═════════════▼═════════════▼═══════════════╗  ║
║  ║          VSDSquadron ULTRA (THEJAS32 RISC-V, 100 MHz)            ║  ║
║  ║                                                                   ║  ║
║  ║  ┌──────────────────────────────────────────────────────────────┐ ║  ║
║  ║  │   FAST LOOP (100ms)  │  MEDIUM LOOP (500ms) │  SLOW (5s)   │ ║  ║
║  ║  │   ─────────────────  │  ──────────────────── │  ─────────── │ ║  ║
║  ║  │   • Voltage check    │  • Temperature dT/dt  │  • Adaptive  │ ║  ║
║  ║  │   • Current check    │  • Gas ratio (VOC)    │    threshold │ ║  ║
║  ║  │   • R_int calc       │  • Pressure delta     │    adjust    │ ║  ║
║  ║  │   • Short-circuit    │  • Swelling check     │  • Telemetry │ ║  ║
║  ║  │     detection        │  • Cell ΔT imbalance  │    to ESP32  │ ║  ║
║  ║  └──────────────────────┴────────────────────────┴──────────────┘ ║  ║
║  ║                                                                   ║  ║
║  ║  ┌──────────────────────────────────────────────────────────────┐ ║  ║
║  ║  │        🧠  CORRELATION ENGINE  (runs after every Med loop)  │ ║  ║
║  ║  │  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐       │ ║  ║
║  ║  │  │ELECTRICAL│ │ THERMAL  │ │   GAS    │ │ PRESSURE │       │ ║  ║
║  ║  │  │  V/I/R   │ │  T/dTdt  │ │ VOC drop │ │  ΔP rise │       │ ║  ║
║  ║  │  └────┬─────┘ └────┬─────┘ └────┬─────┘ └────┬─────┘       │ ║  ║
║  ║  │       └──────────┬─┴────────────┬┘            │             │ ║  ║
║  ║  │          Count active categories (bitmask)    │             │ ║  ║
║  ║  │       ┌──────────┴────────────────────────────┘             │ ║  ║
║  ║  │       │ 1 category → WARNING                                │ ║  ║
║  ║  │       │ 2 categories → CRITICAL                             │ ║  ║
║  ║  │       │ 3+ categories → EMERGENCY → DISCONNECT              │ ║  ║
║  ║  └──────────────────────────────────────────────────────────────┘ ║  ║
║  ╚═══════════════════════════════════════════════════════════════════╝  ║
║          │ GPIO                               │ UART                    ║
║    ┌─────▼──────────┐                  ┌──────▼────────────┐           ║
║    │ SAFETY OUTPUTS  │                  │ ESP32-C3 (WiFi)   │           ║
║    │ • Relay Driver  │                  │ • Dashboard       │           ║
║    │   (Module       │                  │ • Cloud Logging   │           ║
║    │   Isolation)    │                  │ • BLE Alert       │           ║
║    │ • Buzzer/LED    │                  └────────────────────┘           ║
║    └─────────────────┘                                                  ║
╚═════════════════════════════════════════════════════════════════════════╝
```

**Why VSDSquadron ULTRA:**  
The THEJAS32 RISC-V core at 100 MHz provides enough processing power to run all three monitoring loops with **<5% CPU utilization** — leaving headroom for future ML inference. The board's 4 ADC channels, 3 I2C buses, and ample GPIO let us connect all sensors directly without additional interface hardware.

**What runs on the board:**  
The entire detection pipeline — from raw sensor reads through filtering, baseline tracking, threshold checking, and multi-parameter correlation — runs fully on the VSDSquadron ULTRA. No cloud dependency for safety decisions. The ESP32-C3 handles only logging and dashboard display.

---

## 4. Hardware & Interfaces

### 4.1 Compute Platform
- VSDSquadron ULTRA (THEJAS32 RISC-V SoC, 100 MHz)

### 4.2 Sensors / Inputs

| Parameter | Sensor | Interface | Why This Sensor |
|-----------|--------|-----------|-----------------|
| **Cell Temperature** | NTC Thermistors (10kΩ) × 4+1 ambient | Analog (via CD4051 MUX) | Industry-standard, ₹10/unit, automotive-grade equivalent |
| **Gas / VOC + Pressure** | Bosch BME680 | I2C | 4-in-1 chip: VOC + Temp + Humidity + Pressure for ₹600 |
| **Voltage + Current** | INA219 | I2C | High-side V+I sensing in one chip; enables R_int calculation |
| **Cell Swelling** | FSR402 | Analog | Detects mechanical deformation before temp rises |

### 4.3 Interfaces Used on VSDSquadron ULTRA
- ☑ **ADC:** NTC thermistors (via CD4051 MUX) + FSR402
- ☑ **I2C:** BME680 (Gas/Pressure) + INA219 (V/I)
- ☑ **GPIO:** MUX channel select, Relay driver, Buzzer, Status LEDs
- ☑ **UART:** Data stream to ESP32-C3 (simulating CAN-bus telemetry)

---

## 5. Firmware & Algorithm Approach

### The Core Innovation: 3-Speed Loop + Correlation Engine

Instead of sampling everything at one rate, our firmware uses **three nested loops matched to the physics of each failure mode:**

| Loop | Rate | What It Monitors | Why This Speed |
|------|------|-------------------|----------------|
| **Fast** | 100ms (10 Hz) | Voltage, Current, R_internal, Short Circuit | Electrical faults manifest in milliseconds |
| **Medium** | 500ms (2 Hz) | Temperature, dT/dt, Gas (VOC), Pressure, Swelling | Thermal/chemical precursors evolve over seconds |
| **Slow** | 5s (0.2 Hz) | Adaptive threshold adjustment, telemetry to ESP32 | Environmental context changes slowly |

**The key insight:** During normal operation (99.9% of the time), this keeps the system lightweight. When an anomaly is detected, **sampling rates automatically escalate** — Fast moves to 20ms, Medium to 100ms — providing maximum resolution exactly when it matters.

### Correlation Engine (The False-Positive Killer)

A single hot cell doesn't trigger an emergency. Neither does a single gas reading. The Correlation Engine counts **how many distinct anomaly categories are active** (Electrical, Thermal, Gas, Pressure, Swelling) and escalates accordingly:

- **1 category active** → WARNING (increase monitoring)
- **2 categories active** → CRITICAL (prepare for disconnect, 10s countdown)
- **3+ categories active** → EMERGENCY (immediate relay disconnect)

This multi-modal confirmation approach ensures **near-zero false positives** while catching real threats early.

### Gas & Pressure Detection (Our Main Differentiator)

Using the BME680, we track two critical pre-runaway indicators that most BMS systems miss:

1. **VOC Detection:** When electrolyte begins decomposing (~80-120°C internally), it releases volatile organic compounds that reduce the BME680's gas resistance. We track the ratio: `gas_current / gas_baseline`. A ratio drop below 0.7 = warning, below 0.4 = critical.

2. **Pressure Detection:** Enclosure pressure rises when a cell vents internally. We track `ΔP = current - baseline`. A rise of >5 hPa = warning, >15 hPa = critical.

Both signals appear **2-5 minutes before** the temperature spike — giving actionable time to prevent thermal runaway, not just alarm about it.

---

## 6. Data Flow & Dashboard

- **Logged Data:** All sensor readings + system state + anomaly flags, timestamped
- **Transmission:** Every 5 seconds (normal) or 1 second (during alert), via UART to ESP32-C3
- **Format:** Compact binary packet (32 bytes) with XOR checksum
- **Visualization:** Real-time Python dashboard showing all sensor channels, correlation engine state, and alert history

---

## 7. Validation & Testing Plan

| Test | Method | What It Proves |
|------|--------|----------------|
| **Thermal Anomaly** | Controlled resistive heater on one cell (simulates dT/dt rise) | Temperature detection + dT/dt computation accuracy |
| **Gas Venting Simulation** | Isopropyl alcohol vapor near BME680 (simulates electrolyte VOC) | Gas ratio drop detection, response time (<2s) |
| **Pressure Change** | Sealed enclosure + syringe to inject air (simulates cell venting) | Pressure delta detection with baseline tracking |
| **False Positive Stress** | Heat gun on ambient (no gas, no pressure change) — single-mode anomaly | Proves correlation engine does NOT trigger emergency on single-category events |
| **Multi-Modal Fault** | Combine heater + IPA vapor simultaneously | Correlation engine correctly escalates to CRITICAL/EMERGENCY |
| **Short Circuit** | Sudden load step (resistor bank switching) | Fast-loop short circuit cross-check, <100ms response |

---

## 8. Expected Output by Final Demo

- ✅ **Working prototype:** VSDSquadron ULTRA monitoring a 4S Li-Ion module with 4 sensor types
- ✅ **Live correlation demo:** Real-time dashboard showing all sensor channels and the correlation engine decision-making
- ✅ **Safety demo:** Automatic relay disconnect when multi-parameter fault is confirmed
- ✅ **False positive demo:** Showing that single-sensor anomalies do NOT trigger false emergencies
- ✅ **GitHub repository:** Complete firmware (C), dashboard (Python), schematics, and documentation

---

## 9. Future Scope

- **TinyML Integration:** Run a lightweight anomaly classifier on the THEJAS32 to predict "time to runaway"
- **Distributed Architecture:** Scale the VSDSquadron ULTRA as a "Smart Module Node" — one per 12-16 cells in a full EV pack (96S+), communicating via CAN-FD/ISO-SPI
- **Active Thermal Management:** Trigger cooling pumps/fans on early Warning state, not on Critical

---

## 10. Team Details

| Name | Role | Background |
|------|------|------------|
| Shakeb | Firmware & System Architecture | Embedded systems, signal processing, algorithmic design |
| [Team Member 2] | Hardware Interface & Analytics | [Background] |
