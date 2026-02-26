/*
 * anomaly_eval.c — Anomaly Category Evaluator (Full Pack Edition)
 *
 * Evaluates all 139 sensor channels from the 104S8P battery pack
 * against thresholds. Each sensor domain is checked independently.
 * The result is a bitmask telling the correlation engine which
 * categories are active, plus hotspot and risk information.
 *
 * Physics basis:
 *   - LFP (LiFePO₄) Tata Nexon EV Max: 104S8P, 332.8V, 120Ah
 *   - Thermal runaway cascade: Normal → SEI(80°C) → Separator(130°C)
 *     → Electrolyte(150°C) → Cathode(200°C) → Runaway(300°C+)
 *   - In 8P parallel groups, a single-cell fault is partially masked
 *     in voltage (~1/8 of full deviation) but visible in temperature
 */

#include "anomaly_eval.h"

/* -----------------------------------------------------------------------
 * Cascade stage thresholds (°C, core temperature)
 * Based on Feng et al. (2018) and LFP-specific research
 * ----------------------------------------------------------------------- */

/* Stage boundaries: Normal / Elevated / SEI / Separator / Electrolyte / Cathode
 * / Runaway */
static const float CASCADE_THRESHOLDS[] = {60.0f,  80.0f,  120.0f,
                                           150.0f, 200.0f, 300.0f};
#define NUM_CASCADE_THRESHOLDS 6

static const char *CASCADE_NAMES[] = {
    "Normal",      "Elevated", "SEI_Decomp", "Separator",
    "Electrolyte", "Cathode",  "RUNAWAY",
};

/* -----------------------------------------------------------------------
 * Default thresholds — Full 104S8P pack scale
 *
 * These are engineering values from the spec documents, scaled
 * for the real Tata Nexon EV Max battery (332.8V, 120Ah, LFP).
 * ----------------------------------------------------------------------- */

void anomaly_eval_init(anomaly_thresholds_t *t) {
  /* Electrical — full pack scale */
  t->voltage_low_v = 260.0f;       /* 104 × 2.5V = 260V cutoff         */
  t->voltage_high_v = 380.0f;      /* 104 × 3.65V = 379.6V ≈ 380V      */
  t->group_v_deviation_mv = 15.0f; /* Tight for 8P parallel masking     */
  t->v_spread_warn_mv = 50.0f;     /* Pack-wide voltage spread warning  */
  t->v_spread_crit_mv = 150.0f;    /* Pack-wide voltage spread critical */
  t->current_warning_a = 180.0f;   /* >1.5C = 180A warning              */
  t->current_short_a = 350.0f;     /* >> 2C peak = definite fault       */
  t->r_int_warning_mohm = 0.55f;   /* Group R_int (cell/8) warning      */

  /* Thermal — from spec, India-adapted */
  t->temp_warning_c = 55.0f;         /* Getting dangerous                 */
  t->temp_critical_c = 65.0f;        /* Approaching runaway onset         */
  t->dt_dt_warning = 0.50f;          /* 0.5°C/min warning                 */
  t->inter_module_dt_warn_c = 5.0f;  /* ΔT between modules warning        */
  t->inter_module_dt_crit_c = 10.0f; /* ΔT between modules critical       */
  t->intra_module_dt_warn_c = 3.0f;  /* ΔT within module warning          */
  t->intra_module_dt_crit_c = 8.0f;  /* ΔT within module critical         */

  /* Ambient-compensated thermal (spec §3.3) */
  t->delta_t_ambient_warning = 20.0f; /* ΔT > 20°C above ambient = anomaly*/

  /* Emergency thresholds — physics-based limits (spec §4.3)
   * These bypass multi-parameter correlation entirely. */
  t->temp_emergency_c = 80.0f;     /* Thermal runaway imminent          */
  t->dt_dt_emergency = 5.0f;       /* 5°C/min = runaway onset           */
  t->current_emergency_a = 500.0f; /* Massive current spike             */

  /* Gas (2× BME680) */
  t->gas_warning_ratio = 0.70f;  /* VOC detected                      */
  t->gas_critical_ratio = 0.40f; /* Heavy off-gassing                 */

  /* Pressure (2× BME680 co-located) */
  t->pressure_warning_hpa = 2.0f;  /* Cell starting to vent             */
  t->pressure_critical_hpa = 5.0f; /* Significant venting               */

  /* Coolant monitoring */
  t->coolant_dt_min_c = 2.0f; /* Min ΔT during high C-rate         */

  /* Mechanical (8× swelling sensors) */
  t->swelling_warning_pct = 3.0f; /* Module expanding (spec value)     */
}

