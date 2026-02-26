/*
 * fsr.c — FSR402 Driver Implementation
 */

#include "fsr.h"
#include "../hal/hal_adc.h"

/* The FSR402 has a logarithmic response.
 * Approximate conversion: bigger force → lower resistance → higher ADC.
 * We normalize to 0-100% where 100% = maximum expected swelling force. */

#define FSR_ADC_MAX_FORCE 3000 /* ADC value at "max swelling" */

/* -----------------------------------------------------------------------
 * HOST MODE
 * ----------------------------------------------------------------------- */
#if HAL_HOST_MODE

static float sim_swelling = 2.0f;

hal_status_t fsr_init(void) { return HAL_OK; }

hal_status_t fsr_read(fsr_reading_t *r) {
  r->raw_adc = (uint16_t)(sim_swelling / 100.0f * FSR_ADC_MAX_FORCE);
  r->swelling_pct = sim_swelling;
  r->force_n = sim_swelling * 0.2f; /* Rough estimate */
  return HAL_OK;
}

void fsr_sim_set(float swelling_pct) { sim_swelling = swelling_pct; }

/* -----------------------------------------------------------------------
 * TARGET MODE
 * ----------------------------------------------------------------------- */
#else

hal_status_t fsr_init(void) { return hal_adc_init(); }

hal_status_t fsr_read(fsr_reading_t *r) {
  int16_t raw = hal_adc_read_raw(ADC_CHANNEL_FSR);
  if (raw < 0)
    return HAL_ERROR;

  r->raw_adc = (uint16_t)raw;

  /* Normalize to percentage */
  if (raw > FSR_ADC_MAX_FORCE)
    raw = FSR_ADC_MAX_FORCE;
  r->swelling_pct = (float)raw / (float)FSR_ADC_MAX_FORCE * 100.0f;

  /* Approximate force (FSR402 logarithmic response) */
  r->force_n = r->swelling_pct * 0.2f;

  return HAL_OK;
}

#endif /* HAL_HOST_MODE */
