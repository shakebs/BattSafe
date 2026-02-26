/*
 * test_main.c — Host-Side Test Runner (Full Pack Edition)
 *
 * Tests the core logic modules with 104S8P battery pack data.
 * Compiles and runs ON YOUR PC (not on the board).
 *
 * Compile:
 *   cd 3_Firmware
 *   gcc -Wall -Wextra -o test_runner tests/test_main.c \
 *       src/anomaly_eval.c src/correlation_engine.c \
 *       src/packet_format.c -I src -lm
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
 * Helper: create a "normal" snapshot (all 139 channels safe)
 * ----------------------------------------------------------------------- */

static sensor_snapshot_t make_normal_snapshot(void) {
  sensor_snapshot_t s;
  memset(&s, 0, sizeof(s));

  /* Electrical — full pack */
  s.pack_voltage_v = 332.8f; /* 104 × 3.2V */
  s.pack_current_a = 60.0f;  /* 0.5C = 60A */
  s.r_internal_mohm = 0.44f; /* 3.5mΩ/8 cells */

  /* 8 modules, each with 13 groups */
  for (int m = 0; m < NUM_MODULES; m++) {
    s.modules[m].ntc1_c = 28.0f + (float)m * 0.3f;
    s.modules[m].ntc2_c = 28.2f + (float)m * 0.3f;
    s.modules[m].swelling_pct = 0.5f;
    s.modules[m].max_dt_dt = 0.01f;
    for (int g = 0; g < GROUPS_PER_MODULE; g++) {
      s.modules[m].group_voltages_v[g] = 3.20f;
    }
  }

  /* Environment */
  s.temp_ambient_c = 25.0f;
  s.coolant_inlet_c = 25.0f;
  s.coolant_outlet_c = 27.0f;
  s.gas_ratio_1 = 0.98f;
  s.gas_ratio_2 = 0.97f;
  s.pressure_delta_1_hpa = 0.1f;
  s.pressure_delta_2_hpa = 0.1f;
  s.humidity_pct = 50.0f;
  s.isolation_mohm = 500.0f;
  s.short_circuit = false;

  return s;
}

/* Helper: compute derived fields */
static void compute_snapshot(sensor_snapshot_t *s) {
  anomaly_thresholds_t t;
  anomaly_eval_init(&t);
  anomaly_eval_compute(s, &t);
}

/* -----------------------------------------------------------------------
 * Test 1: Normal operation — no anomalies detected
 * ----------------------------------------------------------------------- */

static void test_normal_operation(void) {
  printf("\n--- Test 1: Normal Full-Pack Operation ---\n");

  anomaly_thresholds_t thresholds;
  anomaly_eval_init(&thresholds);

  sensor_snapshot_t snap = make_normal_snapshot();
  compute_snapshot(&snap);
  anomaly_result_t result = anomaly_eval_run(&thresholds, &snap);

  TEST_ASSERT(result.active_mask == CAT_NONE,
              "No categories active during normal operation");
  TEST_ASSERT(result.active_count == 0, "Active count is 0");
  TEST_ASSERT(!result.is_short_circuit, "No short circuit");
  TEST_ASSERT(result.cascade_stage == 0, "Cascade stage = Normal");
  TEST_ASSERT(result.risk_factor < 0.01f, "Risk factor ~0");

  /* Feed to correlation engine */
  correlation_engine_t engine;
  correlation_engine_init(&engine);

  system_state_t state = correlation_engine_update(&engine, &result);
  TEST_ASSERT(state == STATE_NORMAL, "System state is NORMAL");
}

/* -----------------------------------------------------------------------
 * Test 2: Single module thermal anomaly — should be WARNING
 * ----------------------------------------------------------------------- */

