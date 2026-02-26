/*
 * anomaly_eval.c — Anomaly Category Evaluator (Implementation)
 *
 * Evaluates sensor readings against thresholds. Each sensor domain
 * is checked independently. The result is a bitmask telling the
 * correlation engine which categories are active.
 */

#include "anomaly_eval.h"

/* -----------------------------------------------------------------------
 * Default thresholds
 *
 * These are starting values based on Li-Ion cell safety data.
 * They WILL be tuned when we have real hardware.
 * ----------------------------------------------------------------------- */

void anomaly_eval_init(anomaly_thresholds_t *t) {
  /* Electrical */
  t->voltage_low_v = 12.0f;       /* 4S pack: 3.0V/cell min */
  t->current_warning_a = 8.0f;    /* Above normal load */
  t->current_short_a = 15.0f;     /* Definite short circuit */
  t->r_int_warning_mohm = 100.0f; /* Degraded cell */

  /* Thermal */
  t->temp_warning_c = 55.0f;  /* Getting dangerous */
  t->temp_critical_c = 70.0f; /* Approaching runaway onset */
  t->dt_dt_warning = 2.0f;    /* 2°C/sec rise = fast event */

  /* Ambient-compensated thermal (spec §3.3) */
  t->delta_t_ambient_warning = 20.0f; /* ΔT > 20°C above ambient = anomaly */

  /* Emergency thresholds — physics-based limits (spec §4.3)
   * These bypass multi-parameter correlation entirely. */
  t->temp_emergency_c = 80.0f;    /* Thermal runaway imminent */
  t->dt_dt_emergency = 0.083f;    /* 5°C/min = 0.083°C/s */
  t->current_emergency_a = 20.0f; /* Prototype-scaled (500A → 20A) */

  /* Gas (BME680) */
  t->gas_warning_ratio = 0.70f;  /* VOC detected */
  t->gas_critical_ratio = 0.40f; /* Heavy off-gassing */

  /* Pressure */
  t->pressure_warning_hpa = 5.0f;   /* Cell starting to vent */
  t->pressure_critical_hpa = 15.0f; /* Significant venting */

  /* Mechanical */
  t->swelling_warning_pct = 30.0f; /* Cell expanding */
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
 * Main evaluation function
 *
 * Checks each sensor domain against its thresholds and builds a
 * bitmask of active anomaly categories.
 * ----------------------------------------------------------------------- */

anomaly_result_t anomaly_eval_run(const anomaly_thresholds_t *t,
                                  const sensor_snapshot_t *s) {
  anomaly_result_t result;
  result.active_mask = CAT_NONE;
  result.is_short_circuit = false;
  result.is_emergency_direct = false;

  /* --- Electrical category ---
   * Triggered by: low voltage, high current, or high internal resistance.
   * These indicate cell degradation or overload conditions.
   */
  if (s->voltage_v < t->voltage_low_v || s->current_a > t->current_warning_a ||
      s->r_internal_mohm > t->r_int_warning_mohm) {
    result.active_mask |= CAT_ELECTRICAL;
  }

  /* Short circuit check (separate from normal electrical anomaly) */
  if (s->short_circuit || s->current_a > t->current_short_a) {
    result.is_short_circuit = true;
    result.active_mask |= CAT_ELECTRICAL;
  }

  /* Emergency current spike (spec §4.3) */
  if (s->current_a > t->current_emergency_a) {
    result.is_emergency_direct = true;
    result.active_mask |= CAT_ELECTRICAL;
  }

  /* --- Thermal category ---
   * Two complementary checks (spec §3.3):
   * 1. Absolute threshold: any cell > temp_warning_c
   * 2. Ambient-compensated: ΔT = max(cell) - ambient > delta_t_ambient_warning
   *    This prevents false alarms in hot climates while catching real faults
   *    in cold environments where the same absolute temp is more suspicious.
   */
  float max_cell_temp = s->temp_cells_c[0];
  for (int i = 1; i < 4; i++) {
    if (s->temp_cells_c[i] > max_cell_temp) {
      max_cell_temp = s->temp_cells_c[i];
    }
  }

  /* Check absolute threshold */
  for (int i = 0; i < 4; i++) {
    if (s->temp_cells_c[i] > t->temp_warning_c) {
      result.active_mask |= CAT_THERMAL;
      break;
    }
  }

  /* Check ambient-compensated threshold */
  float delta_t_ambient = max_cell_temp - s->temp_ambient_c;
  if (delta_t_ambient >= t->delta_t_ambient_warning) {
    result.active_mask |= CAT_THERMAL;
  }

  /* Also check rate of temperature change */
  if (s->dt_dt_max > t->dt_dt_warning) {
    result.active_mask |= CAT_THERMAL;
  }

  /* Emergency thermal thresholds (spec §4.3) — direct bypass */
  if (max_cell_temp > t->temp_emergency_c ||
      s->dt_dt_max > t->dt_dt_emergency) {
    result.is_emergency_direct = true;
    result.active_mask |= CAT_THERMAL;
  }

  /* --- Gas category ---
   * Triggered by: gas ratio dropping below warning level.
   * The BME680 gas resistance drops when VOCs are present.
   * VOCs are released when electrolyte begins decomposing.
   * This happens BEFORE the temperature spike — our key advantage.
   */
  if (s->gas_ratio < t->gas_warning_ratio) {
    result.active_mask |= CAT_GAS;
  }

  /* --- Pressure category ---
   * Triggered by: enclosure pressure rising above baseline.
   * When a cell vents internally, gas escapes and raises the
   * pressure inside the module enclosure. Another early indicator.
   */
  if (s->pressure_delta_hpa > t->pressure_warning_hpa) {
    result.active_mask |= CAT_PRESSURE;
  }

  /* --- Swelling category ---
   * Triggered by: force sensor detecting cell expansion.
   * Li-Ion cells physically swell when internal gas builds up.
   * The FSR402 pressed against the cell detects this.
   */
  if (s->swelling_pct > t->swelling_warning_pct) {
    result.active_mask |= CAT_SWELLING;
  }

  /* Count total active categories */
  result.active_count = anomaly_count_categories(result.active_mask);

  return result;
}
