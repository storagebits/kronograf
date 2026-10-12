#!/usr/bin/env -S sh -c 'exec "$(dirname "$0")/../.venv/bin/python" "$0" "$@"'
"""Outil Mac pour la console série Kronograf.

  tools/kg.py status                     envoie une commande et affiche la réponse
  tools/kg.py screenshot docs/screens/device/t0-diag.png
  tools/kg.py --port /dev/cu.usbserial-XXXX status

Le port est auto-détecté (premier /dev/cu.usbserial-* ou cu.wchusbserial*).
"""
import argparse
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
        sys.exit("Aucun port série de montre trouvé")
    return ports[0]


def open_port(path):
    # dsrdtr/rtscts désactivés et lignes basses : ouvrir le port ne doit pas redémarrer l'ESP32.
    s = serial.Serial()
    s.port = path
    s.baudrate = 115200
    s.timeout = 2
    s.dtr = False
    s.rts = False
    s.open()
    time.sleep(0.2)
    s.reset_input_buffer()
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
            sys.exit("Pas de réponse KGSHOT")
    parts = line.split()
    if parts[1] == "ERR":
        sys.exit(f"Erreur montre : {line}")
    w, h, swap = int(parts[1]), int(parts[2]), parts[4] == "swap=1"
    rows = []
    s.timeout = 5
    while True:
        row = s.readline().decode(errors="replace").strip()
        if row == "KGSHOT END":
            break
        if not row:
            sys.exit(f"Capture interrompue après {len(rows)} rangées")
        rows.append(base64.b64decode(row))
    if len(rows) != h or any(len(r) != w * 2 for r in rows):
        sys.exit(f"Capture incomplète : {len(rows)}/{h} rangées")
    write_png(path, w, h, rgb565_to_rgb888(b"".join(rows), swap))
    print(f"capture {path} ({w}×{h})")


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
    ap.add_argument("cmd")
    ap.add_argument("args", nargs="*")
    a = ap.parse_args()
    s = open_port(a.port or find_port())
    if a.cmd == "screenshot":
        screenshot(s, a.args[0] if a.args else "screenshot.png")
    else:
        command(s, " ".join([a.cmd] + a.args))


if __name__ == "__main__":
    main()
