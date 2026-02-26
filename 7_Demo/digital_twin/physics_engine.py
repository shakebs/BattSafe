"""
Battery Pack Digital Twin — Physics Engine
============================================
Hierarchical battery model: Cell → ParallelGroup → Module → Pack
with cross-parameter dependencies and physics-inspired dynamics.

Pack: 104S8P = 8 modules × 13 groups × 8 cells = 832 cells
All display numbering is 1-based (M1–M8, G1–G13).
No decision-making — pure sensor data generation.
"""

import numpy as np
from typing import List, Dict

from digital_twin.config import (
    CELL_NOMINAL_VOLTAGE, CELL_CAPACITY_AH, CELL_MIN_VOLTAGE, CELL_MAX_VOLTAGE,
    CELL_NOMINAL_RINT_MOHM, CELL_THERMAL_RESISTANCE, GROUP_THERMAL_CAPACITANCE,
    LFP_OCV_SOC, LFP_OCV_VOLTAGE,
    RINT_TEMP_BREAKPOINTS, RINT_TEMP_MULTIPLIERS,
    RINT_SOC_BREAKPOINTS, RINT_SOC_MULTIPLIERS,
    NUM_MODULES, GROUPS_PER_MODULE, CELLS_PER_GROUP,
    PACK_CAPACITY_AH, AMBIENT_TEMP_DEFAULT,
    COOLANT_INLET_TEMP, COOLANT_FLOW_RATE, COOLANT_SPECIFIC_HEAT,
    GROUP_THERMAL_COUPLING, MODULE_COOLANT_COUPLING, MODULE_AMBIENT_COUPLING,
    NTC_POSITIONS, GAS_ONSET_TEMP, GAS_RATE_COEFFICIENT,
    ENCLOSURE_VOLUME_LITERS, PRESSURE_BASELINE_HPA,
    SWELLING_ONSET_TEMP, SWELLING_RATE_COEFFICIENT, SWELLING_BASELINE_FORCE_N,
    TOTAL_SENSOR_CHANNELS, SAMPLING_RATE_HZ,
    THERMAL_CASCADE, STAGE_THRESHOLDS,
    SIM_DT,
)


def ocv_from_soc(soc: float) -> float:
    """LFP Open Circuit Voltage from State of Charge."""
    return float(np.interp(np.clip(soc, 0, 1), LFP_OCV_SOC, LFP_OCV_VOLTAGE))


def rint_multiplier_temp(temp_c: float) -> float:
    """R_int temperature multiplier (Arrhenius-inspired)."""
    return float(np.interp(temp_c, RINT_TEMP_BREAKPOINTS, RINT_TEMP_MULTIPLIERS))


def rint_multiplier_soc(soc: float) -> float:
    """R_int SOC multiplier (higher at extremes)."""
    return float(np.interp(np.clip(soc, 0, 1), RINT_SOC_BREAKPOINTS, RINT_SOC_MULTIPLIERS))


def get_cascade_stage(core_temp: float) -> Dict:
    """Determine which thermal cascade stage we're in based on core temp."""
    stages = list(THERMAL_CASCADE.items())
    for i, (key, stage) in enumerate(stages):
        if core_temp <= stage['temp_max']:
            return {
                'key': key,
                'index': i,
                'label': stage['label'],
                'color': stage['color'],
                'desc': stage['desc'],
                'temp_max': stage['temp_max'],
            }
    # If above all thresholds, return last stage
    key, stage = stages[-1]
    return {
        'key': key,
        'index': len(stages) - 1,
        'label': stage['label'],
        'color': stage['color'],
        'desc': stage['desc'],
        'temp_max': stage['temp_max'],
    }


