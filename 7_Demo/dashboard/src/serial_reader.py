"""
Serial Reader — Multi-Frame Telemetry Decoder (Full Pack Edition)
===================================================================
Reads multi-frame telemetry from the VSDSquadron ULTRA output.

Protocol:
  Frame 0x01 (Pack):   40 bytes — Pack summary + anomaly + risk
  Frame 0x02 (Module): 20 bytes × 8 — Per-module NTC, swelling, voltage

Each frame: [0xAA][LEN][TYPE][payload][XOR_checksum]
"""

import struct
import time
from dataclasses import dataclass, field
from typing import List, Optional

try:
    import serial
    import serial.tools.list_ports
    HAS_SERIAL = True
except ImportError:
    HAS_SERIAL = False

# Protocol constants
SYNC_BYTE = 0xAA
PACK_FRAME_TYPE = 0x01
MODULE_FRAME_TYPE = 0x02
PACK_FRAME_SIZE = 38
MODULE_FRAME_SIZE = 17

NUM_MODULES = 8

# State names
STATE_NAMES = {0: "NORMAL", 1: "WARNING", 2: "CRITICAL", 3: "EMERGENCY"}

# Cascade stage names
CASCADE_NAMES = ["Normal", "Elevated", "SEI_Decomp", "Separator",
                 "Electrolyte", "Cathode", "RUNAWAY"]


def active_categories(mask):
    """Return list of active category names from bitmask."""
    cats = []
    if mask & 0x01: cats.append("Electrical")
    if mask & 0x02: cats.append("Thermal")
    if mask & 0x04: cats.append("Gas")
    if mask & 0x08: cats.append("Pressure")
    if mask & 0x10: cats.append("Swelling")
    return cats


@dataclass
class ModuleReading:
    """Per-module telemetry data from firmware output."""
    module_index: int = 0
    ntc1_c: float = 25.0
    ntc2_c: float = 25.0
    swelling_pct: float = 0.0
    delta_t_intra: float = 0.0
    max_dt_dt: float = 0.0
    module_voltage: float = 41.6   # 13 × 3.2V
    v_spread_mv: float = 0.0

    def to_dict(self):
        return {
            "module_index": self.module_index,
            "ntc1": self.ntc1_c,
            "ntc2": self.ntc2_c,
            "swelling_pct": self.swelling_pct,
            "delta_t_intra": self.delta_t_intra,
            "max_dt_dt": self.max_dt_dt,
            "module_voltage": self.module_voltage,
            "v_spread_mv": self.v_spread_mv,
        }


@dataclass
class BoardReading:
    """Full-pack telemetry reading from the board."""
    # Pack summary
    timestamp_ms: int = 0
    voltage_v: float = 332.8
    current_a: float = 0.0
    r_internal_mohm: float = 0.44

    # Thermal
    max_temp_c: float = 30.0
    temp_ambient_c: float = 25.0
    core_temp_est_c: float = 30.0
    dt_dt_max: float = 0.0

    # Gas & pressure
    gas_ratio_1: float = 1.0
    gas_ratio_2: float = 1.0
    pressure_delta_1_hpa: float = 0.0
    pressure_delta_2_hpa: float = 0.0

    # Pack health
    v_spread_mv: float = 0.0
    temp_spread_c: float = 0.0

    # System state
    system_state: str = "NORMAL"
    anomaly_mask: int = 0
    anomaly_count: int = 0
    anomaly_modules: int = 0
    hotspot_module: int = 0

    # Risk
    risk_pct: int = 0
    cascade_stage: int = 0
    emergency_direct: bool = False

    # Per-module data
    module_data: List[dict] = field(default_factory=list)

    # UI compatibility fields (first 4 module NTC1 values)
    temp_cell1_c: float = 25.0
    temp_cell2_c: float = 25.0
    temp_cell3_c: float = 25.0
    temp_cell4_c: float = 25.0

    # UI compatibility single-value fields
    gas_ratio: float = 1.0
    pressure_delta_hpa: float = 0.0
    swelling_pct: float = 0.0

    def update_ui_compat(self):
        """Populate UI compatibility fields from full module data."""
        if len(self.module_data) > 0:
            self.temp_cell1_c = self.module_data[0].get('ntc1', 25.0)
        if len(self.module_data) > 1:
            self.temp_cell2_c = self.module_data[1].get('ntc1', 25.0)
        if len(self.module_data) > 2:
            self.temp_cell3_c = self.module_data[2].get('ntc1', 25.0)
        if len(self.module_data) > 3:
            self.temp_cell4_c = self.module_data[3].get('ntc1', 25.0)

        self.gas_ratio = min(self.gas_ratio_1, self.gas_ratio_2)
        self.pressure_delta_hpa = max(self.pressure_delta_1_hpa,
                                       self.pressure_delta_2_hpa)
        if self.module_data:
            self.swelling_pct = max(m.get('swelling_pct', 0)
                                    for m in self.module_data)


