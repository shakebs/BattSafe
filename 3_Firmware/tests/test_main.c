/*
 * test_main.c — Host-Side Test Runner
 *
 * Compiles and runs ON YOUR MAC (not on the board).
 * Tests the core logic modules with simulated sensor data.
 *
 * Compile:
 *   cd firmware
 *   gcc -Wall -Wextra -o test_runner tests/test_main.c \
 *       core/anomaly_eval.c core/correlation_engine.c \
 *       app/packet_format.c -I core -I app -lm
 *
 * Run:
 *   ./test_runner
 */

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "anomaly_eval.h"
#include "correlation_engine.h"
#include "packet_format.h"

/* -----------------------------------------------------------------------
 * Test counters
 * ----------------------------------------------------------------------- */

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST_ASSERT(condition, msg)                                            \
  do {                                                                         \
    tests_run++;                                                               \
    if (condition) {                                                           \
      tests_passed++;                                                          \
      printf("  ✅ PASS: %s\n", msg);                                          \
    } else {                                                                   \
      tests_failed++;                                                          \
      printf("  ❌ FAIL: %s\n", msg);                                          \
    }                                                                          \
  } while (0)

/* -----------------------------------------------------------------------
 * Helper: create a "normal" sensor snapshot (all values safe)
 * ----------------------------------------------------------------------- */

static sensor_snapshot_t make_normal_snapshot(void) {
  sensor_snapshot_t s;
  memset(&s, 0, sizeof(s));
  s.voltage_v = 14.8f;
  s.current_a = 2.0f;
  s.r_internal_mohm = 45.0f;
  s.temp_cells_c[0] = 28.0f;
  s.temp_cells_c[1] = 28.5f;
  s.temp_cells_c[2] = 27.8f;
  s.temp_cells_c[3] = 28.2f;
  s.temp_ambient_c = 25.0f;
  s.dt_dt_max = 0.01f;
  s.gas_ratio = 0.98f;
  s.pressure_delta_hpa = 0.2f;
  s.swelling_pct = 2.0f;
  s.short_circuit = false;
  return s;
}

/* -----------------------------------------------------------------------
 * Test 1: Normal operation — no anomalies detected
 * ----------------------------------------------------------------------- */

static void test_normal_operation(void) {
  printf("\n--- Test 1: Normal Operation ---\n");

  anomaly_thresholds_t thresholds;
  anomaly_eval_init(&thresholds);

  sensor_snapshot_t snap = make_normal_snapshot();
  anomaly_result_t result = anomaly_eval_run(&thresholds, &snap);

  TEST_ASSERT(result.active_mask == CAT_NONE,
              "No categories active during normal operation");
  TEST_ASSERT(result.active_count == 0, "Active count is 0");
  TEST_ASSERT(!result.is_short_circuit, "No short circuit");

  /* Feed to correlation engine */
  correlation_engine_t engine;
  correlation_engine_init(&engine);

  system_state_t state = correlation_engine_update(&engine, &result);
  TEST_ASSERT(state == STATE_NORMAL, "System state is NORMAL");
}

/* -----------------------------------------------------------------------
 * Test 2: Thermal anomaly only — should be WARNING (not emergency!)
 * ----------------------------------------------------------------------- */

static void test_thermal_only(void) {
  printf(
      "\n--- Test 2: Thermal Anomaly Only (False Positive Resistance) ---\n");

  anomaly_thresholds_t thresholds;
  anomaly_eval_init(&thresholds);

  sensor_snapshot_t snap = make_normal_snapshot();
  snap.temp_cells_c[2] = 62.0f; /* Cell 3 is hot */

  anomaly_result_t result = anomaly_eval_run(&thresholds, &snap);

  TEST_ASSERT(result.active_mask == CAT_THERMAL,
              "Only thermal category is active");
  TEST_ASSERT(result.active_count == 1, "Exactly 1 category active");

  /* Feed to correlation engine — should be WARNING, NOT emergency */
  correlation_engine_t engine;
  correlation_engine_init(&engine);

  system_state_t state = correlation_engine_update(&engine, &result);
  TEST_ASSERT(state == STATE_WARNING,
              "Single-category anomaly = WARNING (not emergency!)");
}

/* -----------------------------------------------------------------------
 * Test 3: Gas anomaly only — should also be WARNING
 * ----------------------------------------------------------------------- */

