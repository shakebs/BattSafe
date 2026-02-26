/*
 * packet_format.c — UART Telemetry Packet Encoder
 *
 * Converts floating-point sensor values to fixed-point integers
 * and packs them into a compact 32-byte UART packet.
 */

#include "packet_format.h"
#include <string.h>

/* -----------------------------------------------------------------------
 * Compute XOR checksum
 *
 * XOR is simple, fast, and good enough for short-range UART.
 * For production, you'd use CRC-8 or CRC-16.
 * ----------------------------------------------------------------------- */

uint8_t packet_checksum(const uint8_t *data, uint8_t length) {
  uint8_t csum = 0;
  for (uint8_t i = 0; i < length; i++) {
    csum ^= data[i];
  }
  return csum;
}

/* -----------------------------------------------------------------------
 * Encode a telemetry packet
 * ----------------------------------------------------------------------- */

uint8_t packet_encode(telemetry_packet_t *pkt, uint32_t timestamp_ms,
                      const sensor_snapshot_t *sensors,
                      const anomaly_result_t *anomaly, system_state_t state) {
  /* Clear the packet */
  memset(pkt, 0, sizeof(telemetry_packet_t));

  /* Header */
  pkt->sync = PACKET_SYNC_BYTE;
  pkt->length = PACKET_MAX_SIZE;

  /* Timestamp */
  pkt->timestamp_ms = timestamp_ms;

  /* Electrical: convert float to fixed-point ×100 */
  pkt->voltage_cv = (uint16_t)(sensors->voltage_v * 100.0f);
  pkt->current_ca = (uint16_t)(sensors->current_a * 100.0f);
  pkt->r_int_mohm = (uint16_t)(sensors->r_internal_mohm);

  /* Thermal: convert float to fixed-point ×10 (deci-degrees) */
  pkt->temp_cell1_dt = (int16_t)(sensors->temp_cells_c[0] * 10.0f);
  pkt->temp_cell2_dt = (int16_t)(sensors->temp_cells_c[1] * 10.0f);
  pkt->temp_cell3_dt = (int16_t)(sensors->temp_cells_c[2] * 10.0f);
  pkt->temp_cell4_dt = (int16_t)(sensors->temp_cells_c[3] * 10.0f);

  /* Gas & Pressure: convert float to fixed-point ×100 */
  pkt->gas_ratio_cp = (uint16_t)(sensors->gas_ratio * 100.0f);
  pkt->pressure_delta_chpa = (int16_t)(sensors->pressure_delta_hpa * 100.0f);

  /* Mechanical */
  pkt->swelling_pct = (uint8_t)(sensors->swelling_pct);

  /* System state */
  pkt->system_state = (uint8_t)state;
  pkt->anomaly_mask = anomaly->active_mask;
  pkt->anomaly_count = anomaly->active_count;

  /* Ambient & rate data (spec §3.3) */
  pkt->temp_ambient_dt = (int8_t)(sensors->temp_ambient_c * 10.0f);
  float dt_clamped = sensors->dt_dt_max;
  if (dt_clamped > 2.55f)
    dt_clamped = 2.55f;
  if (dt_clamped < 0.0f)
    dt_clamped = 0.0f;
  pkt->dt_dt_max_cdps = (uint8_t)(dt_clamped * 100.0f);
  pkt->flags = 0;
  if (anomaly->is_emergency_direct)
    pkt->flags |= 0x01;

  /* Compute checksum over everything except the checksum byte itself */
  uint8_t csum_len = PACKET_MAX_SIZE - 1;
  pkt->checksum = packet_checksum((const uint8_t *)pkt, csum_len);

  return PACKET_MAX_SIZE;
}

/* -----------------------------------------------------------------------
 * Validate a received packet
 * Returns 0 if valid, -1 if invalid.
 * ----------------------------------------------------------------------- */

int packet_validate(const telemetry_packet_t *pkt) {
  /* Check sync byte */
  if (pkt->sync != PACKET_SYNC_BYTE) {
    return -1;
  }
  if (pkt->length != PACKET_MAX_SIZE) {
    return -1;
  }

  /* Verify checksum */
  uint8_t csum_len = PACKET_MAX_SIZE - 1;
  uint8_t expected = packet_checksum((const uint8_t *)pkt, csum_len);
  if (pkt->checksum != expected) {
    return -1;
  }

  return 0;
}
