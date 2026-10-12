#!/bin/bash
# Sauvegarde intégrale (16 Mo) de la flash par blocs de 1 Mo, résistante aux
# déconnexions USB : attend le port, relit seulement le bloc interrompu.
# Lecture seule : n'écrit jamais sur la montre.
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
    echo "bloc $i/15 tentative $attempt sur $PORT"
    rm -f "$part"
    if esptool.py --port "$PORT" --baud "$BAUD" read_flash $((i * CHUNK)) $CHUNK "$part" >/dev/null 2>&1 \
       && [ "$(stat -f %z "$part")" -eq $CHUNK ]; then
      break
    fi
    sleep 3
  done
  [ -f "$part" ] && [ "$(stat -f %z "$part")" -eq $CHUNK ] || { echo "ECHEC bloc $i"; exit 1; }
done

cat "$PARTS"/*.bin > "$OUT"
size=$(stat -f %z "$OUT")
[ "$size" -eq $((16 * CHUNK)) ] || { echo "ECHEC taille $size"; exit 1; }
rm -r "$PARTS"
echo "OK $OUT $size octets"
shasum -a 256 "$OUT"