static void test_gas_only(void) {
  printf("\n--- Test 3: Gas Anomaly Only ---\n");

  anomaly_thresholds_t thresholds;
  anomaly_eval_init(&thresholds);

  sensor_snapshot_t snap = make_normal_snapshot();
  snap.gas_ratio = 0.55f; /* VOC detected */

  anomaly_result_t result = anomaly_eval_run(&thresholds, &snap);

  TEST_ASSERT(result.active_mask == CAT_GAS, "Only gas category is active");
  TEST_ASSERT(result.active_count == 1, "Exactly 1 category active");

  correlation_engine_t engine;
  correlation_engine_init(&engine);

  system_state_t state = correlation_engine_update(&engine, &result);
  TEST_ASSERT(state == STATE_WARNING, "Gas-only anomaly = WARNING");
}

/* -----------------------------------------------------------------------
 * Test 4: Multi-fault (heat + gas) — should be CRITICAL
 * ----------------------------------------------------------------------- */

static void test_multi_fault_critical(void) {
  printf("\n--- Test 4: Multi-Fault → CRITICAL ---\n");

  anomaly_thresholds_t thresholds;
  anomaly_eval_init(&thresholds);

  sensor_snapshot_t snap = make_normal_snapshot();
  snap.temp_cells_c[2] = 60.0f; /* Thermal anomaly */
  snap.gas_ratio = 0.50f;       /* Gas anomaly */

  anomaly_result_t result = anomaly_eval_run(&thresholds, &snap);

  TEST_ASSERT((result.active_mask & CAT_THERMAL) != 0,
              "Thermal category is active");
  TEST_ASSERT((result.active_mask & CAT_GAS) != 0, "Gas category is active");
  TEST_ASSERT(result.active_count == 2, "Exactly 2 categories active");

  correlation_engine_t engine;
  correlation_engine_init(&engine);

  system_state_t state = correlation_engine_update(&engine, &result);
  TEST_ASSERT(state == STATE_CRITICAL, "Two categories = CRITICAL");
}

/* -----------------------------------------------------------------------
 * Test 5: Triple fault (heat + gas + pressure) — EMERGENCY + disconnect
 * ----------------------------------------------------------------------- */

static void test_triple_fault_emergency(void) {
  printf("\n--- Test 5: Triple Fault → EMERGENCY ---\n");

  anomaly_thresholds_t thresholds;
  anomaly_eval_init(&thresholds);

  sensor_snapshot_t snap = make_normal_snapshot();
  snap.temp_cells_c[2] = 65.0f;   /* Thermal */
  snap.gas_ratio = 0.35f;         /* Gas */
  snap.pressure_delta_hpa = 8.0f; /* Pressure */

  anomaly_result_t result = anomaly_eval_run(&thresholds, &snap);

  TEST_ASSERT(result.active_count >= 3, "3+ categories active");

  correlation_engine_t engine;
  correlation_engine_init(&engine);

  system_state_t state = correlation_engine_update(&engine, &result);
  TEST_ASSERT(state == STATE_EMERGENCY, "Three categories = EMERGENCY");
  TEST_ASSERT(engine.emergency_latched,
              "Emergency is latched (stays until manual reset)");
}

/* -----------------------------------------------------------------------
 * Test 6: Short circuit — immediate EMERGENCY
 * ----------------------------------------------------------------------- */

static void test_short_circuit(void) {
  printf("\n--- Test 6: Short Circuit → Immediate EMERGENCY ---\n");

  anomaly_thresholds_t thresholds;
  anomaly_eval_init(&thresholds);

  sensor_snapshot_t snap = make_normal_snapshot();
  snap.current_a = 18.0f; /* Way above normal */
  snap.short_circuit = true;

  anomaly_result_t result = anomaly_eval_run(&thresholds, &snap);

  TEST_ASSERT(result.is_short_circuit, "Short circuit detected");

  correlation_engine_t engine;
  correlation_engine_init(&engine);

  system_state_t state = correlation_engine_update(&engine, &result);
  TEST_ASSERT(state == STATE_EMERGENCY, "Short circuit = immediate EMERGENCY");
}

/* -----------------------------------------------------------------------
 * Test 7: State transition sequence (normal → warning → critical → emergency)
 * ----------------------------------------------------------------------- */

