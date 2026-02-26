/*
 * main.c — EV Battery Intelligence Main Application
 * ==================================================
 *
 * This is the firmware entry point. It implements a 3-speed cooperative
 * scheduler that runs different tasks at different rates:
 *
 *   FAST  LOOP (every 100ms / 10Hz): Electrical monitoring
 *   MED   LOOP (every 500ms /  2Hz): All sensors + correlation engine
 *   SLOW  LOOP (every   5s  / 0.2Hz): Telemetry packet + housekeeping
 *
 *   During alert conditions, sampling auto-escalates:
 *   FAST -> 20ms, MED -> 100ms, SLOW -> 1s
 *
 * DEMO MODE (no sensors):
 *   Since we only have the board (no INA219, BME680, NTC, FSR),
 *   the firmware injects simulated sensor data through all 7 validation
 *   scenarios. The correlation engine processes this data on-chip and
 *   sends UART telemetry to the dashboard.
 *
 *   Judges see: Real firmware on real hardware → UART → Live dashboard
 *
 * Architecture:
 *   ┌──────────┐    ┌──────────┐    ┌──────────┐    ┌──────────┐
 *   │ Simulated│───→│ Anomaly  │───→│ Corr.    │───→│ UART TX  │
 *   │ Sensors  │    │ Eval     │    │ Engine   │    │ → Dashboard
 *   └──────────┘    └──────────┘    └──────────┘    └──────────┘
 */

#include <stdio.h>
#include <string.h>

/* HAL layer */
#include "../hal/hal_gpio.h"
#include "../hal/hal_platform.h"
#include "../hal/hal_uart.h"

/* Core intelligence */
#include "../core/anomaly_eval.h"
#include "../core/correlation_engine.h"

/* Application */
#include "input_packet.h"
#include "packet_format.h"

/* -----------------------------------------------------------------------
 * Loop timing configuration
 * ----------------------------------------------------------------------- */
#define FAST_LOOP_NORMAL_MS 100  /* 10Hz */
#define MED_LOOP_NORMAL_MS 500   /* 2Hz */
#define SLOW_LOOP_NORMAL_MS 5000 /* 0.2Hz */

#define FAST_LOOP_ALERT_MS 20   /* 50Hz */
#define MED_LOOP_ALERT_MS 100   /* 10Hz */
#define SLOW_LOOP_ALERT_MS 1000 /* 1Hz */
/* Demo tuning: when external twin input is actively streaming, send telemetry
 * more frequently for a faster dashboard response loop. */
#define SLOW_LOOP_EXTERNAL_MS 1000 /* 1Hz while external input active */

/* Correlation timing windows (constant in real time, not cycles) */
#define CRITICAL_HOLD_MS 10000    /* 10s before CRITICAL -> EMERGENCY */
#define DEESCALATION_HOLD_MS 5000 /* 5s sustained normal before drop */

/* Scheduler bookkeeping tick */
#define SCHED_TICK_MS 10

/* Total simulation duration */
#define SIM_DURATION_S 215 /* ~3.5 minutes — covers all 7 scenarios */

/* -----------------------------------------------------------------------
 * Global state
 * ----------------------------------------------------------------------- */

/* Latest evaluation results */
static sensor_snapshot_t g_snapshot;
static anomaly_result_t g_anomaly;
static anomaly_thresholds_t g_thresholds;

/* Correlation engine */
static correlation_engine_t g_corr;

/* R_int tracking for dR/dt computation */
static float g_prev_r_int_mohm = 0.0f;

/* External input (digital twin → board) */
static input_rx_state_t g_input_rx;
static uint8_t g_external_input_active = 0;
static uint32_t g_last_external_ms = 0;
#define EXTERNAL_INPUT_TIMEOUT_MS 2000 /* Fall back to sim if no input for 2s  \
                                        */

/* Thermal constant for core temp estimation (spec §2.3)
 * R_thermal ≈ 0.5 °C/W for typical cylindrical cell. */
#define R_THERMAL_CW 0.5f

