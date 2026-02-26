#!/usr/bin/env python3
"""
Serial Packet Reader — Connects dashboard to the real board
============================================================

When the VSDSquadron ULTRA board is connected via USB,
this module reads UART telemetry packets and converts them
into SensorReading objects that the dashboard can display.

Usage:
  # Auto-detect board
  python3 dashboard/src/serial_reader.py

  # Specify port
  python3 dashboard/src/serial_reader.py --port /dev/tty.usbserial-XXX

  # List available ports
  python3 dashboard/src/serial_reader.py --list
"""

import struct
import time
import sys
from dataclasses import dataclass

try:
    import serial
    import serial.tools.list_ports
except ImportError:
    print("ERROR: pyserial not installed. Run:")
    print("  pip install pyserial")
    sys.exit(1)


# Telemetry packet format (must match packet_format.h)
SYNC_BYTE = 0xAA
PACKET_SIZE = 32

# Packet field layout (after sync + length bytes):
# Offset  Size  Field
#   0      1    sync (0xAA)
#   1      1    length
#   2-5    4    timestamp_ms (uint32_t LE)
#   6-7    2    voltage_x100 (uint16_t LE)
#   8-9    2    current_x100 (uint16_t LE)
#  10-11   2    r_internal_mohm (uint16_t LE)
#  12-13   2    temp_cell1_x10 (int16_t LE)
#  14-15   2    temp_cell2_x10 (int16_t LE)
#  16-17   2    temp_cell3_x10 (int16_t LE)
#  18-19   2    temp_cell4_x10 (int16_t LE)
#  20-21   2    gas_ratio_x100 (uint16_t LE)
#  22-23   2    pressure_delta_x100 (int16_t LE)
#  24      1    swelling_pct
#  25      1    system_state
#  26      1    anomaly_mask
#  27      1    anomaly_count
#  28      1    temp_ambient_dt (int8, deci-°C)
#  29      1    dt_dt_max_cdps  (uint8, ×100 °C/s)
#  30      1    flags (bit0: emergency_direct)
#  31      1    checksum (XOR)


@dataclass
class BoardReading:
    """Decoded telemetry reading from the board."""
    timestamp_ms: int
    voltage_v: float
    current_a: float
    r_internal_mohm: float
    temp_cell1_c: float
    temp_cell2_c: float
    temp_cell3_c: float
    temp_cell4_c: float
    gas_ratio: float
    pressure_delta_hpa: float
    swelling_pct: float
    system_state: str
    anomaly_mask: int
    anomaly_count: int
    temp_ambient_c: float = 25.0
    dt_dt_max: float = 0.0
    emergency_direct: bool = False


STATE_NAMES = {0: "NORMAL", 1: "WARNING", 2: "CRITICAL", 3: "EMERGENCY"}
CATEGORY_NAMES = {
    0x01: "electrical",
    0x02: "thermal",
    0x04: "gas",
    0x08: "pressure",
    0x10: "swelling",
}


def decode_packet(raw: bytes) -> BoardReading | None:
    """Decode a raw telemetry packet into a BoardReading.

    Returns None if the packet is invalid.
    """
    if len(raw) < PACKET_SIZE:
        return None
    if raw[0] != SYNC_BYTE:
        return None
    if raw[1] != PACKET_SIZE:
        return None

    # Verify checksum (XOR of all bytes except last)
    checksum = 0
    for b in raw[:-1]:
        checksum ^= b
    if checksum != raw[-1]:
        return None

    # Unpack fields (little-endian)
    # Skip sync(1) + length(1) = 2 bytes header
    (timestamp_ms,) = struct.unpack_from("<I", raw, 2)
    (voltage_x100,) = struct.unpack_from("<H", raw, 6)
    (current_x100,) = struct.unpack_from("<H", raw, 8)
    (r_int_mohm,) = struct.unpack_from("<H", raw, 10)
    (t1_x10,) = struct.unpack_from("<h", raw, 12)
    (t2_x10,) = struct.unpack_from("<h", raw, 14)
    (t3_x10,) = struct.unpack_from("<h", raw, 16)
    (t4_x10,) = struct.unpack_from("<h", raw, 18)
    (gas_x100,) = struct.unpack_from("<H", raw, 20)
    (press_x100,) = struct.unpack_from("<h", raw, 22)
    swelling = raw[24]
    state = raw[25]
    mask = raw[26]
    count = raw[27]
    # New fields (formerly reserved)
    temp_ambient_dt = struct.unpack_from("<b", raw, 28)[0]  # int8
    dt_dt_cdps = raw[29]                                      # uint8
    flags = raw[30]                                            # uint8

    return BoardReading(
        timestamp_ms=timestamp_ms,
        voltage_v=voltage_x100 / 100.0,
        current_a=current_x100 / 100.0,
        r_internal_mohm=float(r_int_mohm),
        temp_cell1_c=t1_x10 / 10.0,
        temp_cell2_c=t2_x10 / 10.0,
        temp_cell3_c=t3_x10 / 10.0,
        temp_cell4_c=t4_x10 / 10.0,
        gas_ratio=gas_x100 / 100.0,
        pressure_delta_hpa=press_x100 / 100.0,
        swelling_pct=swelling,
        system_state=STATE_NAMES.get(state, f"UNKNOWN({state})"),
        anomaly_mask=mask,
        anomaly_count=count,
        temp_ambient_c=temp_ambient_dt / 10.0,
        dt_dt_max=dt_dt_cdps / 100.0,
        emergency_direct=bool(flags & 0x01),
    )


