/*
 * hal_adc.c — ADC Implementation (HOST + TARGET)
 */

#include "hal_adc.h"

/* -----------------------------------------------------------------------
 * HOST MODE — Mock implementation for Mac testing
 * ----------------------------------------------------------------------- */
#if HAL_HOST_MODE

static uint16_t sim_values[ADC_NUM_CHANNELS] = {0};

hal_status_t hal_adc_init(void) {
  /* Nothing to initialize on host */
  return HAL_OK;
}

int16_t hal_adc_read_raw(uint8_t channel) {
  if (channel >= ADC_NUM_CHANNELS)
    return HAL_ERROR;
  return (int16_t)sim_values[channel];
}

int16_t hal_adc_read_mv(uint8_t channel) {
  int16_t raw = hal_adc_read_raw(channel);
  if (raw < 0)
    return raw;
  /* Convert 12-bit raw value to millivolts */
  return (int16_t)((uint32_t)raw * ADC_VREF_MV / ADC_MAX_VALUE);
}

void hal_adc_sim_set(uint8_t channel, uint16_t raw_value) {
  if (channel < ADC_NUM_CHANNELS) {
    sim_values[channel] = raw_value;
  }
}

/* -----------------------------------------------------------------------
 * TARGET MODE — Real THEJAS32 hardware
 * ----------------------------------------------------------------------- */
#else

/*
 * Target ADC register integration placeholder.
 *
 * For direct on-board sensor acquisition (without twin-fed frames),
 * map these calls to THEJAS32 memory-mapped ADC registers.
 *
 * Typical flow:
 *   1. Enable ADC clock
 *   2. Configure channel and sample time
 *   3. Start conversion
 *   4. Wait for conversion complete flag
 *   5. Read data register
 */

hal_status_t hal_adc_init(void) {
  /* TODO: Configure THEJAS32 ADC registers */
  return HAL_OK;
}

int16_t hal_adc_read_raw(uint8_t channel) {
  (void)channel;
  /* TODO: Read THEJAS32 ADC data register */
  return 0;
}

int16_t hal_adc_read_mv(uint8_t channel) {
  int16_t raw = hal_adc_read_raw(channel);
  if (raw < 0)
    return raw;
  return (int16_t)((uint32_t)raw * ADC_VREF_MV / ADC_MAX_VALUE);
}

#endif /* HAL_HOST_MODE */
