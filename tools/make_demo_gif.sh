#!/bin/bash
# Records the simulator's scripted demo (sim/main.c, DEMO) and turns it into the README GIF.
#   tools/make_demo_gif.sh            needs the simulator built (sim/build) and ffmpeg
set -euo pipefail
cd "$(dirname "$0")/.."
FRAMES=$(mktemp -d)
trap 'rm -rf "$FRAMES"' EXIT
KG_TIME=10:09:30 KG_TIME_RUN=1 KG_FACE=0 KG_NOTIF=3 KG_NOTIF_ALERT=1 KG_NOTIF_AT=15500 \
  sim/build/kronograf_sim --record "$FRAMES"
ffmpeg -loglevel error -y -framerate 20 -i "$FRAMES/frame%04d.png" \
  -vf "scale=480:480:flags=neighbor,split[a][b];[a]palettegen=max_colors=128:stats_mode=diff[p];[b][p]paletteuse=dither=none:diff_mode=rectangle" \
  -loop 0 docs/screens/kronograf-demo.gif
echo "docs/screens/kronograf-demo.gif ($(stat -f %z docs/screens/kronograf-demo.gif 2>/dev/null || stat -c %s docs/screens/kronograf-demo.gif) bytes)"
