/*
 * main.c — EV Battery Intelligence Main Application (Full Pack Edition)
 * ======================================================================
 *
 * Firmware for VSDSquadron ULTRA running on 104S8P battery pack data.
 * Processes ~139 sensor channels through the correlation engine.
 *
 *   FAST  LOOP (100ms / 10Hz): Electrical monitoring + short circuit
 *   MED   LOOP (500ms /  2Hz): Full anomaly evaluation + correlation
 *   SLOW  LOOP (  5s  / 0.2Hz): Multi-frame telemetry output
 *
 * Modes:
 *   HOST_MODE: Runs all 7 scenarios on host (testing)
 *   TARGET:    Receives from digital twin via UART + fallback sim
 *
 * Architecture (full pack):
 *   ┌──────────┐    ┌──────────┐    ┌──────────┐    ┌──────────┐
 *   │ 139 ch   │───>│ Anomaly  │───>│ Corr.    │───>│ 9-frame  │
 *   │ Sensors  │    │ Eval     │    │ Engine   │    │ Telemetry│
 *   └──────────┘    └──────────┘    └──────────┘    └──────────┘
 */

#include <stdio.h>
#include <string.h>

/* HAL layer */
#include "hal_gpio.h"
#include "hal_platform.h"
#include "hal_uart.h"

/* Core intelligence */
#include "anomaly_eval.h"
#include "correlation_engine.h"

/* Application */
#include "input_packet.h"
#include "packet_format.h"

/* -----------------------------------------------------------------------
 * Loop timing configuration
 * ----------------------------------------------------------------------- */
#define FAST_LOOP_NORMAL_MS 100
#define MED_LOOP_NORMAL_MS 500
#define SLOW_LOOP_NORMAL_MS 5000

#define FAST_LOOP_ALERT_MS 20
#define MED_LOOP_ALERT_MS 100
#define SLOW_LOOP_ALERT_MS 1000
#define SLOW_LOOP_EXTERNAL_MS 1000

/* Correlation timing windows */
#define CRITICAL_HOLD_MS 10000
#define DEESCALATION_HOLD_MS 5000

#define SCHED_TICK_MS 10
#define SIM_DURATION_S 215

/* -----------------------------------------------------------------------
 * Global state
 * ----------------------------------------------------------------------- */

static sensor_snapshot_t g_snapshot;
static anomaly_result_t g_anomaly;
static anomaly_thresholds_t g_thresholds;

static correlation_engine_t g_corr;
static float g_prev_r_int_mohm = 0.0f;

/* NTC history for dT/dt computation (per module, 2 NTCs each) */
static float g_prev_ntc[NUM_MODULES][2]; /* [module][ntc_index] */

/* External input state */
static input_rx_state_t g_input_rx;
static uint8_t g_external_input_active = 0;
static uint32_t g_last_external_ms = 0;
#define EXTERNAL_INPUT_TIMEOUT_MS 2000

/* Core temperature estimation constant */
#define R_THERMAL_CW 3.0f /* °C/W for IFR32135 cylindrical */

/* Timing */
static uint32_t g_uptime_ms = 0;
static uint32_t g_fast_loop_ms = FAST_LOOP_NORMAL_MS;
static uint32_t g_med_loop_ms = MED_LOOP_NORMAL_MS;
static uint32_t g_slow_loop_ms = SLOW_LOOP_NORMAL_MS;
static uint32_t g_next_fast_ms = 0;
static uint32_t g_next_med_ms = 0;
static uint32_t g_next_slow_ms = 0;
static bool g_startup_self_check_passed = false;

/* -----------------------------------------------------------------------
 * Scheduler helpers
 * ----------------------------------------------------------------------- */

static uint16_t ms_to_cycles(uint32_t window_ms, uint32_t period_ms) {
  if (period_ms == 0)
    return 1;
  uint32_t cycles = (window_ms + period_ms - 1u) / period_ms;
  if (cycles == 0u)
    return 1;
  if (cycles > 65535u)
    return 65535u;
  return (uint16_t)cycles;
}

