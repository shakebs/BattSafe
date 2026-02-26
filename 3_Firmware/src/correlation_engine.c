/*
 * correlation_engine.c — Multi-Modal Correlation Engine (Full Pack Edition)
 *
 * State transition logic:
 *
 *  NORMAL ──(1 cat)──> WARNING ──(2 cats)──> CRITICAL ──(countdown)──>
 * EMERGENCY ^                    |                     |
 *           |     (cats drop)    |                     |
 *           +── (0 cats, cooldown expired) <────────────+
 *           [LATCHED]
 *
 * EMERGENCY is latched, but it can auto-release after sustained nominal
 * conditions. This keeps demo operation recoverable without manual board reset.
 *
 * Enhanced: Now tracks hotspot module, anomaly mask, risk factor, and
 * cascade stage from the full-pack anomaly evaluation.
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

  /* CRITICAL countdown: 20 cycles × 500ms = 10 seconds */
  engine->critical_countdown = 0;
  engine->critical_countdown_limit = 20;

  /* De-escalation: 10 cycles × 500ms = 5 seconds */
  engine->deescalation_counter = 0;
  engine->deescalation_limit = 10;

  engine->emergency_latched = false;
  engine->emergency_recovery_counter = 0;
  engine->emergency_recovery_limit = 10;

  /* Hotspot / risk tracking */
  engine->hotspot_module = 0;
  engine->anomaly_modules_mask = 0;
  engine->risk_factor = 0.0f;
  engine->cascade_stage = 0;

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

  /* Always update tracking fields from latest evaluation */
  engine->hotspot_module = anomaly->hotspot_module;
  engine->anomaly_modules_mask = anomaly->anomaly_modules_mask;
  engine->risk_factor = anomaly->risk_factor;
  engine->cascade_stage = anomaly->cascade_stage;

  /* If emergency is latched, require sustained nominal readings to release */
  if (engine->emergency_latched) {
    if (anomaly->is_short_circuit || anomaly->is_emergency_direct ||
        anomaly->active_count > 0) {
      engine->emergency_recovery_counter = 0;
      engine->emergency_count++;
      return STATE_EMERGENCY;
    }

    engine->emergency_recovery_counter++;
    if (engine->emergency_recovery_counter >= engine->emergency_recovery_limit) {
      engine->emergency_latched = false;
      engine->emergency_recovery_counter = 0;
      engine->current_state = STATE_NORMAL;
      engine->deescalation_counter = 0;
      engine->critical_countdown = 0;
      return STATE_NORMAL;
    }

    engine->emergency_count++;
    return STATE_EMERGENCY;
  }

  /* --- Immediate EMERGENCY triggers --- */

  /* Short circuit = immediate emergency, no waiting */
  if (anomaly->is_short_circuit) {
    engine->current_state = STATE_EMERGENCY;
    engine->emergency_latched = true;
    engine->emergency_recovery_counter = 0;
    engine->emergency_count++;
    return STATE_EMERGENCY;
  }

  /* Emergency direct bypass (spec §4.3):
   * Physics-based limits (T>80°C, dT/dt>5°C/min, current spike) */
  if (anomaly->is_emergency_direct) {
    engine->current_state = STATE_EMERGENCY;
    engine->emergency_latched = true;
    engine->emergency_recovery_counter = 0;
    engine->emergency_count++;
    return STATE_EMERGENCY;
  }

  /* 3 or more categories active = immediate emergency */
  if (anomaly->active_count >= 3) {
    engine->current_state = STATE_EMERGENCY;
    engine->emergency_latched = true;
    engine->emergency_recovery_counter = 0;
    engine->emergency_count++;
    return STATE_EMERGENCY;
  }

  /* --- 2 categories: CRITICAL with countdown --- */

  if (anomaly->active_count >= 2) {
    if (engine->current_state != STATE_CRITICAL) {
      engine->current_state = STATE_CRITICAL;
      engine->critical_countdown = 0;
    }

    engine->critical_countdown++;
    engine->critical_count++;
    engine->deescalation_counter = 0;

    if (engine->critical_countdown >= engine->critical_countdown_limit) {
      engine->current_state = STATE_EMERGENCY;
      engine->emergency_latched = true;
      engine->emergency_recovery_counter = 0;
      engine->emergency_count++;
      return STATE_EMERGENCY;
    }

    return STATE_CRITICAL;
  }

  /* --- 1 category: WARNING --- */

  if (anomaly->active_count == 1) {
    engine->current_state = STATE_WARNING;
    engine->critical_countdown = 0;
    engine->deescalation_counter = 0;
    engine->warning_count++;
    return STATE_WARNING;
  }

  /* --- 0 categories: try to de-escalate --- */

  if (engine->current_state != STATE_NORMAL) {
    engine->deescalation_counter++;

    if (engine->deescalation_counter >= engine->deescalation_limit) {
      engine->current_state = STATE_NORMAL;
      engine->deescalation_counter = 0;
    }
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