class ParallelGroup:
    """
    Models one parallel group (8 cells in parallel at one series position).
    The AFE measures voltage across this group.
    """

    def __init__(self, module_idx: int, group_idx: int):
        self.module_idx = module_idx    # 0-based internal
        self.group_idx = group_idx      # 0-based internal

        # Display labels (1-based)
        self.display_module = module_idx + 1  # M1–M8
        self.display_group = group_idx + 1    # G1–G13

        # State - START at ambient (no initial delta)
        self.soc = 0.50
        self.voltage = ocv_from_soc(self.soc)
        self.temp_surface = AMBIENT_TEMP_DEFAULT  # Start AT ambient
        self.temp_core = AMBIENT_TEMP_DEFAULT      # No initial offset
        self.dt_dt = 0.0  # °C/min

        # Internal resistance (per CELL in mΩ)
        self.rint_mohm = CELL_NOMINAL_RINT_MOHM

        # Fault modifiers (applied by fault injection engine)
        self.fault_rint_multiplier = 1.0
        self.fault_heat_w = 0.0          # Extra heat injection (W)
        self.fault_voltage_offset = 0.0  # Voltage offset (V)
        self.fault_soc_drain_rate = 0.0  # SOC drain per second

        # dT/dt history — use 10-second moving window for stable reading
        self._temp_history = []        # (time, temp) pairs
        self._history_window = 10.0    # seconds

    def get_effective_rint(self, temp: float, soc: float) -> float:
        """Calculate effective internal resistance with all modifiers."""
        base = CELL_NOMINAL_RINT_MOHM
        base *= rint_multiplier_temp(temp)
        base *= rint_multiplier_soc(soc)
        base *= self.fault_rint_multiplier
        return base

    def update(self, i_pack: float, dt: float, ambient_temp: float,
               coolant_temp: float, neighbor_temps: List[float], sim_time: float):
        """Update group state for one timestep."""
        # Current per cell (8P parallel split)
        i_cell = i_pack / CELLS_PER_GROUP

        # Update R_int
        self.rint_mohm = self.get_effective_rint(self.temp_surface, self.soc)
        rint_ohm = self.rint_mohm / 1000.0

        # Heat generation: I²R per cell × 8 cells + fault heat
        heat_per_cell = i_cell ** 2 * rint_ohm
        heat_total = heat_per_cell * CELLS_PER_GROUP + self.fault_heat_w

        # Thermal model (1D lumped) — per group
        q_coolant = (MODULE_COOLANT_COUPLING / GROUPS_PER_MODULE) * (self.temp_surface - coolant_temp)
        q_ambient = (MODULE_AMBIENT_COUPLING / GROUPS_PER_MODULE) * (self.temp_surface - ambient_temp)
        q_neighbor = 0.0
        for nt in neighbor_temps:
            q_neighbor += GROUP_THERMAL_COUPLING * (self.temp_surface - nt)

        q_net = heat_total - q_coolant - q_ambient - q_neighbor

        # Temperature change using proper group thermal mass
        d_temp = q_net * dt / GROUP_THERMAL_CAPACITANCE
        self.temp_surface += d_temp

        # Core temp: surface + I²R thermal gradient
        self.temp_core = self.temp_surface + abs(heat_per_cell) * CELL_THERMAL_RESISTANCE

        # dT/dt calculation: 10-second moving window → °C/min
        self._temp_history.append((sim_time, self.temp_surface))
        cutoff = sim_time - self._history_window
        self._temp_history = [(t, v) for t, v in self._temp_history if t >= cutoff]
        if len(self._temp_history) >= 2:
            t0, v0 = self._temp_history[0]
            t1, v1 = self._temp_history[-1]
            elapsed = t1 - t0
            if elapsed > 0.5:
                self.dt_dt = (v1 - v0) / elapsed * 60.0  # °C/min
            else:
                self.dt_dt = 0.0
        else:
            self.dt_dt = 0.0

        # SOC update (Coulomb counting)
        dsoc = -(i_cell * dt) / (CELL_CAPACITY_AH * 3600.0)
        self.soc = float(np.clip(self.soc + dsoc - self.fault_soc_drain_rate * dt, 0.0, 1.0))

        # Voltage = OCV + IR drop + fault offset
        ocv = ocv_from_soc(self.soc)
        ir_drop = i_cell * rint_ohm
        self.voltage = ocv - ir_drop + self.fault_voltage_offset
        self.voltage = float(np.clip(self.voltage, 0.0, 5.0))

    def reset_to_defaults(self, ambient_temp: float):
        """Full reset to default state."""
        self.soc = 0.50
        self.voltage = ocv_from_soc(0.50)
        self.temp_surface = ambient_temp
        self.temp_core = ambient_temp
        self.dt_dt = 0.0
        self.rint_mohm = CELL_NOMINAL_RINT_MOHM
        self.fault_rint_multiplier = 1.0
        self.fault_heat_w = 0.0
        self.fault_voltage_offset = 0.0
        self.fault_soc_drain_rate = 0.0
        self._temp_history.clear()