/* Uptime counter (milliseconds) */
static uint32_t g_uptime_ms = 0;

/* Active scheduler periods (normal or alert) */
static uint32_t g_fast_loop_ms = FAST_LOOP_NORMAL_MS;
static uint32_t g_med_loop_ms = MED_LOOP_NORMAL_MS;
static uint32_t g_slow_loop_ms = SLOW_LOOP_NORMAL_MS;

/* Next absolute run times for each loop */
static uint32_t g_next_fast_ms = 0;
static uint32_t g_next_med_ms = 0;
static uint32_t g_next_slow_ms = 0;
static bool g_startup_self_check_passed = false;

static uint16_t ms_to_cycles(uint32_t window_ms, uint32_t period_ms) {
  uint32_t cycles;

  if (period_ms == 0) {
    return 1;
  }

  cycles = (window_ms + period_ms - 1u) / period_ms;
  if (cycles == 0u) {
    return 1;
  }
  if (cycles > 65535u) {
    return 65535u;
  }
  return (uint16_t)cycles;
}

/* Keep countdown/de-escalation windows stable even when med loop rate changes.
 */
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

/* Tighten or relax loop rates based on live anomaly/state context. */
static void scheduler_apply_sampling_rates(void) {
  uint32_t target_fast = FAST_LOOP_NORMAL_MS;
  uint32_t target_med = MED_LOOP_NORMAL_MS;
  uint32_t target_slow = SLOW_LOOP_NORMAL_MS;

  if (scheduler_is_alert_mode()) {
    target_fast = FAST_LOOP_ALERT_MS;
    target_med = MED_LOOP_ALERT_MS;
    target_slow = SLOW_LOOP_ALERT_MS;
  }

  /* In twin-bridge demos, external input arrives continuously.
   * Speed up the telemetry loop so board output feels responsive. */
  if (g_external_input_active && target_slow > SLOW_LOOP_EXTERNAL_MS) {
    target_slow = SLOW_LOOP_EXTERNAL_MS;
  }

  g_fast_loop_ms = target_fast;
  g_med_loop_ms = target_med;
  g_slow_loop_ms = target_slow;

  /* If we accelerated, pull next deadlines closer immediately. */
  if (g_next_fast_ms > (g_uptime_ms + g_fast_loop_ms)) {
    g_next_fast_ms = g_uptime_ms + g_fast_loop_ms;
  }
  if (g_next_med_ms > (g_uptime_ms + g_med_loop_ms)) {
    g_next_med_ms = g_uptime_ms + g_med_loop_ms;
  }
  if (g_next_slow_ms > (g_uptime_ms + g_slow_loop_ms)) {
    g_next_slow_ms = g_uptime_ms + g_slow_loop_ms;
  }
}

