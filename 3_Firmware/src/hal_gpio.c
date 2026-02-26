/*
 * hal_gpio.c — GPIO Implementation (HOST + TARGET)
 */

#include "hal_gpio.h"

/* -----------------------------------------------------------------------
 * HOST MODE
 * ----------------------------------------------------------------------- */
#if HAL_HOST_MODE

#include <stdio.h>

#define MAX_PINS 32
static gpio_level_t pin_states[MAX_PINS] = {0};
static gpio_mode_t pin_modes[MAX_PINS] = {0};
static bool g_safety_armed = false;

hal_status_t hal_gpio_init(void) {
  /* Set safe defaults */
  for (int i = 0; i < MAX_PINS; i++) {
    pin_states[i] = GPIO_LOW;
    pin_modes[i] = GPIO_MODE_INPUT;
  }
  /* Configure output pins */
  pin_modes[GPIO_PIN_MUX_S0] = GPIO_MODE_OUTPUT;
  pin_modes[GPIO_PIN_MUX_S1] = GPIO_MODE_OUTPUT;
  pin_modes[GPIO_PIN_MUX_S2] = GPIO_MODE_OUTPUT;
  pin_modes[GPIO_PIN_RELAY] = GPIO_MODE_OUTPUT;
  pin_modes[GPIO_PIN_LED_GREEN] = GPIO_MODE_OUTPUT;
  pin_modes[GPIO_PIN_LED_YELLOW] = GPIO_MODE_OUTPUT;
  pin_modes[GPIO_PIN_LED_RED] = GPIO_MODE_OUTPUT;
  pin_modes[GPIO_PIN_BUZZER] = GPIO_MODE_OUTPUT;

  /* Relay starts HIGH = battery disconnected (fail-safe default) */
  pin_states[GPIO_PIN_RELAY] = GPIO_HIGH;
  g_safety_armed = false;

  return HAL_OK;
}

hal_status_t hal_gpio_set_mode(uint8_t pin, gpio_mode_t mode) {
  if (pin >= MAX_PINS)
    return HAL_ERROR;
  pin_modes[pin] = mode;
  return HAL_OK;
}

hal_status_t hal_gpio_write(uint8_t pin, gpio_level_t level) {
  if (pin >= MAX_PINS)
    return HAL_ERROR;
  pin_states[pin] = level;
  return HAL_OK;
}

gpio_level_t hal_gpio_read(uint8_t pin) {
  if (pin >= MAX_PINS)
    return GPIO_LOW;
  return pin_states[pin];
}

void hal_gpio_mux_select(uint8_t channel) {
  /* CD4051 uses 3 select lines to choose 1 of 8 channels */
  pin_states[GPIO_PIN_MUX_S0] = (channel & 0x01) ? GPIO_HIGH : GPIO_LOW;
  pin_states[GPIO_PIN_MUX_S1] = (channel & 0x02) ? GPIO_HIGH : GPIO_LOW;
  pin_states[GPIO_PIN_MUX_S2] = (channel & 0x04) ? GPIO_HIGH : GPIO_LOW;
}

void hal_gpio_relay_disconnect(void) {
  pin_states[GPIO_PIN_RELAY] = GPIO_HIGH;
  printf("[HAL] RELAY TRIGGERED — Battery DISCONNECTED\n");
}

void hal_gpio_relay_connect(void) {
  if (!g_safety_armed) {
    printf("[HAL] Relay connect blocked: safety not armed\n");
    return;
  }
  pin_states[GPIO_PIN_RELAY] = GPIO_LOW;
  printf("[HAL] Relay released — Battery connected\n");
}

void hal_gpio_set_safety_armed(bool armed) { g_safety_armed = armed; }

bool hal_gpio_is_safety_armed(void) { return g_safety_armed; }

void hal_gpio_set_status_leds(uint8_t state) {
  /* 0=NORMAL(green), 1=WARNING(yellow), 2=CRITICAL(red), 3=EMERGENCY(red blink)
   */
  pin_states[GPIO_PIN_LED_GREEN] = (state == 0) ? GPIO_HIGH : GPIO_LOW;
  pin_states[GPIO_PIN_LED_YELLOW] = (state == 1) ? GPIO_HIGH : GPIO_LOW;
  pin_states[GPIO_PIN_LED_RED] = (state >= 2) ? GPIO_HIGH : GPIO_LOW;
}

void hal_gpio_buzzer_pulse(uint16_t duration_ms) {
  (void)duration_ms;
  printf("[HAL] Buzzer: %dms pulse\n", duration_ms);
}

/* -----------------------------------------------------------------------
 * TARGET MODE — THEJAS32 real GPIO registers
 *
 * Per datasheet: two GPIO banks:
 *   GPIO0: pins 0-15  (base 0x10080000)
 *   GPIO1: pins 16-31 (base 0x10180000)
 *
 * On-board blue LEDs: GPIO 16-19 (GPIO1 bank, bits 0-3)
 * LEDs are ACTIVE-LOW: write 0 = ON, write 1 = OFF.
 * ----------------------------------------------------------------------- */
#else

#include "../target/thejas32_regs.h"
static bool g_safety_armed = false;

/* ---- Helper: resolve pin to the correct GPIO bank ---- */

static inline volatile uint32_t *gpio_dir_reg(uint8_t pin) {
  return (pin < 16) ? (volatile uint32_t *)(GPIO0_BASE + GPIO_DIR)
                    : (volatile uint32_t *)(GPIO1_BASE + GPIO_DIR);
}