/* -----------------------------------------------------------------------
 * Count active bits in a bitmask
 * ----------------------------------------------------------------------- */

uint8_t anomaly_count_categories(uint8_t mask) {
  uint8_t count = 0;
  while (mask) {
    count += (mask & 1);
    mask >>= 1;
  }
  return count;
}

/* -----------------------------------------------------------------------
 * Get cascade stage from core temperature
 * ----------------------------------------------------------------------- */

uint8_t get_cascade_stage(float core_temp_c) {
  for (int i = 0; i < NUM_CASCADE_THRESHOLDS; i++) {
    if (core_temp_c <= CASCADE_THRESHOLDS[i]) {
      return (uint8_t)i;
    }
  }
  return NUM_CASCADE_THRESHOLDS; /* Full runaway */
}

const char *cascade_stage_name(uint8_t stage) {
  if (stage <= NUM_CASCADE_THRESHOLDS) {
    return CASCADE_NAMES[stage];
  }
  return "UNKNOWN";
}

/* -----------------------------------------------------------------------
 * Compute derived fields in snapshot
 *
 * Call this BEFORE anomaly_eval_run(). Fills in:
 * - Per-module: module_voltage, mean_group_v, v_spread_mv, delta_t_intra
 * - Pack: v_spread_mv, temp_spread_c, hotspot, t_core_est_c
 * ----------------------------------------------------------------------- */

void anomaly_eval_compute(sensor_snapshot_t *s, const anomaly_thresholds_t *t) {
  (void)t; /* May use thresholds for context-dependent computations later */

  float global_v_min = 999.0f;
  float global_v_max = 0.0f;
  float global_t_min = 999.0f;
  float global_t_max = -999.0f;
  float max_dt_dt = 0.0f;
  float max_temp = -999.0f;
  uint8_t hot_module = 0;

  for (int m = 0; m < NUM_MODULES; m++) {
    module_data_t *mod = &s->modules[m];

    /* Per-module voltage statistics */
    float v_sum = 0.0f;
    float v_min = 999.0f;
    float v_max = 0.0f;

    for (int g = 0; g < GROUPS_PER_MODULE; g++) {
      float v = mod->group_voltages_v[g];
      v_sum += v;
      if (v < v_min)
        v_min = v;
      if (v > v_max)
        v_max = v;
      if (v < global_v_min)
        global_v_min = v;
      if (v > global_v_max)
        global_v_max = v;
    }

    mod->module_voltage = v_sum;
    mod->mean_group_v = v_sum / GROUPS_PER_MODULE;
    mod->v_spread_mv = (v_max - v_min) * 1000.0f;

    /* Per-module thermal */
    mod->delta_t_intra = mod->ntc1_c - mod->ntc2_c;
    if (mod->delta_t_intra < 0)
      mod->delta_t_intra = -mod->delta_t_intra;

    /* Track temps for pack-wide spread */
    if (mod->ntc1_c < global_t_min)
      global_t_min = mod->ntc1_c;
    if (mod->ntc2_c < global_t_min)
      global_t_min = mod->ntc2_c;
    if (mod->ntc1_c > global_t_max)
      global_t_max = mod->ntc1_c;
    if (mod->ntc2_c > global_t_max)
      global_t_max = mod->ntc2_c;

    /* Track hotspot */
    float mod_max_t = mod->ntc1_c > mod->ntc2_c ? mod->ntc1_c : mod->ntc2_c;
    if (mod_max_t > max_temp) {
      max_temp = mod_max_t;
      hot_module = (uint8_t)(m + 1);
    }

    if (mod->max_dt_dt > max_dt_dt) {
      max_dt_dt = mod->max_dt_dt;
    }
  }

  /* Pack-wide derived values */
  s->v_spread_mv = (global_v_max - global_v_min) * 1000.0f;
  s->temp_spread_c = global_t_max - global_t_min;
  s->dt_dt_max = max_dt_dt;
  s->hotspot_module = hot_module;
  s->hotspot_temp_c = max_temp;

  /* Core temperature estimation (spec §2.3):
   * T_core = T_surface + I_cell² × R_int_cell × R_thermal
   * I_cell = I_pack / 8 (parallel group splits current)
   * R_thermal ≈ 3.0 °C/W for IFR32135 cylindrical */
  float i_cell = s->pack_current_a / (float)CELLS_PER_GROUP;
  float r_int_ohm = s->r_internal_mohm / 1000.0f;
  float r_thermal = 3.0f; /* °C/W for cylindrical LFP cell */
  float core_delta = i_cell * i_cell * r_int_ohm * r_thermal;
  s->t_core_est_c = max_temp + core_delta;

  /* Coolant delta */
  s->coolant_delta_t = s->coolant_outlet_c - s->coolant_inlet_c;
}

