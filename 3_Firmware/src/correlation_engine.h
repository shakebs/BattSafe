/*
 * correlation_engine.h — Multi-Modal Correlation Engine
 *
 * This is the "false-positive killer" — the core innovation of
 * the project. Instead of triggering an emergency on a single
 * sensor alarm, it counts HOW MANY independent anomaly categories
 * are active simultaneously and escalates accordingly:
 *
 *   0 categories → NORMAL
 *   1 category   → WARNING  (increase monitoring)
 *   2 categories → CRITICAL (prepare for disconnect, countdown starts)
 *   3+ categories→ EMERGENCY (immediate relay disconnect)
 *   Short circuit → EMERGENCY (immediate, bypass counting)
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
 *
 * Holds the current state and manages the CRITICAL→EMERGENCY countdown.
 * ----------------------------------------------------------------------- */

typedef struct {
  system_state_t current_state;

  /* CRITICAL state countdown:
   * When we enter CRITICAL, we start a countdown. If the condition
   * persists for `critical_countdown_limit` cycles, we escalate
   * to EMERGENCY. This prevents false alarms from brief spikes. */
  uint16_t critical_countdown;
  uint16_t critical_countdown_limit; /* Default: 20 cycles = 10s at 500ms */

  /* De-escalation cooldown:
   * After entering WARNING/CRITICAL, we don't immediately drop back
   * to NORMAL — we wait for sustained normal readings. */
  uint16_t deescalation_counter;
  uint16_t deescalation_limit; /* Default: 10 cycles = 5s at 500ms */

  /* Emergency latch:
   * Once we enter EMERGENCY, we stay there until manually reset.
   * This is a safety feature — you don't auto-recover from a
   * potential thermal runaway. */
  bool emergency_latched;

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

/* Manually reset the engine (e.g., after servicing an emergency) */
void correlation_engine_reset(correlation_engine_t *engine);

#endif /* CORRELATION_ENGINE_H */
