/*
 * input_packet.c — Digital Twin Input Packet Parser
 *
 * Accumulates UART RX bytes and extracts valid 0xBB input packets.
 */

#include "input_packet.h"
#include <string.h>

void input_rx_init(input_rx_state_t *rx) {
  memset(rx, 0, sizeof(input_rx_state_t));
}

int input_rx_feed(input_rx_state_t *rx, uint8_t byte) {
  /* State machine: look for sync byte at position 0 */
  if (rx->write_pos == 0) {
    if (byte == INPUT_SYNC_BYTE) {
      rx->buf[0] = byte;
      rx->write_pos = 1;
    }
    return 0;
  }

  /* Position 1: length byte — must be INPUT_PACKET_SIZE */
  if (rx->write_pos == 1) {
    if (byte != INPUT_PACKET_SIZE) {
      rx->write_pos = 0; /* Reset — wrong length */
      return 0;
    }
    rx->buf[1] = byte;
    rx->write_pos = 2;
    return 0;
  }

  /* Accumulate remaining bytes */
  rx->buf[rx->write_pos++] = byte;

  /* Have we collected a full packet? */
  if (rx->write_pos >= INPUT_PACKET_SIZE) {
    /* Verify checksum: XOR of bytes [0..INPUT_PACKET_SIZE-2] */
    uint8_t csum = 0;
    for (uint8_t i = 0; i < INPUT_PACKET_SIZE - 1; i++) {
      csum ^= rx->buf[i];
    }

    if (csum == rx->buf[INPUT_PACKET_SIZE - 1]) {
      /* Valid packet! Copy to struct */
      memcpy(&rx->last_packet, rx->buf, INPUT_PACKET_SIZE);
      rx->have_packet = 1;
      rx->write_pos = 0;
      return 1; /* Packet ready */
    }

    /* Bad checksum — reset and search for next sync */
    rx->write_pos = 0;
  }

  return 0;
}

int input_rx_has_packet(const input_rx_state_t *rx) { return rx->have_packet; }

input_packet_t input_rx_get(input_rx_state_t *rx) {
  rx->have_packet = 0;
  return rx->last_packet;
}
