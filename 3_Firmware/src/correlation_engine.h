/*
 * correlation_engine.h — Multi-Modal Correlation Engine (Full Pack Edition)
 *
 * The "false-positive killer" — counts HOW MANY independent anomaly
 * categories are active simultaneously and escalates accordingly:
 *
 *   0 categories → NORMAL
 *   1 category   → WARNING  (increase monitoring)
 *   2 categories → CRITICAL (prepare for disconnect, countdown starts)
 *   3+ categories→ EMERGENCY (immediate relay disconnect)
 *   Short circuit → EMERGENCY (immediate, bypass counting)
 *
 * Enhanced with: hotspot tracking, per-module anomaly mask, risk scoring
 */

#ifndef CORRELATION_ENGINE_H
#define CORRELATION_ENGINE_H

#include "anomaly_eval.h"
#include <stdbool.h>
#include <stdint.h>

/* -----------------------------------------------------------------------
 * System states
 * ----------------------------------------------------------------------- */

typedef enum {
  STATE_NORMAL = 0,
  STATE_WARNING = 1,
  STATE_CRITICAL = 2,
  STATE_EMERGENCY = 3,
} system_state_t;

/* -----------------------------------------------------------------------
 * Correlation engine context
 * ----------------------------------------------------------------------- */

typedef struct {
  system_state_t current_state;

  /* CRITICAL state countdown */
  uint16_t critical_countdown;
  uint16_t critical_countdown_limit; /* Default: 20 cycles = 10s at 500ms */

  /* De-escalation cooldown */
  uint16_t deescalation_counter;
  uint16_t deescalation_limit; /* Default: 10 cycles = 5s at 500ms */

  /* Emergency latch with recovery hold */
  bool emergency_latched;
  uint16_t emergency_recovery_counter;
  uint16_t emergency_recovery_limit; /* Default: 10 cycles = 5s at 500ms */

  /* Hotspot tracking (from latest anomaly result) */
  uint8_t hotspot_module;       /* Module with worst anomaly (1-based)     */
  uint8_t anomaly_modules_mask; /* Which modules have anomalies            */
  float risk_factor;            /* 0.0 – 1.0 from anomaly eval            */
  uint8_t cascade_stage;        /* Thermal cascade stage index             */

  /* Statistics */
  uint32_t total_evaluations;
  uint32_t warning_count;
  uint32_t critical_count;
  uint32_t emergency_count;
} correlation_engine_t;

/* -----------------------------------------------------------------------
 * Public API
 * ----------------------------------------------------------------------- */

/* Initialize the engine with default parameters */
void correlation_engine_init(correlation_engine_t *engine);

/* Process an anomaly evaluation result and update the system state.
 * Returns the new system state. */
system_state_t correlation_engine_update(correlation_engine_t *engine,
                                         const anomaly_result_t *anomaly);

/* Get a human-readable name for a state */
const char *correlation_state_name(system_state_t state);

/* Manually reset the engine */
void correlation_engine_reset(correlation_engine_t *engine);

#endif /* CORRELATION_ENGINE_H */
