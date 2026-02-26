/*
 * packet_format.c — UART Telemetry Packet Encoder (Full Pack)
 *
 * Encodes the full-pack sensor snapshot + anomaly results into
 * multi-frame telemetry packets for the dashboard.
 */

#include "packet_format.h"
#include <string.h>

/* -----------------------------------------------------------------------
 * XOR checksum
 * ----------------------------------------------------------------------- */

uint8_t packet_checksum(const uint8_t *data, uint8_t length) {
  uint8_t csum = 0;
  for (uint8_t i = 0; i < length; i++) {
    csum ^= data[i];
  }
  return csum;
}

/* -----------------------------------------------------------------------
 * Clamp helpers
 * ----------------------------------------------------------------------- */
static inline int16_t clamp_i16(float v) {
  if (v > 32767.0f)
    return 32767;
  if (v < -32768.0f)
    return -32768;
  return (int16_t)v;
}

static inline uint8_t clamp_u8(float v) {
  if (v > 255.0f)
    return 255;
  if (v < 0.0f)
    return 0;
  return (uint8_t)v;
}

static inline uint16_t clamp_u16(float v) {
  if (v > 65535.0f)
    return 65535;
  if (v < 0.0f)
    return 0;
  return (uint16_t)v;
}

/* -----------------------------------------------------------------------
 * Encode pack summary frame
 * ----------------------------------------------------------------------- */

uint8_t packet_encode_pack(telemetry_pack_frame_t *pkt, uint32_t timestamp_ms,
                           const sensor_snapshot_t *sensors,
                           const anomaly_result_t *anomaly,
                           system_state_t state) {
  memset(pkt, 0, sizeof(telemetry_pack_frame_t));

  pkt->sync = PACKET_SYNC_BYTE;
  pkt->length = PACKET_PACK_SIZE;
  pkt->frame_type = PACKET_TYPE_PACK;

  pkt->timestamp_ms = timestamp_ms;

  /* Electrical */
  pkt->pack_voltage_dv = clamp_u16(sensors->pack_voltage_v * 10.0f);
  pkt->pack_current_da = clamp_i16(sensors->pack_current_a * 10.0f);
  pkt->r_int_cmohm = clamp_u16(sensors->r_internal_mohm * 100.0f);

  /* Thermal summary */
  pkt->max_temp_dt = clamp_i16(sensors->hotspot_temp_c * 10.0f);
  pkt->ambient_temp_dt = clamp_i16(sensors->temp_ambient_c * 10.0f);
  pkt->core_temp_est_dt = clamp_i16(sensors->t_core_est_c * 10.0f);

  float dt_dt_clamped = sensors->dt_dt_max;
  if (dt_dt_clamped > 2.55f)
    dt_dt_clamped = 2.55f;
  if (dt_dt_clamped < 0.0f)
    dt_dt_clamped = 0.0f;
  pkt->dt_dt_max_cdpm = (uint8_t)(dt_dt_clamped * 100.0f);

  /* Gas & pressure */
  pkt->gas_ratio_1_cp = clamp_u8(sensors->gas_ratio_1 * 100.0f);
  pkt->gas_ratio_2_cp = clamp_u8(sensors->gas_ratio_2 * 100.0f);
  pkt->pressure_delta_1_chpa =
      clamp_i16(sensors->pressure_delta_1_hpa * 100.0f);
  pkt->pressure_delta_2_chpa =
      clamp_i16(sensors->pressure_delta_2_hpa * 100.0f);

  /* Pack health */
  pkt->v_spread_dmv = clamp_u16(sensors->v_spread_mv * 10.0f);
  pkt->temp_spread_dt = clamp_u8(sensors->temp_spread_c * 10.0f);

  /* System state */
  pkt->system_state = (uint8_t)state;
  pkt->anomaly_mask = anomaly->active_mask;
  pkt->anomaly_count = anomaly->active_count;
  pkt->anomaly_modules = anomaly->anomaly_modules_mask;

  /* Hotspot */
  pkt->hotspot_module = anomaly->hotspot_module;

  /* Risk */
  pkt->risk_factor_pct = clamp_u8(anomaly->risk_factor * 100.0f);
  pkt->cascade_stage = anomaly->cascade_stage;

  /* Flags */
  pkt->flags = 0;
  if (anomaly->is_emergency_direct)
    pkt->flags |= 0x01;

  /* Checksum */
  uint8_t csum_len = PACKET_PACK_SIZE - 1;
  pkt->checksum = packet_checksum((const uint8_t *)pkt, csum_len);

  return PACKET_PACK_SIZE;
}

/* -----------------------------------------------------------------------
 * Encode module detail frame
 * ----------------------------------------------------------------------- */

uint8_t packet_encode_module(telemetry_module_frame_t *pkt,
                             uint8_t module_index,
                             const sensor_snapshot_t *sensors) {
  memset(pkt, 0, sizeof(telemetry_module_frame_t));

  if (module_index >= NUM_MODULES)
    return 0;

  const module_data_t *mod = &sensors->modules[module_index];

  pkt->sync = PACKET_SYNC_BYTE;
  pkt->length = PACKET_MODULE_SIZE;
  pkt->frame_type = PACKET_TYPE_MODULE;
  pkt->module_index = module_index;

  /* NTC temps */
  pkt->ntc1_dt = clamp_i16(mod->ntc1_c * 10.0f);
  pkt->ntc2_dt = clamp_i16(mod->ntc2_c * 10.0f);

  /* Swelling */
  pkt->swelling_pct = clamp_u8(mod->swelling_pct);

  /* Thermal dynamics */
  pkt->delta_t_intra_dt = clamp_u8(mod->delta_t_intra * 10.0f);
  float dt = mod->max_dt_dt;
  if (dt > 2.55f)
    dt = 2.55f;
  if (dt < 0.0f)
    dt = 0.0f;
  pkt->max_dt_dt_cdpm = (uint8_t)(dt * 100.0f);

  /* Module voltage */
  pkt->module_voltage_dv = clamp_u16(mod->module_voltage * 10.0f);
  pkt->v_spread_mv = clamp_u16(mod->v_spread_mv);

  pkt->reserved = 0;

  /* Checksum */
  uint8_t csum_len = PACKET_MODULE_SIZE - 1;
  pkt->checksum = packet_checksum((const uint8_t *)pkt, csum_len);

  return PACKET_MODULE_SIZE;
}

/* -----------------------------------------------------------------------
 * Legacy API (backward compat for tests)
 * ----------------------------------------------------------------------- */

uint8_t packet_encode(telemetry_packet_t *pkt, uint32_t timestamp_ms,
                      const sensor_snapshot_t *sensors,
                      const anomaly_result_t *anomaly, system_state_t state) {
  return packet_encode_pack(pkt, timestamp_ms, sensors, anomaly, state);
}

int packet_validate_pack(const telemetry_pack_frame_t *pkt) {
  if (pkt->sync != PACKET_SYNC_BYTE)
    return -1;
  if (pkt->length != PACKET_PACK_SIZE)
    return -1;

  uint8_t csum_len = PACKET_PACK_SIZE - 1;
  uint8_t expected = packet_checksum((const uint8_t *)pkt, csum_len);
  if (pkt->checksum != expected)
    return -1;

  return 0;
}

int packet_validate(const telemetry_packet_t *pkt) {
  return packet_validate_pack(pkt);
}
