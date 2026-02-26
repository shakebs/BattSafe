/*
 * anomaly_eval.h — Anomaly Category Evaluator (Full Pack Edition)
 *
 * Evaluates raw sensor readings from ALL 139 channels of a 104S8P
 * battery pack (8 modules × 13 groups × 8 cells = 832 cells) and
 * returns which anomaly categories are active.
 *
 * Architecture:
 *   - 104 group voltages (13 per module × 8 modules)
 *   - 16 NTC temperatures (2 per module × 8 modules)
 *   - 2 gas sensors (BME680 at opposite ends of pack)
 *   - 2 pressure sensors (co-located with gas)
 *   - 8 swelling sensors (1 per module end-plate)
 *   - 1 pack current, 1 ambient temp, 2 coolant temps
 *   - 1 humidity, 1 isolation resistance
 */

#ifndef ANOMALY_EVAL_H
#define ANOMALY_EVAL_H

#include <stdbool.h>
#include <stdint.h>

/* -----------------------------------------------------------------------
 * Pack geometry constants
 * ----------------------------------------------------------------------- */

#define NUM_MODULES 8
#define GROUPS_PER_MODULE 13
#define TOTAL_SERIES (NUM_MODULES * GROUPS_PER_MODULE) /* 104 */
#define CELLS_PER_GROUP 8

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
 * Thresholds — Full-pack scale (104S8P, 332.8V nominal, 120Ah)
 *
 * Based on thermal_anomaly_detection_parameters.md and
 * prototype_sensor_architecture_engineering_spec.md
 * ----------------------------------------------------------------------- */

typedef struct {
  /* Electrical thresholds (pack scale) */
  float voltage_low_v;           /* Pack voltage below this = anomaly (260V)  */
  float voltage_high_v;          /* Pack voltage above this = anomaly (380V)  */
  float group_v_deviation_mv;    /* Per-group deviation from module mean (mV) */
  float v_spread_warn_mv;        /* Max-min spread across 104 groups warning  */
  float v_spread_crit_mv;        /* Max-min spread critical                   */
  float current_warning_a;       /* Current above this is anomalous           */
  float current_short_a;         /* Current above this = short circuit        */
  float r_int_warning_mohm;      /* Internal resistance above this is bad     */

  /* Thermal thresholds */
  float temp_warning_c;          /* Cell temp above this = thermal anomaly    */
  float temp_critical_c;         /* Cell temp critical                        */
  float dt_dt_warning;           /* Rate of rise (°C/s) warning              */
  float inter_module_dt_warn_c;  /* ΔT between modules warning (5°C)         */
  float inter_module_dt_crit_c;  /* ΔT between modules critical (10°C)       */
  float intra_module_dt_warn_c;  /* ΔT within module (NTC1 vs NTC2) warn     */
  float intra_module_dt_crit_c;  /* ΔT within module critical                */

  /* Ambient-compensated thermal thresholds (spec §3.3)
   * ΔT = T_cell - T_ambient.  A 45°C cell in 25°C ambient (ΔT=20)
   * is suspicious, but 45°C in 38°C ambient (ΔT=7) is normal. */
  float delta_t_ambient_warning; /* ΔT above ambient for WARNING (°C)       */

  /* Emergency thresholds — physics-based limits (spec §4.3)
   * Any SINGLE reading at these levels → immediate EMERGENCY bypass. */
  float temp_emergency_c;        /* T_cell above this → EMERGENCY (80°C)    */
  float dt_dt_emergency;         /* dT/dt above this → EMERGENCY (°C/s)     */
  float current_emergency_a;     /* Current spike → EMERGENCY               */

  /* Gas thresholds (2× BME680) */
  float gas_warning_ratio;       /* Gas ratio below this = gas anomaly       */
  float gas_critical_ratio;      /* Gas ratio critical                       */

  /* Pressure thresholds (2× BME680 co-located) */
  float pressure_warning_hpa;    /* Pressure delta warning                   */
  float pressure_critical_hpa;   /* Pressure delta critical                  */

  /* Coolant monitoring */
  float coolant_dt_min_c;        /* Minimum coolant ΔT during load           */

  /* Mechanical thresholds (8× swelling sensors) */
  float swelling_warning_pct;    /* Swelling above this = anomaly            */
} anomaly_thresholds_t;

/* -----------------------------------------------------------------------
 * Per-module data (what the firmware processes per module)
 * ----------------------------------------------------------------------- */