def active_categories(mask: int) -> list[str]:
    """Convert anomaly bitmask to list of category names."""
    return [name for bit, name in CATEGORY_NAMES.items() if mask & bit]


def find_board_port() -> str | None:
    """Auto-detect the VSDSquadron ULTRA serial port."""
    ports = list(serial.tools.list_ports.comports())
    if not ports:
        return None

    def score_port(port) -> int:
        device = (port.device or "").lower()
        desc = (port.description or "").lower()
        hwid = (port.hwid or "").lower()
        blob = f"{device} {desc} {hwid}"

        # Avoid common non-target pseudo ports.
        # Include Bluetooth SPP devices that don't say "bluetooth" in name.
        if any(kw in blob for kw in ["debug-console", "bluetooth", "irda"]):
            return -100

        # Bluetooth SPP ports on macOS: /dev/cu.DeviceName (no "usb" in path).
        # Real USB-serial adapters always have "usb" in the macOS device path.
        is_usb_port = "usb" in device
        if not is_usb_port:
            return -50  # Almost certainly Bluetooth or built-in, not our board

        score = 0
        if any(kw in blob for kw in ["usbserial", "usb serial", "wch", "ch340", "cp210", "ftdi"]):
            score += 80
        if any(kw in blob for kw in ["uart", "tty.usb", "cu.usb"]):
            score += 40
        if "modem" in blob:
            score += 20
        if "debug" in blob:
            score -= 40
        return score

    ranked = sorted(ports, key=score_port, reverse=True)
    best = ranked[0]
    if score_port(best) > 0:
        return best.device

    # Conservative fallback: first USB-based device.
    for p in ranked:
        device = (p.device or "").lower()
        if "usb" in device:
            return p.device
    return None


def list_ports():
    """Print all available serial ports."""
    ports = serial.tools.list_ports.comports()
    if not ports:
        print("No serial ports found.")
        return
    print(f"Found {len(ports)} port(s):\n")
    for p in ports:
        print(f"  {p.device}")
        print(f"    Description: {p.description}")
        print(f"    Hardware ID: {p.hwid}")
        print()


