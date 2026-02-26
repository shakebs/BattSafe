/*
 * thejas32_regs.h — THEJAS32 SoC Register Definitions
 *
 * Memory-mapped I/O register addresses for the THEJAS32 SoC
 * (VEGA ET1031). These are used by the HAL layer when compiling
 * for the real board (TARGET mode).
 *
 * Reference: THEJAS32 SoC Technical Reference Manual
 *            and VEGA Processor documentation
 */

#ifndef THEJAS32_REGS_H
#define THEJAS32_REGS_H

#include <stdint.h>

/* -----------------------------------------------------------------------
 * Memory Map (key ranges)
 *
 *   0x0020_0000 — 0x0023_FFFF   256KB SRAM
 *   0x1000_0100 — 0x1000_01FF   UART0
 *   0x1000_0200 — 0x1000_02FF   UART1
 *   0x1000_0300 — 0x1000_03FF   UART2
 *   0x1008_0000 — 0x101C_0000   GPIO
 * ----------------------------------------------------------------------- */

/* Helper macro for volatile register access */
#define REG32(addr) (*(volatile uint32_t *)(addr))

/* -----------------------------------------------------------------------
 * UART Registers (16550-compatible)
 *
 * UART0 is connected to CP2102N USB-serial.
 * This is what we use for telemetry output to the dashboard.
 * ----------------------------------------------------------------------- */
#define UART0_BASE 0x10000100

/* Standard 16550 UART register offsets */
#define UART_RBR 0x00 /* Receive Buffer Register (read)  */
#define UART_THR 0x00 /* Transmit Holding Register (write) */
#define UART_IER 0x04 /* Interrupt Enable Register */
#define UART_IIR 0x08 /* Interrupt Identification Register (read) */
#define UART_FCR 0x08 /* FIFO Control Register (write) */
#define UART_LCR 0x0C /* Line Control Register */
#define UART_MCR 0x10 /* Modem Control Register */
#define UART_LSR 0x14 /* Line Status Register */
#define UART_MSR 0x18 /* Modem Status Register */
#define UART_SCR 0x1C /* Scratch Register */
#define UART_DLL 0x00 /* Divisor Latch Low (when DLAB=1) */
#define UART_DLH 0x04 /* Divisor Latch High (when DLAB=1) */

/* UART Line Status Register bits */
#define UART_LSR_DR (1 << 0)   /* Data Ready */
#define UART_LSR_THRE (1 << 5) /* Transmit Holding Reg Empty */
#define UART_LSR_TEMT (1 << 6) /* Transmitter Empty */

/* UART Line Control Register bits */
#define UART_LCR_DLAB (1 << 7) /* Divisor Latch Access Bit */
#define UART_LCR_8N1 0x03      /* 8 data bits, no parity, 1 stop */

/* UART FIFO Control Register bits */
#define UART_FCR_ENABLE (1 << 0) /* FIFO Enable */
#define UART_FCR_CLEAR (0x06)    /* Clear both FIFOs */

/* Convenience macros for UART0 */
#define UART0_THR REG32(UART0_BASE + UART_THR)
#define UART0_RBR REG32(UART0_BASE + UART_RBR)
#define UART0_LSR REG32(UART0_BASE + UART_LSR)
#define UART0_LCR REG32(UART0_BASE + UART_LCR)
#define UART0_FCR REG32(UART0_BASE + UART_FCR)
#define UART0_DLL REG32(UART0_BASE + UART_DLL)
#define UART0_DLH REG32(UART0_BASE + UART_DLH)
#define UART0_IER REG32(UART0_BASE + UART_IER)

/* -----------------------------------------------------------------------
 * GPIO Registers
 *
 * THEJAS32 has two GPIO banks (per datasheet):
 *   GPIO0: GPIO0-GPIO15  (base 0x10080000)
 *   GPIO1: GPIO16-GPIO31 (base 0x10180000)
 *
 * On-board blue LEDs are on GPIO16-19 (GPIO1 bank, bits 0-3).
 * LEDs are ACTIVE-LOW: write 0 = LED ON, write 1 = LED OFF.
 * ----------------------------------------------------------------------- */
#define GPIO0_BASE 0x10080000
#define GPIO1_BASE 0x10180000

/* GPIO Register offsets (per bank) */
#define GPIO_OUTPUT 0x00 /* Output data register */
#define GPIO_INPUT 0x04  /* Input data register */
#define GPIO_DIR 0x08    /* Direction: 1=output, 0=input */

/* GPIO0 bank macros (pins 0-15) */
#define GPIO0_OUTPUT_REG REG32(GPIO0_BASE + GPIO_OUTPUT)
#define GPIO0_INPUT_REG REG32(GPIO0_BASE + GPIO_INPUT)
#define GPIO0_DIR_REG REG32(GPIO0_BASE + GPIO_DIR)

/* GPIO1 bank macros (pins 16-31, bit position = pin - 16) */
#define GPIO1_OUTPUT_REG REG32(GPIO1_BASE + GPIO_OUTPUT)
#define GPIO1_INPUT_REG REG32(GPIO1_BASE + GPIO_INPUT)
#define GPIO1_DIR_REG REG32(GPIO1_BASE + GPIO_DIR)

/* On-board blue LEDs (per datasheet: GPIO 16-19)
 * These are in GPIO1 bank, bits 0-3.
 * ACTIVE-LOW: write 0 = ON, write 1 = OFF. */
#define LED1_GPIO 16 /* Blue LED 1 */
#define LED2_GPIO 17 /* Blue LED 2 */
#define LED3_GPIO 18 /* Blue LED 3 */
#define LED4_GPIO 19 /* Blue LED 4 */

/* Bit positions within GPIO1 bank */
#define LED1_BIT (LED1_GPIO - 16) /* bit 0 */
#define LED2_BIT (LED2_GPIO - 16) /* bit 1 */
#define LED3_BIT (LED3_GPIO - 16) /* bit 2 */
#define LED4_BIT (LED4_GPIO - 16) /* bit 3 */
#define LED_ALL_BITS                                                           \
  ((1u << LED1_BIT) | (1u << LED2_BIT) | (1u << LED3_BIT) | (1u << LED4_BIT))

#endif /* THEJAS32_REGS_H */