static void correlation_sync_timing_limits(void) {
  g_corr.critical_countdown_limit =
      ms_to_cycles(CRITICAL_HOLD_MS, g_med_loop_ms);
  g_corr.deescalation_limit = ms_to_cycles(DEESCALATION_HOLD_MS, g_med_loop_ms);
}

static void scheduler_reset(void) {
  g_fast_loop_ms = FAST_LOOP_NORMAL_MS;
  g_med_loop_ms = MED_LOOP_NORMAL_MS;
  g_slow_loop_ms = SLOW_LOOP_NORMAL_MS;
  g_next_fast_ms = g_uptime_ms;
  g_next_med_ms = g_uptime_ms;
  g_next_slow_ms = g_uptime_ms;
  correlation_sync_timing_limits();
}

static bool scheduler_is_alert_mode(void) {
  return g_snapshot.short_circuit || (g_anomaly.active_count > 0) ||
         (g_corr.current_state != STATE_NORMAL);
}

static void scheduler_apply_sampling_rates(void) {
  uint32_t target_fast = FAST_LOOP_NORMAL_MS;
  uint32_t target_med = MED_LOOP_NORMAL_MS;
  uint32_t target_slow = SLOW_LOOP_NORMAL_MS;

  if (scheduler_is_alert_mode()) {
    target_fast = FAST_LOOP_ALERT_MS;
    target_med = MED_LOOP_ALERT_MS;
    target_slow = SLOW_LOOP_ALERT_MS;
  }

  if (g_external_input_active && target_slow > SLOW_LOOP_EXTERNAL_MS) {
    target_slow = SLOW_LOOP_EXTERNAL_MS;
  }

  g_fast_loop_ms = target_fast;
  g_med_loop_ms = target_med;
  g_slow_loop_ms = target_slow;

  if (g_next_fast_ms > (g_uptime_ms + g_fast_loop_ms))
    g_next_fast_ms = g_uptime_ms + g_fast_loop_ms;
  if (g_next_med_ms > (g_uptime_ms + g_med_loop_ms))
    g_next_med_ms = g_uptime_ms + g_med_loop_ms;
  if (g_next_slow_ms > (g_uptime_ms + g_slow_loop_ms))
    g_next_slow_ms = g_uptime_ms + g_slow_loop_ms;
}

/* -----------------------------------------------------------------------
 * Self-check
 * ----------------------------------------------------------------------- */
static bool startup_self_check(void) {
  if (PACKET_PACK_SIZE != sizeof(telemetry_pack_frame_t)) {
    hal_uart_print("[SAFE] Self-check FAIL: pack frame size mismatch\r\n");
    return false;
  }

  if (!(g_thresholds.temp_warning_c < g_thresholds.temp_critical_c &&
        g_thresholds.gas_warning_ratio > g_thresholds.gas_critical_ratio &&
        g_thresholds.pressure_warning_hpa <
            g_thresholds.pressure_critical_hpa &&
        g_thresholds.current_warning_a < g_thresholds.current_short_a)) {
    hal_uart_print("[SAFE] Self-check FAIL: threshold ordering\r\n");
    return false;
  }

  /* Quick functional test */
  sensor_snapshot_t probe;
  memset(&probe, 0, sizeof(probe));
  probe.pack_voltage_v = 332.8f;
  probe.pack_current_a = 60.0f;
  probe.r_internal_mohm = 0.44f;

  for (int m = 0; m < NUM_MODULES; m++) {
    probe.modules[m].ntc1_c = 28.0f;
    probe.modules[m].ntc2_c = 28.5f;
    probe.modules[m].swelling_pct = 0.5f;
    for (int g = 0; g < GROUPS_PER_MODULE; g++) {
      probe.modules[m].group_voltages_v[g] = 3.20f;
    }
  }
  probe.temp_ambient_c = 25.0f;
  probe.coolant_inlet_c = 25.0f;
  probe.coolant_outlet_c = 27.0f;
  probe.gas_ratio_1 = 0.98f;
  probe.gas_ratio_2 = 0.97f;
  probe.pressure_delta_1_hpa = 0.1f;
  probe.pressure_delta_2_hpa = 0.1f;

  anomaly_eval_compute(&probe, &g_thresholds);
  anomaly_result_t ar = anomaly_eval_run(&g_thresholds, &probe);

  telemetry_pack_frame_t pkt;
  (void)packet_encode_pack(&pkt, 0, &probe, &ar, STATE_NORMAL);
  if (packet_validate_pack(&pkt) != 0) {
    hal_uart_print("[SAFE] Self-check FAIL: packet validate\r\n");
    return false;
  }

  hal_uart_print("[SAFE] Self-check PASS\r\n");
  return true;
}

