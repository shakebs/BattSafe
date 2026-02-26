"""
Battery Pack Digital Twin — Serial Bridge (Full Pack Edition)
================================================================
Encodes 139-channel simulation data into multi-frame binary packets
for the VSDSquadron Ultra.

Multi-frame protocol:
  Frame 0x01 (Pack):   Pack voltage, current, gas×2, pressure×2, coolant, etc.
  Frame 0x02 (Module): Per-module NTC1/2, swelling, 13 group voltages (×8)

Each frame: [0xBB][LEN][TYPE][payload][XOR_checksum]
Total: 1 pack frame + 8 module frames = 9 frames per cycle
"""

import struct
import serial
import serial.tools.list_ports
import threading
import queue
import time
from typing import Dict, Optional

from digital_twin.config import SERIAL_BAUD_RATE, NUM_MODULES, GROUPS_PER_MODULE

# Protocol constants
INPUT_SYNC = 0xBB
PACK_FRAME_TYPE = 0x01
MODULE_FRAME_TYPE = 0x02
PACK_FRAME_SIZE = 30
MODULE_FRAME_SIZE = 24


def _xor_checksum(data: bytes) -> int:
    """XOR all bytes for checksum."""
    csum = 0
    for b in data:
        csum ^= b
    return csum


class SerialBridge:
    """Encodes pack snapshot → multi-frame binary → serial port."""

    def __init__(self, port: Optional[str] = None, baud: int = SERIAL_BAUD_RATE):
        self.port = port or self._auto_detect_port()
        self.baud = baud
        self.is_connected = False
        self._serial = None
        self._send_queue = queue.Queue(maxsize=10)
        self._thread = None

        if self.port:
            self._connect()

    def _auto_detect_port(self) -> Optional[str]:
        """Try to find VSDSquadron or CH340 port."""
        ports = serial.tools.list_ports.comports()
        for p in ports:
            desc = (p.description or '').lower()
            if any(k in desc for k in ['ch340', 'ch341', 'usb-serial', 'vsdsquadron']):
                return p.device
        if ports:
            return ports[0].device
        return None

    def _connect(self):
        """Connect to serial port."""
        try:
            self._serial = serial.Serial(
                self.port, self.baud, timeout=1,
                write_timeout=1
            )
            self.is_connected = True
            self._thread = threading.Thread(target=self._send_loop, daemon=True)
            self._thread.start()
            print(f"[Serial] Connected to {self.port} @ {self.baud}")
        except Exception as e:
            print(f"[Serial] Connection failed: {e}")
            self.is_connected = False

    def send_data(self, snapshot: Dict):
        """Queue snapshot for sending."""
        if self.is_connected:
            try:
                self._send_queue.put_nowait(snapshot)
            except queue.Full:
                pass

    def encode_all_frames(self, snapshot: Dict) -> bytes:
        """Encode full 139-channel snapshot into 9 binary frames.

        Returns concatenated bytes: 1 pack frame + 8 module frames.
        """
        frames = bytearray()
        frames.extend(self._encode_pack_frame(snapshot))
        for m_idx in range(NUM_MODULES):
            frames.extend(self._encode_module_frame(snapshot, m_idx))
        return bytes(frames)

    def _encode_pack_frame(self, snapshot: Dict) -> bytes:
        """Encode pack-level data into a 30-byte frame."""
        # Pack voltage in deci-volts
        pack_v_dv = int(snapshot.get('pack_voltage', 332.8) * 10)
        # Pack current in deci-amps
        pack_i_da = int(snapshot.get('pack_current', 0.0) * 10)
        # Ambient temp deci-°C
        ambient_dt = int(snapshot.get('ambient_temp', 30.0) * 10)
        # Coolant temps deci-°C
        coolant_in_dt = int(snapshot.get('coolant_inlet', 25.0) * 10)
        coolant_out_dt = int(snapshot.get('coolant_outlet', 27.0) * 10)
        # Gas ratios ×100
        gas1_cp = int(snapshot.get('gas_ratio_1', 1.0) * 100)
        gas2_cp = int(snapshot.get('gas_ratio_2', 1.0) * 100)
        # Pressure deltas in centi-hPa
        p1_chpa = int(snapshot.get('pressure_delta_1', 0.0) * 100)
        p2_chpa = int(snapshot.get('pressure_delta_2', 0.0) * 100)
        # Humidity
        humidity = int(snapshot.get('humidity', 50.0))
        humidity = max(0, min(100, humidity))
        # Isolation (MΩ × 10)
        iso_mohm = int(snapshot.get('isolation_mohm', 500.0) * 10)

        payload = struct.pack('<HhhhhhHHhhBH',
            pack_v_dv,       # uint16 pack voltage deci-V
            pack_i_da,       # int16  pack current deci-A
            ambient_dt,      # int16  ambient temp deci-°C
            coolant_in_dt,   # int16  coolant inlet deci-°C
            coolant_out_dt,  # int16  coolant outlet deci-°C
            0,               # int16  reserved
            gas1_cp,         # uint16 gas ratio 1 ×100
            gas2_cp,         # uint16 gas ratio 2 ×100
            p1_chpa,         # int16  pressure Δ1 centi-hPa
            p2_chpa,         # int16  pressure Δ2 centi-hPa
            humidity,        # uint8  humidity %
            iso_mohm,        # uint16 isolation MΩ×10
        )

        # Build frame: [sync][len][type][payload][checksum]
        frame_no_csum = struct.pack('BBB', INPUT_SYNC, PACK_FRAME_SIZE,
                                    PACK_FRAME_TYPE) + payload
        csum = _xor_checksum(frame_no_csum)
        return frame_no_csum + struct.pack('B', csum)

    def _encode_module_frame(self, snapshot: Dict, module_idx: int) -> bytes:
        """Encode per-module data into a 24-byte frame."""
        modules = snapshot.get('modules', [])
        if module_idx < len(modules):
            mdata = modules[module_idx]
        else:
            mdata = {}

        # NTC temperatures in deci-°C
        ntc1_dt = int(mdata.get('temp_ntc1', 30.0) * 10)
        ntc2_dt = int(mdata.get('temp_ntc2', 30.0) * 10)

        # Swelling percentage
        swelling = int(mdata.get('swelling_pct', 0.0))
        swelling = max(0, min(100, swelling))

        # Group voltages: base (mean) + 13 deltas
        groups = mdata.get('groups', [])
        if groups:
            group_vs = [g.get('voltage', 3.20) for g in groups]
        else:
            group_vs = [3.20] * GROUPS_PER_MODULE

        # Pad or trim to exactly 13
        while len(group_vs) < GROUPS_PER_MODULE:
            group_vs.append(3.20)
        group_vs = group_vs[:GROUPS_PER_MODULE]

        base_v_mv = int(sum(group_vs) / len(group_vs) * 1000)
        deltas = []
        for v in group_vs:
            d = int(v * 1000) - base_v_mv
            d = max(-127, min(127, d))
            deltas.append(d)

        # Pack payload
        payload = struct.pack('<BhhBH',
            module_idx,      # uint8  module index (0-7)
            ntc1_dt,         # int16  NTC1 deci-°C
            ntc2_dt,         # int16  NTC2 deci-°C
            swelling,        # uint8  swelling %
            base_v_mv,       # uint16 base voltage mV
        )
        # Add 13 delta bytes (int8)
        for d in deltas:
            payload += struct.pack('<b', d)

        # Build frame
        frame_no_csum = struct.pack('BBB', INPUT_SYNC, MODULE_FRAME_SIZE,
                                    MODULE_FRAME_TYPE) + payload
        csum = _xor_checksum(frame_no_csum)
        return frame_no_csum + struct.pack('B', csum)

    # Compatibility API — encode compact single packet for fallback paths
    def encode_packet(self, snapshot: Dict) -> bytes:
        """Encode ALL frames for one snapshot cycle."""
        return self.encode_all_frames(snapshot)

    def _send_loop(self):
        """Background send loop."""
        while self.is_connected:
            try:
                snapshot = self._send_queue.get(timeout=1.0)
                frames = self.encode_all_frames(snapshot)
                if self._serial and self._serial.is_open:
                    self._serial.write(frames)
            except queue.Empty:
                continue
            except Exception as e:
                print(f"[Serial] Send error: {e}")
                self.is_connected = False
                break

    def close(self):
        """Close serial connection."""
        self.is_connected = False
        if self._serial:
            try:
                self._serial.close()
            except:
                pass
