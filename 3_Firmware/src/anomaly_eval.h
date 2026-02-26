/*
 * anomaly_eval.h — Anomaly Category Evaluator
 *
 * Evaluates raw sensor readings against thresholds and returns
 * which anomaly categories are active (electrical, thermal, gas,
 * pressure, swelling).
 *
 * This is one of the core modules that makes the system work:
 * instead of just checking "is temperature high?", it looks at
 * FIVE different physical phenomena to determine what's going wrong.
 */

#ifndef ANOMALY_EVAL_H
#define ANOMALY_EVAL_H

#include <stdbool.h>
#include <stdint.h>

/* -----------------------------------------------------------------------
 * Anomaly category bitmask
 *
 * Each bit represents one physical domain. The correlation engine
 * counts how many bits are set to decide the alert level.
 * ----------------------------------------------------------------------- */

#define CAT_NONE 0x00
#define CAT_ELECTRICAL 0x01 /* Voltage/Current/R_int anomaly */
#define CAT_THERMAL 0x02    /* Temperature or dT/dt anomaly  */
#define CAT_GAS 0x04        /* VOC gas ratio anomaly         */
#define CAT_PRESSURE 0x08   /* Enclosure pressure anomaly    */
#define CAT_SWELLING 0x10   /* Cell mechanical deformation   */

/* Total number of independent categories */
#define CAT_COUNT 5

/* -----------------------------------------------------------------------
 * Thresholds
 *
 * These values come from battery safety literature and will be
 * tuned once we have real sensor data from the hardware.
 * ----------------------------------------------------------------------- */

typedef struct {
  /* Electrical thresholds */
  float voltage_low_v;      /* Pack voltage below this is anomalous     */
  float current_warning_a;  /* Current above this is anomalous          */
  float current_short_a;    /* Current above this = short circuit       */
  float r_int_warning_mohm; /* Internal resistance above this is bad    */

  /* Thermal thresholds */
  float temp_warning_c;  /* Cell temp above this = thermal anomaly   */
  float temp_critical_c; /* Cell temp critical (used for severity)   */
  float dt_dt_warning;   /* Rate of rise (°C/s) above this = anomaly*/

  /* Ambient-compensated thermal thresholds (spec §3.3)
   * ΔT = T_cell - T_ambient.  A 45°C cell in 25°C ambient (ΔT=20)
   * is suspicious, but 45°C in 38°C ambient (ΔT=7) is normal. */
  float delta_t_ambient_warning; /* ΔT above ambient for WARNING (°C)   */

  /* Emergency thresholds — physics-based limits (spec §4.3)
   * Any SINGLE reading at these levels → immediate EMERGENCY bypass,
   * no multi-parameter correlation required. */
  float temp_emergency_c;    /* T_cell above this → EMERGENCY (80°C)    */
  float dt_dt_emergency;     /* dT/dt above this → EMERGENCY (°C/s)     */
  float current_emergency_a; /* Current spike → EMERGENCY               */

  /* Gas thresholds (BME680) */
  float gas_warning_ratio;  /* Gas ratio below this = gas anomaly       */
  float gas_critical_ratio; /* Gas ratio critical                       */

  /* Pressure thresholds (BME680) */
  float pressure_warning_hpa;  /* Pressure delta above this = anomaly      */
  float pressure_critical_hpa; /* Pressure delta critical                  */

  /* Mechanical thresholds (FSR402) */
  float swelling_warning_pct; /* Swelling above this = anomaly            */
} anomaly_thresholds_t;

/* -----------------------------------------------------------------------
 * Sensor data input
 *
 * This struct holds one snapshot of all sensor readings.
 * The firmware fills this in from the sensor drivers, and then
 * passes it to anomaly_eval_run().
 * ----------------------------------------------------------------------- */

typedef struct {
  /* Electrical (from INA219) */
  float voltage_v;
  float current_a;
  float r_internal_mohm;

  /* Thermal (from NTC thermistors via MUX) */
  float temp_cells_c[4]; /* 4 cell temperatures */
  float temp_ambient_c;  /* Ambient temperature */
  float dt_dt_max;       /* Max rate of rise across all cells (°C/s) */

  /* Core temperature estimation (spec §2.3)
   * T_core = T_surface + I_cell² × R_int × R_thermal
   * Computed in med_loop from surface temp, current, and R_int. */
  float t_core_est_c; /* Estimated cell core temperature          */

  /* Internal resistance tracking */
  float dr_dt_mohm_per_s; /* Rate of change of R_int (mΩ/s)          */

  /* Gas & Pressure (from BME680) */
  float gas_ratio;          /* current/baseline ratio (1.0 = clean)    */
  float pressure_delta_hpa; /* change from baseline pressure           */

  /* Mechanical (from FSR402) */
  float swelling_pct; /* 0-100% force reading                    */

  /* Fast-loop flags */
  bool short_circuit; /* Set by fast loop if current spike detected */
} sensor_snapshot_t;

/* -----------------------------------------------------------------------
 * Evaluation result
 * ----------------------------------------------------------------------- */

typedef struct {
  uint8_t active_mask;      /* Bitmask of active categories (CAT_*)    */
  uint8_t active_count;     /* Number of active categories (0-5)       */
  bool is_short_circuit;    /* True if short circuit detected           */
  bool is_emergency_direct; /* True if any single reading hit
                             * emergency-level threshold (spec §4.3).
                             * Bypasses multi-parameter counting.    */
} anomaly_result_t;

/* -----------------------------------------------------------------------
 * Public API
 * ----------------------------------------------------------------------- */

/* Initialize with default thresholds */
void anomaly_eval_init(anomaly_thresholds_t *thresholds);

/* Evaluate a sensor snapshot and return which categories are active */
anomaly_result_t anomaly_eval_run(const anomaly_thresholds_t *thresholds,
                                  const sensor_snapshot_t *snapshot);

/* Count the number of set bits in a category mask */
uint8_t anomaly_count_categories(uint8_t mask);

#endif /* ANOMALY_EVAL_H */
