/*
 * bme680.h — BME680 Gas/Pressure/Temp/Humidity Sensor Driver
 *
 * The BME680 is our KEY differentiator. It detects:
 *   - Volatile Organic Compounds (VOCs) from electrolyte decomposition
 *   - Enclosure pressure changes from cell venting
 *
 * Both signals appear 2-5 minutes BEFORE the temperature spike.
 *
 * I2C Address: 0x76 (SDO=GND) or 0x77 (SDO=VDD)
 */

#ifndef BME680_H
#define BME680_H

#include "../hal/hal_platform.h"

/* I2C address — adjust if needed */
#define BME680_ADDR 0x76

/* Readings */
typedef struct {
  float gas_resistance_ohm; /* Raw gas resistance from sensor     */
  float gas_ratio;          /* current/baseline (1.0 = normal)    */
  float pressure_hpa;       /* Absolute pressure in hPa           */
  float pressure_delta_hpa; /* Change from baseline               */
  float temperature_c;      /* On-chip temperature                */
  float humidity_pct;       /* Relative humidity %                */
} bme680_reading_t;

/*
 * Initialize the BME680 sensor.
 * Configures measurement parameters and takes initial baseline readings.
 */
hal_status_t bme680_init(void);

/*
 * Read all values from the BME680.
 * Computes gas_ratio and pressure_delta relative to baseline.
 */
hal_status_t bme680_read(bme680_reading_t *reading);

/*
 * Reset the gas and pressure baselines.
 * Call this after the system has been in a known-safe state
 * for several minutes to establish a new reference.
 */
void bme680_reset_baseline(void);

/* -----------------------------------------------------------------------
 * HOST-MODE: set simulated values
 * ----------------------------------------------------------------------- */
#if HAL_HOST_MODE
void bme680_sim_set(float gas_ratio, float pressure_delta_hpa,
                    float temperature_c, float humidity_pct);
#endif

#endif /* BME680_H */