/* -----------------------------------------------------------------------
 * Simulated sensor injection (Full Pack — 8 modules × 13 groups)
 *
 * Same 7 scenarios as before, adapted for 104S8P scale.
 *
 *   Scenario 1 (  0- 30s): Normal Operation — all 8 modules steady
 *   Scenario 2 ( 30- 70s): Thermal Anomaly — Module 3 heats up
 *   Scenario 3 ( 70-100s): Gas Anomaly — electrolyte off-gassing
 *   Scenario 4 (100-150s): Multi-Fault — thermal + gas + pressure
 *   Scenario 5 (150-165s): Short Circuit — massive current spike
 *   Scenario 6 (165-185s): Recovery — sensors normal, EMERGENCY latched
 *   Scenario 7 (185-215s): Ambient Compensation — same temp, different ambient
 * ----------------------------------------------------------------------- */

static void sim_inject_data(sensor_snapshot_t *snap, uint32_t t_ms) {
  float t_s = (float)t_ms / 1000.0f;

  /* Default safe values for full pack */
  snap->pack_voltage_v = 332.8f;
  snap->pack_current_a = 60.0f;  /* 0.5C = 60A */
  snap->r_internal_mohm = 0.44f; /* Group R_int = 3.5mΩ/8 */

  for (int m = 0; m < NUM_MODULES; m++) {
    snap->modules[m].ntc1_c = 28.0f + (float)m * 0.3f;
    snap->modules[m].ntc2_c = 28.2f + (float)m * 0.3f;
    snap->modules[m].swelling_pct = 0.5f;
    snap->modules[m].max_dt_dt = 0.0f;
    for (int g = 0; g < GROUPS_PER_MODULE; g++) {
      /* Slight natural variation per group */
      snap->modules[m].group_voltages_v[g] = 3.20f + 0.002f * (float)(g % 3);
    }
  }

  snap->temp_ambient_c = 30.0f;
  snap->coolant_inlet_c = 25.0f;
  snap->coolant_outlet_c = 27.0f;
  snap->gas_ratio_1 = 0.98f;
  snap->gas_ratio_2 = 0.97f;
  snap->pressure_delta_1_hpa = 0.1f;
  snap->pressure_delta_2_hpa = 0.1f;
  snap->humidity_pct = 50.0f;
  snap->isolation_mohm = 500.0f;
  snap->short_circuit = false;

  /* ---- Scenario 1: Normal Operation (0-30s) ---- */
  if (t_s < 30.0f)
    return;

  /* ---- Scenario 2: Thermal Anomaly — Module 3 heats up (30-70s) ---- */
  if (t_s < 70.0f) {
    float progress = (t_s - 30.0f) / 40.0f;

    /* Module 3 (index 2) develops a thermal hotspot */
    snap->modules[2].ntc1_c = 28.5f + progress * 35.0f; /* → 63.5°C */
    snap->modules[2].ntc2_c = 28.8f + progress * 28.0f; /* → 56.8°C */
    snap->modules[2].max_dt_dt = 0.3f * progress;       /* Slow rise */

    /* Adjacent modules slightly warm */
    snap->modules[1].ntc1_c = 28.3f + progress * 4.0f;
    snap->modules[3].ntc1_c = 28.6f + progress * 3.5f;

    /* No gas, no pressure — single category = WARNING */
    return;
  }

  /* ---- Scenario 3: Gas Anomaly Only (70-100s) ---- */
  if (t_s < 100.0f) {
    float progress = (t_s - 70.0f) / 30.0f;

    /* Module 3 cools down */
    snap->modules[2].ntc1_c = 35.0f - progress * 5.0f;
    snap->modules[2].ntc2_c = 34.0f - progress * 4.0f;

    /* Gas ratio drops: electrolyte decomposition detected */
    snap->gas_ratio_1 = 0.95f - progress * 0.40f; /* → 0.55 */
    snap->gas_ratio_2 = 0.96f - progress * 0.30f; /* → 0.66 */

    return;
  }

  /* ---- Scenario 4: Multi-Fault Escalation (100-150s) ---- */
  if (t_s < 150.0f) {
    float progress = (t_s - 100.0f) / 50.0f;

    /* Module 5 develops a severe thermal event */
    snap->modules[4].ntc1_c = 35.0f + progress * 38.0f; /* → 73°C */
    snap->modules[4].ntc2_c = 34.0f + progress * 30.0f; /* → 64°C */
    snap->modules[4].max_dt_dt = 0.2f + progress * 0.6f;
    snap->modules[4].swelling_pct = 0.5f + progress * 8.0f;

    /* Gas worsens */
    snap->gas_ratio_1 = 0.55f - progress * 0.25f;
    snap->gas_ratio_2 = 0.60f - progress * 0.20f;

    /* Pressure rises (from 120s onward) */
    if (t_s > 120.0f) {
      float p2 = (t_s - 120.0f) / 30.0f;
      snap->pressure_delta_1_hpa = p2 * 4.0f;
      snap->pressure_delta_2_hpa = p2 * 3.0f;
    }

    /* Voltage drops under fault */
    snap->pack_voltage_v = 332.8f - progress * 15.0f;
    snap->pack_current_a = 60.0f + progress * 40.0f;

    /* Adjacent modules 4 & 6 warm via thermal coupling */
    snap->modules[3].ntc1_c = 28.6f + progress * 6.0f;
    snap->modules[5].ntc1_c = 28.8f + progress * 5.5f;

    return;
  }

  /* ---- Scenario 5: Short Circuit (150-165s) ---- */
  if (t_s < 165.0f) {
    snap->pack_voltage_v = 280.0f;
    snap->pack_current_a = 400.0f;
    snap->short_circuit = true;

    snap->modules[4].ntc1_c = 95.0f;
    snap->modules[4].ntc2_c = 82.0f;
    snap->modules[4].swelling_pct = 12.0f;
    snap->modules[4].max_dt_dt = 3.0f;

    /* Severe gas/pressure across pack */
    snap->gas_ratio_1 = 0.20f;
    snap->gas_ratio_2 = 0.25f;
    snap->pressure_delta_1_hpa = 8.0f;
    snap->pressure_delta_2_hpa = 7.0f;

    return;
  }

  /* ---- Scenario 6: Recovery (165-185s) — EMERGENCY stays latched ---- */
  if (t_s < 185.0f) {
    float progress = (t_s - 165.0f) / 20.0f;

    snap->pack_voltage_v = 280.0f + progress * 52.8f;
    snap->pack_current_a = 400.0f - progress * 340.0f;
    snap->short_circuit = false;

    snap->modules[4].ntc1_c = 95.0f - progress * 65.0f;
    snap->modules[4].ntc2_c = 82.0f - progress * 52.0f;
    snap->modules[4].swelling_pct = 12.0f - progress * 11.0f;

    snap->gas_ratio_1 = 0.20f + progress * 0.78f;
    snap->gas_ratio_2 = 0.25f + progress * 0.72f;
    snap->pressure_delta_1_hpa = 8.0f - progress * 8.0f;
    snap->pressure_delta_2_hpa = 7.0f - progress * 7.0f;

    return;
  }

  /* ---- Scenario 7: Ambient Compensation (185-215s) ---- */
  if (t_s < 200.0f) {
    /* Phase A: Cold ambient — 45°C cells are suspicious */
    for (int m = 0; m < NUM_MODULES; m++) {
      snap->modules[m].ntc1_c = 44.5f + (float)m * 0.2f;
      snap->modules[m].ntc2_c = 44.8f + (float)m * 0.15f;
    }
    snap->temp_ambient_c = 25.0f;
    return;
  }

  /* Phase B: Hot ambient — same 45°C cells are normal */
  for (int m = 0; m < NUM_MODULES; m++) {
    snap->modules[m].ntc1_c = 44.5f + (float)m * 0.2f;
    snap->modules[m].ntc2_c = 44.8f + (float)m * 0.15f;
  }
  snap->temp_ambient_c = 38.0f;
}

