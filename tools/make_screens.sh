#!/bin/bash
# Renders the simulator screenshots of the newer screens into docs/screens/sim, then the
# README overview sheet (4 × 4). Demo data only: documentation addresses, fixed time.
#   tools/make_screens.sh            (after building the simulator, see README)
set -euo pipefail
cd "$(dirname "$0")/.."
SIM=sim/build/kronograf_sim
OUT=docs/screens/sim
export KG_TIME=10:09:30
shot() { # name VAR=value…
  local name=$1; shift
  env "$@" "$SIM" --shot "$OUT/$name.png" >/dev/null
}
shot t4-launcher KG_APP=launcher
cp "$OUT/t4-launcher.png" "$OUT/t3-launcher.png"
shot t6-launcher-tools KG_APP=launcher KG_PAGE=1
shot t6-remote KG_APP=remote
shot t8-net-menu KG_APP=net
shot t8-net-wifi KG_APP=net:wifi KG_SETTLE_MS=900
shot t8-net-link KG_APP=net:link
shot t8-net-ping KG_APP=net:ping
shot t8-net-trace KG_APP=net:trace
shot t8-net-dns KG_APP=net:dns
shot t8-net-ports KG_APP=net:ports
shot t8-net-lan KG_APP=net:lan
shot t8-net-speed KG_APP=net:speed
shot t8-net-ble KG_APP=net:ble
shot t8-net-subnet KG_APP=net:cidr
shot t8-world KG_APP=world
shot t8-sky KG_APP=sky
shot t8-level KG_APP=level
shot t8-torch KG_APP=torch
shot t8-system KG_APP=system

python3 tools/contact_sheet.py docs/screens/kronograf-overview.png --cols 4 \
  $OUT/t5-aviator-badge.png $OUT/t2-face1.png $OUT/t2-face2.png $OUT/t4-launcher.png \
  $OUT/t4-weather.png $OUT/t5-card.png $OUT/t3-timer.png $OUT/t3-alarms.png \
  $OUT/t8-net-wifi.png $OUT/t8-net-ping.png $OUT/t8-net-trace.png $OUT/t8-net-speed.png \
  $OUT/t8-world.png $OUT/t8-sky.png $OUT/t6-remote.png $OUT/t8-level.png
