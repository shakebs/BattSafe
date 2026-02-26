"""
Battery Pack Digital Twin — Fault Injection Engine
====================================================
8 correlation-aware fault types that exercise ALL sensors
and produce multi-parameter signatures for the correlation engine.

Each fault follows real physics/chemistry and affects multiple
sensor channels simultaneously.

Heat values are calibrated against GROUP_THERMAL_CAPACITANCE = 2800 J/°C.
Example: 10W fault heat → ~0.0036 °C/s → ~0.21 °C/min (realistic).
         100W (extreme) → 0.036 °C/s → ~2.1 °C/min (runaway level).
"""

import time
import random
from typing import Dict, List, Optional
from dataclasses import dataclass, field

from digital_twin.config import (
    FaultType, FAULT_CATALOG,
    GROUPS_PER_MODULE, NUM_MODULES,
    CELL_NOMINAL_RINT_MOHM, AMBIENT_TEMP_DEFAULT,
)


@dataclass
class ActiveFault:
    """Represents an active fault in the system."""
    fault_type: str
    module_idx: int          # 0-based (internal)
    group_idx: int           # 0-based (internal), -1 for pack-wide
    severity: float          # 0.0 – 1.0
    duration: float          # seconds (0 = indefinite)
    start_time: float = 0.0
    elapsed: float = 0.0
    fault_id: str = ''

    def __post_init__(self):
        if not self.fault_id:
            self.fault_id = f"{self.fault_type}_{self.module_idx}_{self.group_idx}_{int(time.time()*1000)%100000}"


