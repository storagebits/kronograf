#!/usr/bin/env -S sh -c 'exec "$(dirname "$0")/../.venv/bin/python" "$0" "$@"'
"""Mac tool for the Kronograf console.

  tools/kg.py status                     sends a command and prints the reply
  tools/kg.py screenshot shot.png
  tools/kg.py --port /dev/cu.usbserial-XXXX status
  tools/kg.py --host kronograf.local status    workshop mode (Wi-Fi)

The TCP console is used when kronograf.local answers; otherwise the serial port is
auto-detected (first /dev/cu.usbserial-* or cu.wchusbserial*).
"""
import argparse
import socket
import base64
import glob
import struct
import sys
import time
import zlib

import serial


def find_port():
    ports = sorted(glob.glob("/dev/cu.usbserial-*") + glob.glob("/dev/cu.wchusbserial*"))
    if not ports:
        sys.exit("No watch serial port found")
    return ports[0]


def open_tcp(host):
    for attempt in range(3):
        try:
            s = serial.serial_for_url(f"socket://{host}:2323", timeout=2)
            s.reset_input_buffer()
            return s
        except serial.SerialException:
            if attempt == 2:
                raise
            time.sleep(1)


def open_port(path):
    # On macOS, opening the port often reboots the ESP32 (CP210x auto-reset circuit).
    # Listen for 1.5 s: if a boot is in progress, wait for "Kronograf ready".
    s = serial.Serial()
    s.port = path
    s.baudrate = 115200
    s.timeout = 0.3
    s.dtr = False
    s.rts = False
    s.open()
    end = time.time() + 1.5
    booting = False
    while time.time() < end:
        line = s.readline().decode(errors="replace")
        if "ets " in line or "Kronograf — booting" in line:
            booting = True
            end = time.time() + 15
        if "Kronograf ready" in line:
            time.sleep(0.5)  # let the last boot logs through
            break
    if booting:
        time.sleep(4)  # NTP sync running at boot: its Wi-Fi logs arrive around 4 s
    s.reset_input_buffer()
    s.timeout = 2
    return s


def write_png(path, w, h, rgb):
    def chunk(kind, data):
        c = kind + data
        return struct.pack(">I", len(data)) + c + struct.pack(">I", zlib.crc32(c) & 0xFFFFFFFF)

    raw = b"".join(b"\x00" + rgb[y * w * 3:(y + 1) * w * 3] for y in range(h))
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)


def rgb565_to_rgb888(data, swap):
    out = bytearray()
    for i in range(0, len(data), 2):
        c = (data[i] << 8 | data[i + 1]) if swap else (data[i + 1] << 8 | data[i])
        r, g, b = (c >> 11) & 0x1F, (c >> 5) & 0x3F, c & 0x1F
        out += bytes(((r * 527 + 23) >> 6, (g * 259 + 33) >> 6, (b * 527 + 23) >> 6))
    return bytes(out)


def screenshot(s, path):
    s.write(b"screenshot\n")
    deadline = time.time() + 10
    while True:
        line = s.readline().decode(errors="replace").strip()
        if line.startswith("KGSHOT "):
            break
        if time.time() > deadline:
            sys.exit("No KGSHOT reply")
    parts = line.split()
    if parts[1] == "ERR":
        sys.exit(f"Watch error: {line}")
    w, h, swap = int(parts[1]), int(parts[2]), parts[4] == "swap=1"
    rows = {}
    s.timeout = 5
    while True:
        line = s.readline().decode(errors="replace").strip()
        if line == "KGSHOT END":
            break
        if not line:
            print(f"capture interrupted after {len(rows)} rows", file=sys.stderr)
            return False
        if not line.startswith("R") or ":" not in line:
            continue  # interleaved log line
        idx, _, b64 = line[1:].partition(":")
        try:
            data = base64.b64decode(b64, validate=True)
        except ValueError:
            continue
        if idx.isdigit() and len(data) == w * 2:
            rows[int(idx)] = data
    missing = [y for y in range(h) if y not in rows]
    if missing:
        print(f"incomplete capture: {len(missing)} rows missing (first: {missing[0]})", file=sys.stderr)
        return False
    write_png(path, w, h, rgb565_to_rgb888(b"".join(rows[y] for y in range(h)), swap))
    print(f"screenshot {path} ({w}×{h})")
    return True


def coredump_raw(s, path):
    """Fetches the coredump image (ELF); decode it with:
    esp-coredump info_corefile -t raw -c <file> .pio/build/<env>/firmware.elf"""
    s.write(b"coredump raw\n")
    s.timeout = 5
    chunks, size = {}, None
    while True:
        line = s.readline().decode(errors="replace").strip()
        if not line:
            sys.exit("coredump interrupted")
        if line.startswith("COREDUMP ERR"):
            sys.exit(line)
        if line.startswith("COREDUMP ") and line != "COREDUMP END":
            size = int(line.split()[1])
        elif line == "COREDUMP END":
            break
        elif line.startswith("C") and ":" in line:
            off, _, b64 = line[1:].partition(":")
            chunks[int(off)] = base64.b64decode(b64)
    data = b"".join(chunks[k] for k in sorted(chunks))
    if size is None or len(data) != size:
        sys.exit(f"incomplete coredump ({len(data)}/{size})")
    open(path, "wb").write(data)
    print(f"coredump {path} ({size} bytes)")


def command(s, cmd, wait=1.0):
    s.write(cmd.encode() + b"\n")
    end = time.time() + wait
    while time.time() < end:
        line = s.readline()
        if line:
            sys.stdout.write(line.decode(errors="replace"))
            end = time.time() + 0.3


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port")
    ap.add_argument("--host", help="workshop mode: TCP console, e.g. kronograf.local")
    ap.add_argument("cmd")
    ap.add_argument("args", nargs="*")
    a = ap.parse_args()
    host = a.host
    if not host and not a.port:
        # Opening the serial port reboots the watch on macOS (and wipes the in-memory battery
        # log): the workshop TCP console is preferred when it answers.
        try:
            socket.create_connection(("kronograf.local", 2323), timeout=1.5).close()
            host = "kronograf.local"
        except OSError:
            pass
    s = open_tcp(host) if host else open_port(a.port or find_port())
    if a.cmd == "coredump-raw":
        coredump_raw(s, a.args[0] if a.args else "coredump.bin")
    elif a.cmd == "screenshot":
        # A log line from another task may land in the middle of a row: try again.
        if not any(screenshot(s, a.args[0] if a.args else "screenshot.png") for _ in range(3)):
            sys.exit("Screenshot failed after 3 attempts")
    else:
        command(s, " ".join([a.cmd] + a.args))


if __name__ == "__main__":
    main()