typedef struct {
  float group_voltages_v[GROUPS_PER_MODULE]; /* 13 group voltages           */
  float ntc1_c;                 /* NTC between groups 3-4 (°C)              */
  float ntc2_c;                 /* NTC between groups 10-11 (°C)            */
  float swelling_pct;           /* End-plate force sensor (0-100%)          */
  /* Computed fields — filled by med_loop */
  float max_dt_dt;              /* Max dT/dt in this module (°C/min)        */
  float delta_t_intra;          /* |NTC1 - NTC2| (°C)                       */
  float module_voltage;         /* Sum of 13 group voltages                 */
  float mean_group_v;           /* Mean of 13 group voltages                */
  float v_spread_mv;            /* Max-min voltage within module (mV)       */
} module_data_t;

/* -----------------------------------------------------------------------
 * Sensor data input — full pack snapshot (all 139 channels)
 * ----------------------------------------------------------------------- */

typedef struct {
  /* Electrical — pack level */
  float pack_voltage_v;          /* Full pack voltage (~332V)                */
  float pack_current_a;          /* Pack current (±315A)                     */
  float r_internal_mohm;         /* Estimated from V/I transients            */

  /* Per-module data (8 modules) */
  module_data_t modules[NUM_MODULES];

  /* Pack-level environment sensors */
  float temp_ambient_c;          /* Ambient temperature                      */
  float coolant_inlet_c;         /* Coolant inlet temp                       */
  float coolant_outlet_c;        /* Coolant outlet temp                      */
  float gas_ratio_1;             /* BME680 sensor 1 (near M1-M2)            */
  float gas_ratio_2;             /* BME680 sensor 2 (near M7-M8)            */
  float pressure_delta_1_hpa;    /* Co-located with gas 1                   */
  float pressure_delta_2_hpa;    /* Co-located with gas 2                   */
  float humidity_pct;            /* Pack humidity (%)                        */
  float isolation_mohm;          /* HV isolation resistance (MΩ)            */

  /* Computed fields — filled by med_loop before anomaly_eval_run */
  float dt_dt_max;               /* Max dT/dt across ALL modules (°C/min)   */
  float v_spread_mv;             /* Max-min voltage across 104 groups (mV)  */
  float temp_spread_c;           /* Max-min NTC spread across pack (°C)     */
  float t_core_est_c;            /* Estimated core temp of hottest cell     */
  float dr_dt_mohm_per_s;        /* R_int rate of change (mΩ/s)             */
  float coolant_delta_t;         /* Coolant outlet - inlet (°C)             */

  /* Hotspot tracking — filled by compute step */
  uint8_t hotspot_module;        /* Module with max temp (1-based, 0=none)  */
  uint8_t hotspot_group;         /* Group within that module (1-based)      */
  float hotspot_temp_c;          /* Temperature at hotspot                   */

  /* Fast-loop flags */
  bool short_circuit;            /* Set by fast loop if current spike       */
} sensor_snapshot_t;

/* -----------------------------------------------------------------------
 * Evaluation result
 * ----------------------------------------------------------------------- */

typedef struct {
  uint8_t active_mask;           /* Bitmask of active categories (CAT_*)    */
  uint8_t active_count;          /* Number of active categories (0-5)       */
  bool is_short_circuit;         /* True if short circuit detected          */
  bool is_emergency_direct;      /* True if physics-limit bypass triggered  */

  /* Hotspot info for dashboard */
  uint8_t hotspot_module;        /* Module with worst anomaly (1-based)     */
  uint8_t anomaly_modules_mask;  /* Bitmask: which modules have anomalies   */

  /* Thermal runaway risk assessment */
  float risk_factor;             /* 0.0 = safe, 1.0 = runaway imminent     */
  uint8_t cascade_stage;         /* 0=Normal..6=Full_Runaway                */
} anomaly_result_t;

/* -----------------------------------------------------------------------
 * Public API
 * ----------------------------------------------------------------------- */

/* Initialize with default thresholds for full 104S8P pack */
void anomaly_eval_init(anomaly_thresholds_t *thresholds);

/* Compute derived fields in snapshot (call before anomaly_eval_run) */
void anomaly_eval_compute(sensor_snapshot_t *snapshot,
                          const anomaly_thresholds_t *thresholds);

/* Evaluate a sensor snapshot and return which categories are active */
anomaly_result_t anomaly_eval_run(const anomaly_thresholds_t *thresholds,
                                  const sensor_snapshot_t *snapshot);

/* Count the number of set bits in a category mask */
uint8_t anomaly_count_categories(uint8_t mask);

/* Get cascade stage index from core temperature */
uint8_t get_cascade_stage(float core_temp_c);

/* Get cascade stage name string */
const char *cascade_stage_name(uint8_t stage);

#endif /* ANOMALY_EVAL_H */
