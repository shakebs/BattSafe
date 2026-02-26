"""
Battery Pack Digital Twin — Serial Bridge
============================================
Encodes simulation data into binary packets for VSDSquadron Ultra.
"""

import struct
import serial
import serial.tools.list_ports
import threading
import queue
import time
from typing import Dict, Optional

from digital_twin.config import SERIAL_BAUD_RATE, SERIAL_SYNC_BYTE, SERIAL_PAYLOAD_LEN


class SerialBridge:
    """Encodes pack snapshot → binary packet → serial port."""

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
                pass  # Drop if queue full

    def encode_packet(self, snapshot: Dict) -> bytes:
        """Encode snapshot into 20-byte input packet (0xBB sync) for the board.

        Maps 139-channel digital twin data to the 7 prototype sensor values
        that the VSDSquadron firmware expects.
        """
        INPUT_SYNC = 0xBB
        INPUT_LEN = 20

        # Electrical — use pack-level voltage scaled to prototype range
        # Digital twin: ~341V (104S), prototype expects ~14.8V (4S)
        # Scale: V_proto = V_pack / 104 * 4 ≈ V_pack * 0.0385
        pack_v = snapshot.get('pack_voltage', 341.0)
        proto_v = pack_v * 4.0 / 104.0  # Scale to 4S
        voltage_cv = int(proto_v * 100)

        pack_i = snapshot.get('pack_current', 0.0)
        current_ca = int(pack_i * 100)

        # Thermal — pick module 1's NTC readings as representative
        modules = snapshot.get('modules', [])
        if modules:
            m0 = modules[0]
            temp1 = m0.get('temp_ntc1', 30.0)
            temp2 = m0.get('temp_ntc2', 30.0)
            # Use two more modules for spatial variation
            m1 = modules[1] if len(modules) > 1 else m0
            temp3 = m1.get('temp_ntc1', 30.0)
            temp4 = m1.get('temp_ntc2', 30.0)
        else:
            temp1 = temp2 = temp3 = temp4 = 30.0

        temp1_dt = int(temp1 * 10)
        temp2_dt = int(temp2 * 10)
        temp3_dt = int(temp3 * 10)
        temp4_dt = int(temp4 * 10)

        # Gas — average of two gas ratios
        gas1 = snapshot.get('gas_ratio_1', 1.0)
        gas2 = snapshot.get('gas_ratio_2', 1.0)
        gas_ratio_cp = int(((gas1 + gas2) / 2.0) * 100)

        # Pressure — average of two deltas
        p1 = snapshot.get('pressure_delta_1', 0.0)
        p2 = snapshot.get('pressure_delta_2', 0.0)
        pressure_chpa = int(((p1 + p2) / 2.0) * 100)

        # Swelling — use module 1's swelling percentage
        if modules:
            swelling = int(modules[0].get('swelling_pct', 0))
        else:
            swelling = 0
        swelling = min(swelling, 100)

        # Build payload (matching input_packet_t struct, little-endian)
        payload = struct.pack('<HhhhhhHhB',
            voltage_cv,       # uint16 voltage_cv
            current_ca,       # int16  current_ca
            temp1_dt,         # int16  temp1_dt
            temp2_dt,         # int16  temp2_dt
            temp3_dt,         # int16  temp3_dt
            temp4_dt,         # int16  temp4_dt
            gas_ratio_cp,     # uint16 gas_ratio_cp
            pressure_chpa,    # int16  pressure_delta_chpa
            swelling,         # uint8  swelling_pct
        )

        # Frame: SYNC + LEN + PAYLOAD + CHECKSUM
        frame_no_csum = struct.pack('BB', INPUT_SYNC, INPUT_LEN) + payload
        checksum = 0
        for b in frame_no_csum:
            checksum ^= b
        frame = frame_no_csum + struct.pack('B', checksum)
        return frame

    def _send_loop(self):
        """Background send loop."""
        while self.is_connected:
            try:
                snapshot = self._send_queue.get(timeout=1.0)
                packet = self.encode_packet(snapshot)
                if self._serial and self._serial.is_open:
                    self._serial.write(packet)
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
