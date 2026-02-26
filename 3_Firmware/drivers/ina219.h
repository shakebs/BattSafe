/*
 * ina219.h — INA219 Voltage/Current Sensor Driver
 *
 * The INA219 is an I2C-based high-side current and voltage sensor.
 * In this repository it is used as a bench/compatibility interface.
 * Full-pack 104S8P logic uses pack snapshot channels in the runtime
 * pipeline (`main` + `anomaly_eval`) and can map to isolated sensors.
 *
 * I2C Address: 0x40 (default, A0=A1=GND)
 */

#ifndef INA219_H
#define INA219_H

#include "../hal/hal_platform.h"

/* INA219 I2C address */
#define INA219_ADDR 0x40

/* INA219 register addresses */
#define INA219_REG_CONFIG 0x00
#define INA219_REG_SHUNT_V 0x01 /* Shunt voltage (across sense resistor) */
#define INA219_REG_BUS_V 0x02   /* Bus voltage (battery voltage) */
#define INA219_REG_POWER 0x03
#define INA219_REG_CURRENT 0x04
#define INA219_REG_CALIB 0x05

/* Calibration values for the configured shunt interface */
#define INA219_SHUNT_RESISTOR_MOHM 100 /* 100 milliohms = 0.1 ohm */

/* Readings */
typedef struct {
  float voltage_v;       /* Bus voltage in volts          */
  float current_a;       /* Current in amps               */
  float power_w;         /* Power in watts (V × I)        */
  float r_internal_mohm; /* Computed internal resistance  */
} ina219_reading_t;

/*
 * Initialize the INA219 sensor.
 * Sets up configuration and calibration registers.
 */
hal_status_t ina219_init(void);

/*
 * Read all values from the INA219.
 * Fills in voltage, current, power, and computes R_int.
 */
hal_status_t ina219_read(ina219_reading_t *reading);

/* -----------------------------------------------------------------------
 * HOST-MODE: set simulated values
 * ----------------------------------------------------------------------- */
#if HAL_HOST_MODE
void ina219_sim_set(float voltage_v, float current_a);
#endif

#endif /* INA219_H */
