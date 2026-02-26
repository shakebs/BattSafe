/*
 * hal_uart.c — UART Implementation (HOST + TARGET)
 */

#include "hal_uart.h"
#include <stdio.h>
#include <string.h>

/* -----------------------------------------------------------------------
 * HOST MODE — Output to stdout
 * ----------------------------------------------------------------------- */
#if HAL_HOST_MODE

hal_status_t hal_uart_init(void) { return HAL_OK; }

hal_status_t hal_uart_send(const uint8_t *data, uint8_t length) {
  /* In host mode, print raw bytes as hex for debugging */
  printf("[UART TX %d bytes] ", length);
  for (uint8_t i = 0; i < length; i++) {
    printf("%02X ", data[i]);
  }
  printf("\n");
  return HAL_OK;
}

hal_status_t hal_uart_print(const char *str) {
  printf("%s", str);
  return HAL_OK;
}

int hal_uart_recv_byte(void) {
  /* In host mode, no serial input — return -1 */
  return -1;
}

/* -----------------------------------------------------------------------
 * TARGET MODE
 * ----------------------------------------------------------------------- */
#else

hal_status_t hal_uart_init(void) {
/* THEJAS32 UART0 is already initialized by the bootloader
 * at 115200 baud, 8N1. We just ensure FIFOs are enabled. */
#include "../target/thejas32_regs.h"
  UART0_FCR = UART_FCR_ENABLE | UART_FCR_CLEAR;
  UART0_IER = 0; /* Disable interrupts — we use polling */
  return HAL_OK;
}

static void uart0_putc(uint8_t c) {
#include "../target/thejas32_regs.h"
  /* Wait until Transmit Holding Register is empty */
  while (!(UART0_LSR & UART_LSR_THRE)) {
  }
  UART0_THR = c;
}

hal_status_t hal_uart_send(const uint8_t *data, uint8_t length) {
  for (uint8_t i = 0; i < length; i++) {
    uart0_putc(data[i]);
  }
  return HAL_OK;
}

hal_status_t hal_uart_print(const char *str) {
  while (*str) {
    if (*str == '\n')
      uart0_putc('\r'); /* CRLF for terminal */
    uart0_putc((uint8_t)*str++);
  }
  return HAL_OK;
}

int hal_uart_recv_byte(void) {
#include "../target/thejas32_regs.h"
  if (UART0_LSR & UART_LSR_DR) {
    return (int)(UART0_RBR & 0xFF);
  }
  return -1; /* No data available */
}

#endif /* HAL_HOST_MODE */
