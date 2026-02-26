/*
 * hal_adc.h — ADC (Analog-to-Digital Converter) Abstraction
 *
 * The VSDSquadron ULTRA has ADC channels that we use to read:
 *   - NTC thermistors (temperature) via the CD4051 analog MUX
 *   - FSR402 (cell swelling force)
 *
 * On HOST mode: returns simulated values for testing.
 * On TARGET mode: reads the THEJAS32 ADC registers.
 */

#ifndef HAL_ADC_H
#define HAL_ADC_H

#include "hal_platform.h"

/* ADC channel assignments on the VSDSquadron ULTRA */
#define ADC_CHANNEL_MUX_OUT 0 /* CD4051 MUX output (thermistors) */
#define ADC_CHANNEL_FSR 1     /* FSR402 force sensor */
#define ADC_NUM_CHANNELS 2

/* ADC resolution: 12-bit (0-4095) */
#define ADC_MAX_VALUE 4095
#define ADC_VREF_MV 3300 /* 3.3V reference in millivolts */

/*
 * Initialize the ADC peripheral.
 * Must be called once at startup before any reads.
 */
hal_status_t hal_adc_init(void);

/*
 * Read a raw ADC value from the specified channel.
 *
 * channel: ADC_CHANNEL_MUX_OUT or ADC_CHANNEL_FSR
 * Returns: 0-4095 (12-bit), or negative on error
 */
int16_t hal_adc_read_raw(uint8_t channel);

/*
 * Read an ADC channel and convert to millivolts.
 *
 * channel: ADC_CHANNEL_MUX_OUT or ADC_CHANNEL_FSR
 * Returns: voltage in mV (0-3300), or negative on error
 */
int16_t hal_adc_read_mv(uint8_t channel);

/* -----------------------------------------------------------------------
 * HOST-MODE simulation helpers (only available in mock builds)
 * ----------------------------------------------------------------------- */
#if HAL_HOST_MODE
/*
 * Set a simulated ADC value for testing.
 * Only callable in HOST mode — ignored on real hardware.
 */
void hal_adc_sim_set(uint8_t channel, uint16_t raw_value);
#endif

#endif /* HAL_ADC_H */
