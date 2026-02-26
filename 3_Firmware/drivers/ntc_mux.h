/*
 * ntc_mux.h — NTC Thermistor Array via CD4051 MUX
 *
 * Reads 4 cell temperatures + 1 ambient temperature using
 * NTC thermistors connected through an analog multiplexer.
 *
 * How it works:
 *   1. Set MUX channel select pins (S0, S1, S2) via GPIO
 *   2. Wait briefly for signal to settle
 *   3. Read the MUX output via ADC
 *   4. Convert raw ADC value to temperature using NTC curve
 *   5. Repeat for each channel
 */

#ifndef NTC_MUX_H
#define NTC_MUX_H

#include "../hal/hal_platform.h"

/* Number of thermistors */
#define NTC_NUM_CELLS 4
#define NTC_NUM_CHANNELS 5 /* 4 cells + 1 ambient */

/* MUX channel assignments */
#define NTC_MUX_CH_CELL1 0
#define NTC_MUX_CH_CELL2 1
#define NTC_MUX_CH_CELL3 2
#define NTC_MUX_CH_CELL4 3
#define NTC_MUX_CH_AMBIENT 4

/* NTC thermistor parameters (10kΩ NTC, B=3950) */
#define NTC_R_NOMINAL 10000.0f /* Resistance at 25°C */
#define NTC_T_NOMINAL 25.0f    /* Reference temperature */
#define NTC_BETA 3950.0f       /* B coefficient */
#define NTC_R_SERIES 10000.0f  /* Series resistor (10kΩ pullup) */

/* Temperature readings */
typedef struct {
  float cell_temps_c[NTC_NUM_CELLS]; /* Cell surface temperatures */
  float ambient_c;                   /* Ambient temperature */
  float max_temp_c;                  /* Highest cell temperature */
  float max_delta_c;                 /* Max cell-to-cell difference */
  float dt_dt_max;                   /* Max rate of change (°C/s) */
} ntc_reading_t;

/*
 * Initialize the NTC subsystem (configures MUX pins + ADC).
 */
hal_status_t ntc_mux_init(void);

/*
 * Read all thermistors through the MUX.
 * This takes ~5ms (5 channels × settling time).
 */
hal_status_t ntc_mux_read_all(ntc_reading_t *reading);

/*
 * Convert a raw ADC value to temperature in °C.
 * Uses the Steinhart-Hart simplified (B-parameter) equation.
 */
float ntc_adc_to_temp_c(uint16_t adc_raw);

#if HAL_HOST_MODE
void ntc_sim_set_temps(const float temps[NTC_NUM_CHANNELS]);
#endif

#endif /* NTC_MUX_H */
