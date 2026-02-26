/*
 * hal_i2c.c — I2C Implementation (HOST + TARGET)
 */

#include "hal_i2c.h"
#include <string.h>

/* -----------------------------------------------------------------------
 * HOST MODE — Mock I2C with simulated register map
 * ----------------------------------------------------------------------- */
#if HAL_HOST_MODE

/* Simple simulated register storage:
 * sim_regs[device_addr][register_addr] = byte value
 * Limited to 128 addresses × 256 registers for simplicity. */
#define SIM_MAX_DEVICES 128
#define SIM_MAX_REGS 256

static uint8_t sim_regs[SIM_MAX_DEVICES][SIM_MAX_REGS];
static bool sim_device_present[SIM_MAX_DEVICES] = {false};

hal_status_t hal_i2c_init(uint8_t bus) {
  (void)bus;
  return HAL_OK;
}

hal_status_t hal_i2c_write(uint8_t bus, uint8_t addr, const uint8_t *data,
                           uint8_t length) {
  (void)bus;
  if (addr >= SIM_MAX_DEVICES)
    return HAL_ERROR;
  if (!sim_device_present[addr])
    return HAL_ERROR; /* NACK */

  /* First byte is usually the register address, rest is data */
  if (length >= 2) {
    uint8_t reg = data[0];
    for (uint8_t i = 1; i < length; i++) {
      sim_regs[addr][reg + i - 1] = data[i];
    }
  }
  return HAL_OK;
}

hal_status_t hal_i2c_read_reg(uint8_t bus, uint8_t addr, uint8_t reg,
                              uint8_t *buf, uint8_t length) {
  (void)bus;
  if (addr >= SIM_MAX_DEVICES)
    return HAL_ERROR;
  if (!sim_device_present[addr])
    return HAL_ERROR; /* NACK */

  for (uint8_t i = 0; i < length; i++) {
    buf[i] = sim_regs[addr][reg + i];
  }
  return HAL_OK;
}

uint8_t hal_i2c_scan(uint8_t bus, uint8_t *found) {
  (void)bus;
  uint8_t count = 0;
  for (uint8_t addr = 1; addr < SIM_MAX_DEVICES; addr++) {
    if (sim_device_present[addr]) {
      found[count++] = addr;
    }
  }
  return count;
}

void hal_i2c_sim_set_reg(uint8_t addr, uint8_t reg, const uint8_t *data,
                         uint8_t length) {
  if (addr >= SIM_MAX_DEVICES)
    return;
  sim_device_present[addr] = true;
  for (uint8_t i = 0; i < length; i++) {
    sim_regs[addr][reg + i] = data[i];
  }
}

/* -----------------------------------------------------------------------
 * TARGET MODE — Real THEJAS32 I2C
 * ----------------------------------------------------------------------- */
#else

/*
 * Target I2C register integration placeholder.
 *
 * The board-in-loop demo path can ingest twin-fed frames over UART.
 * For direct sensor bus acquisition, map these calls to THEJAS32 I2C
 * memory-mapped registers.
 *
 * Typical initialization:
 *   1. Enable I2C clock in system control
 *   2. Set SCL frequency (100kHz or 400kHz)
 *   3. Enable I2C controller
 *
 * For each transaction:
 *   1. Set slave address
 *   2. Write register address
 *   3. Read/write data bytes
 *   4. Check ACK/NACK status
 */

hal_status_t hal_i2c_init(uint8_t bus) {
  (void)bus;
  /* TODO: THEJAS32 I2C init */
  return HAL_OK;
}

hal_status_t hal_i2c_write(uint8_t bus, uint8_t addr, const uint8_t *data,
                           uint8_t length) {
  (void)bus;
  (void)addr;
  (void)data;
  (void)length;
  /* TODO: THEJAS32 I2C write */
  return HAL_OK;
}

hal_status_t hal_i2c_read_reg(uint8_t bus, uint8_t addr, uint8_t reg,
                              uint8_t *buf, uint8_t length) {
  (void)bus;
  (void)addr;
  (void)reg;
  (void)buf;
  (void)length;
  /* TODO: THEJAS32 I2C read */
  return HAL_OK;
}

uint8_t hal_i2c_scan(uint8_t bus, uint8_t *found) {
  (void)bus;
  (void)found;
  /* TODO: THEJAS32 I2C scan */
  return 0;
}

#endif /* HAL_HOST_MODE */
