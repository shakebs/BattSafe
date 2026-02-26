/*
 * correlation_engine.c — Multi-Modal Correlation Engine (Implementation)
 *
 * State transition logic:
 *
 *  NORMAL ──(1 cat)──> WARNING ──(2 cats)──> CRITICAL ──(countdown)──>
 * EMERGENCY ^                    |                     | | | |     (cats drop)
 * |                          | +── (0 cats, cooldown expired) ◄───────────+
 * [LATCHED]
 *
 * EMERGENCY is latched — once triggered, it stays until manually reset.
 * This is a safety design choice: you need a human to verify it's safe.
 */

#include "correlation_engine.h"

/* State name lookup table */
static const char *STATE_NAMES[] = {
    "NORMAL",
    "WARNING",
    "CRITICAL",
    "EMERGENCY",
};

/* -----------------------------------------------------------------------
 * Initialize
 * ----------------------------------------------------------------------- */

void correlation_engine_init(correlation_engine_t *engine) {
  engine->current_state = STATE_NORMAL;

  /* CRITICAL countdown: 20 cycles × 500ms = 10 seconds
   * If 2 categories are active for 10 consecutive seconds,
   * escalate to EMERGENCY. */
  engine->critical_countdown = 0;
  engine->critical_countdown_limit = 20;

  /* De-escalation: 10 cycles × 500ms = 5 seconds
   * Must remain at lower severity for 5 seconds before
   * dropping the alert level. */
  engine->deescalation_counter = 0;
  engine->deescalation_limit = 10;

  engine->emergency_latched = false;

  engine->total_evaluations = 0;
  engine->warning_count = 0;
  engine->critical_count = 0;
  engine->emergency_count = 0;
}

/* -----------------------------------------------------------------------
 * Main update function — called every medium loop cycle (500ms)
 * ----------------------------------------------------------------------- */

system_state_t correlation_engine_update(correlation_engine_t *engine,
                                         const anomaly_result_t *anomaly) {
  engine->total_evaluations++;

  /* If emergency is latched, stay there until manual reset */
  if (engine->emergency_latched) {
    engine->emergency_count++;
    return STATE_EMERGENCY;
  }

  /* --- Immediate EMERGENCY triggers --- */

  /* Short circuit = immediate emergency, no waiting */
  if (anomaly->is_short_circuit) {
    engine->current_state = STATE_EMERGENCY;
    engine->emergency_latched = true;
    engine->emergency_count++;
    return STATE_EMERGENCY;
  }

  /* Emergency direct bypass (spec §4.3):
   * Physics-based limits (T>80°C, dT/dt>5°C/min, current spike)
   * bypass multi-parameter correlation entirely. */
  if (anomaly->is_emergency_direct) {
    engine->current_state = STATE_EMERGENCY;
    engine->emergency_latched = true;
    engine->emergency_count++;
    return STATE_EMERGENCY;
  }

  /* 3 or more categories active = immediate emergency */
  if (anomaly->active_count >= 3) {
    engine->current_state = STATE_EMERGENCY;
    engine->emergency_latched = true;
    engine->emergency_count++;
    return STATE_EMERGENCY;
  }

  /* --- 2 categories: CRITICAL with countdown --- */

  if (anomaly->active_count >= 2) {
    if (engine->current_state != STATE_CRITICAL) {
      /* Just entered CRITICAL — start countdown */
      engine->current_state = STATE_CRITICAL;
      engine->critical_countdown = 0;
    }

    engine->critical_countdown++;
    engine->critical_count++;
    engine->deescalation_counter = 0; /* Reset de-escalation */

    /* If CRITICAL persists long enough, escalate to EMERGENCY */
    if (engine->critical_countdown >= engine->critical_countdown_limit) {
      engine->current_state = STATE_EMERGENCY;
      engine->emergency_latched = true;
      engine->emergency_count++;
      return STATE_EMERGENCY;
    }

    return STATE_CRITICAL;
  }

  /* --- 1 category: WARNING --- */

  if (anomaly->active_count == 1) {
    engine->current_state = STATE_WARNING;
    engine->critical_countdown = 0; /* Reset CRITICAL countdown */
    engine->deescalation_counter = 0;
    engine->warning_count++;
    return STATE_WARNING;
  }

  /* --- 0 categories: try to de-escalate --- */

  if (engine->current_state != STATE_NORMAL) {
    /* Don't immediately drop — wait for sustained normal readings */
    engine->deescalation_counter++;

    if (engine->deescalation_counter >= engine->deescalation_limit) {
      engine->current_state = STATE_NORMAL;
      engine->deescalation_counter = 0;
    }
    /* Otherwise stay at current state while cooling down */
  }

  engine->critical_countdown = 0;
  return engine->current_state;
}

/* -----------------------------------------------------------------------
 * Utility functions
 * ----------------------------------------------------------------------- */

const char *correlation_state_name(system_state_t state) {
  if (state <= STATE_EMERGENCY) {
    return STATE_NAMES[state];
  }
  return "UNKNOWN";
}

void correlation_engine_reset(correlation_engine_t *engine) {
  correlation_engine_init(engine);
}