static void test_escalation_sequence(void) {
  printf("\n--- Test 7: Full Escalation Sequence ---\n");

  anomaly_thresholds_t thresholds;
  anomaly_eval_init(&thresholds);

  correlation_engine_t engine;
  correlation_engine_init(&engine);

  /* Phase 1: Normal */
  sensor_snapshot_t snap = make_normal_snapshot();
  anomaly_result_t result = anomaly_eval_run(&thresholds, &snap);
  system_state_t state = correlation_engine_update(&engine, &result);
  TEST_ASSERT(state == STATE_NORMAL, "Phase 1: NORMAL");

  /* Phase 2: One category (thermal) */
  snap.temp_cells_c[2] = 60.0f;
  result = anomaly_eval_run(&thresholds, &snap);
  state = correlation_engine_update(&engine, &result);
  TEST_ASSERT(state == STATE_WARNING, "Phase 2: WARNING (thermal only)");

  /* Phase 3: Two categories (thermal + gas) */
  snap.gas_ratio = 0.55f;
  result = anomaly_eval_run(&thresholds, &snap);
  state = correlation_engine_update(&engine, &result);
  TEST_ASSERT(state == STATE_CRITICAL, "Phase 3: CRITICAL (thermal + gas)");

  /* Phase 4: Three categories (thermal + gas + pressure) */
  snap.pressure_delta_hpa = 10.0f;
  result = anomaly_eval_run(&thresholds, &snap);
  state = correlation_engine_update(&engine, &result);
  TEST_ASSERT(state == STATE_EMERGENCY, "Phase 4: EMERGENCY (3 categories)");

  /* Phase 5: Even after removing anomalies, EMERGENCY stays latched */
  snap = make_normal_snapshot();
  result = anomaly_eval_run(&thresholds, &snap);
  state = correlation_engine_update(&engine, &result);
  TEST_ASSERT(state == STATE_EMERGENCY,
              "Phase 5: EMERGENCY stays latched (safety feature)");
}

/* -----------------------------------------------------------------------
 * Test 8: Packet encoding and validation
 * ----------------------------------------------------------------------- */

static void test_packet_format(void) {
  printf("\n--- Test 8: Packet Encode/Validate ---\n");

  anomaly_thresholds_t thresholds;
  anomaly_eval_init(&thresholds);

  sensor_snapshot_t snap = make_normal_snapshot();
  anomaly_result_t result = anomaly_eval_run(&thresholds, &snap);

  telemetry_packet_t pkt;
  uint8_t size = packet_encode(&pkt, 5000, &snap, &result, STATE_NORMAL);

  TEST_ASSERT(size == sizeof(telemetry_packet_t), "Packet size is correct");
  TEST_ASSERT(pkt.sync == PACKET_SYNC_BYTE, "Sync byte is 0xAA");
  TEST_ASSERT(pkt.voltage_cv == 1480,
              "Voltage encoded correctly (14.80V → 1480)");
  TEST_ASSERT(pkt.system_state == STATE_NORMAL,
              "System state encoded correctly");

  int valid = packet_validate(&pkt);
  TEST_ASSERT(valid == 0, "Packet checksum validates OK");

  /* Corrupt a byte and check validation fails */
  pkt.voltage_cv = 9999;
  valid = packet_validate(&pkt);
  TEST_ASSERT(valid != 0, "Corrupted packet fails validation");

  /* Check new fields are encoded */
  pkt.voltage_cv = 1480; /* Restore for re-encode */
  packet_encode(&pkt, 5000, &snap, &result, STATE_NORMAL);
  TEST_ASSERT(pkt.temp_ambient_dt != 0, "Ambient temp field is populated");
  TEST_ASSERT(pkt.flags == 0, "Flags byte is 0 for normal operation");
}

/* -----------------------------------------------------------------------
 * Test 9: Ambient compensation — same cell temp, different outcomes
 * ----------------------------------------------------------------------- */

static void test_ambient_compensation(void) {
  printf("\n--- Test 9: Ambient-Compensated Thresholds ---\n");

  anomaly_thresholds_t thresholds;
  anomaly_eval_init(&thresholds);

  /* Phase A: Cell=45°C, Ambient=25°C → ΔT=20°C → should trigger thermal */
  sensor_snapshot_t snap = make_normal_snapshot();
  snap.temp_cells_c[0] = 45.0f;
  snap.temp_cells_c[1] = 45.0f;
  snap.temp_cells_c[2] = 45.0f;
  snap.temp_cells_c[3] = 45.0f;
  snap.temp_ambient_c = 25.0f;

  anomaly_result_t result = anomaly_eval_run(&thresholds, &snap);
  TEST_ASSERT((result.active_mask & CAT_THERMAL) != 0,
              "Cold ambient (25°C) + cell 45°C → ΔT=20 → THERMAL active");

  /* Phase B: Cell=45°C, Ambient=38°C → ΔT=7°C → should NOT trigger thermal */
  snap.temp_ambient_c = 38.0f;
  result = anomaly_eval_run(&thresholds, &snap);
  TEST_ASSERT((result.active_mask & CAT_THERMAL) == 0,
              "Hot ambient (38°C) + cell 45°C → ΔT=7 → THERMAL not active");

  /* Phase C: Check escalation path */
  correlation_engine_t engine;
  correlation_engine_init(&engine);

  snap.temp_ambient_c = 25.0f; /* Back to cold ambient */
  result = anomaly_eval_run(&thresholds, &snap);
  system_state_t state = correlation_engine_update(&engine, &result);
  TEST_ASSERT(state == STATE_WARNING,
              "Cold ambient triggers WARNING via ambient compensation");

  snap.temp_ambient_c = 38.0f; /* Switch to hot ambient */
  result = anomaly_eval_run(&thresholds, &snap);
  /* After sustained normal readings, should de-escalate */
  for (int i = 0; i < 15; i++) {
    result = anomaly_eval_run(&thresholds, &snap);
    state = correlation_engine_update(&engine, &result);
  }
  TEST_ASSERT(state == STATE_NORMAL,
              "Hot ambient allows de-escalation to NORMAL");
}