/* -----------------------------------------------------------------------
 * Apply external input frames to snapshot
 * ----------------------------------------------------------------------- */
static void apply_external_input(sensor_snapshot_t *snap,
                                 const input_rx_state_t *rx) {
  const input_pack_frame_t *pf = &rx->last_pack;

  snap->pack_voltage_v = pf->pack_voltage_dv / 10.0f;
  snap->pack_current_a = pf->pack_current_da / 10.0f;
  snap->temp_ambient_c = pf->ambient_temp_dt / 10.0f;
  snap->coolant_inlet_c = pf->coolant_inlet_dt / 10.0f;
  snap->coolant_outlet_c = pf->coolant_outlet_dt / 10.0f;
  snap->gas_ratio_1 = pf->gas_ratio_1_cp / 100.0f;
  snap->gas_ratio_2 = pf->gas_ratio_2_cp / 100.0f;
  snap->pressure_delta_1_hpa = pf->pressure_delta_1_chpa / 100.0f;
  snap->pressure_delta_2_hpa = pf->pressure_delta_2_chpa / 100.0f;
  snap->humidity_pct = (float)pf->humidity_pct;
  snap->isolation_mohm = pf->isolation_mohm / 10.0f;

  for (int m = 0; m < NUM_MODULES; m++) {
    const input_module_frame_t *mf = &rx->last_modules[m];

    snap->modules[m].ntc1_c = mf->ntc1_dt / 10.0f;
    snap->modules[m].ntc2_c = mf->ntc2_dt / 10.0f;
    snap->modules[m].swelling_pct = (float)mf->swelling_pct;

    /* Decode group voltages from base + delta */
    float base_v = mf->v_base_mv / 1000.0f;
    for (int g = 0; g < GROUPS_PER_MODULE; g++) {
      snap->modules[m].group_voltages_v[g] = base_v + mf->v_delta[g] / 1000.0f;
    }
  }

  /* R_int and dT/dt are computed by med_loop, defaults: */
  snap->r_internal_mohm = 0.44f;
  snap->short_circuit = false;
}

