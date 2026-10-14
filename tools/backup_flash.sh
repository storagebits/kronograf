#!/bin/bash
# Full backup (16 MB) of the flash in 1 MB blocks, resilient to USB dropouts: waits for
# the port and only re-reads the interrupted block. Read-only: never writes to the watch.
set -u
cd "$(dirname "$0")/.."
OUT=backups/twatch-original-$(date +%Y%m%d).bin
PARTS=backups/parts
BAUD=${BAUD:-460800}
CHUNK=$((0x100000))
mkdir -p "$PARTS"

wait_port() {
  while :; do
    PORT=$(ls /dev/cu.usbserial-* /dev/cu.wchusbserial* 2>/dev/null | head -1)
    [ -n "$PORT" ] && return
    sleep 2
  done
}

for i in $(seq 0 15); do
  part=$(printf "%s/%02d.bin" "$PARTS" "$i")
  if [ -f "$part" ] && [ "$(stat -f %z "$part")" -eq $CHUNK ]; then continue; fi
  for attempt in $(seq 1 20); do
    wait_port
    echo "block $i/15 attempt $attempt on $PORT"
    rm -f "$part"
    if esptool.py --port "$PORT" --baud "$BAUD" read_flash $((i * CHUNK)) $CHUNK "$part" >/dev/null 2>&1 \
       && [ "$(stat -f %z "$part")" -eq $CHUNK ]; then
      break
    fi
    sleep 3
  done
  [ -f "$part" ] && [ "$(stat -f %z "$part")" -eq $CHUNK ] || { echo "FAILED block $i"; exit 1; }
done

cat "$PARTS"/*.bin > "$OUT"
size=$(stat -f %z "$OUT")
[ "$size" -eq $((16 * CHUNK)) ] || { echo "FAILED size $size"; exit 1; }
rm -r "$PARTS"
echo "OK $OUT $size bytes"
shasum -a 256 "$OUT"
