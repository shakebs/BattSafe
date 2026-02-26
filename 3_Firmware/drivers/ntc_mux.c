/*
 * ntc_mux.c — NTC Thermistor Array Driver Implementation
 */

#include "ntc_mux.h"
#include "../hal/hal_adc.h"
#include "../hal/hal_gpio.h"
#include <math.h>

/* Previous reading for dT/dt computation (compat subset channels) */
static float prev_temps[NTC_NUM_CELLS] = {25.0f, 25.0f, 25.0f, 25.0f};
static bool first_reading = true;

/* -----------------------------------------------------------------------
 * NTC Temperature Conversion
 *
 * Uses the simplified Steinhart-Hart equation (B-parameter model):
 *   1/T = 1/T0 + (1/B) × ln(R/R0)
 *
 * The voltage divider circuit:
 *   3.3V → [10kΩ pullup] → [ADC pin] → [NTC] → GND
 *
 * So: R_ntc = R_series × ADC_raw / (ADC_MAX - ADC_raw)
 * ----------------------------------------------------------------------- */

float ntc_adc_to_temp_c(uint16_t adc_raw) {
  if (adc_raw == 0 || adc_raw >= ADC_MAX_VALUE) {
    return -999.0f; /* Error: open or short circuit */
  }

  /* Calculate NTC resistance from voltage divider */
  float r_ntc =
      NTC_R_SERIES * (float)adc_raw / (float)(ADC_MAX_VALUE - adc_raw);

  /* Steinhart-Hart simplified (B-parameter equation) */
  float steinhart = logf(r_ntc / NTC_R_NOMINAL) / NTC_BETA;
  steinhart += 1.0f / (NTC_T_NOMINAL + 273.15f);
  float temp_k = 1.0f / steinhart;
  float temp_c = temp_k - 273.15f;

  return temp_c;
}

/* -----------------------------------------------------------------------
 * HOST MODE
 * ----------------------------------------------------------------------- */
#if HAL_HOST_MODE

static float sim_temps[NTC_NUM_CHANNELS] = {28.0f, 28.5f, 27.8f, 28.2f, 25.0f};

hal_status_t ntc_mux_init(void) {
  first_reading = true;
  return HAL_OK;
}

hal_status_t ntc_mux_read_all(ntc_reading_t *r) {
  /* Copy simulated subset channels */
  for (int i = 0; i < NTC_NUM_CELLS; i++) {
    r->cell_temps_c[i] = sim_temps[i];
  }
  r->ambient_c = sim_temps[NTC_MUX_CH_AMBIENT];

  /* Find max temperature */
  r->max_temp_c = r->cell_temps_c[0];
  for (int i = 1; i < NTC_NUM_CELLS; i++) {
    if (r->cell_temps_c[i] > r->max_temp_c) {
      r->max_temp_c = r->cell_temps_c[i];
    }
  }

  /* Find max point-to-point difference */
  float min_temp = r->cell_temps_c[0];
  for (int i = 1; i < NTC_NUM_CELLS; i++) {
    if (r->cell_temps_c[i] < min_temp)
      min_temp = r->cell_temps_c[i];
  }
  r->max_delta_c = r->max_temp_c - min_temp;

  /* Compute dT/dt (°C per second) — assuming 500ms between reads */
  r->dt_dt_max = 0.0f;
  if (!first_reading) {
    for (int i = 0; i < NTC_NUM_CELLS; i++) {
      float dt = (r->cell_temps_c[i] - prev_temps[i]) / 0.5f;
      if (dt > r->dt_dt_max)
        r->dt_dt_max = dt;
    }
  }
  first_reading = false;

  /* Save for next dT/dt computation */
  for (int i = 0; i < NTC_NUM_CELLS; i++) {
    prev_temps[i] = r->cell_temps_c[i];
  }

  return HAL_OK;
}

void ntc_sim_set_temps(const float temps[NTC_NUM_CHANNELS]) {
  for (int i = 0; i < NTC_NUM_CHANNELS; i++) {
    sim_temps[i] = temps[i];
  }
}

/* -----------------------------------------------------------------------
 * TARGET MODE
 * ----------------------------------------------------------------------- */
#else

hal_status_t ntc_mux_init(void) {
  hal_adc_init();
  hal_gpio_init();
  first_reading = true;
  return HAL_OK;
}

hal_status_t ntc_mux_read_all(ntc_reading_t *r) {
  /* Read each configured subset thermistor channel via MUX */
  for (uint8_t ch = 0; ch < NTC_NUM_CHANNELS; ch++) {
    /* Set MUX channel */
    hal_gpio_mux_select(ch);

    /* Brief delay for MUX output to settle (~10µs typical)
     * TODO: Replace with proper microsecond delay */
    for (volatile int d = 0; d < 100; d++) {
    }

    /* Read ADC */
    int16_t raw = hal_adc_read_raw(ADC_CHANNEL_MUX_OUT);
    if (raw < 0) {
      if (ch < NTC_NUM_CELLS)
        r->cell_temps_c[ch] = -999.0f;
      else
        r->ambient_c = -999.0f;
      continue;
    }

    float temp = ntc_adc_to_temp_c((uint16_t)raw);
    if (ch < NTC_NUM_CELLS) {
      r->cell_temps_c[ch] = temp;
    } else {
      r->ambient_c = temp;
    }
  }

  /* Compute derived values (same as host mode) */
  r->max_temp_c = r->cell_temps_c[0];
  for (int i = 1; i < NTC_NUM_CELLS; i++) {
    if (r->cell_temps_c[i] > r->max_temp_c) {
      r->max_temp_c = r->cell_temps_c[i];
    }
  }

  float min_temp = r->cell_temps_c[0];
  for (int i = 1; i < NTC_NUM_CELLS; i++) {
    if (r->cell_temps_c[i] < min_temp)
      min_temp = r->cell_temps_c[i];
  }
  r->max_delta_c = r->max_temp_c - min_temp;

  r->dt_dt_max = 0.0f;
  if (!first_reading) {
    for (int i = 0; i < NTC_NUM_CELLS; i++) {
      float dt = (r->cell_temps_c[i] - prev_temps[i]) / 0.5f;
      if (dt > r->dt_dt_max)
        r->dt_dt_max = dt;
    }
  }
  first_reading = false;

  for (int i = 0; i < NTC_NUM_CELLS; i++) {
    prev_temps[i] = r->cell_temps_c[i];
  }

  return HAL_OK;
}

#endif /* HAL_HOST_MODE */