static void test_thermal_single_module(void) {
  printf("\n--- Test 2: Thermal Anomaly in Module 3 Only ---\n");

  anomaly_thresholds_t thresholds;
  anomaly_eval_init(&thresholds);

  sensor_snapshot_t snap = make_normal_snapshot();
  /* Module 3 (index 2) has hot NTCs */
  snap.modules[2].ntc1_c = 62.0f;
  snap.modules[2].ntc2_c = 58.0f;

  compute_snapshot(&snap);
  anomaly_result_t result = anomaly_eval_run(&thresholds, &snap);

  TEST_ASSERT((result.active_mask & CAT_THERMAL) != 0,
              "Thermal category is active");
  TEST_ASSERT(result.active_count == 1, "Exactly 1 category active");
  TEST_ASSERT(result.hotspot_module == 3, "Hotspot identified as Module 3");
  TEST_ASSERT((result.anomaly_modules_mask & (1 << 2)) != 0,
              "Module 3 flagged in anomaly mask");

  /* Correlation engine */
  correlation_engine_t engine;
  correlation_engine_init(&engine);

  system_state_t state = correlation_engine_update(&engine, &result);
  TEST_ASSERT(state == STATE_WARNING,
              "Single-category anomaly = WARNING (not emergency!)");
}

/* -----------------------------------------------------------------------
 * Test 3: Gas anomaly only — should be WARNING
 * ----------------------------------------------------------------------- */