/* -----------------------------------------------------------------------
 * FAST LOOP — Short-circuit detection (100ms / 10Hz)
 * ----------------------------------------------------------------------- */
static void fast_loop(void) {
  float abs_i = g_snapshot.pack_current_a;
  if (abs_i < 0)
    abs_i = -abs_i;

  if (abs_i > 350.0f) {
    g_snapshot.short_circuit = true;
    anomaly_eval_compute(&g_snapshot, &g_thresholds);
    g_anomaly = anomaly_eval_run(&g_thresholds, &g_snapshot);
    correlation_engine_update(&g_corr, &g_anomaly);
    scheduler_apply_sampling_rates();

    if (g_corr.current_state == STATE_EMERGENCY) {
      hal_gpio_set_status_leds(3);
#if !HAL_HOST_MODE
      hal_gpio_relay_disconnect();
      hal_gpio_buzzer_pulse(1000);
#endif
    }
  }
}

/* -----------------------------------------------------------------------
 * MED LOOP — Full evaluation + correlation (500ms / 2Hz)
 * ----------------------------------------------------------------------- */
static void med_loop(void) {
  /* Compute dR/dt */
  if (g_prev_r_int_mohm > 0.0f) {
    float dt_s = (float)g_med_loop_ms / 1000.0f;
    if (dt_s > 0.0f) {
      g_snapshot.dr_dt_mohm_per_s =
          (g_snapshot.r_internal_mohm - g_prev_r_int_mohm) / dt_s;
    }
  }
  g_prev_r_int_mohm = g_snapshot.r_internal_mohm;

  /* Compute per-module dT/dt from NTC history */
  float dt_s = (float)g_med_loop_ms / 1000.0f;
  for (int m = 0; m < NUM_MODULES; m++) {
    if (dt_s > 0.0f) {
      float d1 =
          (g_snapshot.modules[m].ntc1_c - g_prev_ntc[m][0]) / dt_s * 60.0f;
      float d2 =
          (g_snapshot.modules[m].ntc2_c - g_prev_ntc[m][1]) / dt_s * 60.0f;
      if (d1 < 0)
        d1 = -d1;
      if (d2 < 0)
        d2 = -d2;
      g_snapshot.modules[m].max_dt_dt = d1 > d2 ? d1 : d2;
    }
    g_prev_ntc[m][0] = g_snapshot.modules[m].ntc1_c;
    g_prev_ntc[m][1] = g_snapshot.modules[m].ntc2_c;
  }

  /* Compute derived fields (voltage stats, temp stats, hotspot, core temp) */
  anomaly_eval_compute(&g_snapshot, &g_thresholds);

  /* Evaluate anomaly categories */
  g_anomaly = anomaly_eval_run(&g_thresholds, &g_snapshot);

  correlation_sync_timing_limits();

  /* Update correlation engine */
  system_state_t prev_state = g_corr.current_state;
  correlation_engine_update(&g_corr, &g_anomaly);
  system_state_t new_state = g_corr.current_state;

  /* Log state transitions */
  if (new_state != prev_state) {
    char buf[120];
    snprintf(buf, sizeof(buf),
             "[STATE] %s -> %s (cats=%d, hotspot=M%d, risk=%d%%)%s\r\n",
             correlation_state_name(prev_state),
             correlation_state_name(new_state), g_anomaly.active_count,
             g_anomaly.hotspot_module, (int)(g_anomaly.risk_factor * 100),
             g_anomaly.is_emergency_direct ? " [DIRECT]" : "");
    hal_uart_print(buf);
  }

  /* Update status LEDs */
  hal_gpio_set_status_leds((uint8_t)new_state);

  /* EMERGENCY actions */
  if (new_state == STATE_EMERGENCY) {
#if !HAL_HOST_MODE
    hal_gpio_relay_disconnect();
    hal_gpio_buzzer_pulse(500);
#else
    if (new_state != prev_state) {
      printf("[HAL] RELAY TRIGGERED — Battery DISCONNECTED\n");
    }
#endif
  }

  scheduler_apply_sampling_rates();
}