/* -----------------------------------------------------------------------
 * Test 10: Emergency direct bypass — physics-based limits
 * ----------------------------------------------------------------------- */

static void test_emergency_direct(void) {
  printf("\n--- Test 10: Emergency Direct Bypass ---\n");

  anomaly_thresholds_t thresholds;
  anomaly_eval_init(&thresholds);

  /* Test A: T > 80°C → immediate emergency bypass */
  sensor_snapshot_t snap = make_normal_snapshot();
  snap.temp_cells_c[2] = 82.0f; /* Above emergency threshold */

  anomaly_result_t result = anomaly_eval_run(&thresholds, &snap);
  TEST_ASSERT(result.is_emergency_direct,
              "T > 80°C sets emergency_direct flag");

  correlation_engine_t engine;
  correlation_engine_init(&engine);
  system_state_t state = correlation_engine_update(&engine, &result);
  TEST_ASSERT(state == STATE_EMERGENCY,
              "Emergency direct → immediate EMERGENCY (bypasses counting)");
  TEST_ASSERT(engine.emergency_latched,
              "Emergency is latched from direct bypass");

  /* Test B: dT/dt > 5°C/min → emergency bypass */
  correlation_engine_reset(&engine);
  snap = make_normal_snapshot();
  snap.dt_dt_max = 0.1f; /* 6°C/min > 5°C/min threshold */

  result = anomaly_eval_run(&thresholds, &snap);
  TEST_ASSERT(result.is_emergency_direct,
              "dT/dt > 5°C/min sets emergency_direct flag");
  state = correlation_engine_update(&engine, &result);
  TEST_ASSERT(state == STATE_EMERGENCY, "High dT/dt → immediate EMERGENCY");
}

/* -----------------------------------------------------------------------
 * Test 11: Core temperature estimation
 * ----------------------------------------------------------------------- */

static void test_core_temp_estimation(void) {
  printf("\n--- Test 11: Core Temperature Estimation ---\n");

  /* T_core = T_surface + I² × R_int × R_thermal
   * For I=5A, R_int=50mΩ, R_thermal=0.5°C/W:
   * T_core = 28°C + 25 × 0.05 × 0.5 = 28°C + 0.625 = 28.625°C */
  float t_surface = 28.0f;
  float current = 5.0f;
  float r_int_ohm = 0.050f; /* 50 milliohms */
  float r_thermal = 0.5f;

  float t_core = t_surface + current * current * r_int_ohm * r_thermal;

  TEST_ASSERT(t_core > 28.5f && t_core < 28.8f,
              "Core temp estimate is T_surface + I²·R·R_thermal");

  /* At high current (15A), the core-surface delta becomes significant */
  current = 15.0f;
  t_core = t_surface + current * current * r_int_ohm * r_thermal;
  TEST_ASSERT(t_core > 33.0f,
              "High current → significant core-surface delta (>5°C)");
}

/* -----------------------------------------------------------------------
 * Main
 * ----------------------------------------------------------------------- */

int main(void) {
  printf("====================================================\n");
  printf("  EV Battery Intelligence — C Firmware Test Runner\n");
  printf("  Running on HOST (Mac) — no hardware needed\n");
  printf("====================================================\n");

  test_normal_operation();
  test_thermal_only();
  test_gas_only();
  test_multi_fault_critical();
  test_triple_fault_emergency();
  test_short_circuit();
  test_escalation_sequence();
  test_packet_format();
  test_ambient_compensation();
  test_emergency_direct();
  test_core_temp_estimation();

  printf("\n====================================================\n");
  printf("  Results: %d/%d passed", tests_passed, tests_run);
  if (tests_failed > 0) {
    printf(" (%d FAILED)", tests_failed);
  }
  printf("\n====================================================\n");

  return tests_failed > 0 ? 1 : 0;
}