class SerialReader:
    """Reads telemetry packets from the board over USB serial.

    Usage:
        reader = SerialReader("/dev/tty.usbserial-XXX")
        reader.open()

        while True:
            reading = reader.read_packet()
            if reading:
                print(reading)
    """

    def __init__(self, port: str, baud: int = 115200):
        self.port = port
        self.baud = baud
        self.ser = None
        self.buffer = bytearray()

    def open(self):
        """Open the serial port."""
        self.ser = serial.Serial(
            port=self.port,
            baudrate=self.baud,
            timeout=0.1,    # 100ms read timeout
            bytesize=serial.EIGHTBITS,
            parity=serial.PARITY_NONE,
            stopbits=serial.STOPBITS_ONE,
        )
        # Bring the board into a known state: pulse reset lines similar to uploader.
        try:
            self.ser.dtr = False
            self.ser.rts = False
            time.sleep(0.1)
            self.ser.dtr = True
            self.ser.rts = True
            time.sleep(0.25)
            self.ser.dtr = False
            self.ser.rts = False
            time.sleep(0.35)

            # Probe startup output to decide whether ROM bootloader is active.
            probe = bytearray()
            deadline = time.time() + 0.4
            while time.time() < deadline:
                chunk = self.ser.read(32)
                if chunk:
                    probe.extend(chunk)

            # If we mostly see 'C', ROM is waiting for XMODEM/ENTER.
            if probe and probe.count(ord("C")) >= int(len(probe) * 0.6):
                self.ser.write(b"\r\n")
                self.ser.flush()
                time.sleep(0.1)
            else:
                # Keep already-read bytes so packet parser can consume them.
                self.buffer.extend(probe)
        except Exception:
            pass
        print(f"Opened {self.port} at {self.baud} baud")

    def close(self):
        if self.ser and self.ser.is_open:
            self.ser.close()

    def read_packet(self) -> BoardReading | None:
        """Read and decode one telemetry packet.

        Returns None if no valid packet is available yet.
        Also auto-detects the ROM bootloader's 'C' pattern after a board
        reset and sends ENTER to jump to existing firmware.
        """
        if not self.ser:
            return None

        # Read available bytes into buffer
        available = self.ser.in_waiting
        if available > 0:
            self.buffer.extend(self.ser.read(available))

        # --- Bootloader auto-skip ---
        # If the board was reset, the ROM bootloader sends continuous 'C'
        # (XMODEM CRC handshake).  Sending CAN (cancel) bytes followed by
        # ENTER tells it to abort XMODEM and jump to firmware in flash.
        if len(self.buffer) >= 8:
            c_count = self.buffer.count(ord("C"))
            if c_count >= int(len(self.buffer) * 0.6):
                # Rate-limit: only attempt once every 3 seconds
                now = time.time()
                if now - getattr(self, "_last_boot_skip", 0) < 3.0:
                    self.buffer.clear()
                    return None
                self._last_boot_skip = now

                print("[AUTO] Bootloader detected (CCC pattern) — "
                      "sending CAN + ENTER to jump to firmware...")
                CAN = b"\x18"  # XMODEM cancel byte
                self.ser.write(CAN * 8)  # Cancel any pending transfer
                self.ser.flush()
                time.sleep(0.3)
                self.ser.write(b"\r\n")  # Tell bootloader to jump
                self.ser.flush()
                self.buffer.clear()
                time.sleep(1.5)  # Give firmware time to boot
                # Drain the boot banner text
                if self.ser.in_waiting > 0:
                    self.buffer.extend(self.ser.read(self.ser.in_waiting))
                return None

        # Search for sync byte
        while len(self.buffer) >= PACKET_SIZE:
            # Find next sync byte
            idx = self.buffer.find(SYNC_BYTE)
            if idx < 0:
                self.buffer.clear()
                return None

            # Discard bytes before sync
            if idx > 0:
                self.buffer = self.buffer[idx:]

            # Check if we have a full packet
            if len(self.buffer) < PACKET_SIZE:
                return None

            # Try to decode
            raw = bytes(self.buffer[:PACKET_SIZE])
            reading = decode_packet(raw)

            if reading:
                # Valid packet — consume it
                self.buffer = self.buffer[PACKET_SIZE:]
                return reading
            else:
                # Invalid — skip this sync byte and search for next
                self.buffer = self.buffer[1:]

        return None

    def read_latest_packet(self, max_packets: int = 128) -> BoardReading | None:
        """Read all currently decodable packets and return the newest one.

        This prevents UI lag when producer rate temporarily exceeds
        dashboard frame rate.
        """
        latest = None
        for _ in range(max_packets):
            reading = self.read_packet()
            if reading is None:
                break
            latest = reading
        return latest

    def read_text_line(self) -> str | None:
        """Read a text debug line (like [STATE] or [TEL] messages)."""
        if not self.ser:
            return None
        if self.ser.in_waiting > 0:
            try:
                line = self.ser.readline().decode("utf-8", errors="replace").strip()
                if line:
                    return line
            except Exception:
                pass
        return None


# ---------------------------------------------------------------------------
# CLI — Run standalone to test the serial connection
# ---------------------------------------------------------------------------

def main():
    import argparse
    parser = argparse.ArgumentParser(
        description="Read telemetry from VSDSquadron ULTRA board"
    )
    parser.add_argument("--port", type=str, help="Serial port (e.g., /dev/tty.usbserial-XXX)")
    parser.add_argument("--baud", type=int, default=115200, help="Baud rate")
    parser.add_argument("--list", action="store_true", help="List available ports")
    parser.add_argument("--raw", action="store_true", help="Show raw text output instead of decoded packets")
    args = parser.parse_args()

    if args.list:
        list_ports()
        return

    port = args.port or find_board_port()
    if not port:
        print("ERROR: No serial port found. Use --port or --list")
        sys.exit(1)

    reader = SerialReader(port, args.baud)
    try:
        reader.open()
        print("Waiting for telemetry...\n")

        if args.raw:
            # Raw mode — print text lines as-is
            while True:
                line = reader.read_text_line()
                if line:
                    print(line)
        else:
            # Decoded mode — parse binary packets
            while True:
                reading = reader.read_packet()
                if reading:
                    cats = active_categories(reading.anomaly_mask)
                    print(f"t={reading.timestamp_ms:>6}ms | "
                          f"V={reading.voltage_v:.1f} I={reading.current_a:.1f} | "
                          f"T=[{reading.temp_cell1_c:.0f},{reading.temp_cell2_c:.0f},"
                          f"{reading.temp_cell3_c:.0f},{reading.temp_cell4_c:.0f}] | "
                          f"gas={reading.gas_ratio:.2f} dP={reading.pressure_delta_hpa:.1f} | "
                          f"state={reading.system_state} cats={','.join(cats) or 'none'}")
                time.sleep(0.01)

    except KeyboardInterrupt:
        print("\nStopped.")
    except serial.SerialException as e:
        print(f"Serial error: {e}")
    finally:
        reader.close()


if __name__ == "__main__":
    main()