/* -----------------------------------------------------------------------
 * SLOW LOOP — Multi-frame telemetry output (5s / 0.2Hz)
 * ----------------------------------------------------------------------- */
static void slow_loop(void) {
  /* Send pack summary frame */
  telemetry_pack_frame_t pack_pkt;
  packet_encode_pack(&pack_pkt, g_uptime_ms, &g_snapshot, &g_anomaly,
                     g_corr.current_state);
  hal_uart_send((const uint8_t *)&pack_pkt, sizeof(pack_pkt));

  /* Send 8 module detail frames */
  for (int m = 0; m < NUM_MODULES; m++) {
    telemetry_module_frame_t mod_pkt;
    packet_encode_module(&mod_pkt, (uint8_t)m, &g_snapshot);
    hal_uart_send((const uint8_t *)&mod_pkt, sizeof(mod_pkt));
  }

  /* Human-readable debug line */
  char buf[200];
  snprintf(buf, sizeof(buf),
           "[TEL] t=%lums V=%.0f I=%.0f Tmax=%.1f dT/dt=%.2f "
           "gas=[%.2f,%.2f] dP=[%.1f,%.1f] state=%s cats=%d "
           "hot=M%d risk=%d%% stg=%s\r\n",
           (unsigned long)g_uptime_ms, g_snapshot.pack_voltage_v,
           g_snapshot.pack_current_a, g_snapshot.hotspot_temp_c,
           g_snapshot.dt_dt_max, g_snapshot.gas_ratio_1, g_snapshot.gas_ratio_2,
           g_snapshot.pressure_delta_1_hpa, g_snapshot.pressure_delta_2_hpa,
           correlation_state_name(g_corr.current_state), g_anomaly.active_count,
           g_anomaly.hotspot_module, (int)(g_anomaly.risk_factor * 100),
           cascade_stage_name(g_anomaly.cascade_stage));
  hal_uart_print(buf);
}

