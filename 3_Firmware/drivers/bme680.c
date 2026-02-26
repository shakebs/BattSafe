/*
 * bme680.c — BME680 Driver Implementation
 *
 * NOTE: The BME680 is a complex sensor with a multi-step measurement
 * process. For the real hardware, consider using Bosch's official
 * BSEC library (bme68x) which handles the complex compensation
 * algorithms. This driver provides the interface and baseline tracking.
 */

#include "bme680.h"
#include "../hal/hal_i2c.h"

/* Baseline values (set during init, updated periodically) */
static float gas_baseline_ohm = 50000.0f;      /* Typical clean air value */
static float pressure_baseline_hpa = 1013.25f; /* Standard atmosphere */

/* -----------------------------------------------------------------------
 * HOST MODE
 * ----------------------------------------------------------------------- */
#if HAL_HOST_MODE

static float sim_gas_ratio = 0.98f;
static float sim_pressure_delta = 0.0f;
static float sim_temperature = 25.0f;
static float sim_humidity = 45.0f;

hal_status_t bme680_init(void) { return HAL_OK; }

hal_status_t bme680_read(bme680_reading_t *r) {
  r->gas_resistance_ohm = gas_baseline_ohm * sim_gas_ratio;
  r->gas_ratio = sim_gas_ratio;
  r->pressure_hpa = pressure_baseline_hpa + sim_pressure_delta;
  r->pressure_delta_hpa = sim_pressure_delta;
  r->temperature_c = sim_temperature;
  r->humidity_pct = sim_humidity;
  return HAL_OK;
}

void bme680_reset_baseline(void) { /* In sim mode, baseline is already set */ }

void bme680_sim_set(float gas_ratio, float pressure_delta_hpa,
                    float temperature_c, float humidity_pct) {
  sim_gas_ratio = gas_ratio;
  sim_pressure_delta = pressure_delta_hpa;
  sim_temperature = temperature_c;
  sim_humidity = humidity_pct;
}

/* -----------------------------------------------------------------------
 * TARGET MODE
 * ----------------------------------------------------------------------- */
#else

/* BME680 register addresses (key ones) */
#define BME680_REG_CHIP_ID 0xD0
#define BME680_REG_CTRL_MEAS 0x74
#define BME680_REG_CTRL_HUM 0x72
#define BME680_REG_CTRL_GAS 0x71
#define BME680_REG_GAS_WAIT 0x64
#define BME680_REG_RES_HEAT 0x5A
#define BME680_CHIP_ID_VALUE 0x61

static float baseline_samples[10];
static uint8_t baseline_idx = 0;

hal_status_t bme680_init(void) {
  /* Verify chip ID */
  uint8_t chip_id = 0;
  hal_status_t status = hal_i2c_read_reg(I2C_BUS_DEFAULT, BME680_ADDR,
                                         BME680_REG_CHIP_ID, &chip_id, 1);
  if (status != HAL_OK)
    return status;
  if (chip_id != BME680_CHIP_ID_VALUE)
    return HAL_ERROR;

  /* TODO: Configure measurement parameters
   * - Temperature oversampling: x2
   * - Pressure oversampling: x16
   * - Humidity oversampling: x1
   * - Gas heater: 320°C for 150ms (standard VOC detection profile)
   *
   * Consider using Bosch BSEC library for accurate gas computation.
   */

  return HAL_OK;
}

hal_status_t bme680_read(bme680_reading_t *r) {
  /* TODO: Trigger forced measurement and read results
   *
   * Steps:
   * 1. Set forced mode in ctrl_meas register
   * 2. Wait for measurement complete (check status register)
   * 3. Read temp, pressure, humidity data registers
   * 4. Apply compensation formulas from datasheet
   * 5. Read gas resistance data register
   * 6. Compute gas_ratio = current / baseline
   * 7. Compute pressure_delta = current - baseline
   */

  (void)r;
  return HAL_OK;
}

void bme680_reset_baseline(void) {
  /* TODO: Take multiple readings over 2 minutes
   * and average them to establish new baseline */
  baseline_idx = 0;
}

#endif /* HAL_HOST_MODE */
