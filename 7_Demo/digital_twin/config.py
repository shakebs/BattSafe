"""
Battery Pack Digital Twin — Configuration
==========================================
All constants, thresholds, and pack parameters for the
Tata Nexon EV Max (104S8P, 832 cells, LFP IFR32135-15Ah).

Simplified for clear sensor-data demonstration and
correlation-aware fault injection.
"""

import numpy as np

# ═══════════════════════════════════════════════════════════════
# CELL PARAMETERS — LFP IFR32135-15Ah (Guoxuan/Gotion)
# ═══════════════════════════════════════════════════════════════

CELL_NOMINAL_VOLTAGE = 3.2        # V
CELL_CAPACITY_AH = 15.0           # Ah
CELL_MIN_VOLTAGE = 2.5            # V (absolute cutoff)
CELL_MAX_VOLTAGE = 3.65           # V (absolute cutoff)
CELL_NOMINAL_RINT_MOHM = 3.5      # mΩ at 25°C, 50% SOC, fresh
CELL_THERMAL_RESISTANCE = 3.0     # °C/W (radial, cylindrical)

# Thermal capacitance per GROUP (8 cells + busbar + housing share)
# A single IFR32135 cell: ~300g, Cp ≈ 1000 J/(kg·°C) → ~300 J/°C per cell
# 8 cells + thermal mass of busbar/housing → ~2800 J/°C per group
GROUP_THERMAL_CAPACITANCE = 2800.0  # J/°C per parallel group

# LFP OCV lookup table (SOC → OCV in Volts)
LFP_OCV_SOC = np.array([
    0.00, 0.02, 0.05, 0.08, 0.10, 0.15, 0.20, 0.25,
    0.30, 0.35, 0.40, 0.45, 0.50, 0.55, 0.60, 0.65,
    0.70, 0.75, 0.80, 0.85, 0.90, 0.92, 0.95, 0.98, 1.00
])
LFP_OCV_VOLTAGE = np.array([
    2.50, 2.80, 3.00, 3.10, 3.15, 3.20, 3.22, 3.24,
    3.25, 3.26, 3.27, 3.28, 3.28, 3.29, 3.29, 3.30,
    3.30, 3.31, 3.32, 3.33, 3.35, 3.37, 3.40, 3.50, 3.65
])

# R_int temperature coefficient (multiplier vs temperature)
RINT_TEMP_BREAKPOINTS = np.array([-10, 0, 10, 20, 25, 35, 45, 55, 65, 80])
RINT_TEMP_MULTIPLIERS = np.array([2.5, 1.8, 1.4, 1.1, 1.0, 0.95, 0.92, 0.95, 1.1, 1.5])

# R_int SOC coefficient (multiplier vs SOC)
RINT_SOC_BREAKPOINTS = np.array([0.0, 0.05, 0.10, 0.20, 0.50, 0.80, 0.90, 0.95, 1.0])
RINT_SOC_MULTIPLIERS = np.array([2.0, 1.5, 1.2, 1.05, 1.0, 1.0, 1.05, 1.2, 1.8])

# ═══════════════════════════════════════════════════════════════
# PACK ARCHITECTURE — 104S8P
# ═══════════════════════════════════════════════════════════════

NUM_MODULES = 8
GROUPS_PER_MODULE = 13            # 13 series positions per module
CELLS_PER_GROUP = 8               # 8 parallel cells per group
TOTAL_SERIES = NUM_MODULES * GROUPS_PER_MODULE  # 104
TOTAL_CELLS = TOTAL_SERIES * CELLS_PER_GROUP     # 832

PACK_NOMINAL_VOLTAGE = TOTAL_SERIES * CELL_NOMINAL_VOLTAGE  # 332.8V
PACK_CAPACITY_AH = CELLS_PER_GROUP * CELL_CAPACITY_AH       # 120 Ah