static void test_gas_only(void) {
  printf("\n--- Test 3: Gas Anomaly Only ---\n");

  anomaly_thresholds_t thresholds;
  anomaly_eval_init(&thresholds);

  sensor_snapshot_t snap = make_normal_snapshot();
  snap.gas_ratio_1 = 0.55f; /* VOC detected */
  snap.gas_ratio_2 = 0.60f; /* Slightly less on sensor 2 */

  compute_snapshot(&snap);
  anomaly_result_t result = anomaly_eval_run(&thresholds, &snap);

  TEST_ASSERT((result.active_mask & CAT_GAS) != 0, "Gas category is active");
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
  snap.modules[4].ntc1_c = 60.0f; /* Module 5 thermal */
  snap.gas_ratio_1 = 0.50f;       /* Gas anomaly */

  compute_snapshot(&snap);
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
 * Test 5: Triple fault (heat + gas + pressure) — EMERGENCY
 * ----------------------------------------------------------------------- */

static void test_triple_fault_emergency(void) {
  printf("\n--- Test 5: Triple Fault → EMERGENCY ---\n");

  anomaly_thresholds_t thresholds;
  anomaly_eval_init(&thresholds);

  sensor_snapshot_t snap = make_normal_snapshot();
  snap.modules[4].ntc1_c = 65.0f;   /* Module 5 thermal */
  snap.gas_ratio_1 = 0.35f;         /* Gas */
  snap.pressure_delta_1_hpa = 8.0f; /* Pressure */

  compute_snapshot(&snap);
  anomaly_result_t result = anomaly_eval_run(&thresholds, &snap);

  TEST_ASSERT(result.active_count >= 3, "3+ categories active");

  correlation_engine_t engine;
  correlation_engine_init(&engine);

  system_state_t state = correlation_engine_update(&engine, &result);
  TEST_ASSERT(state == STATE_EMERGENCY, "Three categories = EMERGENCY");
  TEST_ASSERT(engine.emergency_latched,
              "Emergency latch engages on EMERGENCY");
}

/* -----------------------------------------------------------------------
 * Test 6: Short circuit — immediate EMERGENCY
 * ----------------------------------------------------------------------- */

static void test_short_circuit(void) {
  printf("\n--- Test 6: Short Circuit → Immediate EMERGENCY ---\n");

  anomaly_thresholds_t thresholds;
  anomaly_eval_init(&thresholds);

  sensor_snapshot_t snap = make_normal_snapshot();
  snap.pack_current_a = 400.0f; /* Way above threshold */
  snap.short_circuit = true;

  compute_snapshot(&snap);
  anomaly_result_t result = anomaly_eval_run(&thresholds, &snap);

  TEST_ASSERT(result.is_short_circuit, "Short circuit detected");

  correlation_engine_t engine;
  correlation_engine_init(&engine);

  system_state_t state = correlation_engine_update(&engine, &result);
  TEST_ASSERT(state == STATE_EMERGENCY, "Short circuit = immediate EMERGENCY");
}

/* -----------------------------------------------------------------------
 * Test 7: State transition sequence
 * ----------------------------------------------------------------------- */

static void test_escalation_sequence(void) {
  printf("\n--- Test 7: Full Escalation Sequence ---\n");

  anomaly_thresholds_t thresholds;
  anomaly_eval_init(&thresholds);

  correlation_engine_t engine;
  correlation_engine_init(&engine);

  /* Phase 1: Normal */
  sensor_snapshot_t snap = make_normal_snapshot();
  compute_snapshot(&snap);
  anomaly_result_t result = anomaly_eval_run(&thresholds, &snap);
  system_state_t state = correlation_engine_update(&engine, &result);
  TEST_ASSERT(state == STATE_NORMAL, "Phase 1: NORMAL");

  /* Phase 2: One category (thermal in Module 6) */
  snap.modules[5].ntc1_c = 60.0f;
  compute_snapshot(&snap);
  result = anomaly_eval_run(&thresholds, &snap);
  state = correlation_engine_update(&engine, &result);
  TEST_ASSERT(state == STATE_WARNING, "Phase 2: WARNING (thermal M6 only)");

  /* Phase 3: Two categories (thermal + gas) */
  snap.gas_ratio_1 = 0.55f;
  compute_snapshot(&snap);
  result = anomaly_eval_run(&thresholds, &snap);
  state = correlation_engine_update(&engine, &result);
  TEST_ASSERT(state == STATE_CRITICAL, "Phase 3: CRITICAL (thermal + gas)");

  /* Phase 4: Three categories (thermal + gas + pressure) */
  snap.pressure_delta_1_hpa = 6.0f;
  compute_snapshot(&snap);
  result = anomaly_eval_run(&thresholds, &snap);
  state = correlation_engine_update(&engine, &result);
  TEST_ASSERT(state == STATE_EMERGENCY, "Phase 4: EMERGENCY (3 categories)");

  /* Phase 5: after sustained nominal input, EMERGENCY auto-recovers */
  snap = make_normal_snapshot();
  for (int i = 0; i < (int)engine.emergency_recovery_limit + 1; i++) {
    compute_snapshot(&snap);
    result = anomaly_eval_run(&thresholds, &snap);
    state = correlation_engine_update(&engine, &result);
  }
  TEST_ASSERT(state == STATE_NORMAL,
              "Phase 5: returns to NORMAL after nominal recovery window");
}

/* -----------------------------------------------------------------------
 * Test 8: Packet encoding and validation
 * ----------------------------------------------------------------------- */

static void test_packet_format(void) {
  printf("\n--- Test 8: Multi-Frame Packet Encode/Validate ---\n");

  anomaly_thresholds_t thresholds;
  anomaly_eval_init(&thresholds);

  sensor_snapshot_t snap = make_normal_snapshot();
  compute_snapshot(&snap);
  anomaly_result_t result = anomaly_eval_run(&thresholds, &snap);

  /* Test pack frame */
  telemetry_pack_frame_t pkt;
  uint8_t size = packet_encode_pack(&pkt, 5000, &snap, &result, STATE_NORMAL);

  TEST_ASSERT(size == sizeof(telemetry_pack_frame_t),
              "Pack frame size correct");
  TEST_ASSERT(pkt.sync == PACKET_SYNC_BYTE, "Sync byte is 0xAA");
  TEST_ASSERT(pkt.frame_type == PACKET_TYPE_PACK, "Frame type is PACK");
  TEST_ASSERT(pkt.pack_voltage_dv == 3328,
              "Pack voltage encoded correctly (332.8V → 3328)");
  TEST_ASSERT(pkt.system_state == STATE_NORMAL, "System state = NORMAL");
  TEST_ASSERT(pkt.cascade_stage == 0, "Cascade stage = 0 (Normal)");

  int valid = packet_validate_pack(&pkt);
  TEST_ASSERT(valid == 0, "Pack frame checksum validates OK");

  /* Corrupt and check validation fails */
  pkt.pack_voltage_dv = 9999;
  valid = packet_validate_pack(&pkt);
  TEST_ASSERT(valid != 0, "Corrupted frame fails validation");

  /* Test module frame */
  telemetry_module_frame_t mod_pkt;
  uint8_t msize = packet_encode_module(&mod_pkt, 3, &snap);

  TEST_ASSERT(msize == sizeof(telemetry_module_frame_t),
              "Module frame size correct");
  TEST_ASSERT(mod_pkt.sync == PACKET_SYNC_BYTE, "Module sync byte is 0xAA");
  TEST_ASSERT(mod_pkt.frame_type == PACKET_TYPE_MODULE, "Frame type is MODULE");
  TEST_ASSERT(mod_pkt.module_index == 3, "Module index = 3");
}

/* -----------------------------------------------------------------------
 * Test 9: Ambient compensation — same cell temp, different outcomes
 * ----------------------------------------------------------------------- */

static void test_ambient_compensation(void) {
  printf("\n--- Test 9: Ambient-Compensated Thresholds (16 NTCs) ---\n");

  anomaly_thresholds_t thresholds;
  anomaly_eval_init(&thresholds);

  /* Phase A: All modules at 45°C, Ambient=25°C → ΔT=20°C → should trigger */
  sensor_snapshot_t snap = make_normal_snapshot();
  for (int m = 0; m < NUM_MODULES; m++) {
    snap.modules[m].ntc1_c = 45.0f;
    snap.modules[m].ntc2_c = 45.0f;
  }
  snap.temp_ambient_c = 25.0f;

  compute_snapshot(&snap);
  anomaly_result_t result = anomaly_eval_run(&thresholds, &snap);
  TEST_ASSERT((result.active_mask & CAT_THERMAL) != 0,
              "Cold ambient (25°C) + NTCs 45°C → ΔT=20 → THERMAL active");

  /* Phase B: Same temps, Ambient=38°C → ΔT=7°C → NOT triggered */
  snap.temp_ambient_c = 38.0f;
  compute_snapshot(&snap);
  result = anomaly_eval_run(&thresholds, &snap);
  TEST_ASSERT((result.active_mask & CAT_THERMAL) == 0,
              "Hot ambient (38°C) + NTCs 45°C → ΔT=7 → THERMAL not active");

  /* Phase C: Check de-escalation */
  correlation_engine_t engine;
  correlation_engine_init(&engine);

  snap.temp_ambient_c = 25.0f;
  compute_snapshot(&snap);
  result = anomaly_eval_run(&thresholds, &snap);
  system_state_t state = correlation_engine_update(&engine, &result);
  TEST_ASSERT(state == STATE_WARNING,
              "Cold ambient triggers WARNING via ambient compensation");

  snap.temp_ambient_c = 38.0f;
  compute_snapshot(&snap);
  result = anomaly_eval_run(&thresholds, &snap);
  for (int i = 0; i < 15; i++) {
    compute_snapshot(&snap);
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
  snap.modules[2].ntc1_c = 82.0f;

  compute_snapshot(&snap);
  anomaly_result_t result = anomaly_eval_run(&thresholds, &snap);
  TEST_ASSERT(result.is_emergency_direct,
              "T > 80°C sets emergency_direct flag");

  correlation_engine_t engine;
  correlation_engine_init(&engine);
  system_state_t state = correlation_engine_update(&engine, &result);
  TEST_ASSERT(state == STATE_EMERGENCY,
              "Emergency direct → immediate EMERGENCY");
  TEST_ASSERT(engine.emergency_latched, "Emergency is latched from direct");

  /* Test B: dT/dt > 5°C/min → emergency bypass */
  correlation_engine_reset(&engine);
  snap = make_normal_snapshot();
  snap.modules[0].max_dt_dt = 6.0f; /* > 5°C/min threshold */
  snap.dt_dt_max = 6.0f;

  compute_snapshot(&snap);
  result = anomaly_eval_run(&thresholds, &snap);
  TEST_ASSERT(result.is_emergency_direct,
              "dT/dt > 5°C/min sets emergency_direct flag");
  state = correlation_engine_update(&engine, &result);
  TEST_ASSERT(state == STATE_EMERGENCY, "High dT/dt → immediate EMERGENCY");
}

/* -----------------------------------------------------------------------
 * Test 11: Inter-module thermal gradient detection
 * ----------------------------------------------------------------------- */

static void test_inter_module_gradient(void) {
  printf("\n--- Test 11: Inter-Module Thermal Gradient ---\n");

  anomaly_thresholds_t thresholds;
  anomaly_eval_init(&thresholds);

  sensor_snapshot_t snap = make_normal_snapshot();

  /* Module 5 is 8°C hotter than all others → inter-module ΔT > 5°C */
  snap.modules[4].ntc1_c = 36.0f;
  snap.modules[4].ntc2_c = 37.0f;

  compute_snapshot(&snap);
  anomaly_result_t result = anomaly_eval_run(&thresholds, &snap);

  /* The temp spread should be ~8°C (36 vs 28) */
  TEST_ASSERT(snap.temp_spread_c > 5.0f, "Temperature spread > 5°C detected");
  TEST_ASSERT((result.active_mask & CAT_THERMAL) != 0,
              "Inter-module gradient triggers THERMAL");
  TEST_ASSERT(result.hotspot_module == 5,
              "Hotspot correctly identified as Module 5");
}

/* -----------------------------------------------------------------------
 * Test 12: Intra-module NTC delta detection
 * ----------------------------------------------------------------------- */

static void test_intra_module_delta(void) {
  printf("\n--- Test 12: Intra-Module NTC Delta ---\n");

  anomaly_thresholds_t thresholds;
  anomaly_eval_init(&thresholds);

  sensor_snapshot_t snap = make_normal_snapshot();

  /* Module 2: large internal gradient (4°C > 3°C threshold) */
  snap.modules[1].ntc1_c = 32.0f;
  snap.modules[1].ntc2_c = 28.0f;

  compute_snapshot(&snap);
  anomaly_result_t result = anomaly_eval_run(&thresholds, &snap);

  TEST_ASSERT(snap.modules[1].delta_t_intra > 3.0f,
              "Intra-module ΔT > 3°C computed");
  TEST_ASSERT((result.active_mask & CAT_THERMAL) != 0,
              "Intra-module gradient triggers THERMAL");
  TEST_ASSERT((result.anomaly_modules_mask & (1 << 1)) != 0,
              "Module 2 flagged in anomaly mask");
}

/* -----------------------------------------------------------------------
 * Test 13: Per-module swelling detection
 * ----------------------------------------------------------------------- */

static void test_per_module_swelling(void) {
  printf("\n--- Test 13: Per-Module Swelling Detection ---\n");

  anomaly_thresholds_t thresholds;
  anomaly_eval_init(&thresholds);

  sensor_snapshot_t snap = make_normal_snapshot();

  /* Module 7 swelling above threshold */
  snap.modules[6].swelling_pct = 5.0f; /* > 3% warning threshold */

  compute_snapshot(&snap);
  anomaly_result_t result = anomaly_eval_run(&thresholds, &snap);

  TEST_ASSERT((result.active_mask & CAT_SWELLING) != 0,
              "Swelling category active for Module 7");
  TEST_ASSERT((result.anomaly_modules_mask & (1 << 6)) != 0,
              "Module 7 flagged in anomaly mask");
}

/* -----------------------------------------------------------------------
 * Test 14: Dual gas sensor logic (worst-case)
 * ----------------------------------------------------------------------- */

static void test_dual_gas_sensors(void) {
  printf("\n--- Test 14: Dual Gas Sensor Worst-Case Logic ---\n");

  anomaly_thresholds_t thresholds;
  anomaly_eval_init(&thresholds);

  sensor_snapshot_t snap = make_normal_snapshot();

  /* Only sensor 1 below threshold — should still trigger */
  snap.gas_ratio_1 = 0.55f; /* Below 0.70 warning */
  snap.gas_ratio_2 = 0.85f; /* Above threshold */

  compute_snapshot(&snap);
  anomaly_result_t result = anomaly_eval_run(&thresholds, &snap);

  TEST_ASSERT((result.active_mask & CAT_GAS) != 0,
              "Worst-case gas ratio triggers even if one sensor is OK");

  /* Both sensors normal → no trigger */
  snap.gas_ratio_1 = 0.85f;
  snap.gas_ratio_2 = 0.90f;
  compute_snapshot(&snap);
  result = anomaly_eval_run(&thresholds, &snap);
  TEST_ASSERT((result.active_mask & CAT_GAS) == 0,
              "Both sensors normal → no GAS category");
}

/* -----------------------------------------------------------------------
 * Test 15: Cascade stage estimation
 * ----------------------------------------------------------------------- */

static void test_cascade_stages(void) {
  printf("\n--- Test 15: Thermal Cascade Stage Assessment ---\n");

  TEST_ASSERT(get_cascade_stage(25.0f) == 0, "25°C = Normal (stage 0)");
  TEST_ASSERT(get_cascade_stage(60.0f) == 0, "60°C = Normal boundary");
  TEST_ASSERT(get_cascade_stage(61.0f) == 1, "61°C = Elevated (stage 1)");
  TEST_ASSERT(get_cascade_stage(100.0f) == 2,
              "100°C = SEI Decomposition (stage 2)");
  TEST_ASSERT(get_cascade_stage(140.0f) == 3,
              "140°C = Separator Collapse (stage 3)");
  TEST_ASSERT(get_cascade_stage(180.0f) == 4,
              "180°C = Electrolyte Decomp (stage 4)");
  TEST_ASSERT(get_cascade_stage(250.0f) == 5,
              "250°C = Cathode Decomp (stage 5)");
  TEST_ASSERT(get_cascade_stage(350.0f) == 6, "350°C = FULL RUNAWAY (stage 6)");
}

/* -----------------------------------------------------------------------
 * Test 16: Hotspot module tracking through correlation engine
 * ----------------------------------------------------------------------- */

static void test_hotspot_tracking(void) {
  printf("\n--- Test 16: Hotspot Tracking Through Engine ---\n");

  anomaly_thresholds_t thresholds;
  anomaly_eval_init(&thresholds);

  correlation_engine_t engine;
  correlation_engine_init(&engine);

  sensor_snapshot_t snap = make_normal_snapshot();
  snap.modules[4].ntc1_c = 60.0f; /* Module 5 is hotspot */

  compute_snapshot(&snap);
  anomaly_result_t result = anomaly_eval_run(&thresholds, &snap);
  correlation_engine_update(&engine, &result);

  TEST_ASSERT(engine.hotspot_module == 5, "Engine tracks hotspot as Module 5");
  TEST_ASSERT(engine.risk_factor > 0.0f,
              "Risk factor > 0 when thermal anomaly present");
  TEST_ASSERT(engine.cascade_stage <= 1,
              "Cascade stage Normal or Elevated (core temp near boundary)");
}

/* -----------------------------------------------------------------------
 * Test 17: Core temperature estimation
 * ----------------------------------------------------------------------- */

static void test_core_temp_estimation(void) {
  printf("\n--- Test 17: Core Temperature Estimation ---\n");

  /* T_core = T_surface + I_cell² × R_int × R_thermal
   * I_cell = 60A / 8 = 7.5A
   * R_int = 0.44mΩ = 0.00044Ω
   * R_thermal = 3.0 °C/W
   * ΔT = 7.5² × 0.00044 × 3.0 = 0.074°C (small at normal current)
   *
   * At high current (200A):
   * I_cell = 25A
   * ΔT = 25² × 0.00044 × 3.0 = 0.825°C
   */
  anomaly_thresholds_t thresholds;
  anomaly_eval_init(&thresholds);

  sensor_snapshot_t snap = make_normal_snapshot();
  snap.pack_current_a = 200.0f;

  compute_snapshot(&snap);

  float delta = snap.t_core_est_c - snap.hotspot_temp_c;
  TEST_ASSERT(delta > 0.5f && delta < 2.0f,
              "Core-surface delta significant at high current");

  /* At very high current (500A), delta should be large */
  snap.pack_current_a = 500.0f;
  compute_snapshot(&snap);
  delta = snap.t_core_est_c - snap.hotspot_temp_c;
  TEST_ASSERT(delta > 5.0f,
              "Core-surface delta > 5°C at extreme current (500A)");
}

/* -----------------------------------------------------------------------
 * Main
 * ----------------------------------------------------------------------- */

int main(void) {
  printf("====================================================\n");
  printf("  EV Battery Intelligence — C Firmware Test Runner\n");
  printf("  Full Pack: 104S8P | 832 Cells | 139 Channels\n");
  printf("  Running on HOST — no hardware needed\n");
  printf("====================================================\n");

  test_normal_operation();
  test_thermal_single_module();
  test_gas_only();
  test_multi_fault_critical();
  test_triple_fault_emergency();
  test_short_circuit();
  test_escalation_sequence();
  test_packet_format();
  test_ambient_compensation();
  test_emergency_direct();
  test_inter_module_gradient();
  test_intra_module_delta();
  test_per_module_swelling();
  test_dual_gas_sensors();
  test_cascade_stages();
  test_hotspot_tracking();
  test_core_temp_estimation();

  printf("\n====================================================\n");
  printf("  Results: %d/%d passed", tests_passed, tests_run);
  if (tests_failed > 0) {
    printf(" (%d FAILED)", tests_failed);
  }
  printf("\n====================================================\n");

  return tests_failed > 0 ? 1 : 0;
}