/* Basic self-test before arming safety outputs. */
static bool startup_self_check(void) {
  if (PACKET_MAX_SIZE != sizeof(telemetry_packet_t)) {
    hal_uart_print("[SAFE] Self-check FAIL: packet size mismatch\r\n");
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

  sensor_snapshot_t probe;
  memset(&probe, 0, sizeof(probe));
  probe.voltage_v = 14.8f;
  probe.current_a = 2.0f;
  probe.r_internal_mohm = 45.0f;
  probe.temp_cells_c[0] = 28.0f;
  probe.temp_cells_c[1] = 28.5f;
  probe.temp_cells_c[2] = 27.8f;
  probe.temp_cells_c[3] = 28.2f;
  probe.temp_ambient_c = 25.0f;
  probe.gas_ratio = 0.98f;

  anomaly_result_t ar = anomaly_eval_run(&g_thresholds, &probe);
  telemetry_packet_t pkt;
  (void)packet_encode(&pkt, 0, &probe, &ar, STATE_NORMAL);
  if (packet_validate(&pkt) != 0) {
    hal_uart_print("[SAFE] Self-check FAIL: packet validate\r\n");
    return false;
  }

  hal_uart_print("[SAFE] Self-check PASS\r\n");
  return true;
}

/* -----------------------------------------------------------------------
 * Simulated sensor injection (runs on BOTH host and target)
 *
 * This replaces real sensor drivers since we don't have sensors.
 * It generates the same 7 scenarios as the Python dashboard to ensure
 * the C firmware output matches what the dashboard expects.
 *
 *   Scenario 1 (  0- 30s): Normal Operation
 *   Scenario 2 ( 30- 70s): Thermal Anomaly Only → WARNING
 *   Scenario 3 ( 70-100s): Gas Anomaly Only → WARNING
 *   Scenario 4 (100-150s): Multi-Fault Escalation → CRITICAL → EMERGENCY
 *   Scenario 5 (150-165s): Short Circuit → immediate EMERGENCY
 *   Scenario 6 (165-185s): Recovery to Normal (but EMERGENCY is latched)
 *   Scenario 7 (185-215s): Ambient Compensation Demo
 * ----------------------------------------------------------------------- */

static void sim_inject_data(sensor_snapshot_t *snap, uint32_t t_ms) {
  float t_s = (float)t_ms / 1000.0f;

  /* Default safe values */
  snap->voltage_v = 14.8f;
  snap->current_a = 2.1f;
  snap->r_internal_mohm = 25.0f;
  snap->temp_cells_c[0] = 28.0f;
  snap->temp_cells_c[1] = 28.5f;
  snap->temp_cells_c[2] = 27.8f;
  snap->temp_cells_c[3] = 28.2f;
  snap->temp_ambient_c = 25.0f;
  snap->dt_dt_max = 0.0f;
  snap->gas_ratio = 0.98f;
  snap->pressure_delta_hpa = 0.0f;
  snap->swelling_pct = 2.0f;
  snap->short_circuit = false;

  /* ---- Scenario 1: Normal Operation (0-30s) ---- */
  if (t_s < 30.0f) {
    /* All defaults — everything safe */
    return;
  }

  /* ---- Scenario 2: Thermal Anomaly Only (30-70s) ---- */
  if (t_s < 70.0f) {
    float progress = (t_s - 30.0f) / 40.0f; /* 0.0 → 1.0 */

    /* Cell 3 gradually heats up: 28°C → 72°C */
    snap->temp_cells_c[2] = 28.0f + progress * 44.0f;
    /* Keep dT/dt below emergency-direct threshold (5C/min = 0.083 C/s)
     * so this remains a WARNING-only single-category scenario. */
    snap->dt_dt_max = 0.06f * progress;

    /* Other cells slightly warm */
    snap->temp_cells_c[0] = 28.0f + progress * 2.0f;
    snap->temp_cells_c[1] = 28.5f + progress * 1.5f;
    snap->temp_cells_c[3] = 28.2f + progress * 1.8f;

    /* Gas and pressure stay normal — this is the key!
     * Single-category = WARNING, not EMERGENCY */
    return;
  }

  /* ---- Scenario 3: Gas Anomaly Only (70-100s) ---- */
  if (t_s < 100.0f) {
    float progress = (t_s - 70.0f) / 30.0f;

    /* Temperatures back to normal (the thermal event resolved) */
    snap->temp_cells_c[2] = 35.0f - progress * 5.0f; /* Cooling down */

    /* Gas ratio drops: 0.95 → 0.55 (VOC detected) */
    snap->gas_ratio = 0.95f - progress * 0.40f;

    /* Pressure still normal, no swelling */
    return;
  }

  /* ---- Scenario 4: Multi-Fault Escalation (100-150s) ---- */
  if (t_s < 150.0f) {
    float progress = (t_s - 100.0f) / 50.0f;

    /* Phase 4a (100-120s): Thermal + Gas = CRITICAL */
    /* Keep peak temp below direct-emergency threshold (80C) so
     * CRITICAL/EMERGENCY transitions are driven by correlation categories. */
    snap->temp_cells_c[2] = 45.0f + progress * 33.0f;
    snap->gas_ratio = 0.55f - progress * 0.30f;
    snap->dt_dt_max = 0.03f + progress * 0.04f;

    /* Phase 4b (120-150s): Add pressure = EMERGENCY */
    if (t_s > 120.0f) {
      float p2 = (t_s - 120.0f) / 30.0f;
      snap->pressure_delta_hpa = p2 * 6.0f;
      snap->swelling_pct = 2.0f + p2 * 15.0f;
    }

    /* Voltage drops under heavy fault */
    snap->voltage_v = 14.8f - progress * 3.0f;
    snap->current_a = 2.0f + progress * 4.0f;
    return;
  }

  /* ---- Scenario 5: Short Circuit (150-165s) ---- */
  if (t_s < 165.0f) {
    /* Sudden massive current spike */
    snap->voltage_v = 8.0f;
    snap->current_a = 18.5f;
    snap->short_circuit = true;
    snap->temp_cells_c[2] = 95.0f;
    snap->gas_ratio = 0.20f;
    snap->pressure_delta_hpa = 8.0f;
    snap->swelling_pct = 25.0f;
    return;
  }

  /* ---- Scenario 6: Recovery Attempt (165-185s) ---- */
  /* Sensors go back to normal, but EMERGENCY stays LATCHED.
   * This proves the system doesn't auto-recover from a real event. */
  if (t_s < 185.0f) {
    float progress = (t_s - 165.0f) / 20.0f;
    snap->voltage_v = 8.0f + progress * 6.8f;
    snap->current_a = 18.5f - progress * 16.5f;
    snap->short_circuit = false;
    snap->temp_cells_c[2] = 95.0f - progress * 65.0f;
    snap->gas_ratio = 0.20f + progress * 0.78f;
    snap->pressure_delta_hpa = 8.0f - progress * 8.0f;
    snap->swelling_pct = 25.0f - progress * 23.0f;
    return;
  }

  /* ---- Scenario 7: Ambient Compensation Demo (185-215s) ---- */
  /* Shows that the SAME cell temp behaves differently based on ambient:
   *   Phase A (185-200s): ambient=25°C, cell=45°C → ΔT=20 → WARNING
   *   Phase B (200-215s): ambient=38°C, cell=45°C → ΔT=7  → NORMAL
   * This demonstrates the ambient-compensated thresholds from spec §3.3 */
  if (t_s < 200.0f) {
    /* Phase A: Cold ambient — same 45°C is suspicious */
    snap->temp_cells_c[0] = 44.5f;
    snap->temp_cells_c[1] = 45.0f;
    snap->temp_cells_c[2] = 45.2f;
    snap->temp_cells_c[3] = 44.8f;
    snap->temp_ambient_c = 25.0f;
    return;
  }

  /* Phase B: Hot ambient — same 45°C is expected */
  snap->temp_cells_c[0] = 44.5f;
  snap->temp_cells_c[1] = 45.0f;
  snap->temp_cells_c[2] = 45.2f;
  snap->temp_cells_c[3] = 44.8f;
  snap->temp_ambient_c = 38.0f;
}

/* -----------------------------------------------------------------------
 * FAST LOOP — Short-circuit detection
 * Normal: 100ms, Alert: 20ms
 * ----------------------------------------------------------------------- */
static void fast_loop(void) {
  /* Quick short-circuit check */
  if (g_snapshot.current_a > 15.0f) {
    g_snapshot.short_circuit = true;
    g_anomaly = anomaly_eval_run(&g_thresholds, &g_snapshot);
    correlation_engine_update(&g_corr, &g_anomaly);
    scheduler_apply_sampling_rates();

    if (g_corr.current_state == STATE_EMERGENCY) {
      hal_gpio_set_status_leds(3); /* Red LED */
#if !HAL_HOST_MODE
      hal_gpio_relay_disconnect();
      hal_gpio_buzzer_pulse(1000);
#endif
    }
  }
}

/* -----------------------------------------------------------------------
 * MED LOOP — Evaluation + correlation engine (2 Hz)
 * ----------------------------------------------------------------------- */
static void med_loop(void) {
  /* Compute dR/dt before evaluation */
  if (g_prev_r_int_mohm > 0.0f) {
    float dt_s = (float)g_med_loop_ms / 1000.0f;
    if (dt_s > 0.0f) {
      g_snapshot.dr_dt_mohm_per_s =
          (g_snapshot.r_internal_mohm - g_prev_r_int_mohm) / dt_s;
    }
  }
  g_prev_r_int_mohm = g_snapshot.r_internal_mohm;

  /* Compute core temperature estimate (spec §2.3):
   * T_core = T_surface + I_cell² × R_int × R_thermal
   * For prototype, I_cell = I_pack (1P configuration). */
  float max_surface = g_snapshot.temp_cells_c[0];
  for (int i = 1; i < 4; i++) {
    if (g_snapshot.temp_cells_c[i] > max_surface)
      max_surface = g_snapshot.temp_cells_c[i];
  }
  float i_sq = g_snapshot.current_a * g_snapshot.current_a;
  float r_int_ohm = g_snapshot.r_internal_mohm / 1000.0f;
  g_snapshot.t_core_est_c = max_surface + i_sq * r_int_ohm * R_THERMAL_CW;

  /* Evaluate anomaly categories */
  g_anomaly = anomaly_eval_run(&g_thresholds, &g_snapshot);

  correlation_sync_timing_limits();

  /* Update correlation engine */
  system_state_t prev_state = g_corr.current_state;
  correlation_engine_update(&g_corr, &g_anomaly);
  system_state_t new_state = g_corr.current_state;

  /* Log state transitions */
  if (new_state != prev_state) {
    char buf[80];
    snprintf(buf, sizeof(buf), "[STATE] %s -> %s (cats=%d)\r\n",
             correlation_state_name(prev_state),
             correlation_state_name(new_state), g_anomaly.active_count);
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
    /* Host mode: just print */
    if (new_state != prev_state) {
      printf("[HAL] RELAY TRIGGERED — Battery DISCONNECTED\n");
    }
#endif
  }

  scheduler_apply_sampling_rates();
}

/* -----------------------------------------------------------------------
 * SLOW LOOP — Telemetry packet to dashboard (adaptive: 5s nominal, 1s alert/external)
 * ----------------------------------------------------------------------- */
static void slow_loop(void) {
  /* Encode and send telemetry packet over UART */
  telemetry_packet_t pkt;
  packet_encode(&pkt, g_uptime_ms, &g_snapshot, &g_anomaly,
                g_corr.current_state);
  hal_uart_send((const uint8_t *)&pkt, sizeof(pkt));

  /* Also send human-readable debug line */
  char buf[160];
  snprintf(
      buf, sizeof(buf),
      "[TEL] t=%lums V=%.1f I=%.1f T=[%.0f,%.0f,%.0f,%.0f] "
      "gas=%.2f dP=%.1f state=%s cats=%d\r\n",
      (unsigned long)g_uptime_ms, g_snapshot.voltage_v, g_snapshot.current_a,
      g_snapshot.temp_cells_c[0], g_snapshot.temp_cells_c[1],
      g_snapshot.temp_cells_c[2], g_snapshot.temp_cells_c[3],
      g_snapshot.gas_ratio, g_snapshot.pressure_delta_hpa,
      correlation_state_name(g_corr.current_state), g_anomaly.active_count);
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
  scheduler_reset();
  hal_gpio_set_safety_armed(false);

  hal_uart_print("====================================================\r\n");
  hal_uart_print("  EV Battery Intelligence — Firmware v0.1\r\n");
  hal_uart_print("  Thermal Runaway Prevention System\r\n");
#if HAL_HOST_MODE
  hal_uart_print("  Mode: HOST SIMULATION\r\n");
#else
  hal_uart_print("  Mode: BOARD (VSDSquadron ULTRA / THEJAS32)\r\n");
  hal_uart_print("  Demo: Simulated sensors → Correlation Engine → UART\r\n");
#endif
  hal_uart_print(
      "====================================================\r\n\r\n");
}

/* -----------------------------------------------------------------------
 * MAIN — Entry point (works on both HOST and TARGET)
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

  printf("Running %ds simulation (7 scenarios)...\n\n", SIM_DURATION_S);

  g_uptime_ms = 0;
  scheduler_reset();
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
  /* ---- TARGET: Run on the real board with simulated data ---- */

  if (hal_gpio_is_safety_armed()) {
    hal_gpio_relay_connect();
  } else {
    hal_gpio_relay_disconnect();
  }

  /* Blink all LEDs once to show boot complete */
  hal_gpio_set_status_leds(0); /* Green */
  for (volatile uint32_t d = 0; d < 500000; d++) {
  }
  hal_gpio_set_status_leds(1); /* Yellow */
  for (volatile uint32_t d = 0; d < 500000; d++) {
  }
  hal_gpio_set_status_leds(2); /* Red */
  for (volatile uint32_t d = 0; d < 500000; d++) {
  }
  hal_gpio_set_status_leds(0); /* Back to green = running */

  hal_uart_print("Starting demo loop...\r\n\r\n");
  input_rx_init(&g_input_rx);

  while (1) {
    /* Poll UART RX for external input packets (digital twin) */
    {
      int b;
      while ((b = hal_uart_recv_byte()) >= 0) {
        if (input_rx_feed(&g_input_rx, (uint8_t)b)) {
          /* Valid input packet received — use it! */
          input_packet_t ipkt = input_rx_get(&g_input_rx);
          g_snapshot.voltage_v = ipkt.voltage_cv / 100.0f;
          g_snapshot.current_a = ipkt.current_ca / 100.0f;
          g_snapshot.temp_cells_c[0] = ipkt.temp1_dt / 10.0f;
          g_snapshot.temp_cells_c[1] = ipkt.temp2_dt / 10.0f;
          g_snapshot.temp_cells_c[2] = ipkt.temp3_dt / 10.0f;
          g_snapshot.temp_cells_c[3] = ipkt.temp4_dt / 10.0f;
          g_snapshot.gas_ratio = ipkt.gas_ratio_cp / 100.0f;
          g_snapshot.pressure_delta_hpa = ipkt.pressure_delta_chpa / 100.0f;
          g_snapshot.swelling_pct = (float)ipkt.swelling_pct;
          g_snapshot.r_internal_mohm = 25.0f; /* Default */
          g_snapshot.temp_ambient_c = 25.0f;  /* Default */
          g_snapshot.dt_dt_max = 0.0f;        /* Computed by anomaly_eval */
          g_external_input_active = 1;
          g_last_external_ms = g_uptime_ms;
        }
      }
    }

    /* Use external input if recently received, otherwise fall back to sim */
    if (g_external_input_active &&
        (g_uptime_ms - g_last_external_ms) < EXTERNAL_INPUT_TIMEOUT_MS) {
      /* External data already in g_snapshot — use as-is */
    } else {
      /* No external input — use internal simulation */
      if (g_external_input_active) {
        g_external_input_active = 0;
        hal_uart_print("[EXT] Input timeout — reverting to sim\r\n");
      }
      sim_inject_data(&g_snapshot, g_uptime_ms);
    }

    /* Run scheduler slots when their deadlines are reached */
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

    /* Advance virtual time by scheduler tick */
    g_uptime_ms += SCHED_TICK_MS;

    /* Loop back to start after all scenarios complete */
    if (g_uptime_ms > (uint32_t)(SIM_DURATION_S * 1000)) {
      g_uptime_ms = 0;
      correlation_engine_reset(&g_corr);
      memset(&g_anomaly, 0, sizeof(g_anomaly));
      scheduler_reset();
      hal_uart_print("\r\n--- Restarting demo ---\r\n\r\n");
    }

    /* Delay ~10ms (rough, will be adjusted for THEJAS32 clock)
     * Each iteration is approximately 1µs at ~50MHz.
     * 10ms = 10,000 iterations */
    for (volatile uint32_t d = 0; d < 10000; d++) {
    }
  }
#endif

  return 0;
}
