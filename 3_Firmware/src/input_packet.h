/*
 * input_packet.h — Digital Twin → Board Input Packet Format
 *
 * The digital twin sends sensor data to the board via this 20-byte packet.
 * Uses 0xBB sync byte (distinct from 0xAA telemetry output).
 *
 * Packet: [0xBB] [LEN=20] [payload 17 bytes] [checksum]
 */

#ifndef INPUT_PACKET_H
#define INPUT_PACKET_H

#include <stdint.h>

#define INPUT_SYNC_BYTE 0xBB
#define INPUT_PACKET_SIZE 20

/* Input packet from digital twin (packed, little-endian) */
typedef struct __attribute__((packed)) {
  uint8_t sync;   /* 0xBB */
  uint8_t length; /* 20   */

  /* Electrical */
  uint16_t voltage_cv; /* Voltage in centi-volts (14.80V → 1480) */
  int16_t current_ca;  /* Current in centi-amps (signed)         */

  /* Thermal — 4 NTC readings */
  int16_t temp1_dt; /* Cell 1 temp in deci-°C (28.5°C → 285) */
  int16_t temp2_dt; /* Cell 2 temp                            */
  int16_t temp3_dt; /* Cell 3 temp                            */
  int16_t temp4_dt; /* Cell 4 temp                            */

  /* Gas & Pressure */
  uint16_t gas_ratio_cp;       /* Gas ratio ×100 (0.70 → 70)            */
  int16_t pressure_delta_chpa; /* Pressure delta centi-hPa            */

  /* Mechanical */
  uint8_t swelling_pct; /* 0-100%                                */

  /* Checksum */
  uint8_t checksum; /* XOR of bytes [0..18]                  */
} input_packet_t;

/* Ring buffer for accumulating UART RX bytes */
#define INPUT_RX_BUF_SIZE 64

typedef struct {
  uint8_t buf[INPUT_RX_BUF_SIZE];
  uint8_t write_pos;
  uint8_t have_packet;        /* 1 if a valid packet is ready */
  input_packet_t last_packet; /* Most recent valid packet     */
} input_rx_state_t;

/* Initialize the RX state */
void input_rx_init(input_rx_state_t *rx);

/* Feed one byte from UART RX. Returns 1 if a complete valid packet was parsed.
 */
int input_rx_feed(input_rx_state_t *rx, uint8_t byte);

/* Check if a valid input packet is available */
int input_rx_has_packet(const input_rx_state_t *rx);

/* Get the last valid packet and clear the flag */
input_packet_t input_rx_get(input_rx_state_t *rx);

#endif /* INPUT_PACKET_H */