# ═══════════════════════════════════════════════════════════════
# THERMAL PARAMETERS
# ═══════════════════════════════════════════════════════════════

AMBIENT_TEMP_DEFAULT = 30.0       # °C (India typical)
COOLANT_INLET_TEMP = 25.0         # °C
COOLANT_FLOW_RATE = 0.1           # kg/s
COOLANT_SPECIFIC_HEAT = 3500.0    # J/(kg·°C) (50/50 glycol-water)

# Thermal coupling — cells in module share busbar + aluminum cold plate
# Higher coupling = faster heat propagation between adjacent groups
GROUP_THERMAL_COUPLING = 2.0      # W/°C between adjacent groups (tightly packed)
MODULE_COOLANT_COUPLING = 8.0     # W/°C module ↔ coolant plate
MODULE_AMBIENT_COUPLING = 0.2     # W/°C module ↔ ambient (small, insulated)

# NTC positions within each module (group indices, 0-based internal)
# Between groups 3-4 and 10-11 (as per engineering spec)
NTC_POSITIONS = [(3, 4), (10, 11)]

# ═══════════════════════════════════════════════════════════════
# GAS / PRESSURE / SWELLING MODEL PARAMETERS
# ═══════════════════════════════════════════════════════════════

# Gas (BME680 VOC resistance ratio)
GAS_ONSET_TEMP = 100.0            # °C — SEI decomposition begins
GAS_RATE_COEFFICIENT = 0.00005    # ratio drop per °C-above-onset per second
GAS_BASELINE_RESISTANCE = 50000.0 # Ω (BME680 clean air baseline)

# Pressure (BME680 barometric + enclosure effects)
ENCLOSURE_VOLUME_LITERS = 80.0
PRESSURE_BASELINE_HPA = 1013.25   # Standard atmosphere

# Swelling (end-plate force sensor)
SWELLING_ONSET_TEMP = 60.0        # °C (internal) — above this, slow expansion
SWELLING_RATE_COEFFICIENT = 0.0001 # %/°C-above-onset per second (very slow)
SWELLING_BASELINE_FORCE_N = 500.0  # Normal module compression force

# ═══════════════════════════════════════════════════════════════
# THERMAL RUNAWAY CASCADE STAGES (real chemistry)
# ═══════════════════════════════════════════════════════════════
# Based on Feng et al. (2018) "Thermal runaway mechanism of lithium ion battery"
# and references from deep_research_battery_science_and_industry.md

THERMAL_CASCADE = {
    'NORMAL': {
        'temp_max': 60,
        'label': 'Normal',
        'color': '#22c55e',  # green
        'desc': 'All parameters within spec',
    },
    'ELEVATED': {
        'temp_max': 80,
        'label': 'Elevated',
        'color': '#eab308',  # yellow
        'desc': 'Approaching thermal limits — separator softening begins',
    },
    'SEI_DECOMPOSITION': {
        'temp_max': 120,
        'label': 'SEI Decomposition',
        'color': '#f97316',  # orange
        'desc': 'SEI layer breaks down (80–120°C) → exothermic → exposes anode',
    },
    'SEPARATOR_COLLAPSE': {
        'temp_max': 150,
        'label': 'Separator Collapse',
        'color': '#ef4444',  # red
        'desc': 'Separator melts/shrinks (130–150°C) → internal short risk',
    },
    'ELECTROLYTE_DECOMP': {
        'temp_max': 200,
        'label': 'Electrolyte Decomposition',
        'color': '#dc2626',  # dark red
        'desc': 'Electrolyte decomposes (150–200°C) → flammable gas + O₂ release',
    },
    'CATHODE_DECOMP': {
        'temp_max': 300,
        'label': 'Cathode Decomposition',
        'color': '#991b1b',  # very dark red
        'desc': 'Cathode releases O₂ (200–300°C) → fire/explosion — POINT OF NO RETURN',
    },
    'FULL_RUNAWAY': {
        'temp_max': 9999,
        'label': 'THERMAL RUNAWAY',
        'color': '#7f1d1d',  # almost black red
        'desc': 'Uncontrollable exothermic cascade — fire, venting, possible explosion',
    },
}

