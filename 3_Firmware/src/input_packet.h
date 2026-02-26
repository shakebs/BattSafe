/*
 * input_packet.h — Digital Twin → Board Input Protocol (Full Pack)
 *
 * Multi-frame streaming protocol for ~139 sensor channels.
 * Instead of cramming everything into one tiny packet, we send:
 *   Frame 0x01: Pack-level data (voltage, current, gas, pressure, etc.)
 *   Frame 0x02: Module data (×8, one per module: NTCs, swelling, group Vs)
 *
 * Each frame: [0xBB][LEN][TYPE][payload][XOR_checksum]
 *
 * The firmware collects all 9 frames (1 pack + 8 modules) to build
 * a complete sensor_snapshot_t before running the anomaly evaluator.
 */

#ifndef INPUT_PACKET_H
#define INPUT_PACKET_H

#include <stdint.h>

/* Protocol constants */
#define INPUT_SYNC_BYTE 0xBB
#define INPUT_TYPE_PACK 0x01
#define INPUT_TYPE_MODULE 0x02

/* Frame sizes */
#define INPUT_PACK_FRAME_SIZE 30   /* sync + len + type + 26 payload + csum */
#define INPUT_MODULE_FRAME_SIZE 24 /* sync + len + type + 20 payload + csum */
#define INPUT_MAX_FRAME_SIZE 30

/* -----------------------------------------------------------------------
 * Pack-level frame (Type 0x01) — one per cycle
 *
 * Contains all pack-wide sensor readings.
 * ----------------------------------------------------------------------- */
typedef struct __attribute__((packed)) {
  uint8_t sync;       /* 0xBB                                    */
  uint8_t length;     /* Frame size (30)                         */
  uint8_t frame_type; /* 0x01 = pack frame                       */

  /* Electrical */
  uint16_t pack_voltage_dv; /* Pack voltage in deci-volts (332.8V→3328)*/
  int16_t pack_current_da;  /* Pack current in deci-amps (signed)      */

  /* Environment */
  int16_t ambient_temp_dt;   /* Ambient temp in deci-°C (30.0→300)      */
  int16_t coolant_inlet_dt;  /* Coolant inlet temp deci-°C              */
  int16_t coolant_outlet_dt; /* Coolant outlet temp deci-°C             */

  /* Gas sensors (2× BME680) */
  uint16_t gas_ratio_1_cp; /* Gas ratio ×100 (1.00 → 100)             */
  uint16_t gas_ratio_2_cp; /* Gas ratio ×100                          */

  /* Pressure sensors (2× co-located with gas) */
  int16_t pressure_delta_1_chpa; /* Pressure Δ centi-hPa                    */
  int16_t pressure_delta_2_chpa; /* Pressure Δ centi-hPa                    */

  /* Environment extras */
  uint8_t humidity_pct;    /* 0-100%                                  */
  uint16_t isolation_mohm; /* Isolation resistance (MΩ × 10)          */

  /* Checksum */
  uint8_t checksum; /* XOR of all preceding bytes              */
} input_pack_frame_t;

/* -----------------------------------------------------------------------
 * Module-level frame (Type 0x02) — sent 8 times per cycle
 *
 * Contains per-module sensor data including 13 group voltages
 * encoded efficiently as base + 13 delta bytes.
 * ----------------------------------------------------------------------- */
typedef struct __attribute__((packed)) {
  uint8_t sync;       /* 0xBB                                    */
  uint8_t length;     /* Frame size (24)                         */
  uint8_t frame_type; /* 0x02 = module frame                     */

  uint8_t module_index; /* Module number (0-7)                     */

  /* NTC temperatures */
  int16_t ntc1_dt; /* NTC1 temp in deci-°C                    */
  int16_t ntc2_dt; /* NTC2 temp in deci-°C                    */

  /* Swelling */
  uint8_t swelling_pct; /* Module swelling 0-100%                  */

  /* Group voltages: base + 13 deltas (saves 14 bytes vs 13×int16)
   * V_group[g] = base_mv + delta[g] (mV)
   * base_mv = mean of all 13 group voltages in mV                       */
  uint16_t v_base_mv; /* Base voltage in mV (e.g., 3280)        */
  int8_t v_delta[13]; /* Per-group delta from base in mV        */

  /* Checksum */
  uint8_t checksum; /* XOR of all preceding bytes             */
} input_module_frame_t;

/* -----------------------------------------------------------------------
 * Receiver state machine
 * ----------------------------------------------------------------------- */

#define INPUT_RX_BUF_SIZE 64

typedef struct {
  uint8_t buf[INPUT_RX_BUF_SIZE];
  uint8_t write_pos;

  /* Assembled snapshot tracking */
  uint8_t pack_received;    /* 1 if pack frame received this cycle     */
  uint8_t modules_received; /* Bitmask of which modules received       */

  /* Last valid frames */
  input_pack_frame_t last_pack;
  input_module_frame_t last_modules[8];
} input_rx_state_t;

/* Initialize the RX state */
void input_rx_init(input_rx_state_t *rx);

/* Feed one byte from UART RX.
 * Returns 1 if a complete valid frame was parsed (could be pack or module).
 * Returns 2 if ALL 9 frames (1 pack + 8 modules) are now available. */
int input_rx_feed(input_rx_state_t *rx, uint8_t byte);

/* Check if a complete snapshot is available (pack + all 8 modules) */
int input_rx_has_full_snapshot(const input_rx_state_t *rx);

/* Reset the received-frame tracking for next cycle */
void input_rx_reset_cycle(input_rx_state_t *rx);

#endif /* INPUT_PACKET_H */
