/*
 * packet_format.h — UART Telemetry Output Packet Format (Full Pack)
 *
 * Multi-frame output telemetry from VSDSquadron ULTRA to dashboard.
 * Mirrors the input protocol structure:
 *   Frame 0x01: Pack summary (state, V/I, gas, pressure, risk, hotspot)
 *   Frame 0x02: Module detail (×8: NTCs, swelling, dT/dt, V spread)
 *
 * Each frame: [0xAA][LEN][TYPE][payload][XOR_checksum]
 */

#ifndef PACKET_FORMAT_H
#define PACKET_FORMAT_H

#include "anomaly_eval.h"
#include "correlation_engine.h"
#include <stdint.h>

/* Packet framing */
#define PACKET_SYNC_BYTE 0xAA
#define PACKET_TYPE_PACK 0x01
#define PACKET_TYPE_MODULE 0x02

/* Frame sizes */
#define PACKET_PACK_SIZE 38   /* Pack summary frame */
#define PACKET_MODULE_SIZE 17 /* Per-module detail frame */
#define PACKET_MAX_SIZE 38    /* Largest frame */

/* -----------------------------------------------------------------------
 * Pack summary output frame (Type 0x01)
 * ----------------------------------------------------------------------- */
typedef struct __attribute__((packed)) {
  uint8_t sync;       /* 0xAA                                    */
  uint8_t length;     /* Frame size (40)                         */
  uint8_t frame_type; /* 0x01                                    */

  /* Timestamp */
  uint32_t timestamp_ms; /* Milliseconds since boot                 */

  /* Electrical */
  uint16_t pack_voltage_dv; /* Pack voltage deci-volts                 */
  int16_t pack_current_da;  /* Pack current deci-amps (signed)         */
  uint16_t r_int_cmohm;     /* R_int in centi-milliohms (×100)         */

  /* Thermal summary */
  int16_t max_temp_dt;      /* Hottest NTC deci-°C                     */
  int16_t ambient_temp_dt;  /* Ambient temp deci-°C                    */
  int16_t core_temp_est_dt; /* Estimated core temp deci-°C             */
  uint8_t dt_dt_max_cdpm;   /* Max dT/dt ×100 in °C/min (0-255)       */

  /* Gas & Pressure */
  uint8_t gas_ratio_1_cp;        /* Gas ratio 1 ×100 (0-100)               */
  uint8_t gas_ratio_2_cp;        /* Gas ratio 2 ×100 (0-100)               */
  int16_t pressure_delta_1_chpa; /* Pressure Δ1 centi-hPa                  */
  int16_t pressure_delta_2_chpa; /* Pressure Δ2 centi-hPa                  */

  /* Pack health metrics */
  uint16_t v_spread_dmv;  /* V spread across 104 groups deci-mV      */
  uint8_t temp_spread_dt; /* Temp spread deci-°C (0-25.5°C)         */

  /* System state & anomaly */
  uint8_t system_state;    /* 0=NORMAL..3=EMERGENCY                   */
  uint8_t anomaly_mask;    /* Active category bitmask (CAT_*)         */
  uint8_t anomaly_count;   /* Number of active categories             */
  uint8_t anomaly_modules; /* Which modules have anomalies (bitmask)  */

  /* Hotspot */
  uint8_t hotspot_module; /* Module with worst anomaly (1-based)     */

  /* Risk assessment */
  uint8_t risk_factor_pct; /* 0-100% thermal runaway risk             */
  uint8_t cascade_stage;   /* 0=Normal..6=Runaway                     */

  /* Flags */
  uint8_t flags; /* bit0: emergency_direct                  */

  /* Checksum */
  uint8_t checksum; /* XOR of all preceding bytes              */
} telemetry_pack_frame_t;

/* -----------------------------------------------------------------------
 * Module detail output frame (Type 0x02)
 * ----------------------------------------------------------------------- */
typedef struct __attribute__((packed)) {
  uint8_t sync;       /* 0xAA                                    */
  uint8_t length;     /* Frame size (20)                         */
  uint8_t frame_type; /* 0x02                                    */

  uint8_t module_index; /* Module number (0-7)                     */

  /* NTC temperatures */
  int16_t ntc1_dt; /* NTC1 temp deci-°C                       */
  int16_t ntc2_dt; /* NTC2 temp deci-°C                       */

  /* Swelling */
  uint8_t swelling_pct; /* Module swelling 0-100%                  */

  /* Thermal dynamics */
  uint8_t delta_t_intra_dt; /* |NTC1-NTC2| deci-°C (0-25.5)           */
  uint8_t max_dt_dt_cdpm;   /* Max dT/dt ×100 °C/min (0-255)          */

  /* Module voltage summary */
  uint16_t module_voltage_dv; /* Module voltage deci-volts (~416)        */
  uint16_t v_spread_mv;       /* Voltage spread within module (mV)       */

  /* Reserved */
  uint8_t reserved;

  /* Checksum */
  uint8_t checksum; /* XOR of all preceding bytes              */
} telemetry_module_frame_t;

/* For backward compat (old code references PACKET_MAX_SIZE for the packet) */
typedef telemetry_pack_frame_t telemetry_packet_t;

/* -----------------------------------------------------------------------
 * Public API
 * ----------------------------------------------------------------------- */

/* Encode pack summary frame. Returns frame size. */
uint8_t packet_encode_pack(telemetry_pack_frame_t *pkt, uint32_t timestamp_ms,
                           const sensor_snapshot_t *sensors,
                           const anomaly_result_t *anomaly,
                           system_state_t state);

/* Encode one module detail frame. Returns frame size. */
uint8_t packet_encode_module(telemetry_module_frame_t *pkt,
                             uint8_t module_index,
                             const sensor_snapshot_t *sensors);

/* Compute XOR checksum over a buffer */
uint8_t packet_checksum(const uint8_t *data, uint8_t length);

/* Validate a received pack frame (check sync byte and checksum) */
int packet_validate_pack(const telemetry_pack_frame_t *pkt);

/* Legacy API — encode pack frame (backward compat for tests) */
uint8_t packet_encode(telemetry_packet_t *pkt, uint32_t timestamp_ms,
                      const sensor_snapshot_t *sensors,
                      const anomaly_result_t *anomaly, system_state_t state);

int packet_validate(const telemetry_packet_t *pkt);

#endif /* PACKET_FORMAT_H */
