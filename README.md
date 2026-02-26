# BattSafe: EV Battery Intelligence Challenge – Submission Template

Submission Requirements:
- Structured repository mandatory
- One-page README summary required
- Raw dataset required
- Minimum three validation test cases required
- Demo video (max 5 minutes) required
- Explicit usage of VSDSquadron ULTRA mandatory

### Project Title
Edge-Intelligence Thermal Runaway Prevention Using Multi-Modal Sensor Fusion on VSDSquadron ULTRA

### Theme Selected
Theme 2: Intelligent Thermal Anomaly Detection

## 1. Problem Statement
Most Battery Management Systems (BMS) rely only on temperature sensors for thermal protection. By the time a temperature alarm fires, the battery is already seconds away from failure. Research shows that thermal runaway follows a predictable cascade—gas venting and pressure changes occur minutes before the temperature spike. Our system catches these earlier precursors at the edge, using a VSDSquadron ULTRA as a real-time multi-modal sensor fusion engine, to prevent thermal runaway, not just detect it.

## 2. System Overview
- **Compute Platform:** VSDSquadron ULTRA (THEJAS32)
- **Sensors Used:** NTC Thermistors (×5), Bosch BME680 (Gas/Pressure), INA219 (V/I), FSR402 (Swelling)
- **Interfaces Used (ADC / I2C / SPI / UART / GPIO):** ADC, I2C, UART, GPIO
- **Edge Processing Performed:** 3-speed monitoring loop, Correlation Engine mapping multi-modal anomalies
- **Cloud / Dashboard (if applicable):** Local Python Dashboard for visualization and logging

## 3. What Runs on VSDSquadron ULTRA
- **Sampling frequency:** Fast (10Hz), Medium (2Hz), Slow (0.2Hz) nested loops
- **Signal conditioning:** MUX selection, baseline tracking
- **Filtering method:** Moving average / anomaly delta thresholding
- **Detection / estimation logic:** Multi-parameter consensus (1 category=Warning, 2=Critical, 3+=Emergency)
- **Output generated:** Relay disconnect trigger, UART telemetry stream

## 5. Repository Guide
- 1_Project_Overview → Architecture and block diagram
- 2_Hardware → Circuit, BOM, pin mapping
- 3_Firmware → Embedded implementation
- 4_Algorithms → Signal processing and logic
- 5_Data → Raw and processed datasets
- 6_Validation → Testing methodology and results
- 7_Demo → Video and hardware proof

## 6. Demo Video
[Insert public video link here]
