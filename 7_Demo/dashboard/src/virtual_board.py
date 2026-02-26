"""
Virtual VSDSquadron processing pipeline for dashboard-only development.

This module consumes full digital-twin snapshots (139 channels), runs
board-like anomaly correlation logic, and emits telemetry compatible with
the output dashboard contract.

Architecture emulated:
  Digital Twin sensors -> USB input (virtual) -> VSDSquadron algorithm ->
  USB output telemetry -> Output dashboard
"""

from __future__ import annotations

from dataclasses import dataclass
from statistics import mean, median
from typing import Any


STATE_TO_NUM = {"NORMAL": 0, "WARNING": 1, "CRITICAL": 2, "EMERGENCY": 3}

CASCADE_STAGE_META = {
    "NORMAL": {
        "label": "Normal",
        "color": "#22c55e",
        "desc": "All parameters within spec",
    },
    "ELEVATED": {
        "label": "Elevated",
        "color": "#eab308",
        "desc": "Approaching thermal limits; monitor closely",
    },
    "SEI_DECOMPOSITION": {
        "label": "SEI Decomposition",
        "color": "#f59e0b",
        "desc": "Early exothermic chemistry onset",
    },
    "SEPARATOR_COLLAPSE": {
        "label": "Separator Collapse",
        "color": "#f97316",
        "desc": "Separator stability risk increasing",
    },
    "ELECTROLYTE_DECOMP": {
        "label": "Electrolyte Decomposition",
        "color": "#ef4444",
        "desc": "Electrolyte decomposition and vent risk",
    },
    "CATHODE_DECOMP": {
        "label": "Cathode Decomposition",
        "color": "#dc2626",
        "desc": "Oxygen release region; severe hazard",
    },
    "FULL_RUNAWAY": {
        "label": "Runaway",
        "color": "#991b1b",
        "desc": "Self-accelerating runaway behavior",
    },
}

CASCADE_ORDER = [
    "NORMAL",
    "ELEVATED",
    "SEI_DECOMPOSITION",
    "SEPARATOR_COLLAPSE",
    "ELECTROLYTE_DECOMP",
    "CATHODE_DECOMP",
    "FULL_RUNAWAY",
]


def _to_float(value: Any, default: float = 0.0) -> float:
    try:
        v = float(value)
    except (TypeError, ValueError):
        return default
    if v != v:  # NaN
        return default
    return v


def _to_int(value: Any, default: int = 0) -> int:
    try:
        return int(value)
    except (TypeError, ValueError):
        return default


def _normalize_stage_key(value: Any) -> str:
    if value is None:
        return "NORMAL"
    raw = str(value).strip().upper().replace(" ", "_")
    if raw in CASCADE_STAGE_META:
        return raw
    if "RUNAWAY" in raw:
        return "FULL_RUNAWAY"
    if "CATHODE" in raw:
        return "CATHODE_DECOMP"
    if "ELECTROLYTE" in raw:
        return "ELECTROLYTE_DECOMP"
    if "SEPARATOR" in raw:
        return "SEPARATOR_COLLAPSE"
    if "SEI" in raw:
        return "SEI_DECOMPOSITION"
    if "ELEV" in raw:
        return "ELEVATED"
    return "NORMAL"


@dataclass
class Thresholds:
    voltage_low_v: float = 260.0
    voltage_high_v: float = 380.0
    group_v_deviation_mv: float = 15.0
    v_spread_warn_mv: float = 50.0
    current_warning_a: float = 180.0
    current_short_a: float = 350.0
    current_emergency_a: float = 500.0
    temp_warning_c: float = 55.0
    temp_emergency_c: float = 80.0
    dt_dt_warning_c_per_min: float = 0.5
    dt_dt_emergency_c_per_min: float = 5.0
    inter_module_dt_warn_c: float = 5.0
    intra_module_dt_warn_c: float = 3.0
    ambient_delta_warn_c: float = 20.0
    gas_warning_ratio: float = 0.70
    gas_critical_ratio: float = 0.40
    pressure_warning_hpa: float = 2.0
    pressure_critical_hpa: float = 5.0
    swelling_warning_pct: float = 3.0
    isolation_warn_mohm: float = 200.0
    isolation_critical_mohm: float = 100.0


