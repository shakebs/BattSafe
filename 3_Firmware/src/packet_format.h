/*
 * packet_format.h — UART Telemetry Packet Format
 *
 * Defines the binary packet that the VSDSquadron ULTRA sends to the
 * ESP32-C3 (and ultimately to the dashboard) every telemetry cycle.
 *
 * Packet structure (32 bytes total):
 *   [SYNC_BYTE] [LENGTH] [PAYLOAD...] [CHECKSUM]
 *
 * The payload contains compressed sensor readings and system state
 * in a fixed format that's fast to encode and decode.
 */

#ifndef PACKET_FORMAT_H
#define PACKET_FORMAT_H

#include "anomaly_eval.h"
#include "correlation_engine.h"
#include <stdint.h>

/* Packet framing */
#define PACKET_SYNC_BYTE 0xAA
#define PACKET_MAX_SIZE 32
#define PACKET_PAYLOAD_SIZE 28 /* 32 - sync - length - checksum */

/* -----------------------------------------------------------------------
 * Telemetry packet (binary, over UART)
 *
 * All multi-byte values are little-endian (matches RISC-V).
 * Floats are encoded as fixed-point integers to save space.
 * ----------------------------------------------------------------------- */

typedef struct __attribute__((packed)) {
  uint8_t sync;   /* 0xAA sync byte                        */
  uint8_t length; /* Total packet length                   */

  /* Timestamp */
  uint32_t timestamp_ms; /* Milliseconds since boot               */

  /* Electrical (from INA219) — fixed point ×100 */
  uint16_t voltage_cv; /* Voltage in centi-volts (14.80V → 1480) */
  uint16_t current_ca; /* Current in centi-amps (2.50A → 250)    */
  uint16_t r_int_mohm; /* Internal resistance in milliohms       */

  /* Thermal (from NTC) — fixed point ×10 */
  int16_t temp_cell1_dt; /* Cell 1 temp in deci-°C (28.5°C → 285) */
  int16_t temp_cell2_dt; /* Cell 2 temp                           */
  int16_t temp_cell3_dt; /* Cell 3 temp                           */
  int16_t temp_cell4_dt; /* Cell 4 temp                           */

  /* Gas & Pressure (from BME680) — fixed point ×100 */
  uint16_t gas_ratio_cp;       /* Gas ratio ×100 (0.70 → 70)            */
  int16_t pressure_delta_chpa; /* Pressure delta in centi-hPa        */

  /* Mechanical (from FSR402) */
  uint8_t swelling_pct; /* 0-100%                                */

  /* System state */
  uint8_t system_state;  /* 0=NORMAL, 1=WARNING, 2=CRITICAL, 3=EMERGENCY */
  uint8_t anomaly_mask;  /* Active category bitmask (CAT_*)       */
  uint8_t anomaly_count; /* Number of active categories           */

  /* Ambient & rate data (uses former reserved bytes) */
  int8_t temp_ambient_dt; /* Ambient temp in deci-°C offset (±12.7°C)  */
  uint8_t dt_dt_max_cdps; /* dT/dt ×100 in °C/s (0-2.55°C/s range)     */
  uint8_t flags;          /* bit0: emergency_direct, bits 1-7: reserved */

  /* Checksum */
  uint8_t checksum; /* XOR of all preceding bytes            */
} telemetry_packet_t;

/* -----------------------------------------------------------------------
 * Public API
 * ----------------------------------------------------------------------- */

/* Encode sensor data and system state into a telemetry packet.
 * Returns the total packet size (always PACKET_MAX_SIZE). */
uint8_t packet_encode(telemetry_packet_t *pkt, uint32_t timestamp_ms,
                      const sensor_snapshot_t *sensors,
                      const anomaly_result_t *anomaly, system_state_t state);

/* Compute XOR checksum over a buffer */
uint8_t packet_checksum(const uint8_t *data, uint8_t length);

/* Validate a received packet (check sync byte and checksum) */
int packet_validate(const telemetry_packet_t *pkt);

#endif /* PACKET_FORMAT_H */