/* -----------------------------------------------------------------------
 * Main evaluation function — Full pack (139 channels)
 *
 * Checks each sensor domain against thresholds. Builds a bitmask
 * of active anomaly categories, identifies hotspot, and assesses
 * thermal runaway risk.
 * ----------------------------------------------------------------------- */

anomaly_result_t anomaly_eval_run(const anomaly_thresholds_t *t,
                                  const sensor_snapshot_t *s) {
  anomaly_result_t result;
  result.active_mask = CAT_NONE;
  result.is_short_circuit = false;
  result.is_emergency_direct = false;
  result.hotspot_module = s->hotspot_module;
  result.anomaly_modules_mask = 0;
  result.risk_factor = 0.0f;
  result.cascade_stage = 0;

  /* === ELECTRICAL CATEGORY ===
   * Pack voltage, per-group voltage spread, current, R_int */

  /* Pack voltage bounds */
  if (s->pack_voltage_v < t->voltage_low_v ||
      s->pack_voltage_v > t->voltage_high_v) {
    result.active_mask |= CAT_ELECTRICAL;
  }

  /* Voltage spread across all 104 groups */
  if (s->v_spread_mv > t->v_spread_warn_mv) {
    result.active_mask |= CAT_ELECTRICAL;
  }

  /* Per-module voltage deviation check */
  for (int m = 0; m < NUM_MODULES; m++) {
    const module_data_t *mod = &s->modules[m];
    for (int g = 0; g < GROUPS_PER_MODULE; g++) {
      float dev_mv = (mod->group_voltages_v[g] - mod->mean_group_v) * 1000.0f;
      if (dev_mv < 0)
        dev_mv = -dev_mv;
      if (dev_mv > t->group_v_deviation_mv) {
        result.active_mask |= CAT_ELECTRICAL;
        result.anomaly_modules_mask |= (1 << m);
        break; /* One bad group is enough to flag this module */
      }
    }
  }

  /* Current check */
  float abs_current = s->pack_current_a;
  if (abs_current < 0)
    abs_current = -abs_current;

  if (abs_current > t->current_warning_a) {
    result.active_mask |= CAT_ELECTRICAL;
  }

  /* Short circuit detection */
  if (s->short_circuit || abs_current > t->current_short_a) {
    result.is_short_circuit = true;
    result.active_mask |= CAT_ELECTRICAL;
  }

  /* Emergency current spike (spec §4.3) */
  if (abs_current > t->current_emergency_a) {
    result.is_emergency_direct = true;
    result.active_mask |= CAT_ELECTRICAL;
  }

  /* R_int check (group-level: cell R_int / 8) */
  if (s->r_internal_mohm > t->r_int_warning_mohm) {
    result.active_mask |= CAT_ELECTRICAL;
  }

  /* === THERMAL CATEGORY ===
   * 16 NTC temperatures (2 per module × 8 modules)
   * + ambient compensation + dT/dt + inter/intra module ΔT */

  /* Find max NTC temperature across all modules */
  float max_ntc = -999.0f;
  for (int m = 0; m < NUM_MODULES; m++) {
    float ntc1 = s->modules[m].ntc1_c;
    float ntc2 = s->modules[m].ntc2_c;

    /* Absolute threshold check — any NTC */
    if (ntc1 > t->temp_warning_c || ntc2 > t->temp_warning_c) {
      result.active_mask |= CAT_THERMAL;
      result.anomaly_modules_mask |= (1 << m);
    }

    /* Track max for ambient compensation */
    if (ntc1 > max_ntc)
      max_ntc = ntc1;
    if (ntc2 > max_ntc)
      max_ntc = ntc2;

    /* Intra-module ΔT check (one half hotter than other) */
    if (s->modules[m].delta_t_intra > t->intra_module_dt_warn_c) {
      result.active_mask |= CAT_THERMAL;
      result.anomaly_modules_mask |= (1 << m);
    }
  }

  /* Inter-module ΔT check (one module much hotter than others) */
  if (s->temp_spread_c > t->inter_module_dt_warn_c) {
    result.active_mask |= CAT_THERMAL;
  }

  /* Ambient-compensated threshold */
  float delta_t_ambient = max_ntc - s->temp_ambient_c;
  if (delta_t_ambient >= t->delta_t_ambient_warning) {
    result.active_mask |= CAT_THERMAL;
  }

  /* Rate of temperature change (max across all modules) */
  if (s->dt_dt_max > t->dt_dt_warning) {
    result.active_mask |= CAT_THERMAL;
  }

  /* Emergency thermal thresholds (spec §4.3) — direct bypass */
  if (max_ntc > t->temp_emergency_c || s->dt_dt_max > t->dt_dt_emergency) {
    result.is_emergency_direct = true;
    result.active_mask |= CAT_THERMAL;
  }

  /* === GAS CATEGORY ===
   * Use worst-case of 2 BME680 sensors (lower ratio = more gas) */
  float worst_gas =
      s->gas_ratio_1 < s->gas_ratio_2 ? s->gas_ratio_1 : s->gas_ratio_2;

  if (worst_gas < t->gas_warning_ratio) {
    result.active_mask |= CAT_GAS;
  }

  /* === PRESSURE CATEGORY ===
   * Use worst-case of 2 pressure sensors (higher delta = more pressure) */
  float worst_pressure = s->pressure_delta_1_hpa > s->pressure_delta_2_hpa
                             ? s->pressure_delta_1_hpa
                             : s->pressure_delta_2_hpa;

  if (worst_pressure > t->pressure_warning_hpa) {
    result.active_mask |= CAT_PRESSURE;
  }

  /* === SWELLING CATEGORY ===
   * Check all 8 module swelling sensors */
  for (int m = 0; m < NUM_MODULES; m++) {
    if (s->modules[m].swelling_pct > t->swelling_warning_pct) {
      result.active_mask |= CAT_SWELLING;
      result.anomaly_modules_mask |= (1 << m);
    }
  }

  /* === THERMAL RUNAWAY RISK ASSESSMENT ===
   * Based on real cascade chemistry stages */
  result.cascade_stage = get_cascade_stage(s->t_core_est_c);

  /* Risk factor: weighted combination of stage, dT/dt, and gas */
  float risk = 0.0f;

  /* Temperature contribution (linear from 60°C to 300°C) */
  if (s->t_core_est_c > 60.0f) {
    risk += (s->t_core_est_c - 60.0f) / 240.0f;
    if (risk > 1.0f)
      risk = 1.0f;
  }

  /* dT/dt contribution (accelerating = worse) */
  if (s->dt_dt_max > 0.1f) {
    risk += s->dt_dt_max * 0.05f; /* 20°C/min → adds 1.0 */
    if (risk > 1.0f)
      risk = 1.0f;
  }

  /* Gas contribution (off-gassing = worse) */
  if (worst_gas < 0.8f) {
    risk += (0.8f - worst_gas) * 0.5f; /* 0.3 ratio → adds 0.25 */
    if (risk > 1.0f)
      risk = 1.0f;
  }

  /* Pressure contribution */
  if (worst_pressure > 1.0f) {
    risk += worst_pressure * 0.02f; /* 10 hPa → adds 0.2 */
    if (risk > 1.0f)
      risk = 1.0f;
  }

  result.risk_factor = risk;

  /* Count total active categories */
  result.active_count = anomaly_count_categories(result.active_mask);

  return result;
}