class FaultInjectionEngine:
    """
    Manages fault injection for the battery pack digital twin.
    Each fault type applies physics-based multi-parameter effects.
    """

    def __init__(self, pack):
        self.pack = pack
        self.active_faults: List[ActiveFault] = []
        self.fault_log: List[Dict] = []
        self._fault_counter = 0

    def inject_fault(self, fault_type: str, module: int = 1, group: int = 1,
                     severity: float = 0.5, duration: float = 0) -> Dict:
        """
        Inject a fault into the system.
        module/group are 1-based (from dashboard).
        """
        if fault_type not in FAULT_CATALOG:
            return {'success': False, 'error': f'Unknown fault type: {fault_type}'}

        # Convert 1-based to 0-based internal
        mod_idx = max(0, min(module - 1, NUM_MODULES - 1))
        grp_idx = max(0, min(group - 1, GROUPS_PER_MODULE - 1))

        # Pack-wide faults don't target specific groups
        catalog_entry = FAULT_CATALOG[fault_type]
        if catalog_entry['target'] == 'pack':
            grp_idx = -1

        fault = ActiveFault(
            fault_type=fault_type,
            module_idx=mod_idx,
            group_idx=grp_idx,
            severity=max(0.1, min(1.0, severity)),
            duration=duration,
            start_time=time.time(),
        )

        self.active_faults.append(fault)
        self._fault_counter += 1

        log_entry = {
            'id': self._fault_counter,
            'fault_id': fault.fault_id,
            'type': fault_type,
            'name': catalog_entry['name'],
            'module': module,
            'group': group if grp_idx >= 0 else 'ALL',
            'severity': severity,
            'time': time.strftime('%H:%M:%S'),
            'action': 'INJECTED',
        }
        self.fault_log.append(log_entry)

        return {'success': True, 'fault_id': fault.fault_id, **log_entry}

    def clear_fault(self, fault_id: str) -> bool:
        """Clear a specific fault by ID."""
        for f in self.active_faults:
            if f.fault_id == fault_id:
                self.active_faults.remove(f)
                self._reset_fault_effects(f)
                self.fault_log.append({
                    'id': self._fault_counter + 1,
                    'fault_id': fault_id,
                    'type': f.fault_type,
                    'name': FAULT_CATALOG[f.fault_type]['name'],
                    'time': time.strftime('%H:%M:%S'),
                    'action': 'CLEARED',
                })
                return True
        return False

    def clear_all_faults(self):
        """Clear all active faults and reset the pack to normal."""
        self.active_faults.clear()
        self._full_reset()

    def apply_faults(self, dt: float):
        """Apply all active fault effects for one timestep."""
        # First, reset all fault modifiers to default
        self._reset_all_modifiers()

        # Remove expired faults
        expired = []
        for f in self.active_faults:
            f.elapsed += dt
            if f.duration > 0 and f.elapsed >= f.duration:
                expired.append(f)

        for f in expired:
            self.active_faults.remove(f)
            self.fault_log.append({
                'id': self._fault_counter + 1,
                'fault_id': f.fault_id,
                'type': f.fault_type,
                'name': FAULT_CATALOG[f.fault_type]['name'],
                'time': time.strftime('%H:%M:%S'),
                'action': 'EXPIRED',
            })

        # Apply each active fault's effects
        for f in self.active_faults:
            self._apply_fault_effect(f, dt)

    def _reset_all_modifiers(self):
        """Reset all fault modifiers to default values."""
        for m in self.pack.modules:
            m.fault_ntc1_offset = 0.0
            m.fault_ntc2_offset = 0.0
            m.cooling_active = True
            for g in m.groups:
                g.fault_rint_multiplier = 1.0
                g.fault_heat_w = 0.0
                g.fault_voltage_offset = 0.0
                g.fault_soc_drain_rate = 0.0

    def _apply_fault_effect(self, fault: ActiveFault, dt: float):
        """Apply physics-based multi-parameter effects for a fault."""
        s = fault.severity  # 0.1 – 1.0

        if fault.fault_type == FaultType.INTERNAL_SHORT:
            self._apply_isc(fault, s)

        elif fault.fault_type == FaultType.OVERCHARGE:
            self._apply_overcharge(fault, s)

        elif fault.fault_type == FaultType.OVERDISCHARGE:
            self._apply_overdischarge(fault, s)

        elif fault.fault_type == FaultType.COOLING_FAILURE:
            self._apply_cooling_failure(fault, s)

        elif fault.fault_type == FaultType.EXTERNAL_SHORT:
            self._apply_external_short(fault, s)

        elif fault.fault_type == FaultType.THERMAL_RUNAWAY:
            self._apply_thermal_runaway(fault, s, dt)

        elif fault.fault_type == FaultType.HIGH_AMBIENT:
            self._apply_high_ambient(fault, s)

        elif fault.fault_type == FaultType.SENSOR_DRIFT:
            self._apply_sensor_drift(fault, s)

    # ── Fault Effect Implementations ──────────────────────────
    #
    # Heat calibration notes (GROUP_THERMAL_CAPACITANCE = 2800 J/°C):
    #   1 W  → 0.000357 °C/s → 0.021 °C/min  (barely detectable)
    #  10 W  → 0.00357  °C/s → 0.21  °C/min  (noticeable, realistic ISC)
    #  50 W  → 0.0179   °C/s → 1.07  °C/min  (alarm level)
    # 200 W  → 0.0714   °C/s → 4.3   °C/min  (runaway territory)

    def _apply_isc(self, f: ActiveFault, severity: float):
        """
        Internal Short Circuit.
        Physics: Dendrite creates parasitic current path within cell.
        In 8P group: 1 shorted cell drains its 7 neighbors → localized heating.
        Voltage drop masked by parallel cells (~12-60mV visible from group).
        """
        mod = self.pack.modules[f.module_idx]
        grp = mod.groups[f.group_idx]

        # V drop is masked by 8P: only ~1/8 of full deviation visible
        grp.fault_voltage_offset = -0.012 * severity  # 1.2–12mV drop (masked)

        # R_int rises (degraded cell)
        grp.fault_rint_multiplier = 1.0 + (1.5 * severity)  # Up to 2.5×

        # SOC drain (internal short dissipates energy)
        grp.fault_soc_drain_rate = 0.0002 * severity  # Very slow per second

        # Localized heating: 2–20W (enough for 0.04–0.4 °C/min)
        grp.fault_heat_w = 20.0 * severity

    def _apply_overcharge(self, f: ActiveFault, severity: float):
        """
        Overcharge.
        Physics: Voltage pushed beyond safe limit → lithium plating at anode
        → exothermic. Electrolyte oxidation at high voltage → gas generation.
        """
        mod = self.pack.modules[f.module_idx]
        grp = mod.groups[f.group_idx]

        # Force voltage above normal
        grp.fault_voltage_offset = 0.15 * severity  # Push above 3.55V

        # Exothermic lithium plating: 5–50W
        grp.fault_heat_w = 50.0 * severity

        # R_int gradually rises due to degradation
        elapsed_factor = min(1.0, f.elapsed / 60.0)  # Over 60s
        grp.fault_rint_multiplier = 1.0 + (0.5 * severity * elapsed_factor)

    def _apply_overdischarge(self, f: ActiveFault, severity: float):
        """
        Overdischarge.
        Physics: Cu dissolution from anode current collector → forms
        dendrites. R_int rises sharply below 2.5V. Minimal thermal signature.
        """
        mod = self.pack.modules[f.module_idx]
        grp = mod.groups[f.group_idx]

        # Voltage drops below cutoff
        grp.fault_voltage_offset = -0.5 * severity  # Down toward 2.5V

        # R_int rises dramatically
        grp.fault_rint_multiplier = 1.0 + (4.0 * severity)  # Up to 5×

        # Slight heating from increased resistance: 2–5W
        grp.fault_heat_w = 5.0 * severity

    def _apply_cooling_failure(self, f: ActiveFault, severity: float):
        """
        Cooling System Failure.
        Physics: No heat rejection → ALL modules heat up.
        Coolant ΔT collapses to 0 because coolant is not flowing.
        Key signature: ALL modules rise together (not localized).

        Even without load current, cells drift toward ambient.
        With current flowing, temperature rise is proportional to I²R.
        """
        for m in self.pack.modules:
            m.cooling_active = False

            # Without cooling, residual heat from self-discharge + environment
            # causes slow temperature rise. Add small background heat.
            for g in m.groups:
                g.fault_heat_w += 3.0 * severity  # 3W per group baseline

    def _apply_external_short(self, f: ActiveFault, severity: float):
        """
        External Short Circuit.
        Physics: Low-resistance external path → massive current →
        rapid I²R heating across ALL groups uniformly.
        """
        # Massive current spike (50–300A depending on severity)
        short_current = 50.0 + 250.0 * severity
        self.pack.target_current = short_current

        # Additional heat from arc resistance at external short point
        for m in self.pack.modules:
            for g in m.groups:
                g.fault_heat_w += 5.0 * severity  # On top of I²R

    def _apply_thermal_runaway(self, f: ActiveFault, severity: float, dt: float):
        """
        Thermal Runaway.
        Physics: Full cascade → SEI decomposition (100°C) →
        electrolyte decomposition (150°C) → cathode decomposition (200°C+).
        Self-accelerating exothermic.

        Heat ramps up over time (self-accelerating):
        Initial: ~30W, ramping to 200W+ over 60 seconds.
        """
        mod = self.pack.modules[f.module_idx]
        grp = mod.groups[f.group_idx]

        # Self-accelerating heat — increases over time
        time_factor = min(5.0, 1.0 + f.elapsed / 15.0)  # Ramps up
        grp.fault_heat_w = 30.0 * severity * time_factor  # 30 → 150W

        # R_int rises dramatically
        grp.fault_rint_multiplier = 1.0 + (3.0 * severity * min(2.0, time_factor))

        # Voltage sags as cell internals fail
        grp.fault_voltage_offset = -0.2 * severity * min(1.0, f.elapsed / 20.0)

        # SOC consumed by exothermic reactions
        grp.fault_soc_drain_rate = 0.005 * severity

        # Gas and pressure effects are handled by physics engine
        # when temperature exceeds GAS_ONSET_TEMP (100°C)

        # Neighbor group thermal propagation (if severe enough, after delay)
        if severity > 0.6 and f.elapsed > 15.0:
            prop_factor = severity * 0.2 * min(1.0, (f.elapsed - 15) / 30.0)
            gi = f.group_idx
            if gi > 0:
                mod.groups[gi - 1].fault_heat_w += 15.0 * prop_factor
            if gi < GROUPS_PER_MODULE - 1:
                mod.groups[gi + 1].fault_heat_w += 15.0 * prop_factor

    def _apply_high_ambient(self, f: ActiveFault, severity: float):
        """
        High Ambient Temperature (Indian summer).
        Physics: Ambient rises to 40-50°C. All cells warm proportionally.
        Coolant also warms. Tests false alarm rejection.
        Key: NO gas, NO pressure, NO swelling change.
        """
        self.pack.ambient_temp = AMBIENT_TEMP_DEFAULT + 15.0 * severity  # 30 → 45°C
        self.pack.coolant_inlet_temp = 25.0 + 10.0 * severity  # Coolant warms too

    def _apply_sensor_drift(self, f: ActiveFault, severity: float):
        """
        Sensor Drift (NTC).
        Physics: Single NTC reads abnormally due to sensor degradation.
        No actual temperature change — just measurement error.
        Correlation engine should detect: 1 NTC anomaly + zero
        corroboration from V/I/gas/pressure → false alarm / sensor fault.
        """
        mod = self.pack.modules[f.module_idx]
        # Apply offset to NTC1 of targeted module
        mod.fault_ntc1_offset = 15.0 * severity  # +1.5 to +15°C phantom reading

    def _reset_fault_effects(self, fault: ActiveFault):
        """Reset effects of a specific cleared fault."""
        if fault.fault_type == FaultType.HIGH_AMBIENT:
            self.pack.ambient_temp = AMBIENT_TEMP_DEFAULT
            self.pack.coolant_inlet_temp = 25.0

        if fault.fault_type == FaultType.EXTERNAL_SHORT:
            self.pack.target_current = 0.0

    def _full_reset(self):
        """Full reset of all fault effects to baseline."""
        self._reset_all_modifiers()
        self.pack.ambient_temp = AMBIENT_TEMP_DEFAULT
        self.pack.coolant_inlet_temp = 25.0

    def get_active_faults_summary(self) -> List[Dict]:
        """Return summary of all active faults for dashboard."""
        result = []
        for f in self.active_faults:
            catalog = FAULT_CATALOG.get(f.fault_type, {})
            result.append({
                'fault_id': f.fault_id,
                'type': f.fault_type,
                'name': catalog.get('name', f.fault_type),
                'module': f.module_idx + 1,  # 1-based
                'group': f.group_idx + 1 if f.group_idx >= 0 else 'ALL',
                'severity': f.severity,
                'elapsed': round(f.elapsed, 1),
                'duration': f.duration,
                'sensors': catalog.get('sensors', ''),
            })
        return result

    def get_catalog(self) -> Dict:
        """Return fault catalog for dashboard."""
        return FAULT_CATALOG

    def get_recent_log(self, n: int = 20) -> List[Dict]:
        """Return recent fault log entries."""
        return self.fault_log[-n:]
