/*
 * fsr.h — FSR402 Force-Sensitive Resistor Driver
 *
 * Detects cell swelling (mechanical deformation).
 * When a Li-Ion cell starts to fail, internal gas buildup causes
 * the cell to physically expand. An FSR pressed against the cell
 * detects this expansion.
 *
 * Connected to: ADC channel (via voltage divider)
 */

#ifndef FSR_H
#define FSR_H

#include "../hal/hal_platform.h"

/* FSR reading */
typedef struct {
  uint16_t raw_adc;   /* Raw ADC value (0-4095) */
  float force_n;      /* Approximate force in Newtons */
  float swelling_pct; /* 0-100% normalized force */
} fsr_reading_t;

/*
 * Initialize the FSR sensor (configures ADC channel).
 */
hal_status_t fsr_init(void);

/*
 * Read the FSR sensor.
 * Returns force and a 0-100% normalized swelling value.
 */
hal_status_t fsr_read(fsr_reading_t *reading);

#if HAL_HOST_MODE
void fsr_sim_set(float swelling_pct);
#endif

#endif /* FSR_H */
