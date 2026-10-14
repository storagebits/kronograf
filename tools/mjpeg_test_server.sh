#!/bin/bash
# Serves an animated MJPEG test pattern on http://<this machine>:8090/stream.mjpg, to try the
# Video app without a camera (simulator or watch on the same network). Needs ffmpeg.
#   tools/mjpeg_test_server.sh [size] [fps]     defaults: 320x240, 15
set -uo pipefail
SIZE=${1:-320x240}
FPS=${2:-15}
echo "MJPEG test pattern: http://$(ipconfig getifaddr en0 2>/dev/null || hostname -I | cut -d' ' -f1):8090/stream.mjpg (Ctrl-C to stop)"
while true; do # ffmpeg's built-in server takes one client at a time: serve the next one
  ffmpeg -loglevel error -re -f lavfi -i "testsrc2=size=$SIZE:rate=$FPS" -f mpjpeg -q:v 7 \
    -listen 1 http://0.0.0.0:8090/stream.mjpg || sleep 1
done
