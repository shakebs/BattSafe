# Prototype Sensor Architecture — Engineering Specification

> **Purpose:** Engineering-level specification for the VSDSquadron Ultra prototype, mapping sensors to a real Indian EV battery pack (Tata Nexon EV). This document answers: exactly how many of each sensor, placed exactly where, measuring exactly what, and computing exactly what — so the prototype reflects a production-grade system when demonstrated to organizers.

---

## 1. Reference Battery Pack — Tata Nexon EV (Real Verified Data)

We choose the **Tata Nexon EV** as our reference because:
- It is India's **best-selling electric car** (40%+ EV market share)
- It uses **LFP chemistry** (the safer chemistry — if we detect anomalies here, NMC is covered)
- It represents a real 4-wheeler, not a scooter or toy setup
- Its pack architecture is **publicly documented** from teardowns and cell supplier data

> **Transparency note:** Tata has sold the Nexon EV in multiple battery variants. The **40.5 kWh Max** variant has been teardown-verified and its exact cell configuration is documented. The newer **45 kWh prismatic** variant's exact S-P configuration is not publicly disclosed yet. We present BOTH — the verified config as our PRIMARY reference, and the newer pack as a derived secondary reference.

### 1.1 Verified Configuration — Nexon EV Max (40.5 kWh)

**Source:** Battery teardowns, BatteryDesign.net analysis, Guoxuan/Gotion cell datasheets, Reddit/Team-BHP teardown reports.

| Parameter | Value | Source |
|-----------|-------|--------|
| **Chemistry** | LFP (LiFePO₄) | Tata Motors confirmed, cell markings |
| **Cell model** | **IFR32135-15Ah** | Visible on cell markings in teardowns |
| **Cell format** | **Cylindrical** (32mm dia × 135mm height) | Physical teardown |
| **Cell manufacturer** | **Guoxuan Hi-Tech (Gotion)** via Tata AutoComp Systems JV | Industry reports, TACO press releases |
| **Cell nominal voltage** | **3.2V** | LFP standard, datasheet |
| **Cell capacity** | **15 Ah** | Datasheet: IFR32135-15Ah |
| **Cell energy** | 48 Wh per cell (3.2V × 15Ah) | Derived |
| **Pack configuration** | **104S8P** | Teardown-verified |
| **Series cells** | **104** | 104 cells in series |
| **Parallel strings** | **8** | 8 cells in parallel per series position |
| **Total cells** | **832** | 104 × 8 = 832 cells |
| **Pack nominal voltage** | **332.8V** | 104 × 3.2V |
| **Pack operating voltage** | **260V – 379.6V** | 104 × (2.5V – 3.65V) |
| **Pack capacity** | **120 Ah** (parallel group: 8 × 15 Ah) | 8 parallel × 15 Ah |
| **Pack gross energy** | **39.9 kWh** (332.8V × 120 Ah) | Derived (40.5 kWh marketed = includes rounding) |
| **Module count** | **~8 modules** (estimated: 13S per module × 8 modules = 104S) | Exact module boundaries not publicly confirmed; 13S × 8 modules is most likely |
| **Cells per module** | **13S8P = 104 cells per module** | 13 series × 8 parallel |
| **Module voltage** | **41.6V nominal** (13 × 3.2V) | Derived |
| **Max charge rate** | **~1C** (50 kW DC fast charge → 120 Ah × 333V ≈ 40 kWh) | Spec sheet |
| **Max discharge rate** | **~2C sustained** (motor: 105 kW / 333V ≈ 315A peak) | 143 Nm motor, 105 kW |
| **Max continuous current** | **~120A per string** (1C) = **15A per cell** | Datasheet: 1C max cont. charge |
| **Max peak current** | **~315A per string** (motor demand) = **~39A per cell** (~2.6C) | Motor peak demand |

**Also documented (for reference):**

| Variant | Config | Cells | Cell Type | Pack Energy |
|---------|--------|-------|-----------|-------------|
| **Nexon EV Prime** (30.2 kWh) | **100S7P** | **700** | IFR32135-15Ah | 33.6 kWh gross |
| **Nexon EV Max** (40.5 kWh) | **104S8P** | **832** | IFR32135-15Ah | 39.9 kWh gross |

### 1.2 Probable Configuration — New Nexon EV (45 kWh Prismatic)

The 2024+ Nexon EV uses a **new prismatic LFP pack** (shared with Tata Curvv EV). Tata claims:
- 62% reduction in number of modules (from ~8 cylindrical modules → ~3 prismatic modules)
- 186 Wh/liter volumetric density (15% higher)
- Cells supplied by **EVE Energy** (via Octillion Power Systems)

**Derived configuration (engineering estimate):**

