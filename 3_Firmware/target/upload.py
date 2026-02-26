#!/usr/bin/env python3
"""
XMODEM Uploader for VSDSquadron ULTRA / THEJAS32
=================================================

Uploads a binary file to the THEJAS32 board via XMODEM protocol.
Includes DTR/RTS reset sequence matching the official vega-xmodem tool.

Usage:
  python 3_Firmware/target/upload.py 3_Firmware/build/user.bin
  python 3_Firmware/target/upload.py 3_Firmware/build/user.bin --port COM5
"""

import sys
import time
import struct
import argparse

try:
    import serial
    import serial.tools.list_ports
except ImportError:
    print("ERROR: pyserial not installed. Run: pip install pyserial")
    sys.exit(1)


# XMODEM constants
SOH = 0x01
EOT = 0x04
ACK = 0x06
NAK = 0x15
CAN = 0x18
SUB = 0x1A
CRC_MODE = ord('C')
BLOCK_SIZE = 128


def auto_detect_port():
    """Find likely VSDSquadron serial port."""
    try:
        ports = serial.tools.list_ports.comports()
    except Exception:
        return None

    preferred = []
    fallback = []
    for p in ports:
        desc = (p.description or "").lower()
        if any(k in desc for k in ["vsdsquadron", "ch340", "ch341", "cp210", "usb-serial"]):
            preferred.append(p.device)
        else:
            fallback.append(p.device)

    if preferred:
        return preferred[0]
    if fallback:
        return fallback[0]
    return None


def calc_crc16(data: bytes) -> int:
    """Calculate CRC-16/XMODEM (polynomial 0x1021)."""
    crc = 0
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = (crc << 1) ^ 0x1021
            else:
                crc = crc << 1
            crc &= 0xFFFF
    return crc


def reset_board(ser):
    """Reset the board via DTR/RTS toggle on CP2102N USB-serial bridge.
    
    The official vega-xmodem tool does 'sudo ./reset' before upload,
    which triggers the board's hardware reset via USB control signals.
    CP2102N can drive the RESETN pin through DTR/RTS.
    """
    print("Resetting board via DTR/RTS...")
    
    # Method 1: Toggle DTR (most common for CP210x auto-reset)
    ser.dtr = False
    ser.rts = False
    time.sleep(0.1)
    
    ser.dtr = True   # Pull DTR low (active)
    ser.rts = True   # Pull RTS low (active)
    time.sleep(0.25)
    
    ser.dtr = False  # Release
    ser.rts = False  # Release
    time.sleep(0.5)
    
    # Flush any bootloader banner
    ser.reset_input_buffer()
    
    print("Reset complete.")