/* -----------------------------------------------------------------------
 * System initialization
 * ----------------------------------------------------------------------- */
static void system_init(void) {
  hal_gpio_init();
  hal_uart_init();
  anomaly_eval_init(&g_thresholds);
  correlation_engine_init(&g_corr);
  memset(&g_anomaly, 0, sizeof(g_anomaly));
  memset(&g_snapshot, 0, sizeof(g_snapshot));
  memset(g_prev_ntc, 0, sizeof(g_prev_ntc));
  scheduler_reset();
  hal_gpio_set_safety_armed(false);

  hal_uart_print("====================================================\r\n");
  hal_uart_print("  EV Battery Intelligence — Firmware v2.0 (Full Pack)\r\n");
  hal_uart_print("  104S8P | 832 Cells | 139 Sensor Channels\r\n");
  hal_uart_print("  Thermal Runaway Prevention System\r\n");
#if HAL_HOST_MODE
  hal_uart_print("  Mode: HOST SIMULATION\r\n");
#else
  hal_uart_print("  Mode: BOARD (VSDSquadron ULTRA / THEJAS32)\r\n");
  hal_uart_print("  Demo: Digital Twin → Correlation Engine → UART\r\n");
#endif
  hal_uart_print(
      "====================================================\r\n\r\n");
}

/* -----------------------------------------------------------------------
 * MAIN — Entry point
 * ----------------------------------------------------------------------- */