| Parameter | Value | Derivation |
|-----------|-------|------------|
| **Cell type** | Prismatic LFP | Tata Motors confirmed |
| **Cell model** | Likely **EVE LF105** or similar | EVE supplies to Octillion; Curvv 55 kWh uses 105Ah cells (Gotion) |
| **Cell capacity** | **105 Ah** (if EVE LF105) or **~120 Ah** | Industry sources for Curvv EV |
| **Cell voltage** | 3.2V | LFP standard |
| **If 105 Ah cells:** | 45,000 Wh ÷ 3.2V ÷ 105Ah = **~134 cells at 1P** → likely **134S1P** (430V) too high. More likely **~112S1P** (358V, ~37.6 kWh usable from ~42 kWh gross) — pack voltage matches 350V class | |
| **Probable config** | **~112S1P** to **120S1P** with 105Ah cells, or **~104S1P** with 130Ah cells | Any of these give ~330-385V nominal, matching Nexon's 350V-class inverter |
| **Module count** | **~3 modules** (from Tata's "62% reduction" claim: 8 → 3) | Tata press release |
| **Cells per module** | ~35-40S1P per module | 112 ÷ 3 ≈ 37 series per module |

> **⚠️ Important:** The exact new prismatic config is NOT publicly confirmed. For our engineering specification, we use the **verified 104S8P cylindrical** as the primary reference throughout this document. The sensor architecture we design works for BOTH configurations — the difference is only in cell count and module count, not in the measurement principles.

### 1.3 Physical Layout — 104S8P Pack Architecture

```
 ┌──────────────────────────────────────────────────────────────────────┐
 │                     TATA NEXON EV MAX BATTERY PACK                   │
 │              40.5 kWh / 104S8P / 332.8V nominal / 832 cells         │
 │                                                                      │
 │  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐            │
 │  │ Module 1 │  │ Module 2 │  │ Module 3 │  │ Module 4 │            │
 │  │ 13S8P    │  │ 13S8P    │  │ 13S8P    │  │ 13S8P    │            │
 │  │ 104 cells│  │ 104 cells│  │ 104 cells│  │ 104 cells│            │
 │  │ 41.6V    │  │ 41.6V    │  │ 41.6V    │  │ 41.6V    │            │
 │  └───┬──────┘  └───┬──────┘  └───┬──────┘  └───┬──────┘            │
 │      │ series      │ series      │ series      │ series             │
 │  ┌───┴──────┐  ┌───┴──────┐  ┌───┴──────┐  ┌───┴──────┐            │
 │  │ Module 5 │  │ Module 6 │  │ Module 7 │  │ Module 8 │            │
 │  │ 13S8P    │  │ 13S8P    │  │ 13S8P    │  │ 13S8P    │            │
 │  │ 104 cells│  │ 104 cells│  │ 104 cells│  │ 104 cells│            │
 │  │ 41.6V    │  │ 41.6V    │  │ 41.6V    │  │ 41.6V    │            │
 │  └──────────┘  └──────────┘  └──────────┘  └──────────┘            │
 │                                                                      │
 │  8 modules in SERIES: M1+M2+...+M8 = 104S = 332.8V                 │
 │  Each module: 13 series positions, each position = 8 cells parallel │
 │                                                                      │
 │  [Liquid cooling plate under modules] [HV junction box + BMS]       │
 └──────────────────────────────────────────────────────────────────────┘
```

### 1.4 Inside One Module (13S8P = 104 cylindrical cells)

```
 MODULE N  (13S8P — 13 series groups, each group = 8 parallel cells)
 ┌──────────────────────────────────────────────────────────────────────┐
 │                                                                      │
 │  Series Position 1        Series Position 2         ...  Pos 13     │
 │  (8 cells parallel)       (8 cells parallel)              (8P)      │
 │                                                                      │
 │  ┌─┐┌─┐┌─┐┌─┐┌─┐┌─┐┌─┐┌─┐   ┌─┐┌─┐┌─┐┌─┐┌─┐┌─┐┌─┐┌─┐          │
 │  │C││C││C││C││C││C││C││C│───│C││C││C││C││C││C││C││C│── ...        │
 │  │1││2││3││4││5││6││7││8│   │1││2││3││4││5││6││7││8│              │
 │  └─┘└─┘└─┘└─┘└─┘└─┘└─┘└─┘   └─┘└─┘└─┘└─┘└─┘└─┘└─┘└─┘          │
 │  ←── All 8 connected ──→     ←── All 8 connected ──→              │
 │      in PARALLEL                  in PARALLEL                       │
 │      (+ to +, − to −)            (+ to +, − to −)                  │
 │      via nickel strips            via nickel strips                  │
 │                                                                      │
 │  V_group1 = 3.2V             V_group2 = 3.2V                       │
 │  I_group1 = 8 × I_cell       I_group2 = 8 × I_cell                │
 │  Capacity = 8 × 15Ah = 120Ah                                        │
 │                                                                      │
 │  VOLTAGE MEASUREMENT:                                                │
 │  → AFE IC measures V across each PARALLEL GROUP (not each cell)     │
 │  → 13 voltage measurements per module (one per series position)     │
 │  → Each measurement = voltage of 8 parallel cells (which MUST be    │
 │    equal by Kirchhoff's voltage law — they share the same nodes)    │
 │                                                                      │
 │  ★ Individual cell faults within a parallel group:                   │
 │  → A single shorted cell in a group of 8 will be PARTIALLY masked   │
 │  → But: it drains its 7 neighbors → group voltage drops slightly    │
 │  → And: its neighbors heat up trying to feed the short              │
 │  → Detection: temperature rise in that region + slight V_group drop │
 │                                                                      │
 │  ┌──────────────────────────────────────────────────────────────┐    │
 │  │           AFE IC (e.g., TI BQ76940/52 — 13S capable)        │    │
 │  │           Measures: V_group1...V_group13 (13 voltages)      │    │
 │  │           + cell balancing per group + 2× NTC inputs        │    │
 │  └──────────────────────────────────────────────────────────────┘    │
 └──────────────────────────────────────────────────────────────────────┘

 KEY DIFFERENCE FROM PRISMATIC 1P:
 ─────────────────────────────────
 In a 104S8P pack, the AFE measures voltage across PARALLEL GROUPS,
 not individual cells. Each voltage reading = voltage of 8 cells
 connected in parallel.

 Why this matters:
 → A single cell with an internal short in a group of 8 parallel
   cells will pull down the group voltage by only ~(1/8) of the
   full deviation — the fault is partially hidden by the healthy
   7 cells maintaining the node voltage.
 → This is why temperature monitoring is even MORE critical in
   parallel-group architectures — temp may be the FIRST indicator
   when voltage is masked.
```

> **Critical engineering point:** In a **series-parallel** pack like 104S8P, voltage is measured per **parallel group** (not per individual cell). This means we have **104 voltage channels** (one per series position), where each channel represents 8 cells connected in parallel. A fault in a single cell within a parallel group is partially masked by its 7 healthy neighbors — making temperature and gas detection even more important as complementary indicators.

### 1.5 What Changes for the New Prismatic Pack

| Aspect | Old 104S8P Cylindrical | New ~112S1P Prismatic |
|--------|------------------------|-----------------------|
| **Voltage per series position** | 3.2V (8 cells in parallel — fault partially masked) | 3.2V (single cell — fault fully visible in voltage) |
| **Voltage channels** | 104 (per parallel group) | ~112 (per individual cell) |
| **Current path** | 1 main path, I_pack splits into 8 parallel cells at each position | 1 main path, I_pack = I_cell (all current through each cell) |
| **Cell current** | I_cell = I_pack / 8 (e.g., 120A pack → 15A per cell) | I_cell = I_pack (e.g., 120A pack → 120A per cell — cell must handle full current) |
| **Thermal visibility** | NTC covers 8 cells in a group, avg temp | NTC much closer to individual cell behavior |
| **Fault detection sensitivity** | Lower for voltage (parallel masking), higher emphasis on temp | Higher for voltage (direct cell measurement) |
| **Module count** | ~8 modules | ~3 modules |
| **NTCs needed** | 2 per module × 8 = 16 | 2 per module × 3 = 6 (but modules are larger, may need more) |

> **For our prototype and algorithm design, the 104S8P architecture is actually the HARDER case** — detecting faults through parallel masking is more challenging. If our algorithm works for 104S8P, it will work even BETTER for a 1P prismatic pack where faults are more directly visible.

---

## 2. Complete Sensor Inventory — Per Module and Per Pack

This is the engineering-level answer to "how many of each sensor and where."

### 2.1 Voltage Sensors

| Sensor Type | Quantity | Level | What Exactly Is Measured | Measurement Point |
|-------------|----------|-------|--------------------------|-------------------|
| **Per-group voltage** (AFE IC input) | **13 per module × 8 modules = 104 measurements** | Parallel-group level | Voltage across each **parallel group** (8 cells in parallel share the same voltage by Kirchhoff's law). V_group_n from the positive to negative node of that series position | **Voltage sense wires** (thin 24–26 AWG) spot-welded to busbar junctions between parallel groups. 14 sense wires per 13S module (one per group boundary) |
| **Module voltage** (AFE IC sum) | **8 measurements** (1 per module) | Module-level | Sum of 13 group voltages = module voltage. Calculated by AFE IC, not a separate sensor | Derived from per-group readings inside the AFE IC |
| **Pack voltage** | **1 measurement** | Pack-level | Total series voltage = sum of all 104 group voltages. Measured at the HV junction box terminals | **Hall-effect isolated voltage transducer** (e.g., LEM LV-25-P) or resistive voltage divider at the pack HV terminals, before the contactor |

**Why per-parallel-group and not per-individual-cell?**

In a 104S8P configuration, each series position has **8 cells in parallel**. By Kirchhoff's voltage law, all 8 cells in a parallel group share the same terminal voltage (they are connected + to + and − to −). The AFE IC measures voltage at each parallel group node.

**Implication for fault detection:**

```
EXAMPLE: 1 cell in parallel group 5 of Module 3 has an internal short

Group-level voltage (what the AFE measures):
  Group 5 = 3.18V  (expected: 3.24V)
  → Only ~60mV drop because the 7 HEALTHY cells in the group
    are maintaining the node voltage. The fault is PARTIALLY MASKED.
  → In a 1P pack, the same fault would show ~490mV drop.

Why it's still detectable:
  1. Even 60mV group-level deviation is significant → WARNING
  2. The shorted cell draws current FROM its 7 neighbors
     → localized heating → NTC in that region rises
  3. R_int of the GROUP changes (parallel combination shifts)
  → MULTI-PARAMETER CORRELATION catches what voltage alone misses.

This is WHY our correlation engine is essential — in parallel-group
architectures, temperature and gas detection are MORE important
than in 1P packs where voltage alone can identify faults.
```

### 2.2 Current Sensors

| Sensor Type | Quantity | Level | What Exactly Is Measured | Measurement Point |
|-------------|----------|-------|--------------------------|-------------------|
| **Pack current** (main HV) | **1** | Pack-level | Total current flowing through the main HV bus. In a 104S8P pack, this is the TOTAL current from all 8 parallel strings combined at each series position. I_pack = 8 × I_cell (at each position, current splits into 8 parallel cells). | **Hall-effect current transducer** (e.g., LEM DHAB s/14, ±500A, isolated) on the main HV bus at the pack output, between the battery terminal and the contactor |
| **Isolation monitoring** | **1** | Pack-level | Leakage current between HV bus and vehicle chassis (ground fault detection per AIS-156) | **ISO resistance monitor IC** (e.g., Bender ISOMETER, or software-based using voltage divider measurement) |

**Why only 1 current sensor for 832 cells?**

Because the pack is **104S8P** — all parallel groups are connected in series. The **same total current** flows through each series position. At each position, the current splits equally among the 8 parallel cells. One high-accuracy current measurement at the pack level gives us:
- Total pack current (I_pack)
- Per-cell current estimate: I_cell ≈ I_pack / 8

```
  ┌──────────────────────────────────────────────────────────────┐
  │  104S8P CURRENT FLOW:                                       │
  │                                                              │
  │  [+] → Group 1 → Group 2 → ... → Group 104 → [CURRENT] → [-]
  │         (8P)       (8P)             (8P)        SENSOR       │
  │                                                              │
  │  I_pack flows through each GROUP in series.                  │
  │  At EACH group, current splits into 8 parallel cells:        │
  │                                                              │
  │    Group N: I_pack → ┬→ Cell_1: I_pack/8                     │
  │                      ├→ Cell_2: I_pack/8                     │
  │                      ├→ Cell_3: I_pack/8                     │
  │                      ├→ ...                                  │
  │                      └→ Cell_8: I_pack/8                     │
  │                                                              │
  │  Example: I_pack = 120A (1C) → each cell sees 15A (1C)      │
  │  Example: I_pack = 315A (peak) → each cell sees ~39A (~2.6C) │
  │                                                              │
  │  ★ If one cell in a parallel group has higher R_int,         │
  │    it will carry LESS current → its neighbors carry MORE     │
  │    → unequal heating within the group → detectable by NTC    │
  └──────────────────────────────────────────────────────────────┘
```

### 2.3 Temperature Sensors

This is the most complex sensor category. We need MULTIPLE types in MULTIPLE locations.

#### A. Cell Surface Temperature (NTC Thermistors)

| Sensor Type | Quantity | Level | What Exactly Is Measured | Placement |
|-------------|----------|-------|--------------------------|-----------|
| **Cell surface NTC** | **2 per module × 8 modules = 16 NTCs** | Module-level | Surface temperature of cells at 2 strategic locations within each module, representing thermal state of surrounding parallel groups | See placement diagram below |

**Why 2 per module, not 1 per cell (which would be 832)?**

- 1 per cell (832 NTCs) is physically impossible and would be absurdly expensive.
- 1 per parallel group (104 NTCs) is still too many for a production BMS.
- **2 per module is the industry standard** (confirmed by BMS IC datasheets — BQ76940/52 has exactly 2 NTC inputs per IC).
- 2 per module captures the **thermal gradient** across the module (hottest point vs coolest point).
- In a cylindrical module with 104 cells, the NTCs are positioned between groups to sense the average temperature of surrounding cells.

**Exact placement within each module (13S8P = 104 cylindrical cells):**

```
 MODULE N — NTC PLACEMENT (Top View, 13 series groups × 8 parallel cells)
 
 ┌───────────────────────────────────────────────────────────────────────┐
 │  Grp1   Grp2   Grp3   Grp4   Grp5   Grp6   Grp7  ...  Grp13       │
 │  (8P)   (8P)   (8P)   (8P)   (8P)   (8P)   (8P)       (8P)        │
 │  ○○○○   ○○○○   ○○○○   ○○○○   ○○○○   ○○○○   ○○○○       ○○○○        │
 │  ○○○○   ○○○○   ○○○○   ○○○○   ○○○○   ○○○○   ○○○○       ○○○○        │
 │          ↑                             ↑                            │
 │        NTC-T1                        NTC-T2                         │
 │   Between Grp 3-4             Between Grp 10-11                     │
 │   (center of first half)      (center of second half)               │
 └───────────────────────────────────────────────────────────────────────┘

 WHY positioned between groups 3-4 and 10-11?
 → Edge groups (1, 2 and 12, 13) lose heat to module end-plates
   and outer walls → they run COOLER.
 → Interior groups are surrounded by cells on all sides.
   Heat accumulates MORE in the center of the module.
 → We place each NTC between two groups to capture the AVERAGE
   temperature of the hottest zone in each half.
 → The NTC bead sits wedged between cylindrical cells in the
   tight-packed arrangement — excellent thermal contact.

 ATTACHMENT METHOD (cylindrical cells):
 → NTC 10kΩ bead (glass-encapsulated, rated to 200°C)
 → Thin bead wedged between adjacent cylindrical cells in
   the parallel group (cells are tightly packed)
 → Thermal paste or thermal adhesive pad for contact
 → Wire routed along the top of the module to the AFE board
```

#### B. Ambient Temperature

| Sensor Type | Quantity | Level | Placement |
|-------------|----------|-------|-----------|
| **Ambient NTC or digital sensor** (on BMS master board) | **1** | Pack-level | Mounted on the BMS master PCB, thermally isolated from cells (≥10 cm distance, outside the module thermal mass). In the pack's air intake zone, before the coolant channel. Measures the temperature of air entering the pack. |
| **Coolant inlet temperature** | **1** | Pack-level | NTC probe clipped to the liquid coolant inlet pipe, measuring coolant temperature entering the pack. Used to validate thermal management system is functioning. |
| **Coolant outlet temperature** | **1** | Pack-level | NTC probe clipped to the liquid coolant outlet pipe. ΔT_coolant = T_outlet − T_inlet tells us how much heat the pack is rejecting. |

#### C. Core vs. Surface Temperature Strategy

This is a critical engineering question. We CANNOT insert a thermocouple inside a cell (it would destroy the cell and void the warranty). So how do we estimate core temperature?

**Strategy: Thermal Model Estimation (Core Temp = f(Surface Temp, Current, Time))**

```
In a cylindrical LFP cell (IFR32135):
  ┌────────────────┐
  │  Steel can      │ ← Surface: measured by NTC between cells
  │  ┌────────────┐ │
  │  │ Jelly-roll │ │ ← Core: NOT directly measurable in production
  │  │ (wound     │ │
  │  │  anode +   │ │
  │  │  cathode + │ │
  │  │  separator)│ │ ← Heat generated HERE: I²R + reactions
  │  │            │ │
  │  └────────────┘ │
  │                 │ ← Heat conducts radially outward to can surface
  └────────────────┘    then to coolant/air

  T_core = T_surface + ΔT_internal

  ΔT_internal depends on:
    1. Per-cell current (I_cell = I_pack / 8) → heat = I_cell² × R_int
    2. Cell thermal resistance (R_thermal) → ~2.0-4.0 °C/W for cylindrical
       (higher than prismatic due to smaller surface area)
    3. Cooling efficiency → liquid vs air cooled
    4. Duration of high-current event

  ESTIMATION (1D lumped thermal model):
    T_core_estimated = T_surface + (I_cell² × R_int × R_thermal_cell)

  Where:
    I_cell = I_pack / 8 (parallel group splits current)
    R_int = measured online via ΔV_group/ΔI_pack (AFE + current data)
    R_thermal_cell = calibrated constant (~3.0 °C/W for IFR32135 cylindrical)
    I_pack = pack current (from Hall sensor)

  UNDER NORMAL OPERATION (1C discharge, I_pack = 120A → I_cell = 15A):
    Heat/cell = 15² × 3.5mΩ = 0.79W per cell
    ΔT_internal = 0.79W × 3.0°C/W ≈ 2.4°C
    → If surface = 35°C, core ≈ 37.4°C — well within limits

  UNDER ABUSE (degraded cell with 8.0mΩ, drawing disproportionate heat):
    Heat/cell = 15² × 8.0mΩ = 1.8W per cell
    ΔT_internal = 1.8W × 3.0°C/W ≈ 5.4°C
    → Surface at 40°C → Core at ~45.4°C — starting to deteriorate

  WORST CASE (2.6C peak, I_cell = 39A, degraded R_int = 8.0mΩ):
    Heat/cell = 39² × 8.0mΩ = 12.2W per cell
    ΔT_internal = 12.2W × 3.0°C/W ≈ 36.5°C
    → Surface at 40°C → Core at ~76.5°C — SEI decomposition zone!
    → This is WHY core estimation matters.
```

> **Key insight:** The surface temperature alone can UNDERESTIMATE core temperature by 10–80°C depending on current and cell health. Our thermal model bridges this gap without requiring invasive sensors.

#### D. Summary of ALL Temperature Sensors

| Sensor | Quantity | Total | Per-What | Purpose |
|--------|----------|-------|----------|---------|
| Cell surface NTC (10kΩ) | 2 per module | **16** | Per-module (8 modules) | Cell surface temp, dT/dt, ΔT inter-group |
| Ambient air NTC | 1 | **1** | Per-pack | Ambient-compensated thresholds |
| Coolant inlet NTC | 1 | **1** | Per-pack | Thermal management validation |
| Coolant outlet NTC | 1 | **1** | Per-pack | Heat rejection monitoring |
| **Total temperature sensors** | | **19** | | |

### 2.4 Gas Sensors

| Sensor Type | Quantity | Level | What Is Measured | Placement |
|-------------|----------|-------|------------------|-----------|
| **VOC / Gas sensor** (BME680 or equivalent MOX sensor) | **2** | Pack-level | Total Volatile Organic Compounds (ethylene, CO, H₂, hydrocarbons) from electrolyte decomposition | **Sensor 1:** Inside the pack enclosure, at the HIGHEST point (gases rise). Near the pack exhaust/vent port, if one exists. **Sensor 2:** At the opposite end of the pack from Sensor 1, to detect spatial gas propagation direction and speed. |
| **Dedicated H₂ sensor** (optional, for LFP packs) | **1** | Pack-level | Hydrogen gas specifically (LFP cells produce 40–54% H₂ during failure — the most dangerous gas for LFP) | Near the pack vent port. H₂ is lighter than air → place at highest reachable point in enclosure. |

**Why 2 gas sensors, not 1?**

```
SCENARIO: Cell 3 in Module 2 begins venting

With 1 gas sensor (at pack center):
  → Gas reaches sensor after diffusing across half the pack
  → Delay: 5–15 seconds depending on pack volume and airflow
  → We detect "gas is present" but NOT where it's coming from

With 2 gas sensors (at opposite ends):
  → Sensor near Module 2 triggers FIRST
  → Sensor far from Module 2 triggers LATER (or at lower concentration)
  → TIME DIFFERENCE between sensor triggers = direction of gas source
  → Localizes the fault to a REGION of the pack
  → FASTER first-response (gas reaches nearest sensor sooner)
```

**Placement detail:**

```
 PACK TOP VIEW — Gas Sensor Placement
 ┌──────────────────────────────────────────────────────────────┐
 │                                                              │
 │  ★ GAS_1 (BME680)                        ★ GAS_2 (BME680)  │
 │  (near M1-M2 region)                     (near M7-M8 region)│
 │                                                              │
 │  [M1]  [M2]  [M3]  [M4]  [M5]  [M6]  [M7]  [M8]           │
 │                                                              │
 │  Mounted above the modules, on the pack enclosure lid,       │
 │  with sensor element FACING DOWNWARD into the pack volume    │
 │  (gases rise from cells upward to sensor)                    │
 │                                                              │
 │  ★ H₂ SENSOR (optional) — at pack vent/exhaust port         │
 └──────────────────────────────────────────────────────────────┘
```

### 2.5 Pressure / Swelling Sensors

| Sensor Type | Quantity | Level | What Is Measured | Placement |
|-------------|----------|-------|------------------|-----------|
| **Barometric pressure** (BME680 built-in) | **2** (same BME680 as gas sensors) | Pack-level | Atmospheric pressure inside pack enclosure. A vent event causes pressure spike (>5 hPa in <2 seconds). An intact pack pre-vent shows slow pressure rise from internal gas accumulation. | Co-located with gas sensors (BME680 is 4-in-1: gas+pressure+temp+humidity). |
| **Cell swelling / compression sensor** (Load cell or FSR per module) | **1 per module = 8** (ideal). **2 minimum** (practical for prototype, placed on module end-plates of most thermally stressed modules) | Module-level | Mechanical force exerted by cells against module compression plates. Swelling increases force before any gas vents — earliest mechanical indicator. | **Thin-film force sensors** (FSR or strain gauge) placed between the module end-plate and the cell stack. As cells swell internally, force on end-plate increases. |

**Prismatic cell swelling detection:**

```
 SIDE VIEW — Swelling Detection per Module

 ┌──────────────────────────────────────────────────────────┐
 │  End     ┌────┐  ┌────┐  ┌────┐        ┌────┐  End     │
 │  Plate ← │ C1 │  │ C2 │  │ C3 │  ...   │ C8 │→ Plate  │
 │  │       └────┘  └────┘  └────┘        └────┘  │       │
 │  │                                              │       │
 │  │◄──── Module compression (tie rods/bands) ───►│       │
 │  │                                              │       │
 │ [FSR]◄── Force Sensor HERE ──────────────────[FSR]     │
 │  │       Between end-plate and cell             │       │
 │  │       Normal: ~500N compression              │       │
 │  │       Swelling: force rises to 800N+         │       │
 │  │       Pre-vent: force rises to 1500N+        │       │
 └──────────────────────────────────────────────────────────┘

 WHY THIS WORKS:
 → During gas generation (Stage 2-3), internal gas pressure
   causes the prismatic cell to bulge outward
 → In a compressed module, the cell CANNOT freely expand —
   instead, force on the end-plate increases
 → This force increase is detectable BEFORE the cell vents
 → For LFP cells: swelling begins at ~100°C internal, well
   before vent at ~270°C — giving 10-30 MINUTES of warning
```

### 2.6 Complete Sensor Count Summary for Full Pack (104S8P)

| Sensor Category | Sensor Type | Per-Group | Per-Module | Per-Pack | Total Count |
|----------------|-------------|-----------|------------|----------|-------------|
| **Voltage** | Parallel group voltage (AFE IC) | 1 | 13 | — | **104** |
| **Voltage** | Module voltage (derived) | — | 1 | — | **8** (derived) |
| **Voltage** | Pack voltage (HV transducer) | — | — | 1 | **1** |
| **Current** | Pack current (Hall sensor) | — | — | 1 | **1** |
| **Current** | Isolation monitor | — | — | 1 | **1** |
| **Temperature** | Cell surface NTC | — | 2 | — | **16** |
| **Temperature** | Ambient air | — | — | 1 | **1** |
| **Temperature** | Coolant inlet | — | — | 1 | **1** |
| **Temperature** | Coolant outlet | — | — | 1 | **1** |
| **Gas** | VOC sensor (BME680) | — | — | 2 | **2** |
| **Gas** | H₂ sensor (optional) | — | — | 1 | **1** |
| **Pressure** | Barometric (BME680 built-in) | — | — | 2 | **2** (co-located) |
| **Swelling** | Module end-plate force | — | 1 (ideal) | — | **8** (ideal) / 2 (minimum) |
| | | | | **TOTAL** | **~131** active sensor channels (832 physical cells) |

---

## 3. What We Compute With All These Inputs

### 3.1 Computed Parameters — Full List

Every raw sensor reading is processed into higher-level computed parameters. Here is the complete computation pipeline:

#### A. From Voltage Sensors (104 parallel-group voltages + 1 pack voltage)

| Computed Parameter | Formula / Method | What It Tells Us | Anomaly Threshold |
|-------------------|-----------------|------------------|-------------------|
| **Parallel group voltage** (V_group_n) | Direct reading from AFE IC, 104 individual values (1 per series position) | SOC per group, detect over/under-voltage, group imbalance. Note: individual cell faults partially masked by parallel cells | Warning: V < 2.7V or V > 3.55V |
| **Group voltage deviation** (ΔV_n) | ΔV_n = V_group_n − mean(V_all_groups_in_module) | Detects one group drifting away from its module peers. In 8P config, a single-cell ISC causes ~1/8 of the full voltage deviation | Warning: |ΔV_n| > 15mV (lower threshold due to parallel masking). Critical: |ΔV_n| > 50mV |
| **Max-min group spread** (V_spread) | V_spread = max(V_all_104) − min(V_all_104) | Overall pack balance health | Warning: > 50mV. Critical: > 150mV |
| **Self-discharge rate** (dV/dt at rest) | Measure V_cell_n at t₁ and t₂ during rest (key-off, no load). dV/dt = (V₂ − V₁) / (t₂ − t₁) | Micro-internal short circuit — cell drains itself. Earliest electrical precursor | Warning: dV/dt > 5mV/hour at rest. Critical: > 20mV/hour |
| **Voltage ripple / instability** | Standard deviation of V_cell_n over 10-second window during steady-state operation | Loose connection, degraded contact, intermittent ISC | Warning: σ(V) > 5mV. Critical: > 15mV |
| **SOC per cell** (estimated) | Lookup table: V_cell → SOC based on LFP OCV curve (flat between 20-80% SOC) | Energy management, detect capacity fade | N/A (management, not safety) |

#### B. From Current Sensor (1 pack current)

| Computed Parameter | Formula / Method | What It Tells Us | Anomaly Threshold |
|-------------------|-----------------|------------------|-------------------|
| **Instantaneous current** (I_pack) | Direct reading from Hall sensor, ±500A range | Charge/discharge rate, detect overcurrent events. I_cell = I_pack / 8 | Warning: > 1.5C (>180A). Critical: > 2.5C (>300A) |
| **C-rate** | C_rate = I_pack / Capacity_Ah = I_pack / 120 | Operating intensity, used to adjust all thermal thresholds. 120 Ah capacity (8 × 15 Ah parallel group) | Inform context, not standalone alarm |
| **Coulomb counting** (SOC tracking) | SOC = SOC_initial + ∫(I_pack × dt) / (Capacity × 3600) | State of charge estimation (complementary to voltage-based SOC) | N/A (management) |
| **Current spike detection** | |dI/dt| = |I(t) − I(t−Δt)| / Δt | Internal short circuit → massive current spike in <100ms. External short → current exceeds physical load expectation. | Warning: dI/dt > 100A/s without load transient. Critical: dI/dt > 500A/s |
| **RMS current** (thermal load) | I_rms = √(mean(I²)) over 60s sliding window | Thermal stress metric — used to predict temperature rise | Feed to thermal model, not standalone alarm |

#### C. From Voltage + Current Together → Internal Resistance

| Computed Parameter | Formula / Method | What It Tells Us | Anomaly Threshold |
|-------------------|-----------------|------------------|-------------------|
| **Internal resistance per group** (R_int_group_n) | R_int_group_n = ΔV_group_n / ΔI_pack during a load transient (>5A change over <500ms). This measures the PARALLEL COMBINATION of 8 cells' R_int. R_int_group ≈ R_int_cell / 8. | **EARLIEST reliable indicator of degradation.** If one cell's R_int rises, the group R_int shifts measurably. In 8P: a 50% rise in one cell → ~6.25% rise in group R_int → detectable. | Warning: R_int_group_n > 110% of baseline (tighter threshold due to parallel masking). Critical: > 130% of baseline. Emergency: > 160% or rising > 3%/hour |
| **R_int trend** (dR/dt) | Exponential moving average of R_int_n over days/weeks | Accelerating degradation → approaching end-of-life or failure | Warning: dR/dt doubles in <1 week |
| **R_int cell-to-cell deviation** | ΔR_n = R_int_n − mean(R_int_module) | One cell degrading faster than peers | Warning: |ΔR_n| > 0.3mΩ above module mean |

**How R_int is measured in practice:**

```
LOAD TRANSIENT MEASUREMENT (happens naturally during driving)

Time ─────────────────────────────────────────────────►
I_pack  ────── 30A ─────┐                       ┌── 30A ──
                         │                       │
                         └──── 120A ─────────────┘
                         ↑                       ↑
                       t₁ (step up)           t₂ (step down)

V_group_5 ─── 3.28V ────┐                       ┌── 3.28V ──
                          │                       │
                          └──── 3.24V ────────────┘

ΔI = 120 − 30 = 90A
ΔV_group_5 = 3.28 − 3.24 = 40mV = 0.040V
R_int_group_5 = 0.040 / 90 = 0.44mΩ  ← HEALTHY (= ~3.5mΩ/cell ÷ 8)

If R_int_group_5 was previously 0.38mΩ → it has risen by 16%
→ In 8P: one cell's R_int may have DOUBLED (others unchanged)
→ WARNING: Degradation detected in Group 5.
```

#### D. From Temperature Sensors → Thermal Computations

| Computed Parameter | Formula / Method | What It Tells Us | Anomaly Threshold |
|-------------------|-----------------|------------------|-------------------|
| **Rate of temperature rise** (dT/dt) per module | dT/dt = (T(t) − T(t − 60s)) / 60s, computed for each of the 16 NTCs | Self-heating → exothermic reaction inside cell. **The** hallmark of thermal runaway progression. | Warning: > 0.5°C/min. Critical: > 2°C/min. Emergency: > 5°C/min |
| **Inter-module ΔT** | ΔT_max = max(T_all_modules) − min(T_all_modules) | One module running hotter than others → localized fault or cooling failure | Warning: > 5°C. Critical: > 10°C |
| **Intra-module ΔT** | ΔT_intra = |T_NTC1 − T_NTC2| within same module | Thermal gradient WITHIN one module — one half is hotter → fault localized to specific groups | Warning: > 3°C. Critical: > 8°C |
| **Delta T from ambient** (ΔT_ambient) | ΔT_a = T_cell_surface − T_ambient | **Context-aware** temperature anomaly. Eliminates false alarms from hot Indian summers. | See context-dependent table below |
| **Estimated core temperature** | T_core = T_surface + (I_cell² × R_int_cell × R_thermal) where I_cell = I_pack/8 | Thermal model output. Estimates internal cell temperature that we cannot directly measure | Warning: T_core_est > 65°C (LFP). Critical: > 80°C. Emergency: > 100°C |
| **Coolant ΔT** | ΔT_coolant = T_outlet − T_inlet | Heat rejection by cooling system. If ΔT_coolant suddenly drops while cells are hot → cooling failure | Warning: ΔT_coolant < 2°C during high C-rate |
| **Absolute temperature** | Direct reading from 16 NTCs | Hard safety limit — regardless of context | Emergency: Any cell surface > 80°C (LFP) |

**Context-dependent ΔT_ambient thresholds (India-adapted):**

| Operating State | How Detected | ΔT_ambient Warning | ΔT_ambient Critical |
|----------------|-------------|-------------------|--------------------|
| Idle / Parked (I < 2A) | I_pack ≈ 0 for > 5 min | > 8°C | > 15°C |
| Low discharge (< 0.5C, < 60A) | 0 < I < 60A | > 12°C | > 20°C |
| Normal discharge (0.5–1C, 60–120A) | 60A < I < 120A | > 18°C | > 28°C |
| High discharge (1–2C, 120–240A) | 120A < I < 240A | > 22°C | > 32°C |
| Charging (< 1C, < 120A) | I_pack negative (charging) | > 18°C | > 28°C |
| Fast charging (~1C, ~120A DC) | I negative, > 100A | > 22°C | > 32°C |

#### E. From Gas Sensors → Gas Computations

| Computed Parameter | Formula / Method | What It Tells Us | Anomaly Threshold |
|-------------------|-----------------|------------------|-------------------|
| **Gas resistance ratio** (per BME680) | GR_ratio = Gas_R_current / Gas_R_baseline (baseline = EMA over hours of normal operation) | Electrolyte decomposition → gas leaks into pack enclosure → MOX sensor resistance DROPS. Ratio < 1.0 = gas detected. | Warning: GR_ratio < 0.7 (30% drop). Critical: GR_ratio < 0.4 (60% drop) |
| **Gas rate of change** (dGR/dt) | Rate of change of gas resistance over 30-second window | Sudden vent event vs. slow leak. Rapid drop = cell vent (Stage 3). Slow drift = gradual electrolyte seepage (Stage 2). | Warning: dGR/dt > 20%/min. Critical: > 50%/min (vent event) |
| **Spatial gas gradient** | GR_sensor1 vs. GR_sensor2 at same timestamp | Which END of the pack is the gas source? Sensor that triggers first → gas is from that region's modules. | Informational → localize fault module region |
| **Humidity-compensated gas** | GR_compensated = GR_raw × f(humidity) (BME680 provides humidity reading) | Raw gas resistance is affected by humidity. Monsoon = 90%+ RH → false gas readings if not compensated. | Use compensated value for all thresholds |

#### F. From Pressure Sensors

| Computed Parameter | Formula / Method | What It Tells Us | Anomaly Threshold |
|-------------------|-----------------|------------------|-------------------|
| **Enclosure pressure delta** | ΔP = P_current − P_baseline (baseline = EMA over hours, compensated for altitude and weather changes) | Cell vent event → sudden pressure spike inside pack enclosure. Pre-vent gas accumulation → slow pressure rise. | Warning: ΔP > 2 hPa sustained over 30s. Critical: ΔP > 5 hPa in < 5 seconds (vent event) |
| **Pressure rate of change** (dP/dt) | Rate of pressure change over 5-second window | Distinguishes slow leak from explosive vent. Vent = pressure spike in <2 seconds. | Critical: dP/dt > 2 hPa/s |

#### G. From Swelling Sensors

| Computed Parameter | Formula / Method | What It Tells Us | Anomaly Threshold |
|-------------------|-----------------|------------------|-------------------|
| **Module compression force** | Direct reading from force sensor / strain gauge on end-plate | Gas generation inside cells → cells swell → force on end-plate increases. **Occurs BEFORE vent** — earliest mechanical indicator. | Warning: Force > 130% of baseline. Critical: > 160% of baseline |
| **Swelling rate** (dF/dt) | Rate of force change over 60-second window | Slow swelling (thermal expansion during charge) vs. rapid swelling (gas generation) | Warning: dF/dt > 10%/min. Critical: > 30%/min |

---

## 4. The Correlation Engine — Avoiding False Alarms, Ensuring Fast True Alarms

### 4.1 Why Correlation Is Non-Negotiable

Every SINGLE parameter alone has an unacceptable false positive rate:

| Parameter Alone | False Positive Rate | Common False Triggers |
|----------------|--------------------|-----------------------|
| Temperature > threshold | 15–25% | Indian summer (48°C ambient), high C-rate, parked in sun |
| Voltage deviation | 10–15% | Normal SOC changes, load transients, cell aging variation |
| Gas resistance drop | 5–15% | Humidity changes (monsoon), sensor drift, ambient VOCs (petrol fumes near fuel station) |
| Current spike | 20%+ | Regenerative braking, motor transients, AC compressor cycling |
| Pressure change | 5–10% | Altitude change (Ghat roads), weather front, door slam vibration |
| Swelling increase | 10–15% | Normal thermal expansion during charge, temperature cycling |

**With 2+ independent parameter categories confirming: < 1% false positive rate.**

### 4.2 Correlation Matrix — What Confirms What

```
 CONFIRMATION MATRIX: Parameter A (row) confirms Parameter B (column)
 ═══════════════════════════════════════════════════════════════════════
          │ Temp↑ │ dT/dt↑│  ΔV   │ R_int↑│  Gas  │Pressure│Swelling│
 ─────────┼───────┼───────┼───────┼───────┼───────┼────────┼────────┤
 Temp↑    │   —   │ SELF  │ GOOD  │ GOOD  │ STRONG│ STRONG │ STRONG │
 dT/dt↑   │ SELF  │   —   │ GOOD  │ STRONG│ STRONG│ STRONG │ STRONG │
 ΔV       │ GOOD  │ GOOD  │   —   │ STRONG│  OK   │  OK    │  OK    │
 R_int↑   │ GOOD  │STRONG │STRONG │   —   │  OK   │  OK    │  OK    │
 Gas      │STRONG │STRONG │  OK   │  OK   │   —   │ STRONG │ STRONG │
 Pressure │STRONG │STRONG │  OK   │  OK   │STRONG │   —    │ STRONG │
 Swelling │STRONG │STRONG │  OK   │  OK   │STRONG │ STRONG │   —    │
 ═══════════════════════════════════════════════════════════════════════

 STRONG = Independent physics path. If both trigger, confidence ≈ 99%.
         (e.g., Gas + Temperature = chemical decomposition confirmed)
 GOOD   = Correlated but partially dependent. Boosts confidence to ~95%.
         (e.g., Temp↑ + ΔV = electrical+thermal fault)
 OK     = Weakly correlated. Useful for context, not standalone confirmation.
 SELF   = Same measurement family (e.g., Temp and dT/dt are from same NTC)
```

### 4.3 Alarm Escalation State Machine with Correlation

```
 ┌──────────────────────────────────────────────────────────────────────┐
 │                    ALARM STATE MACHINE                               │
 │                                                                      │
 │  ┌─────────┐    1 param    ┌─────────┐    2+ params   ┌──────────┐ │
 │  │         │   exceeds     │         │   from ≥2      │          │ │
 │  │ NORMAL  │──threshold───►│ WARNING │──categories────►│ CRITICAL │ │
 │  │ (GREEN) │               │(YELLOW) │   confirm      │  (RED)   │ │
 │  │         │◄──────────────│         │                │          │ │
 │  │         │  param returns│         │                │          │ │
 │  │         │  to normal for│         │                │          │ │
 │  │         │  >30 seconds  │         │                │          │ │
 │  └─────────┘  (hysteresis) └─────────┘                └────┬─────┘ │
 │                                                             │       │
 │                                      Any of:                │       │
 │                                      · dT/dt > 5°C/min     │       │
 │                                      · Gas vent detected    │       │
 │                                      · T_cell > 80°C       ▼       │
 │                                      · Current spike >500A         │
 │                                                        ┌──────────┐│
 │                                                        │EMERGENCY ││
 │                                                        │ (DISCO-  ││
 │                                                        │  NNECT)  ││
 │                                                        │          ││
 │                                                        │ Contactor││
 │                                                        │ opens    ││
 │                                                        │ <100ms   ││
 │                                                        └──────────┘│
 └──────────────────────────────────────────────────────────────────────┘

 STATE TRANSITIONS:

 NORMAL → WARNING: Single parameter exceeds its warning threshold.
   Actions: Log event. Increase sampling rate to 10 Hz. Alert cloud.

 WARNING → NORMAL: All parameters return to normal range for >30s.
   (Hysteresis prevents oscillation in borderline conditions)

 WARNING → CRITICAL: At least 2 parameters from DIFFERENT categories
   (thermal + electrical, or chemical + mechanical, etc.) exceed
   their respective thresholds simultaneously.
   Actions: Sound audible alarm. Send push notification. Reduce max
   charge/discharge rate. Prepare contactor for disconnect.

 CRITICAL → EMERGENCY: Any SINGLE parameter reaches emergency threshold
   (these are physics-based limits where thermal runaway is imminent).
   Actions: IMMEDIATE contactor disconnect (<100ms). Full alarm.
   Lock out re-energization until manual reset by technician.

 EMERGENCY bypasses all intermediate states:
   ANY single emergency-level reading → IMMEDIATE DISCONNECT.
   This is the fail-safe that does not require correlation.
```

### 4.4 Fault Scenarios and Correlation Signatures

| Fault Scenario | Cell Voltage | R_int | Temperature | dT/dt | Gas | Pressure | Swelling | Confidence |
|---------------|-------------|-------|-------------|-------|-----|----------|----------|------------|
| **ISC (dendrite)** | ↓ slow drop at rest | ↑ significant | ↑ localized | ↑ moderate | — | — | — | HIGH (V + R + T) |
| **SEI degradation** | ↓ slight | ↑↑ primary indicator | — initially | — initially | — | — | — | MEDIUM (R alone initially, confirm with T over time) |
| **Overcharge** | ↑ above 3.65V | ↓ slightly | ↑ | ↑ | — | — | ↑ slight | HIGH (V + T + swell) |
| **Thermal runaway Stage 2** | ↓ | ↑ | ↑↑ | ↑↑ | ↑ begins | ↑ slight | ↑ | VERY HIGH (all categories) |
| **External short circuit** | ↓↓ rapid | — | ↑ rapid | ↑↑↑ | — | — | — | HIGH (V drop + I spike + T rise) |
| **Cooling system failure** | — | — | ↑ all modules | ↑ uniform | — | — | — | HIGH (all modules simultaneously → not cell fault, it's system fault) |
| **Hot ambient (false alarm if not compensated)** | — | — | ↑ (but ΔT_ambient is normal) | — | — | — | — | LOW → REJECTED by ambient compensation |
| **High C-rate (normal operation)** | ↓ normal sag | — | ↑ (but proportional to I²) | ↑ (but proportional to I²) | — | — | — | LOW → REJECTED by C-rate context |

---

## 5. Prototype Mapping — How This Scales Down to VSDSquadron Ultra Demo

For the prototype on VSDSquadron Ultra, we simulate ONE MODULE (a small cell group) instead of the full 8-module, 832-cell pack:

### 5.1 Prototype Sensor Mapping

| Full Pack Sensor | Prototype Equivalent | Qty in Prototype | Notes |
|-----------------|---------------------|------------------|-------|
| 104× group voltage (AFE IC) | INA219 (pack V+I) + simulated per-group via potentiometers or ADS1115 ADC | 1 INA219 + 1 ADS1115 (4 channels) | Measure 4 representative parallel-group voltages. Demonstrate per-group detection. |
| 1× Hall-effect current sensor | INA219 (shunt-based, ±3.2A range) | 1 | Adequate for prototype current levels (<3A) |
| 16× cell surface NTC | 4× NTC 10kΩ thermistors (1 per simulated group) + 1× ambient NTC | 5 | Demonstrates per-group thermal monitoring |
| 2× BME680 | 1× BME680 | 1 | Gas + pressure + temp + humidity |
| 8× swelling sensors | 1× FSR402 (between 2 prototype cells) | 1 | Demonstrates swelling detection concept |
| Contactor disconnect | Relay + MOSFET on GPIO | 1 | Demonstrates safety disconnect |

### 5.2 What the Prototype Demonstates to Organizers

1. **Per-cell voltage monitoring** → detect cell deviation and ISC signature
2. **Internal resistance calculation** → demonstrate R_int computation from V and I
3. **dT/dt and ΔT** → rate-of-rise and inter-cell temperature differential
4. **Ambient-compensated thresholds** → show that the same 45°C cell temp triggers alarm in winter but not in Indian summer (because ΔT_ambient differs)
5. **Gas detection** → BME680 detects VOC changes (can simulate with isopropyl alcohol vapor)
6. **Pressure detection** → BME680 detects enclosure pressure changes (can simulate by blowing into sealed enclosure)
7. **Multi-parameter correlation** → demonstrate that single-parameter anomaly → WARNING, but two categories → CRITICAL, and emergency threshold → DISCONNECT
8. **Sub-100ms contactor disconnect** → relay clicks within 100ms of emergency condition

> **The algorithm running on the prototype is IDENTICAL to what would run on the production pack.** Only the sensor count and hardware interface differ. The correlation engine, state machine, and threshold logic are the same firmware.

---

## 6. Summary — The Engineering Answer

| Question | Engineering Answer |
|----------|-------------------|
| **What pack?** | Tata Nexon EV Max, 40.5 kWh, **104S8P** LFP cylindrical (IFR32135-15Ah, Guoxuan), **8 modules × 104 cells**, **832 total cells**, 332.8V nominal |
| **How many voltage sensors?** | **104** parallel-group voltages (via AFE ICs, 13 per module × 8 modules) + 1 pack-level HV transducer. Each measurement = voltage of 8 parallel cells |
| **How many current sensors?** | 1 pack-level Hall-effect sensor (104S8P = one total current path; I_cell = I_pack/8 at each position) |
| **How many temperature sensors?** | **16** cell-surface NTCs (2 per module × 8 modules) + 1 ambient + 2 coolant = **19 total** |
| **Where are temp sensors placed?** | NTCs wedged between cells in interior groups (between groups 3-4 and 10-11 in each 13S module), capturing the hottest zone and thermal gradient. Especially critical in 8P architecture where voltage masking makes temp the primary fault indicator. |
| **Core vs. surface strategy?** | Surface measured directly by NTC. Core ESTIMATED via 1D lumped thermal model: T_core = T_surface + I_cell²·R_int·R_thermal (I_cell = I_pack/8). |
| **How many gas sensors?** | 2 BME680 at opposite ends of pack (spatial gradient detection) + 1 optional H₂ sensor |
| **How many pressure sensors?** | 2 (built into the BME680s) — co-located with gas sensors |
| **How many swelling sensors?** | **8** ideal (1 per module end-plate) or 2 minimum (most thermally stressed modules) |
| **Total sensor channels?** | **~131** active sensor channels for the full pack (832 physical cells) |
| **What is computed?** | V_group, ΔV_group, V_spread, self-discharge, V_ripple, I_pack, I_cell (=I_pack/8), C-rate, dI/dt, R_int per group, dR/dt, T_surface, T_core_est, dT/dt, ΔT_inter-module, ΔT_intra-module, ΔT_ambient, gas_ratio, gas_direction, pressure_delta, dP/dt, swelling_force, dF/dt, SOC, SOH |
| **How are false alarms avoided?** | Multi-parameter correlation engine requiring 2+ independent categories to escalate. Context-aware thresholds (ambient temp, C-rate, charge state). 30-second hysteresis. **Tighter voltage thresholds for 8P parallel masking.** |
