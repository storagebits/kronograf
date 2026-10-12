#!/usr/bin/env python3
"""Flash de l'application par blocs de 64 Ko, chacun vérifié, repris après chaque décrochage USB.

Pour une liaison USB instable (BLOCKERS.md B2) : chaque session esptool n'écrit qu'un bloc
(~2 s), les blocs vérifiés sont mémorisés et l'on reprend là où l'on s'est arrêté.
otadata (boot_app0) est écrit en dernier : la montre redémarre sur app0.

  ~/.platformio/penv/bin/python tools/flash_chunks.py [.pio/build/twatch-v1/firmware.bin]
"""
import glob
import os
import subprocess
import sys
import tempfile
import time

PY = os.path.expanduser("~/.platformio/penv/bin/python")
ESPTOOL = os.path.expanduser("~/.platformio/packages/tool-esptoolpy/esptool.py")
BOOT = os.path.expanduser("~/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin")
BLOCK = 65536
APP0 = 0x10000


def main():
    fw = sys.argv[1] if len(sys.argv) > 1 else ".pio/build/twatch-v1/firmware.bin"
    data = open(fw, "rb").read()
    work = tempfile.mkdtemp(prefix="kg_flash_")
    todo = []
    for i in range(0, len(data), BLOCK):
        path = os.path.join(work, f"b{i // BLOCK:03d}.bin")
        open(path, "wb").write(data[i:i + BLOCK])
        todo.append((APP0 + i, path))
    todo.append((0xE000, BOOT))

    done, n_try, deadline = set(), 0, time.time() + 3600
    while time.time() < deadline:
        left = [(a, p) for a, p in todo if a not in done]
        if not left:
            print("flash terminé", flush=True)
            return 0
        ports = glob.glob("/dev/cu.usbserial-*") + glob.glob("/dev/cu.SLAB_USBtoUART")
        if not ports:
            time.sleep(0.2)
            continue
        n_try += 1
        if n_try % 2 == 0:
            ports.reverse()  # deux pilotes possibles pour le CP2104 : on alterne
        addr, path = left[0]
        r = subprocess.run([PY, ESPTOOL, "--chip", "esp32", "--port", ports[0], "--baud", "460800",
                            "--after", "no_reset", "write_flash", "-z", "--flash_size", "keep", hex(addr), path],
                           capture_output=True, text=True)
        if "Hash of data verified." in r.stdout:
            done.add(addr)
            print(f"{time.strftime('%T')} {hex(addr)} ok — {len(done)}/{len(todo)}", flush=True)
    print("délai dépassé", flush=True)
    return 1


if __name__ == "__main__":
    sys.exit(main())