class Module:
    """
    Models one battery module (13S8P = 13 series groups of 8 parallel cells).
    Contains 2 NTC measurement points.
    """

    def __init__(self, module_idx: int):
        self.module_idx = module_idx
        self.display_idx = module_idx + 1  # M1–M8

        self.groups: List[ParallelGroup] = [
            ParallelGroup(module_idx, g) for g in range(GROUPS_PER_MODULE)
        ]

        self.voltage = sum(g.voltage for g in self.groups)
        self.temp_ntc1 = AMBIENT_TEMP_DEFAULT
        self.temp_ntc2 = AMBIENT_TEMP_DEFAULT
        self.delta_t_intra = 0.0
        self.max_dt_dt = 0.0

        # Swelling
        self.swelling_force_n = SWELLING_BASELINE_FORCE_N
        self.swelling_percent = 0.0

        # Fault modifiers
        self.fault_ntc1_offset = 0.0
        self.fault_ntc2_offset = 0.0
        self.cooling_active = True

    def update(self, i_pack: float, dt: float, ambient_temp: float,
               coolant_temp: float, sim_time: float):
        """Update all groups in this module."""
        eff_coolant = coolant_temp if self.cooling_active else ambient_temp

        for i, g in enumerate(self.groups):
            neighbors = []
            if i > 0:
                neighbors.append(self.groups[i - 1].temp_surface)
            if i < len(self.groups) - 1:
                neighbors.append(self.groups[i + 1].temp_surface)

            g.update(i_pack, dt, ambient_temp, eff_coolant, neighbors, sim_time)

        self.voltage = sum(g.voltage for g in self.groups)

        (g1a, g1b) = NTC_POSITIONS[0]
        (g2a, g2b) = NTC_POSITIONS[1]
        self.temp_ntc1 = (self.groups[g1a].temp_surface +
                          self.groups[g1b].temp_surface) / 2 + self.fault_ntc1_offset
        self.temp_ntc2 = (self.groups[g2a].temp_surface +
                          self.groups[g2b].temp_surface) / 2 + self.fault_ntc2_offset

        self.delta_t_intra = abs(self.temp_ntc1 - self.temp_ntc2)
        self.max_dt_dt = max(abs(g.dt_dt) for g in self.groups)

        # Swelling model
        max_core = max(g.temp_core for g in self.groups)
        if max_core > SWELLING_ONSET_TEMP:
            delta = max_core - SWELLING_ONSET_TEMP
            self.swelling_percent += SWELLING_RATE_COEFFICIENT * delta * dt
        else:
            self.swelling_percent = max(0, self.swelling_percent - 0.0001 * dt)
        self.swelling_percent = min(self.swelling_percent, 30.0)
        self.swelling_force_n = SWELLING_BASELINE_FORCE_N * (1 + self.swelling_percent / 100)

    def reset_to_defaults(self, ambient_temp: float):
        """Full reset."""
        for g in self.groups:
            g.reset_to_defaults(ambient_temp)
        self.voltage = sum(g.voltage for g in self.groups)
        self.temp_ntc1 = ambient_temp
        self.temp_ntc2 = ambient_temp
        self.delta_t_intra = 0.0
        self.max_dt_dt = 0.0
        self.swelling_force_n = SWELLING_BASELINE_FORCE_N
        self.swelling_percent = 0.0
        self.fault_ntc1_offset = 0.0
        self.fault_ntc2_offset = 0.0
        self.cooling_active = True