class VirtualVsdsquadron:
    """Board-like multi-modal processor operating on digital twin snapshots."""

    def __init__(self, thresholds: Thresholds | None = None):
        self.th = thresholds or Thresholds()
        self.current_state = "NORMAL"
        self.emergency_latched = False
        self.emergency_recovery_counter = 0
        self.emergency_recovery_limit = 10  # 10 x 500ms = 5s stable nominal to release latch
        self.critical_countdown = 0
        self.critical_countdown_limit = 20   # 20 x 500ms = 10s
        self.deescalation_counter = 0
        self.deescalation_limit = 10         # 10 x 500ms = 5s
        self.last_eval_ms: int | None = None
        self.prev_timestamp_ms: int | None = None
        self.prev_pack_voltage: float | None = None

    def reset(self) -> None:
        self.current_state = "NORMAL"
        self.emergency_latched = False
        self.emergency_recovery_counter = 0
        self.critical_countdown = 0
        self.deescalation_counter = 0
        self.last_eval_ms = None
        self.prev_timestamp_ms = None
        self.prev_pack_voltage = None

    def process_snapshot(self, snapshot: dict[str, Any]) -> dict[str, Any]:
        timestamp_ms = max(0, _to_int(round(_to_float(snapshot.get("sim_time")) * 1000.0)))
        if self.prev_timestamp_ms is not None and timestamp_ms < self.prev_timestamp_ms:
            # Digital twin reset/restart; reset latched correlation state.
            self.reset()

        raw = self._build_raw_data(snapshot)
        anomaly = self._evaluate(raw, snapshot, timestamp_ms)

        should_eval = (
            self.last_eval_ms is None
            or (timestamp_ms - self.last_eval_ms) >= 500
            or anomaly["short_circuit"]
            or anomaly["emergency_direct"]
            or anomaly["active_count"] >= 3
        )
        if should_eval:
            self._update_state_machine(
                anomaly["active_count"],
                anomaly["short_circuit"],
                anomaly["emergency_direct"],
            )
            self.last_eval_ms = timestamp_ms

        state = self.current_state
        state_num = STATE_TO_NUM[state]
        prediction = self._build_prediction(snapshot, raw, anomaly, state)

        telemetry = {
            "timestamp_ms": timestamp_ms,
            "mode": "virtual-board",
            "scenario": "Twin -> Virtual VSDSquadron -> Dashboard",
            "voltage_v": round(raw["pack_voltage"], 3),
            "current_a": round(raw["pack_current"], 3),
            "temp_ambient": round(raw["ambient_temp"], 3),
            "gas_ratio_1": round(raw["gas_ratio_1"], 4),
            "gas_ratio_2": round(raw["gas_ratio_2"], 4),
            "pressure_delta_1": round(raw["pressure_delta_1"], 4),
            "pressure_delta_2": round(raw["pressure_delta_2"], 4),
            "dt_dt_max": round(raw["max_dt_dt"], 4),
            "system_state": state,
            "state_num": state_num,
            "anomaly_count": anomaly["active_count"],
            "categories": anomaly["categories"],
            "hotspot_module": raw["hottest"]["module"],
            "risk_pct": int(round(prediction["risk_factor"] * 100)),
            "cascade_stage": prediction["stage"]["key"],
            "emergency_direct": anomaly["emergency_direct"],
            "raw_data": raw,
            "intelligent_detection": {
                "system_state": state,
                "state_num": state_num,
                "anomaly_count": anomaly["active_count"],
                "categories": anomaly["categories"],
                "emergency_direct": anomaly["emergency_direct"],
                "short_circuit": anomaly["short_circuit"],
                "hotspot_module": raw["hottest"]["module"],
                "hotspot_group": raw["hottest"]["group"],
                "hotspot_temp_c": round(raw["hottest"]["core_temp_c"], 3),
                "hotspot_label": f"M{raw['hottest']['module']}:G{raw['hottest']['group']}",
                "anomaly_modules": anomaly["anomaly_modules"],
                "anomaly_modules_mask": anomaly["anomaly_modules_mask"],
                "risk_factor": round(prediction["risk_factor"], 4),
                "cascade_stage": prediction["stage"]["key"],
            },
            "thermal_runaway_prediction": prediction,
            # Backward-compatible aliases consumed by older UI logic
            "twin_raw": raw,
            "prediction": prediction,
        }

        self.prev_timestamp_ms = timestamp_ms
        self.prev_pack_voltage = raw["pack_voltage"]
        return telemetry

    def _build_raw_data(self, snapshot: dict[str, Any]) -> dict[str, Any]:
        modules_in = snapshot.get("modules", [])
        if not isinstance(modules_in, list):
            modules_in = []

        module_rows: list[dict[str, Any]] = []
        group_rows: list[dict[str, Any]] = []
        all_group_voltages: list[float] = []
        all_group_surface_temps: list[float] = []
        all_group_core_temps: list[float] = []
        all_group_dt_dt: list[float] = []
        all_group_rint: list[float] = []

        for module_idx, module in enumerate(modules_in):
            if not isinstance(module, dict):
                continue
            module_id = _to_int(module.get("module"), module_idx + 1)
            groups = module.get("groups", [])
            if not isinstance(groups, list):
                groups = []

            module_group_voltages: list[float] = []
            module_group_core_temps: list[float] = []
            module_group_dt: list[float] = []

            for group in groups:
                if not isinstance(group, dict):
                    continue
                group_id = _to_int(group.get("group"), len(module_group_voltages) + 1)
                voltage = _to_float(group.get("voltage"), 0.0)
                surface_t = _to_float(group.get("temp"), 25.0)
                core_t = _to_float(group.get("temp_core"), surface_t)
                dt_dt = abs(_to_float(group.get("dt_dt"), 0.0))
                rint_group = _to_float(group.get("rint_group"), 0.0)

                module_group_voltages.append(voltage)
                module_group_core_temps.append(core_t)
                module_group_dt.append(dt_dt)

                all_group_voltages.append(voltage)
                all_group_surface_temps.append(surface_t)
                all_group_core_temps.append(core_t)
                all_group_dt_dt.append(dt_dt)
                all_group_rint.append(rint_group)

                group_rows.append(
                    {
                        "module": module_id,
                        "group": group_id,
                        "voltage_v": voltage,
                        "temp_c": surface_t,
                        "core_temp_c": core_t,
                        "dt_dt_c_min": dt_dt,
                        "rint_group_mohm": rint_group,
                    }
                )

            module_rows.append(
                {
                    "module": module_id,
                    "voltage_v": _to_float(module.get("voltage"), 0.0),
                    "ntc1_c": _to_float(module.get("temp_ntc1"), 25.0),
                    "ntc2_c": _to_float(module.get("temp_ntc2"), 25.0),
                    "delta_t_intra_c": _to_float(module.get("delta_t_intra"), 0.0),
                    "max_dt_dt_c_min": abs(_to_float(module.get("max_dt_dt"), 0.0)),
                    "swelling_pct": _to_float(module.get("swelling_pct"), 0.0),
                    "v_spread_mv": _to_float(module.get("groups") and max(module_group_voltages, default=0) - min(module_group_voltages, default=0), 0.0) * 1000.0 if module_group_voltages else 0.0,
                    "avg_group_temp_c": mean(module_group_core_temps) if module_group_core_temps else 25.0,
                    "max_group_temp_c": max(module_group_core_temps) if module_group_core_temps else 25.0,
                    "max_group_dt_dt_c_min": max(module_group_dt) if module_group_dt else 0.0,
                    "groups": len(module_group_core_temps),
                }
            )

        if not all_group_core_temps:
            all_group_core_temps = [25.0]
        if not all_group_surface_temps:
            all_group_surface_temps = [25.0]
        if not all_group_voltages:
            all_group_voltages = [3.2]
        if not all_group_dt_dt:
            all_group_dt_dt = [0.0]
        if not all_group_rint:
            all_group_rint = [0.4]

        hottest = max(group_rows, key=lambda g: g["core_temp_c"], default=None)
        if hottest is None:
            hottest = {
                "module": 1,
                "group": 1,
                "temp_c": 25.0,
                "core_temp_c": 25.0,
                "dt_dt_c_min": 0.0,
                "rint_group_mohm": 0.4,
            }

        median_v = median(all_group_voltages)
        median_t = median(all_group_core_temps)
        median_r = median(all_group_rint)

        voltage_outliers = sorted(
            [
                {
                    "module": g["module"],
                    "group": g["group"],
                    "value_v": round(g["voltage_v"], 5),
                    "delta_mv": round((g["voltage_v"] - median_v) * 1000.0, 3),
                    "abs_delta_mv": round(abs((g["voltage_v"] - median_v) * 1000.0), 3),
                }
                for g in group_rows
            ],
            key=lambda x: x["abs_delta_mv"],
            reverse=True,
        )[:5]

        temperature_outliers = sorted(
            [
                {
                    "module": g["module"],
                    "group": g["group"],
                    "value_c": round(g["core_temp_c"], 3),
                    "delta_c": round(g["core_temp_c"] - median_t, 3),
                    "abs_delta_c": round(abs(g["core_temp_c"] - median_t), 3),
                }
                for g in group_rows
            ],
            key=lambda x: x["abs_delta_c"],
            reverse=True,
        )[:5]

        rint_outliers = sorted(
            [
                {
                    "module": g["module"],
                    "group": g["group"],
                    "value_mohm": round(g["rint_group_mohm"], 5),
                    "delta_pct": round(
                        ((g["rint_group_mohm"] - median_r) / median_r) * 100.0 if median_r else 0.0,
                        3,
                    ),
                    "abs_delta_pct": round(
                        abs(((g["rint_group_mohm"] - median_r) / median_r) * 100.0) if median_r else 0.0,
                        3,
                    ),
                }
                for g in group_rows
            ],
            key=lambda x: x["abs_delta_pct"],
            reverse=True,
        )[:5]

        module_scores = []
        for m in module_rows:
            temp_component = max(0.0, (m["max_group_temp_c"] - 45.0) / 35.0)
            dt_component = max(0.0, m["max_group_dt_dt_c_min"] / 2.0)
            v_component = max(0.0, m["v_spread_mv"] / 80.0)
            swell_component = max(0.0, m["swelling_pct"] / 10.0)
            score = min(1.0, 0.35 * temp_component + 0.25 * dt_component + 0.20 * v_component + 0.20 * swell_component)
            module_scores.append(
                {
                    "module": m["module"],
                    "score": round(score, 4),
                    "max_temp_c": round(m["max_group_temp_c"], 3),
                    "v_spread_mv": round(m["v_spread_mv"], 3),
                    "max_dt_dt_c_min": round(m["max_group_dt_dt_c_min"], 4),
                    "swelling_pct": round(m["swelling_pct"], 3),
                }
            )
        module_scores.sort(key=lambda x: x["score"], reverse=True)

        gas1 = _to_float(snapshot.get("gas_ratio_1"), 1.0)
        gas2 = _to_float(snapshot.get("gas_ratio_2"), 1.0)
        pressure1 = _to_float(snapshot.get("pressure_delta_1"), 0.0)
        pressure2 = _to_float(snapshot.get("pressure_delta_2"), 0.0)
        max_swelling = max([_to_float(m.get("swelling_pct"), 0.0) for m in module_rows], default=0.0)
        avg_temp = mean(all_group_core_temps)
        min_temp = min(all_group_core_temps)
        max_temp = max(all_group_core_temps)
        max_dt_dt = max(all_group_dt_dt)

        return {
            "total_channels": _to_int(snapshot.get("total_channels"), 139),
            "sampling_rate_hz": _to_float(snapshot.get("sampling_rate_hz"), 10.0),
            "sim_time_s": _to_float(snapshot.get("sim_time"), 0.0),
            "pack_voltage": _to_float(snapshot.get("pack_voltage"), 0.0),
            "pack_current": _to_float(snapshot.get("pack_current"), 0.0),
            "pack_power_kw": _to_float(snapshot.get("pack_power"), 0.0),
            "pack_soc": _to_float(snapshot.get("pack_soc"), 0.0),
            "c_rate": _to_float(snapshot.get("c_rate"), 0.0),
            "ambient_temp": _to_float(snapshot.get("ambient_temp"), 25.0),
            "coolant_inlet": _to_float(snapshot.get("coolant_inlet"), 25.0),
            "coolant_outlet": _to_float(snapshot.get("coolant_outlet"), 25.0),
            "coolant_delta_t": _to_float(snapshot.get("coolant_delta_t"), 0.0),
            "humidity": _to_float(snapshot.get("humidity"), 50.0),
            "isolation_mohm": _to_float(snapshot.get("isolation_mohm"), 500.0),
            "gas_ratio_1": gas1,
            "gas_ratio_2": gas2,
            "gas_ratio_min": min(gas1, gas2),
            "pressure_delta_1": pressure1,
            "pressure_delta_2": pressure2,
            "pressure_delta_max": max(pressure1, pressure2),
            "v_spread_mv": _to_float(snapshot.get("v_spread_mv"), (max(all_group_voltages) - min(all_group_voltages)) * 1000.0),
            "temp_spread_c": _to_float(snapshot.get("temp_spread"), max_temp - min_temp),
            "max_temp_c": max_temp,
            "min_temp_c": min_temp,
            "avg_temp_c": avg_temp,
            "median_temp_c": median_t,
            "max_dt_dt": max_dt_dt,
            "max_swelling_pct": max_swelling,
            "hottest": hottest,
            "modules": module_rows,
            "temperature_profile": {
                "max_temp_c": round(max_temp, 3),
                "avg_temp_c": round(avg_temp, 3),
                "min_temp_c": round(min_temp, 3),
                "hotspot_temp_c": round(_to_float(hottest["core_temp_c"], max_temp), 3),
                "ambient_temp_c": round(_to_float(snapshot.get("ambient_temp"), 25.0), 3),
            },
            "deviation": {
                "voltage_spread_mv": round(_to_float(snapshot.get("v_spread_mv"), (max(all_group_voltages) - min(all_group_voltages)) * 1000.0), 3),
                "temp_spread_c": round(_to_float(snapshot.get("temp_spread"), max_temp - min_temp), 3),
                "hotspot_delta_c": round(max_temp - avg_temp, 3),
                "gas_drop_pct": round(max(0.0, (1.0 - min(gas1, gas2)) * 100.0), 3),
                "pressure_peak_hpa": round(max(pressure1, pressure2), 3),
                "max_swelling_pct": round(max_swelling, 3),
                "max_dt_dt_c_min": round(max_dt_dt, 4),
                "isolation_margin_mohm": round(_to_float(snapshot.get("isolation_mohm"), 500.0) - self.th.isolation_warn_mohm, 3),
            },
            "outliers": {
                "voltage": voltage_outliers,
                "temperature": temperature_outliers,
                "rint": rint_outliers,
                "module_scores": module_scores[:5],
            },
        }

    def _evaluate(
        self,
        raw: dict[str, Any],
        snapshot: dict[str, Any],
        timestamp_ms: int,
    ) -> dict[str, Any]:
        categories: set[str] = set()
        anomaly_modules: set[int] = set()
        emergency_direct = False
        short_circuit = False

        pack_voltage = _to_float(raw["pack_voltage"])
        pack_current = _to_float(raw["pack_current"])
        abs_current = abs(pack_current)

        # Electrical anomalies
        if pack_voltage < self.th.voltage_low_v or pack_voltage > self.th.voltage_high_v:
            categories.add("electrical")
        if abs_current > self.th.current_warning_a:
            categories.add("electrical")
        if _to_float(raw["v_spread_mv"]) > self.th.v_spread_warn_mv:
            categories.add("electrical")
        if _to_float(raw["isolation_mohm"]) < self.th.isolation_warn_mohm:
            categories.add("electrical")

        for outlier in raw["outliers"]["voltage"]:
            if _to_float(outlier.get("abs_delta_mv")) > self.th.group_v_deviation_mv:
                categories.add("electrical")
                anomaly_modules.add(_to_int(outlier.get("module"), 0))

        # Thermal anomalies
        max_temp = _to_float(raw["max_temp_c"])
        max_dt_dt = _to_float(raw["max_dt_dt"])
        if max_temp > self.th.temp_warning_c:
            categories.add("thermal")
        if max_dt_dt > self.th.dt_dt_warning_c_per_min:
            categories.add("thermal")
        if _to_float(raw["temp_spread_c"]) > self.th.inter_module_dt_warn_c:
            categories.add("thermal")
        if _to_float(raw["deviation"]["hotspot_delta_c"]) > self.th.ambient_delta_warn_c:
            categories.add("thermal")

        for module in raw["modules"]:
            if _to_float(module.get("delta_t_intra_c")) > self.th.intra_module_dt_warn_c:
                categories.add("thermal")
                anomaly_modules.add(_to_int(module.get("module"), 0))
            if _to_float(module.get("max_dt_dt_c_min")) > self.th.dt_dt_warning_c_per_min:
                categories.add("thermal")
                anomaly_modules.add(_to_int(module.get("module"), 0))
            if _to_float(module.get("swelling_pct")) > self.th.swelling_warning_pct:
                categories.add("swelling")
                anomaly_modules.add(_to_int(module.get("module"), 0))

        # Gas + pressure + swelling
        gas_min = _to_float(raw["gas_ratio_min"], 1.0)
        pressure_max = _to_float(raw["pressure_delta_max"])
        if gas_min < self.th.gas_warning_ratio:
            categories.add("gas")
        if pressure_max > self.th.pressure_warning_hpa:
            categories.add("pressure")
        if _to_float(raw["max_swelling_pct"]) > self.th.swelling_warning_pct:
            categories.add("swelling")

        # Emergency direct thresholds
        if (
            max_temp > self.th.temp_emergency_c
            or max_dt_dt > self.th.dt_dt_emergency_c_per_min
            or abs_current > self.th.current_emergency_a
            or _to_float(raw["isolation_mohm"]) < self.th.isolation_critical_mohm
        ):
            emergency_direct = True

        # Short-circuit detection
        if abs_current > self.th.current_short_a:
            short_circuit = True
        if self.prev_timestamp_ms is not None and self.prev_pack_voltage is not None:
            dt_s = max(1e-3, (timestamp_ms - self.prev_timestamp_ms) / 1000.0)
            dv_dt = (pack_voltage - self.prev_pack_voltage) / dt_s
            if abs_current > (0.8 * self.th.current_short_a) and dv_dt < -15.0:
                short_circuit = True

        # Track modules from strongest thermal outliers as well.
        for outlier in raw["outliers"]["temperature"][:3]:
            if _to_float(outlier.get("abs_delta_c")) > 5.0:
                anomaly_modules.add(_to_int(outlier.get("module"), 0))

        modules_sorted = sorted([m for m in anomaly_modules if m > 0])
        modules_mask = 0
        for m in modules_sorted:
            if 1 <= m <= 8:
                modules_mask |= (1 << (m - 1))

        return {
            "categories": sorted(categories),
            "active_count": len(categories),
            "anomaly_modules": modules_sorted,
            "anomaly_modules_mask": modules_mask,
            "short_circuit": short_circuit,
            "emergency_direct": emergency_direct,
        }

    def _update_state_machine(
        self,
        active_count: int,
        short_circuit: bool,
        emergency_direct: bool,
    ) -> None:
        if self.emergency_latched:
            if short_circuit or emergency_direct or active_count > 0:
                self.current_state = "EMERGENCY"
                self.emergency_recovery_counter = 0
                return

            self.emergency_recovery_counter += 1
            if self.emergency_recovery_counter >= self.emergency_recovery_limit:
                self.emergency_latched = False
                self.emergency_recovery_counter = 0
                self.current_state = "NORMAL"
            else:
                self.current_state = "EMERGENCY"
            return

        if short_circuit or emergency_direct or active_count >= 3:
            self.current_state = "EMERGENCY"
            self.emergency_latched = True
            self.emergency_recovery_counter = 0
            return

        if active_count >= 2:
            if self.current_state != "CRITICAL":
                self.current_state = "CRITICAL"
                self.critical_countdown = 0
            self.critical_countdown += 1
            self.deescalation_counter = 0
            if self.critical_countdown >= self.critical_countdown_limit:
                self.current_state = "EMERGENCY"
                self.emergency_latched = True
            return

        if active_count == 1:
            self.current_state = "WARNING"
            self.critical_countdown = 0
            self.deescalation_counter = 0
            return

        # active_count == 0
        if self.current_state != "NORMAL":
            self.deescalation_counter += 1
            if self.deescalation_counter >= self.deescalation_limit:
                self.current_state = "NORMAL"
                self.deescalation_counter = 0
        self.critical_countdown = 0

    def _build_prediction(
        self,
        snapshot: dict[str, Any],
        raw: dict[str, Any],
        anomaly: dict[str, Any],
        state: str,
    ) -> dict[str, Any]:
        twin_risk = snapshot.get("thermal_risk")
        if not isinstance(twin_risk, dict):
            twin_risk = {}

        twin_stage = twin_risk.get("stage")
        twin_stage_key = _normalize_stage_key(
            twin_stage.get("key") if isinstance(twin_stage, dict) else twin_stage
        )

        stage_key_from_temp = self._stage_from_core_temp(_to_float(raw["hottest"]["core_temp_c"]))
        stage_key = max(
            [twin_stage_key, stage_key_from_temp, self._stage_from_state(state)],
            key=lambda k: CASCADE_ORDER.index(k),
        )
        stage_meta = CASCADE_STAGE_META[stage_key]

        gas_min = _to_float(raw["gas_ratio_min"], 1.0)
        pressure_peak = _to_float(raw["pressure_delta_max"], 0.0)
        max_temp = _to_float(raw["hottest"]["core_temp_c"], _to_float(raw["max_temp_c"], 25.0))
        max_dt_dt = _to_float(raw["max_dt_dt"], 0.0)
        swelling = _to_float(raw["max_swelling_pct"], 0.0)

        risk = 0.0
        risk += max(0.0, min(1.0, (max_temp - 45.0) / 55.0)) * 0.35
        risk += max(0.0, min(1.0, max_dt_dt / 5.0)) * 0.25
        risk += max(0.0, min(1.0, (0.85 - gas_min) / 0.45)) * 0.20
        risk += max(0.0, min(1.0, pressure_peak / 8.0)) * 0.10
        risk += max(0.0, min(1.0, swelling / 10.0)) * 0.10

        twin_risk_factor = _to_float(twin_risk.get("risk_factor"), 0.0)
        risk = max(risk, twin_risk_factor)
        if state == "WARNING":
            risk = max(risk, 0.30)
        elif state == "CRITICAL":
            risk = max(risk, 0.62)
        elif state == "EMERGENCY":
            risk = max(risk, 0.92)
        if anomaly["short_circuit"]:
            risk = 1.0
        risk = max(0.0, min(1.0, risk))

        eta_stages = twin_risk.get("eta_stages")
        if not isinstance(eta_stages, dict):
            eta_stages = self._estimate_eta_minutes(max_temp, max_dt_dt)
        else:
            eta_stages = {
                _normalize_stage_key(k): _to_float(v, -1.0)
                for k, v in eta_stages.items()
            }
            for key in CASCADE_ORDER:
                eta_stages.setdefault(key, -1.0)

        desc = stage_meta["desc"]
        if anomaly["categories"]:
            desc = f"{state}: " + ", ".join(c.upper() for c in anomaly["categories"])

        return {
            "stage": {
                "key": stage_key,
                "label": stage_meta["label"],
                "color": stage_meta["color"],
                "desc": desc,
            },
            "risk_factor": round(risk, 4),
            "hottest": f"M{_to_int(raw['hottest']['module'], 1)}:G{_to_int(raw['hottest']['group'], 1)}",
            "max_core_temp": round(max_temp, 3),
            "max_dt_dt": round(max_dt_dt, 4),
            # ETA values are in minutes to match digital_twin output semantics.
            "eta_stages": eta_stages,
            "physics_source": "digital_twin_thermoelectrochemical_model",
            "chemistry_basis": "SEI->Separator->Electrolyte->Cathode cascade",
        }

    def _estimate_eta_minutes(self, core_temp_c: float, dt_dt_c_per_min: float) -> dict[str, float]:
        thresholds = {
            "NORMAL": 60.0,
            "ELEVATED": 80.0,
            "SEI_DECOMPOSITION": 120.0,
            "SEPARATOR_COLLAPSE": 150.0,
            "ELECTROLYTE_DECOMP": 200.0,
            "CATHODE_DECOMP": 300.0,
            "FULL_RUNAWAY": 350.0,
        }
        eta = {}
        for stage, temp_limit in thresholds.items():
            if core_temp_c >= temp_limit:
                eta[stage] = 0.0
            elif dt_dt_c_per_min > 0.01:
                eta[stage] = round((temp_limit - core_temp_c) / dt_dt_c_per_min, 2)
            else:
                eta[stage] = -1.0
        return eta

    def _stage_from_core_temp(self, core_temp_c: float) -> str:
        if core_temp_c <= 60.0:
            return "NORMAL"
        if core_temp_c <= 80.0:
            return "ELEVATED"
        if core_temp_c <= 120.0:
            return "SEI_DECOMPOSITION"
        if core_temp_c <= 150.0:
            return "SEPARATOR_COLLAPSE"
        if core_temp_c <= 200.0:
            return "ELECTROLYTE_DECOMP"
        if core_temp_c <= 300.0:
            return "CATHODE_DECOMP"
        return "FULL_RUNAWAY"

    def _stage_from_state(self, state: str) -> str:
        state = state.upper()
        if state == "WARNING":
            return "ELEVATED"
        if state == "CRITICAL":
            return "SEI_DECOMPOSITION"
        if state == "EMERGENCY":
            return "FULL_RUNAWAY"
        return "NORMAL"
