/*
 * hal_gpio.h — GPIO (General Purpose Input/Output) Abstraction
 *
 * Used for:
 *   - CD4051 MUX channel select pins (S0, S1, S2) — to select which thermistor
 *   - Relay driver pin — to disconnect the battery module on emergency
 *   - Status LEDs — visual indicators
 *   - Buzzer — audio alert
 */

#ifndef HAL_GPIO_H
#define HAL_GPIO_H

#include "hal_platform.h"

/* GPIO pin assignments on VSDSquadron ULTRA
 * These will be updated when we have the actual pinout. */
#define GPIO_PIN_MUX_S0 2     /* CD4051 channel select bit 0 */
#define GPIO_PIN_MUX_S1 3     /* CD4051 channel select bit 1 */
#define GPIO_PIN_MUX_S2 4     /* CD4051 channel select bit 2 */
#define GPIO_PIN_RELAY 5      /* Relay driver (HIGH = disconnect) */
#define GPIO_PIN_LED_GREEN 6  /* Status: Normal */
#define GPIO_PIN_LED_YELLOW 7 /* Status: Warning */
#define GPIO_PIN_LED_RED 8    /* Status: Critical/Emergency */
#define GPIO_PIN_BUZZER 9     /* Audio alert */

typedef enum {
  GPIO_MODE_INPUT = 0,
  GPIO_MODE_OUTPUT = 1,
} gpio_mode_t;

typedef enum {
  GPIO_LOW = 0,
  GPIO_HIGH = 1,
} gpio_level_t;

/*
 * Initialize the GPIO subsystem.
 * Configures all pins to safe defaults:
 *   - Relay pin = HIGH (battery disconnected on boot, fail-safe)
 *   - LED pins = OUTPUT LOW
 *   - MUX pins = OUTPUT LOW (select channel 0)
 */
hal_status_t hal_gpio_init(void);

/* Set a pin as input or output */
hal_status_t hal_gpio_set_mode(uint8_t pin, gpio_mode_t mode);

/* Write HIGH or LOW to an output pin */
hal_status_t hal_gpio_write(uint8_t pin, gpio_level_t level);

/* Read the current level of a pin */
gpio_level_t hal_gpio_read(uint8_t pin);

/* -----------------------------------------------------------------------
 * Convenience functions for common operations
 * ----------------------------------------------------------------------- */

/* Set the CD4051 MUX channel (0-7) to select which thermistor to read */
void hal_gpio_mux_select(uint8_t channel);

/* Activate the relay to disconnect the battery (EMERGENCY action) */
void hal_gpio_relay_disconnect(void);

/* Deactivate the relay (re-connect battery — only works when safety is armed) */
void hal_gpio_relay_connect(void);

/* Safety arm/disarm gate for relay connect path */
void hal_gpio_set_safety_armed(bool armed);
bool hal_gpio_is_safety_armed(void);

/* Set status LEDs based on system state */
void hal_gpio_set_status_leds(uint8_t state);

/* Sound the buzzer for a specified duration in milliseconds */
void hal_gpio_buzzer_pulse(uint16_t duration_ms);

#endif /* HAL_GPIO_H */