# Stage temperature thresholds (°C, core temperature)
STAGE_THRESHOLDS = [60, 80, 120, 150, 200, 300]  # Matches cascade keys

# ═══════════════════════════════════════════════════════════════
# SENSOR CHANNEL COUNT & SAMPLING
# ═══════════════════════════════════════════════════════════════

SENSOR_CHANNELS = {
    'group_voltage':       TOTAL_SERIES,           # 104 — AFE per group
    'pack_voltage':        1,                       # 1 — HV transducer
    'pack_current':        1,                       # 1 — Hall-effect
    'isolation':           1,                       # 1 — isolation monitor
    'ntc_cell':            NUM_MODULES * 2,         # 16 — 2 per module
    'ntc_ambient':         1,                       # 1 — on BMS PCB
    'coolant_inlet':       1,                       # 1 — inlet pipe
    'coolant_outlet':      1,                       # 1 — outlet pipe
    'gas_bme680':          2,                       # 2 — pack lid
    'pressure_bme680':     2,                       # 2 — co-located w/ gas
    'humidity_bme680':     1,                       # 1 — averaged
    'swelling':            NUM_MODULES,             # 8 — per module end-plate
}
TOTAL_SENSOR_CHANNELS = sum(SENSOR_CHANNELS.values())  # 139

SAMPLING_RATE_HZ = 10             # 10 Hz = 100ms per sample
DASHBOARD_PUSH_RATE_HZ = 2        # 2 Hz = 500ms

# ═══════════════════════════════════════════════════════════════
# SENSOR NORMAL RANGES
# ═══════════════════════════════════════════════════════════════

SENSOR_RANGES = {
    'pack_voltage':    {'min': 260, 'max': 380, 'unit': 'V'},
    'pack_current':    {'min': -315, 'max': 315, 'unit': 'A'},
    'group_voltage':   {'min': 2.5, 'max': 3.65, 'unit': 'V'},
    'cell_temp':       {'min': 10, 'max': 55, 'unit': '°C', 'warn': 45, 'crit': 60},
    'ambient_temp':    {'min': -10, 'max': 55, 'unit': '°C'},
    'coolant_inlet':   {'min': 15, 'max': 35, 'unit': '°C'},
    'coolant_outlet':  {'min': 20, 'max': 45, 'unit': '°C'},
    'gas_ratio':       {'normal_min': 0.85, 'normal_max': 1.0, 'warn': 0.7, 'crit': 0.4, 'unit': 'ratio'},
    'pressure_delta':  {'normal': 0.0, 'warn': 2.0, 'crit': 5.0, 'unit': 'hPa'},
    'humidity':        {'min': 20, 'max': 90, 'unit': '%RH'},
    'isolation':       {'min': 100, 'max': 1000, 'warn': 200, 'unit': 'MΩ'},
    'swelling_pct':    {'normal': 0, 'warn': 3, 'crit': 8, 'unit': '%'},
    'v_spread':        {'normal': 0, 'warn': 50, 'crit': 150, 'unit': 'mV'},
    't_spread':        {'normal': 0, 'warn': 5, 'crit': 10, 'unit': '°C'},
    'dt_dt':           {'normal': 0.1, 'warn': 0.5, 'crit': 1.0, 'unit': '°C/min'},
}

# ═══════════════════════════════════════════════════════════════
# SIMULATION PARAMETERS
# ═══════════════════════════════════════════════════════════════

SIM_DT = 0.1                      # Simulation timestep (seconds)
DASHBOARD_UPDATE_INTERVAL = 0.5   # 500ms — websocket push rate

