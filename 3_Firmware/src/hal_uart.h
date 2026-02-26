/*
 * hal_uart.h — UART (Serial) Abstraction
 *
 * Used for:
 *   - Sending telemetry packets to ESP32-C3 (or directly to laptop via USB)
 *   - Debug printing during development
 *
 * On HOST mode: writes to stdout (printf).
 * On TARGET mode: uses THEJAS32 UART peripheral.
 */

#ifndef HAL_UART_H
#define HAL_UART_H

#include "hal_platform.h"

/* UART configuration */
#define UART_BAUD_RATE 115200
#define UART_TX_BUF_SIZE 64

/*
 * Initialize UART with the configured baud rate.
 */
hal_status_t hal_uart_init(void);

/*
 * Send a buffer of raw bytes over UART.
 * This is used for sending telemetry packets.
 *
 * data:   pointer to bytes to send
 * length: number of bytes
 */
hal_status_t hal_uart_send(const uint8_t *data, uint8_t length);

/*
 * Send a null-terminated string over UART.
 * This is used for debug messages.
 */
hal_status_t hal_uart_print(const char *str);

/*
 * Non-blocking receive: returns byte (0-255) if data available,
 * or -1 if no data. Used for receiving input packets from digital twin.
 */
int hal_uart_recv_byte(void);

#endif /* HAL_UART_H */