def find_board_port() -> Optional[str]:
    """Try to auto-detect the VSDSquadron board serial port."""
    if not HAS_SERIAL:
        return None

    ports = serial.tools.list_ports.comports()
    for p in ports:
        desc = (p.description or '').lower()
        if any(k in desc for k in ['ch340', 'ch341', 'usb-serial', 'vsdsquadron']):
            return p.device
    if ports:
        return ports[0].device
    return None


class SerialReader:
    """Reads and decodes multi-frame telemetry from VSDSquadron ULTRA."""

    def __init__(self, port: str, baud: int = 115200, timeout: float = 2.0):
        self.port = port
        self.baud = baud
        self.timeout = timeout
        self.ser = None
        self._buf = bytearray()

        # Latest parsed data
        self._pack_frame = None
        self._module_frames = {}
        self._last_reading = None

    def open(self):
        """Open serial port."""
        if not HAS_SERIAL:
            raise RuntimeError("pyserial not installed")

        self.ser = serial.Serial(
            self.port, self.baud, timeout=self.timeout,
            write_timeout=1.0
        )
        self._buf = bytearray()
        print(f"[SerialReader] Opened {self.port} @ {self.baud}")

    def close(self):
        """Close serial port."""
        if self.ser:
            try:
                self.ser.close()
            except:
                pass
            self.ser = None

    def read_text_line(self) -> Optional[str]:
        """Read a text line from the serial port."""
        if not self.ser or not self.ser.is_open:
            return None
        try:
            if self.ser.in_waiting:
                line = self.ser.readline().decode('ascii', errors='replace').strip()
                if line:
                    return line
        except:
            pass
        return None

    def read_latest_packet(self) -> Optional[BoardReading]:
        """Read and decode multi-frame telemetry.

        Reads available bytes, parses frames, and returns a BoardReading
        when a complete set (pack + modules) is available.
        """
        if not self.ser or not self.ser.is_open:
            return None

        # Read available bytes
        try:
            avail = self.ser.in_waiting
            if avail > 0:
                data = self.ser.read(min(avail, 1024))
                self._buf.extend(data)
        except Exception:
            return None

        # Try to parse frames from buffer
        changed = self._parse_frames()

        # Return a reading if we have pack frame + at least some modules
        if self._pack_frame and len(self._module_frames) > 0:
            reading = self._build_reading()
            return reading

        return self._last_reading if changed else None

    def _parse_frames(self) -> bool:
        """Parse any complete frames from the buffer. Returns True if new data."""
        changed = False

        while len(self._buf) >= 3:
            # Find sync byte
            try:
                sync_idx = self._buf.index(SYNC_BYTE)
            except ValueError:
                self._buf.clear()
                break

            # Discard bytes before sync
            if sync_idx > 0:
                del self._buf[:sync_idx]

            if len(self._buf) < 3:
                break

            frame_len = self._buf[1]
            frame_type = self._buf[2]

            # Validate frame type and length
            if frame_type == PACK_FRAME_TYPE and frame_len != PACK_FRAME_SIZE:
                del self._buf[0]
                continue
            elif frame_type == MODULE_FRAME_TYPE and frame_len != MODULE_FRAME_SIZE:
                del self._buf[0]
                continue
            elif frame_type not in (PACK_FRAME_TYPE, MODULE_FRAME_TYPE):
                del self._buf[0]
                continue

            # Wait for full frame
            if len(self._buf) < frame_len:
                break

            # Validate checksum
            frame_data = bytes(self._buf[:frame_len])
            expected_csum = 0
            for b in frame_data[:-1]:
                expected_csum ^= b
            if frame_data[-1] != expected_csum:
                del self._buf[0]
                continue

            # Parse the frame
            if frame_type == PACK_FRAME_TYPE:
                self._pack_frame = self._decode_pack_frame(frame_data)
                changed = True
            elif frame_type == MODULE_FRAME_TYPE:
                mod = self._decode_module_frame(frame_data)
                if mod:
                    self._module_frames[mod.module_index] = mod
                    changed = True

            # Consume frame
            del self._buf[:frame_len]

        return changed

    def _decode_pack_frame(self, data: bytes) -> dict:
        """Decode a 40-byte pack summary frame."""
        # Skip sync (1), length (1), type (1) = 3-byte header
        payload = data[3:-1]  # Exclude checksum

        # Unpack: timestamp(u32) + packV(u16) + packI(i16) + Rint(u16) +
        # maxT(i16) + ambT(i16) + coreT(i16) + dtdt(u8) +
        # gas1(u8) + gas2(u8) + p1(i16) + p2(i16) +
        # vspread(u16) + tspread(u8) +
        # state(u8) + mask(u8) + count(u8) + anom_mods(u8) +
        # hotspot(u8) + risk(u8) + cascade(u8) + flags(u8)
        fmt = '<IHhHhhh B BB hh HB BBBB BBBB'
        try:
            vals = struct.unpack(fmt, payload)
        except struct.error:
            return None

        return {
            'timestamp_ms': vals[0],
            'pack_voltage_dv': vals[1],
            'pack_current_da': vals[2],
            'r_int_cmohm': vals[3],
            'max_temp_dt': vals[4],
            'ambient_temp_dt': vals[5],
            'core_temp_est_dt': vals[6],
            'dt_dt_max_cdpm': vals[7],
            'gas_ratio_1_cp': vals[8],
            'gas_ratio_2_cp': vals[9],
            'pressure_delta_1_chpa': vals[10],
            'pressure_delta_2_chpa': vals[11],
            'v_spread_dmv': vals[12],
            'temp_spread_dt': vals[13],
            'system_state': vals[14],
            'anomaly_mask': vals[15],
            'anomaly_count': vals[16],
            'anomaly_modules': vals[17],
            'hotspot_module': vals[18],
            'risk_factor_pct': vals[19],
            'cascade_stage': vals[20],
            'flags': vals[21],
        }

    def _decode_module_frame(self, data: bytes) -> Optional[ModuleReading]:
        """Decode a 20-byte module detail frame."""
        payload = data[3:-1]

        # module_idx(u8) + ntc1(i16) + ntc2(i16) + swelling(u8) +
        # delta_t_intra(u8) + dt_dt(u8) + module_v(u16) + v_spread(u16) + reserved(u8)
        fmt = '<B hh B BB HH B'
        try:
            vals = struct.unpack(fmt, payload)
        except struct.error:
            return None

        mod = ModuleReading()
        mod.module_index = vals[0]
        mod.ntc1_c = vals[1] / 10.0
        mod.ntc2_c = vals[2] / 10.0
        mod.swelling_pct = float(vals[3])
        mod.delta_t_intra = vals[4] / 10.0
        mod.max_dt_dt = vals[5] / 100.0
        mod.module_voltage = vals[6] / 10.0
        mod.v_spread_mv = float(vals[7])

        return mod

    def _build_reading(self) -> BoardReading:
        """Build a BoardReading from the latest pack + module frames."""
        r = BoardReading()
        pf = self._pack_frame

        if pf:
            r.timestamp_ms = pf['timestamp_ms']
            r.voltage_v = pf['pack_voltage_dv'] / 10.0
            r.current_a = pf['pack_current_da'] / 10.0
            r.r_internal_mohm = pf['r_int_cmohm'] / 100.0
            r.max_temp_c = pf['max_temp_dt'] / 10.0
            r.temp_ambient_c = pf['ambient_temp_dt'] / 10.0
            r.core_temp_est_c = pf['core_temp_est_dt'] / 10.0
            r.dt_dt_max = pf['dt_dt_max_cdpm'] / 100.0
            r.gas_ratio_1 = pf['gas_ratio_1_cp'] / 100.0
            r.gas_ratio_2 = pf['gas_ratio_2_cp'] / 100.0
            r.pressure_delta_1_hpa = pf['pressure_delta_1_chpa'] / 100.0
            r.pressure_delta_2_hpa = pf['pressure_delta_2_chpa'] / 100.0
            r.v_spread_mv = pf['v_spread_dmv'] / 10.0
            r.temp_spread_c = pf['temp_spread_dt'] / 10.0
            r.system_state = STATE_NAMES.get(pf['system_state'], "UNKNOWN")
            r.anomaly_mask = pf['anomaly_mask']
            r.anomaly_count = pf['anomaly_count']
            r.anomaly_modules = pf['anomaly_modules']
            r.hotspot_module = pf['hotspot_module']
            r.risk_pct = pf['risk_factor_pct']
            r.cascade_stage = pf['cascade_stage']
            r.emergency_direct = bool(pf['flags'] & 0x01)

        # Add module data
        r.module_data = []
        for i in range(NUM_MODULES):
            if i in self._module_frames:
                r.module_data.append(self._module_frames[i].to_dict())
            else:
                r.module_data.append(ModuleReading(module_index=i).to_dict())

        r.update_ui_compat()

        self._last_reading = r
        return r