# Speed control — how many physics steps per wall-clock tick
SIM_SPEED_OPTIONS = [1, 5, 10, 50, 100]  # Available speed multipliers

# Time jump presets (seconds of simulation time to jump)
TIME_JUMP_OPTIONS = [60, 300, 600, 1800]  # 1m, 5m, 10m, 30m

# Serial bridge
SERIAL_BAUD_RATE = 115200
SERIAL_SYNC_BYTE = 0xAA
SERIAL_PAYLOAD_LEN = 28

# ═══════════════════════════════════════════════════════════════
# FAULT TYPES — 8 Correlation-Aware Faults
# ═══════════════════════════════════════════════════════════════

class FaultType:
    """
    8 fault types designed to exercise ALL sensors and support
    the multi-parameter correlation engine on VSDSquadron.
    """
    INTERNAL_SHORT     = "internal_short"
    OVERCHARGE         = "overcharge"
    OVERDISCHARGE      = "overdischarge"
    COOLING_FAILURE    = "cooling_failure"
    EXTERNAL_SHORT     = "external_short"
    THERMAL_RUNAWAY    = "thermal_runaway"
    HIGH_AMBIENT       = "high_ambient"
    SENSOR_DRIFT       = "sensor_drift"


FAULT_CATALOG = {
    FaultType.INTERNAL_SHORT: {
        'name': 'Internal Short Circuit',
        'desc': 'Dendrite growth creates parasitic path. 1 cell in 8P → drains neighbors → localized heating.',
        'sensors': 'V↓(masked), R_int↑, T↑(local), Gas↓(if severe), Swell↑',
        'target': 'group',
    },
    FaultType.OVERCHARGE: {
        'name': 'Overcharge',
        'desc': 'Voltage pushed beyond safe limit. Lithium plating at anode → exothermic gas generation.',
        'sensors': 'V↑↑, T↑, Gas↓, P↑, Swell↑',
        'target': 'group',
    },
    FaultType.OVERDISCHARGE: {
        'name': 'Overdischarge',
        'desc': 'Voltage below cutoff. Cu dissolution from anode → dendrites. R_int rises sharply.',
        'sensors': 'V↓↓, R_int↑↑, T↑(slight)',
        'target': 'group',
    },
    FaultType.COOLING_FAILURE: {
        'name': 'Cooling System Failure',
        'desc': 'No heat rejection → ALL modules heat uniformly. Coolant ΔT collapses to 0.',
        'sensors': 'ALL NTCs↑, Coolant ΔT→0, dT/dt↑(all)',
        'target': 'pack',
    },
    FaultType.EXTERNAL_SHORT: {
        'name': 'External Short Circuit',
        'desc': 'Low-resistance external path → massive current → rapid I²R heating.',
        'sensors': 'I↑↑↑, V↓↓(rapid), T↑↑(rapid), dT/dt↑↑↑',
        'target': 'pack',
    },
    FaultType.THERMAL_RUNAWAY: {
        'name': 'Thermal Runaway',
        'desc': 'Full cascade: SEI → electrolyte → cathode decomposition. Self-accelerating exothermic.',
        'sensors': 'T↑↑↑, dT/dt↑↑↑, Gas↓↓, P↑↑, Swell↑↑, V↓',
        'target': 'group',
    },
    FaultType.HIGH_AMBIENT: {
        'name': 'High Ambient Temperature',
        'desc': 'Indian summer 45°C+. All cells warm but ΔT_ambient normal. Tests false alarm rejection.',
        'sensors': 'ALL NTCs↑, Ambient↑ — NO V/gas/P change',
        'target': 'pack',
    },
    FaultType.SENSOR_DRIFT: {
        'name': 'Sensor Drift (NTC)',
        'desc': 'Single NTC reads abnormally. No corroboration from V/I/gas → correlation engine rejects.',
        'sensors': 'ONE NTC anomaly only — false alarm test',
        'target': 'module',
    },
}
