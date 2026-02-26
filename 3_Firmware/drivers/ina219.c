/*
 * ina219.c — INA219 Driver Implementation
 */

#include "ina219.h"
#include "../hal/hal_i2c.h"

/* -----------------------------------------------------------------------
 * HOST MODE
 * ----------------------------------------------------------------------- */
#if HAL_HOST_MODE

static float sim_voltage = 14.8f;
static float sim_current = 2.0f;

hal_status_t ina219_init(void) { return HAL_OK; }

hal_status_t ina219_read(ina219_reading_t *r) {
  r->voltage_v = sim_voltage;
  r->current_a = sim_current;
  r->power_w = sim_voltage * sim_current;

  /* Compute internal resistance: R = V_drop / I
   * V_drop = V_nominal - V_measured
   * For a 4S pack, nominal is ~16.8V fully charged */
  float v_drop = 16.8f - sim_voltage;
  if (sim_current > 0.1f) {
    r->r_internal_mohm = (v_drop / sim_current) * 1000.0f;
  } else {
    r->r_internal_mohm = 0.0f;
  }

  return HAL_OK;
}

void ina219_sim_set(float voltage_v, float current_a) {
  sim_voltage = voltage_v;
  sim_current = current_a;
}

/* -----------------------------------------------------------------------
 * TARGET MODE
 * ----------------------------------------------------------------------- */
#else

hal_status_t ina219_init(void) {
  /* TODO: Write configuration register
   * Recommended config for 4S battery monitoring:
   *   - Bus voltage range: 16V (BRNG = 0)
   *   - Shunt voltage range: ±160mV (PGA = /4)
   *   - ADC resolution: 12-bit
   *   - Mode: continuous shunt and bus
   *
   * Config value: 0x019F (example, verify against datasheet)
   */
  uint8_t config_data[3] = {INA219_REG_CONFIG, 0x01, 0x9F};
  hal_status_t status =
      hal_i2c_write(I2C_BUS_DEFAULT, INA219_ADDR, config_data, 3);
  if (status != HAL_OK)
    return status;

  /* TODO: Write calibration register based on shunt resistor value
   * Cal = trunc(0.04096 / (current_LSB × R_shunt))
   * For R_shunt = 0.1 ohm, current_LSB = 0.1mA: Cal = 4096 */
  uint8_t cal_data[3] = {INA219_REG_CALIB, 0x10, 0x00};
  return hal_i2c_write(I2C_BUS_DEFAULT, INA219_ADDR, cal_data, 3);
}

hal_status_t ina219_read(ina219_reading_t *r) {
  uint8_t buf[2] = {0};

  /* Read bus voltage register */
  hal_status_t status =
      hal_i2c_read_reg(I2C_BUS_DEFAULT, INA219_ADDR, INA219_REG_BUS_V, buf, 2);
  if (status != HAL_OK)
    return status;

  /* Bus voltage: bits [15:3] × 4mV, bit 1 = conversion ready */
  uint16_t raw_bus = ((uint16_t)buf[0] << 8) | buf[1];
  r->voltage_v = (float)(raw_bus >> 3) * 0.004f;

  /* Read shunt voltage register */
  status = hal_i2c_read_reg(I2C_BUS_DEFAULT, INA219_ADDR, INA219_REG_SHUNT_V,
                            buf, 2);
  if (status != HAL_OK)
    return status;

  /* Shunt voltage in 10µV steps → current = V_shunt / R_shunt */
  int16_t raw_shunt = (int16_t)((uint16_t)buf[0] << 8 | buf[1]);
  float shunt_v = (float)raw_shunt * 0.00001f; /* 10µV per LSB */
  r->current_a = shunt_v / (INA219_SHUNT_RESISTOR_MOHM / 1000.0f);

  /* Power */
  r->power_w = r->voltage_v * r->current_a;

  /* Internal resistance estimate */
  float v_drop = 16.8f - r->voltage_v;
  if (r->current_a > 0.1f) {
    r->r_internal_mohm = (v_drop / r->current_a) * 1000.0f;
  } else {
    r->r_internal_mohm = 0.0f;
  }

  return HAL_OK;
}

#endif /* HAL_HOST_MODE */