class BatteryPack:
    """
    Full battery pack model (104S8P = 8 modules × 13 groups × 8 cells).
    Generates all sensor readings — no alarm evaluation or decision-making.
    """

    def __init__(self):
        self.modules: List[Module] = [Module(m) for m in range(NUM_MODULES)]

        self.voltage = sum(m.voltage for m in self.modules)
        self.current = 0.0
        self.power = 0.0
        self.soc = 0.50
        self.c_rate = 0.0
        self.is_charging = False
        self.target_current = 0.0

        # Environment
        self.ambient_temp = AMBIENT_TEMP_DEFAULT
        self.coolant_inlet_temp = COOLANT_INLET_TEMP
        self.coolant_outlet_temp = COOLANT_INLET_TEMP

        # Gas sensors (2× BME680)
        self.gas_ratio_1 = 1.0
        self.gas_ratio_2 = 1.0
        self.gas_accumulation = 0.0

        # Pressure sensors (2× co-located)
        self.pressure_delta_1 = 0.0
        self.pressure_delta_2 = 0.0

        # Humidity & Isolation
        self.humidity = 50.0
        self.isolation_resistance_mohm = 500.0

        # Simulation time
        self.sim_time = 0.0
        self.step_count = 0

        # Simulation speed (steps per tick)
        self.sim_speed = 1

    def set_operating_mode(self, charging: bool, c_rate: float = 0.5):
        self.is_charging = charging
        self.c_rate = max(0, c_rate)
        if charging:
            self.target_current = -PACK_CAPACITY_AH * self.c_rate
        else:
            self.target_current = PACK_CAPACITY_AH * self.c_rate

    def set_soc(self, soc: float):
        soc = float(np.clip(soc, 0.0, 1.0))
        self.soc = soc
        for m in self.modules:
            for g in m.groups:
                g.soc = soc
                g.voltage = ocv_from_soc(soc)

    def set_ambient_temp(self, temp: float):
        self.ambient_temp = float(np.clip(temp, -20, 60))

    def full_reset(self):
        """Reset everything to normal defaults."""
        self.current = 0.0
        self.target_current = 0.0
        self.is_charging = False
        self.c_rate = 0.0
        self.ambient_temp = AMBIENT_TEMP_DEFAULT
        self.coolant_inlet_temp = COOLANT_INLET_TEMP
        self.coolant_outlet_temp = COOLANT_INLET_TEMP
        self.soc = 0.50
        for m in self.modules:
            m.reset_to_defaults(AMBIENT_TEMP_DEFAULT)
        self.voltage = sum(m.voltage for m in self.modules)
        self.power = 0.0
        self.gas_ratio_1 = 1.0
        self.gas_ratio_2 = 1.0
        self.gas_accumulation = 0.0
        self.pressure_delta_1 = 0.0
        self.pressure_delta_2 = 0.0
        self.humidity = 50.0
        self.isolation_resistance_mohm = 500.0

    def step(self, dt: float = None):
        """Advance simulation by one timestep."""
        if dt is None:
            dt = SIM_DT

        # Ramp current toward target
        current_diff = self.target_current - self.current
        max_ramp = 50.0 * dt
        if abs(current_diff) > max_ramp:
            self.current += np.sign(current_diff) * max_ramp
        else:
            self.current = self.target_current

        for m in self.modules:
            m.update(self.current, dt, self.ambient_temp,
                     self.coolant_inlet_temp, self.sim_time)

        self.voltage = sum(m.voltage for m in self.modules)
        self.power = self.voltage * self.current / 1000.0

        all_socs = [g.soc for m in self.modules for g in m.groups]
        self.soc = float(np.mean(all_socs))

        if abs(self.current) > 0.1:
            self.c_rate = abs(self.current) / PACK_CAPACITY_AH
        else:
            self.c_rate = 0.0

        # Coolant outlet
        total_heat = sum(
            abs(self.current / CELLS_PER_GROUP) ** 2 *
            (g.rint_mohm / 1000.0) * CELLS_PER_GROUP + g.fault_heat_w
            for m in self.modules for g in m.groups
        )
        if COOLANT_FLOW_RATE > 0:
            self.coolant_outlet_temp = self.coolant_inlet_temp + \
                total_heat / (COOLANT_FLOW_RATE * COOLANT_SPECIFIC_HEAT)
        else:
            self.coolant_outlet_temp = self.coolant_inlet_temp

        self._update_gas_model(dt)
        self._update_pressure_model(dt)

        self.sim_time += dt
        self.step_count += 1

    def step_multiple(self, n: int, fault_engine=None):
        """Run n simulation steps at once (for time jump / speed control).
        Applies fault effects between each step for correct physics.
        """
        for _ in range(n):
            self.step()
            if fault_engine:
                fault_engine.apply_faults(SIM_DT)

    def _update_gas_model(self, dt: float):
        """Gas sensor model — BME680 VOC resistance ratio."""
        any_hot = False
        for mi, m in enumerate(self.modules):
            for g in m.groups:
                if g.temp_core > GAS_ONSET_TEMP:
                    any_hot = True
                    delta_t = g.temp_core - GAS_ONSET_TEMP
                    gas_drop = GAS_RATE_COEFFICIENT * delta_t * dt
                    self.gas_accumulation += gas_drop * 0.1

                    if mi < 4:
                        self.gas_ratio_1 = max(0.1, self.gas_ratio_1 - gas_drop)
                        self.gas_ratio_2 = max(0.1, self.gas_ratio_2 - gas_drop * 0.2)
                    else:
                        self.gas_ratio_2 = max(0.1, self.gas_ratio_2 - gas_drop)
                        self.gas_ratio_1 = max(0.1, self.gas_ratio_1 - gas_drop * 0.2)

        if not any_hot:
            self.gas_ratio_1 = min(1.0, self.gas_ratio_1 + 0.0005 * dt)
            self.gas_ratio_2 = min(1.0, self.gas_ratio_2 + 0.0005 * dt)
            self.gas_accumulation = max(0, self.gas_accumulation - 0.00005 * dt)

    def _update_pressure_model(self, dt: float):
        """Pressure model — ΔP from atmospheric baseline."""
        avg_temp_k = 273.15 + np.mean([g.temp_surface for m in self.modules for g in m.groups])
        base_temp_k = 273.15 + AMBIENT_TEMP_DEFAULT

        temp_dp = (avg_temp_k / base_temp_k - 1.0) * PRESSURE_BASELINE_HPA * 0.001
        gas_dp = self.gas_accumulation * 5.0

        total_dp = temp_dp + gas_dp

        if self.gas_ratio_1 < self.gas_ratio_2:
            self.pressure_delta_1 = total_dp * 1.1
            self.pressure_delta_2 = total_dp * 0.9
        elif self.gas_ratio_2 < self.gas_ratio_1:
            self.pressure_delta_1 = total_dp * 0.9
            self.pressure_delta_2 = total_dp * 1.1
        else:
            self.pressure_delta_1 = total_dp
            self.pressure_delta_2 = total_dp

        self.pressure_delta_1 = float(np.clip(self.pressure_delta_1, -0.5, 20.0))
        self.pressure_delta_2 = float(np.clip(self.pressure_delta_2, -0.5, 20.0))

    def get_thermal_risk(self) -> Dict:
        """
        Calculate thermal runaway risk based on real cascade chemistry.
        Returns current stage, max temp, dT/dt, and estimated time to
        each cascade stage.
        """
        # Find hottest cell (core temp)
        max_core = AMBIENT_TEMP_DEFAULT
        max_dt_dt = 0.0
        hottest_module = 1
        hottest_group = 1

        for m in self.modules:
            for g in m.groups:
                if g.temp_core > max_core:
                    max_core = g.temp_core
                    hottest_module = g.display_module
                    hottest_group = g.display_group
                if abs(g.dt_dt) > max_dt_dt:
                    max_dt_dt = abs(g.dt_dt)

        # Current cascade stage
        stage = get_cascade_stage(max_core)

        # Estimate time to each stage (minutes) based on current dT/dt
        eta_stages = {}
        stage_names = list(THERMAL_CASCADE.keys())
        for i, (key, info) in enumerate(THERMAL_CASCADE.items()):
            threshold = info['temp_max']
            if max_core >= threshold:
                eta_stages[key] = 0  # Already past this stage
            elif max_dt_dt > 0.01:  # At least 0.01 °C/min of rising
                eta_minutes = (threshold - max_core) / max_dt_dt
                eta_stages[key] = round(eta_minutes, 1)
            else:
                eta_stages[key] = -1  # Not trending — infinite time

        # Overall risk factor (0.0 = safe, 1.0 = runaway)
        risk_factor = 0.0
        if max_core > 60:
            risk_factor = min(1.0, (max_core - 60) / 240.0)  # Linear from 60 to 300°C
        if max_dt_dt > 0.1:
            risk_factor = min(1.0, risk_factor + max_dt_dt * 0.1)

        return {
            'max_core_temp': round(max_core, 2),
            'max_dt_dt': round(max_dt_dt, 3),
            'hottest': f'M{hottest_module}:G{hottest_group}',
            'stage': stage,
            'eta_stages': eta_stages,
            'risk_factor': round(risk_factor, 3),
        }

    def get_snapshot(self) -> Dict:
        """Get complete pack state snapshot for dashboard/serial."""
        all_group_voltages = []
        all_group_temps = []
        module_data = []

        for m in self.modules:
            mdata = {
                'module': m.display_idx,
                'voltage': round(m.voltage, 3),
                'temp_ntc1': round(m.temp_ntc1, 2),
                'temp_ntc2': round(m.temp_ntc2, 2),
                'ntc1_placement': f'Between G{NTC_POSITIONS[0][0]+1}–G{NTC_POSITIONS[0][1]+1}',
                'ntc2_placement': f'Between G{NTC_POSITIONS[1][0]+1}–G{NTC_POSITIONS[1][1]+1}',
                'delta_t_intra': round(m.delta_t_intra, 2),
                'max_dt_dt': round(m.max_dt_dt, 3),
                'swelling_pct': round(m.swelling_percent, 2),
                'swelling_force_n': round(m.swelling_force_n, 1),
                'groups': [],
            }
            for g in m.groups:
                gdata = {
                    'group': g.display_group,
                    'voltage': round(g.voltage, 4),
                    'temp': round(g.temp_surface, 2),
                    'temp_core': round(g.temp_core, 2),
                    'dt_dt': round(g.dt_dt, 3),
                    'soc': round(g.soc, 4),
                    'rint_group': round(g.rint_mohm / CELLS_PER_GROUP, 4),
                    'rint_cell': round(g.rint_mohm, 4),
                }
                mdata['groups'].append(gdata)
                all_group_voltages.append(g.voltage)
                all_group_temps.append(g.temp_surface)

            module_data.append(mdata)

        v_spread_mv = (max(all_group_voltages) - min(all_group_voltages)) * 1000
        temp_spread = max(all_group_temps) - min(all_group_temps)

        # Thermal risk assessment
        thermal_risk = self.get_thermal_risk()

        return {
            'sim_time': round(self.sim_time, 2),
            'step_count': self.step_count,
            'sim_speed': self.sim_speed,

            'total_channels': TOTAL_SENSOR_CHANNELS,
            'sampling_rate_hz': SAMPLING_RATE_HZ,

            'pack_voltage': round(self.voltage, 2),
            'pack_current': round(self.current, 2),
            'pack_power': round(self.power, 2),
            'pack_soc': round(self.soc, 4),
            'c_rate': round(self.c_rate, 3),
            'is_charging': self.is_charging,

            'ambient_temp': round(self.ambient_temp, 2),
            'coolant_inlet': round(self.coolant_inlet_temp, 2),
            'coolant_outlet': round(self.coolant_outlet_temp, 2),
            'coolant_delta_t': round(self.coolant_outlet_temp - self.coolant_inlet_temp, 2),

            'gas_ratio_1': round(self.gas_ratio_1, 4),
            'gas_ratio_2': round(self.gas_ratio_2, 4),
            'pressure_delta_1': round(self.pressure_delta_1, 2),
            'pressure_delta_2': round(self.pressure_delta_2, 2),
            'humidity': round(self.humidity, 1),
            'isolation_mohm': round(self.isolation_resistance_mohm, 1),

            'v_spread_mv': round(v_spread_mv, 2),
            'temp_spread': round(temp_spread, 2),
            'max_temp': round(max(all_group_temps), 2),
            'min_temp': round(min(all_group_temps), 2),
            'max_dt_dt': round(max(m.max_dt_dt for m in self.modules), 3),

            # Thermal runaway risk
            'thermal_risk': thermal_risk,

            'modules': module_data,
        }