def xmodem_send(ser, filepath):
    """Send a file via XMODEM protocol. Returns True on success."""
    with open(filepath, "rb") as f:
        filedata = f.read()

    file_size = len(filedata)
    if file_size > 249 * 1024:
        print(f"ERROR: File too large ({file_size} bytes). Max is 249KB.")
        return False
    
    total_blocks = (file_size + BLOCK_SIZE - 1) // BLOCK_SIZE

    print(f"File: {filepath}")
    print(f"Size: {file_size} bytes ({total_blocks} blocks)")
    print()

    # Wait for receiver 'C' (CRC mode) or NAK (checksum mode)
    print("Waiting for bootloader handshake...")
    use_crc = False
    timeout = 30

    start = time.time()
    while time.time() - start < timeout:
        if ser.in_waiting > 0:
            ch = ser.read(1)
            if ch == bytes([CRC_MODE]):
                use_crc = True
                print("Bootloader ready — CRC-16 mode")
                break
            elif ch == bytes([NAK]):
                use_crc = False
                print("Bootloader ready — Checksum mode")
                break
            else:
                # Print any boot messages
                try:
                    rest = ser.read(ser.in_waiting)
                    text = (ch + rest).decode('utf-8', errors='replace').strip()
                    if text:
                        print(f"  Boot: {text}")
                except:
                    pass
        time.sleep(0.05)
    else:
        print("ERROR: Timeout waiting for bootloader handshake.")
        print("Make sure BOOT_SEL jumper is OPEN (UART mode).")
        return False

    # Send blocks
    block_num = 1
    offset = 0

    for i in range(total_blocks):
        block_data = filedata[offset:offset + BLOCK_SIZE]
        if len(block_data) < BLOCK_SIZE:
            block_data += bytes([SUB] * (BLOCK_SIZE - len(block_data)))

        if use_crc:
            crc = calc_crc16(block_data)
            packet = bytes([SOH, block_num & 0xFF, (255 - block_num) & 0xFF])
            packet += block_data
            packet += struct.pack(">H", crc)
        else:
            chk = sum(block_data) & 0xFF
            packet = bytes([SOH, block_num & 0xFF, (255 - block_num) & 0xFF])
            packet += block_data
            packet += bytes([chk])

        retries = 10
        for attempt in range(retries):
            ser.write(packet)
            ser.flush()

            response = ser.read(1)
            if response == bytes([ACK]):
                break
            elif response == bytes([NAK]):
                if attempt < retries - 1:
                    print(f"  Block {block_num}: NAK, retrying...")
                continue
            elif response == bytes([CAN]):
                print("ERROR: Transfer cancelled by receiver")
                return False
            else:
                if attempt < retries - 1:
                    continue
        else:
            print(f"ERROR: Block {block_num} failed after {retries} retries")
            return False

        pct = (i + 1) * 100 // total_blocks
        bar = "=" * (pct // 5) + " " * (20 - pct // 5)
        print(f"\r  [{bar}] {pct:3d}% ({i+1}/{total_blocks} blocks)", end="", flush=True)

        block_num = (block_num + 1) & 0xFF
        offset += BLOCK_SIZE

    # Send EOT
    print()
    for _ in range(3):
        ser.write(bytes([EOT]))
        ser.flush()
        response = ser.read(1)
        if response == bytes([ACK]):
            print(f"\nUpload complete! {file_size} bytes transferred.")

            # *** CRITICAL: ROM bootloader requires ENTER to jump to user code ***
            # Bootloader says: "Please send file using XMODEM and then press ENTER key."
            time.sleep(0.3)
            ser.write(b"\r\n")
            ser.flush()
            print("Sent ENTER — bootloader jumping to user code...")
            return True
        time.sleep(0.1)
    
    print(f"\nWARNING: EOT not acknowledged, but data was sent.")
    # Try sending ENTER anyway
    ser.write(b"\r\n")
    ser.flush()
    return True


def main():
    parser = argparse.ArgumentParser(
        description="Upload firmware to VSDSquadron ULTRA via XMODEM"
    )
    parser.add_argument("binary", help="Path to .bin file")
    parser.add_argument("--port", default=None,
                        help="Serial port (auto-detect if omitted, e.g. COM5)")
    parser.add_argument("--baud", type=int, default=115200,
                        help="Baud rate (default: 115200)")
    parser.add_argument("--no-reset", action="store_true",
                        help="Skip DTR/RTS reset (manual reset only)")
    args = parser.parse_args()

    print("=" * 50)
    print("  VSDSquadron ULTRA — Firmware Uploader")
    print("=" * 50)
    print()

    port = args.port or auto_detect_port()
    if not port:
        print("ERROR: No serial port detected. Plug board and pass --port COMx explicitly.")
        sys.exit(1)

    try:
        ser = serial.Serial(
            port=port,
            baudrate=args.baud,
            timeout=2,
            bytesize=serial.EIGHTBITS,
            parity=serial.PARITY_NONE,
            stopbits=serial.STOPBITS_ONE,
        )
        print(f"Opened {port} at {args.baud} baud")
    except serial.SerialException as e:
        print(f"ERROR: Cannot open {port}: {e}")
        sys.exit(1)

    try:
        # Reset the board first (like official vega-xmodem does)
        if not args.no_reset:
            reset_board(ser)
        
        success = xmodem_send(ser, args.binary)
        if success:
            # Give the bootloader a moment to jump to user code
            time.sleep(0.5)
            
            print("\nListening for board output (30 seconds)...")
            print("-" * 50)
            end_time = time.time() + 30
            got_data = False
            while time.time() < end_time:
                if ser.in_waiting > 0:
                    data = ser.read(ser.in_waiting)
                    try:
                        text = data.decode('utf-8', errors='replace')
                        print(text, end="", flush=True)
                        got_data = True
                    except:
                        print(f"[hex: {data.hex()}]", end="", flush=True)
                        got_data = True
                time.sleep(0.05)
            print("\n" + "-" * 50)
            if got_data:
                print("Board is outputting data!")
            else:
                print("No output received. Try pressing RESET button.")
        else:
            print("\nUpload failed.")
            sys.exit(1)
    except KeyboardInterrupt:
        print("\nCancelled.")
    finally:
        ser.close()


if __name__ == "__main__":
    main()