static inline volatile uint32_t *gpio_out_reg(uint8_t pin) {
  return (pin < 16) ? (volatile uint32_t *)(GPIO0_BASE + GPIO_OUTPUT)
                    : (volatile uint32_t *)(GPIO1_BASE + GPIO_OUTPUT);
}

static inline volatile uint32_t *gpio_in_reg(uint8_t pin) {
  return (pin < 16) ? (volatile uint32_t *)(GPIO0_BASE + GPIO_INPUT)
                    : (volatile uint32_t *)(GPIO1_BASE + GPIO_INPUT);
}

/* Bit position within the bank register (0-15) */
static inline uint8_t gpio_bit(uint8_t pin) { return pin & 0x0F; }

/* ---- Public API ---- */

hal_status_t hal_gpio_init(void) {
  /* Set LED pins (GPIO16-19) as outputs in GPIO1 bank */
  GPIO1_DIR_REG |= LED_ALL_BITS;
  /* All LEDs OFF initially (active-low: HIGH = OFF) */
  GPIO1_OUTPUT_REG |= LED_ALL_BITS;

  /* Set GPIO0 outputs for relay/buzzer if they are on pins 0-15 */
  uint32_t gpio0_out_mask = 0;
  if (GPIO_PIN_RELAY < 16)
    gpio0_out_mask |= (1u << GPIO_PIN_RELAY);
  if (GPIO_PIN_BUZZER < 16)
    gpio0_out_mask |= (1u << GPIO_PIN_BUZZER);
  if (gpio0_out_mask) {
    GPIO0_DIR_REG |= gpio0_out_mask;
    GPIO0_OUTPUT_REG |= gpio0_out_mask; /* relay HIGH = disconnected */
  }
  g_safety_armed = false;

  return HAL_OK;
}

hal_status_t hal_gpio_set_mode(uint8_t pin, gpio_mode_t mode) {
  if (pin >= 32)
    return HAL_ERROR;
  volatile uint32_t *dir = gpio_dir_reg(pin);
  uint8_t bit = gpio_bit(pin);
  if (mode == GPIO_MODE_OUTPUT)
    *dir |= (1u << bit);
  else
    *dir &= ~(1u << bit);
  return HAL_OK;
}

hal_status_t hal_gpio_write(uint8_t pin, gpio_level_t level) {
  if (pin >= 32)
    return HAL_ERROR;
  volatile uint32_t *out = gpio_out_reg(pin);
  uint8_t bit = gpio_bit(pin);
  if (level == GPIO_HIGH)
    *out |= (1u << bit);
  else
    *out &= ~(1u << bit);
  return HAL_OK;
}

gpio_level_t hal_gpio_read(uint8_t pin) {
  if (pin >= 32)
    return GPIO_LOW;
  volatile uint32_t *in = gpio_in_reg(pin);
  uint8_t bit = gpio_bit(pin);
  return (*in & (1u << bit)) ? GPIO_HIGH : GPIO_LOW;
}

void hal_gpio_mux_select(uint8_t channel) {
  hal_gpio_write(GPIO_PIN_MUX_S0, (channel & 0x01) ? GPIO_HIGH : GPIO_LOW);
  hal_gpio_write(GPIO_PIN_MUX_S1, (channel & 0x02) ? GPIO_HIGH : GPIO_LOW);
  hal_gpio_write(GPIO_PIN_MUX_S2, (channel & 0x04) ? GPIO_HIGH : GPIO_LOW);
}

void hal_gpio_relay_disconnect(void) {
  hal_gpio_write(GPIO_PIN_RELAY, GPIO_HIGH);
}

void hal_gpio_relay_connect(void) {
  if (!g_safety_armed) {
    return;
  }
  hal_gpio_write(GPIO_PIN_RELAY, GPIO_LOW);
}

void hal_gpio_set_safety_armed(bool armed) { g_safety_armed = armed; }

bool hal_gpio_is_safety_armed(void) { return g_safety_armed; }

void hal_gpio_set_status_leds(uint8_t state) {
  /* Active-low: write LOW to turn ON, HIGH to turn OFF.
   * LED1=NORMAL, LED2=WARNING, LED3=CRITICAL, LED4=EMERGENCY */
  GPIO1_OUTPUT_REG |= LED_ALL_BITS; /* All OFF first */
  if (state == 0)
    GPIO1_OUTPUT_REG &= ~(1u << LED1_BIT); /* LED1 ON = NORMAL */
  if (state == 1)
    GPIO1_OUTPUT_REG &= ~(1u << LED2_BIT); /* LED2 ON = WARNING */
  if (state == 2)
    GPIO1_OUTPUT_REG &= ~(1u << LED3_BIT); /* LED3 ON = CRITICAL */
  if (state >= 3)
    GPIO1_OUTPUT_REG &= ~(1u << LED4_BIT); /* LED4 ON = EMERGENCY */
}

void hal_gpio_buzzer_pulse(uint16_t duration_ms) {
  hal_gpio_write(GPIO_PIN_BUZZER, GPIO_HIGH);
  /* Busy-loop delay (~100MHz clock, ~10 cycles/iteration) */
  volatile uint32_t count = (uint32_t)duration_ms * 10000u;
  while (count--) {
    __asm__ volatile("nop");
  }
  hal_gpio_write(GPIO_PIN_BUZZER, GPIO_LOW);
}

#endif /* HAL_HOST_MODE */
