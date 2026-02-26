/*
 * input_packet.c — Multi-frame Input Packet Parser (Full Pack)
 *
 * Parses incoming UART bytes from the digital twin into pack-level
 * and module-level frames. Accumulates until all 9 frames (1 pack +
 * 8 modules) are received, then signals the main loop to process.
 */

#include "input_packet.h"
#include <string.h>

/* -----------------------------------------------------------------------
 * XOR checksum
 * ----------------------------------------------------------------------- */
static uint8_t compute_checksum(const uint8_t *data, uint8_t len) {
  uint8_t csum = 0;
  for (uint8_t i = 0; i < len; i++) {
    csum ^= data[i];
  }
  return csum;
}

/* -----------------------------------------------------------------------
 * Initialize
 * ----------------------------------------------------------------------- */
void input_rx_init(input_rx_state_t *rx) {
  memset(rx, 0, sizeof(input_rx_state_t));
}

/* -----------------------------------------------------------------------
 * Try to parse a frame from the buffer
 * Returns: 0 = no frame yet, 1 = frame parsed, 2 = full snapshot ready
 * ----------------------------------------------------------------------- */
static int try_parse_frame(input_rx_state_t *rx) {
  /* Need at least 3 bytes for sync + len + type */
  if (rx->write_pos < 3)
    return 0;

  /* Scan for sync byte */
  uint8_t start = 0;
  while (start < rx->write_pos && rx->buf[start] != INPUT_SYNC_BYTE) {
    start++;
  }

  /* Discard bytes before sync */
  if (start > 0) {
    uint8_t remaining = rx->write_pos - start;
    if (remaining > 0) {
      memmove(rx->buf, rx->buf + start, remaining);
    }
    rx->write_pos = remaining;
  }

  if (rx->write_pos < 3)
    return 0;

  uint8_t frame_len = rx->buf[1];
  uint8_t frame_type = rx->buf[2];

  /* Validate frame length */
  if (frame_type == INPUT_TYPE_PACK && frame_len != INPUT_PACK_FRAME_SIZE) {
    /* Bad length — skip this sync byte */
    memmove(rx->buf, rx->buf + 1, rx->write_pos - 1);
    rx->write_pos--;
    return 0;
  }
  if (frame_type == INPUT_TYPE_MODULE && frame_len != INPUT_MODULE_FRAME_SIZE) {
    memmove(rx->buf, rx->buf + 1, rx->write_pos - 1);
    rx->write_pos--;
    return 0;
  }
  if (frame_type != INPUT_TYPE_PACK && frame_type != INPUT_TYPE_MODULE) {
    /* Unknown frame type — skip */
    memmove(rx->buf, rx->buf + 1, rx->write_pos - 1);
    rx->write_pos--;
    return 0;
  }

  /* Wait until we have the complete frame */
  if (rx->write_pos < frame_len)
    return 0;

  /* Validate checksum */
  uint8_t expected = compute_checksum(rx->buf, frame_len - 1);
  if (rx->buf[frame_len - 1] != expected) {
    /* Checksum mismatch — skip sync */
    memmove(rx->buf, rx->buf + 1, rx->write_pos - 1);
    rx->write_pos--;
    return 0;
  }

  /* Valid frame! Copy to appropriate storage */
  int result = 1;

  if (frame_type == INPUT_TYPE_PACK) {
    memcpy(&rx->last_pack, rx->buf, sizeof(input_pack_frame_t));
    rx->pack_received = 1;
  } else {
    /* Module frame — extract module index */
    input_module_frame_t mf;
    memcpy(&mf, rx->buf, sizeof(input_module_frame_t));

    if (mf.module_index < 8) {
      memcpy(&rx->last_modules[mf.module_index], &mf,
             sizeof(input_module_frame_t));
      rx->modules_received |= (1 << mf.module_index);
    }
  }

  /* Consume the frame from buffer */
  uint8_t remaining = rx->write_pos - frame_len;
  if (remaining > 0) {
    memmove(rx->buf, rx->buf + frame_len, remaining);
  }
  rx->write_pos = remaining;

  /* Check if full snapshot is ready */
  if (rx->pack_received && rx->modules_received == 0xFF) {
    result = 2;
  }

  return result;
}

/* -----------------------------------------------------------------------
 * Feed one byte from UART RX
 * ----------------------------------------------------------------------- */
int input_rx_feed(input_rx_state_t *rx, uint8_t byte) {
  if (rx->write_pos < INPUT_RX_BUF_SIZE) {
    rx->buf[rx->write_pos++] = byte;
  } else {
    /* Buffer overflow — reset */
    rx->write_pos = 0;
    rx->buf[rx->write_pos++] = byte;
    return 0;
  }

  return try_parse_frame(rx);
}

/* -----------------------------------------------------------------------
 * Check if a complete snapshot is available
 * ----------------------------------------------------------------------- */
int input_rx_has_full_snapshot(const input_rx_state_t *rx) {
  return (rx->pack_received && rx->modules_received == 0xFF) ? 1 : 0;
}

/* -----------------------------------------------------------------------
 * Reset cycle tracking (call after consuming the snapshot)
 * ----------------------------------------------------------------------- */
void input_rx_reset_cycle(input_rx_state_t *rx) {
  rx->pack_received = 0;
  rx->modules_received = 0;
}
