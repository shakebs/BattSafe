/*
 * hal_i2c.h — I2C Bus Abstraction
 *
 * The VSDSquadron ULTRA uses I2C to communicate with:
 *   - INA219 (voltage/current sensor)     — address 0x40
 *   - BME680 (gas/pressure/temp/humidity)  — address 0x76 or 0x77
 *
 * On HOST mode: simulated register reads/writes for testing.
 * On TARGET mode: THEJAS32 I2C peripheral access.
 */

#ifndef HAL_I2C_H
#define HAL_I2C_H

#include "hal_platform.h"

/* I2C bus number (THEJAS32 has multiple I2C controllers) */
#define I2C_BUS_DEFAULT 0

/*
 * Initialize the I2C bus.
 * bus: hardware bus number (use I2C_BUS_DEFAULT)
 */
hal_status_t hal_i2c_init(uint8_t bus);

/*
 * Write data to an I2C device.
 *
 * bus:      I2C bus number
 * addr:     7-bit device address (e.g., 0x40 for INA219)
 * data:     pointer to bytes to send
 * length:   number of bytes to send
 *
 * Returns: HAL_OK on success, HAL_ERROR on NACK, HAL_TIMEOUT on bus hang
 */
hal_status_t hal_i2c_write(uint8_t bus, uint8_t addr, const uint8_t *data,
                           uint8_t length);

/*
 * Read data from an I2C device.
 *
 * bus:      I2C bus number
 * addr:     7-bit device address
 * reg:      register address to read from
 * buf:      buffer to store received bytes
 * length:   number of bytes to read
 */
hal_status_t hal_i2c_read_reg(uint8_t bus, uint8_t addr, uint8_t reg,
                              uint8_t *buf, uint8_t length);

/*
 * Scan the I2C bus and return which addresses respond.
 * This is useful for debugging — run it first when the board arrives
 * to verify sensors are connected correctly.
 *
 * found:    output array (must be at least 128 bytes)
 * Returns:  number of devices found
 */
uint8_t hal_i2c_scan(uint8_t bus, uint8_t *found);

/* -----------------------------------------------------------------------
 * HOST-MODE simulation helpers
 * ----------------------------------------------------------------------- */
#if HAL_HOST_MODE
/*
 * Set simulated register values for a device.
 * When hal_i2c_read_reg() is called for this device+register,
 * it will return these bytes.
 */
void hal_i2c_sim_set_reg(uint8_t addr, uint8_t reg, const uint8_t *data,
                         uint8_t length);
#endif

#endif /* HAL_I2C_H */