int main(void) {
  system_init();
  g_startup_self_check_passed = startup_self_check();
  hal_gpio_set_safety_armed(g_startup_self_check_passed);

  if (hal_gpio_is_safety_armed()) {
    hal_uart_print("[SAFE] Relay connect path enabled after self-check\r\n");
  } else {
    hal_uart_print("[SAFE] Relay connect path BLOCKED\r\n");
  }

#if HAL_HOST_MODE
  /* ---- HOST: Run through all scenarios instantly ---- */
  uint32_t total_ms = SIM_DURATION_S * 1000;

  printf("Running %ds full-pack simulation (7 scenarios, 8 modules)...\n\n",
         SIM_DURATION_S);

  g_uptime_ms = 0;
  scheduler_reset();

  /* Initialize NTC history */
  sim_inject_data(&g_snapshot, 0);
  for (int m = 0; m < NUM_MODULES; m++) {
    g_prev_ntc[m][0] = g_snapshot.modules[m].ntc1_c;
    g_prev_ntc[m][1] = g_snapshot.modules[m].ntc2_c;
  }

  for (; g_uptime_ms <= total_ms; g_uptime_ms += SCHED_TICK_MS) {
    sim_inject_data(&g_snapshot, g_uptime_ms);

    if (g_uptime_ms >= g_next_fast_ms) {
      fast_loop();
      g_next_fast_ms = g_uptime_ms + g_fast_loop_ms;
    }
    if (g_uptime_ms >= g_next_med_ms) {
      med_loop();
      g_next_med_ms = g_uptime_ms + g_med_loop_ms;
    }
    if (g_uptime_ms >= g_next_slow_ms) {
      slow_loop();
      g_next_slow_ms = g_uptime_ms + g_slow_loop_ms;
    }
  }

  printf("\nSimulation complete. Final state: %s\n",
         correlation_state_name(g_corr.current_state));

#else
  /* ---- TARGET: Board with digital twin or fallback sim ---- */
  if (hal_gpio_is_safety_armed()) {
    hal_gpio_relay_connect();
  } else {
    hal_gpio_relay_disconnect();
  }

  /* Boot LED sequence */
  hal_gpio_set_status_leds(0);
  for (volatile uint32_t d = 0; d < 500000; d++) {
  }
  hal_gpio_set_status_leds(1);
  for (volatile uint32_t d = 0; d < 500000; d++) {
  }
  hal_gpio_set_status_leds(2);
  for (volatile uint32_t d = 0; d < 500000; d++) {
  }
  hal_gpio_set_status_leds(0);

  hal_uart_print("Starting full-pack demo loop...\r\n\r\n");
  input_rx_init(&g_input_rx);

  /* Initialize NTC history */
  sim_inject_data(&g_snapshot, 0);
  for (int m = 0; m < NUM_MODULES; m++) {
    g_prev_ntc[m][0] = g_snapshot.modules[m].ntc1_c;
    g_prev_ntc[m][1] = g_snapshot.modules[m].ntc2_c;
  }

  while (1) {
    /* Poll UART RX for multi-frame input from digital twin */
    {
      int b;
      while ((b = hal_uart_recv_byte()) >= 0) {
        int rx_result = input_rx_feed(&g_input_rx, (uint8_t)b);
        if (rx_result == 2) {
          /* Complete snapshot received — apply to g_snapshot */
          apply_external_input(&g_snapshot, &g_input_rx);
          input_rx_reset_cycle(&g_input_rx);
          g_external_input_active = 1;
          g_last_external_ms = g_uptime_ms;
        }
      }
    }

    /* Use external input or fall back to internal sim */
    if (g_external_input_active &&
        (g_uptime_ms - g_last_external_ms) < EXTERNAL_INPUT_TIMEOUT_MS) {
      /* External data already in g_snapshot */
    } else {
      if (g_external_input_active) {
        g_external_input_active = 0;
        hal_uart_print("[EXT] Input timeout — reverting to sim\r\n");
      }
      sim_inject_data(&g_snapshot, g_uptime_ms);
    }

    /* Run scheduler */
    if (g_uptime_ms >= g_next_fast_ms) {
      fast_loop();
      g_next_fast_ms = g_uptime_ms + g_fast_loop_ms;
    }
    if (g_uptime_ms >= g_next_med_ms) {
      med_loop();
      g_next_med_ms = g_uptime_ms + g_med_loop_ms;
    }
    if (g_uptime_ms >= g_next_slow_ms) {
      slow_loop();
      g_next_slow_ms = g_uptime_ms + g_slow_loop_ms;
    }

    g_uptime_ms += SCHED_TICK_MS;

    if (g_uptime_ms > (uint32_t)(SIM_DURATION_S * 1000)) {
      g_uptime_ms = 0;
      correlation_engine_reset(&g_corr);
      memset(&g_anomaly, 0, sizeof(g_anomaly));
      scheduler_reset();
      hal_uart_print("\r\n--- Restarting full-pack demo ---\r\n\r\n");
    }

    for (volatile uint32_t d = 0; d < 10000; d++) {
    }
  }
#endif

  return 0;
}
