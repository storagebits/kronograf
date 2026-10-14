#!/usr/bin/env python3
"""MJPEG test stream for the Video app, without a camera.

  tools/mjpeg_test_server.py [--size 320x240] [--fps 15] [--port 8090]

Renders a looping test pattern once with ffmpeg, then serves it to any number of clients
at http://<this machine>:<port>/stream.mjpg (multipart MJPEG) and /snapshot.jpg (one JPEG).
"""
import argparse
import glob
import http.server
import os
import socket
import subprocess
import tempfile
import time

ap = argparse.ArgumentParser()
ap.add_argument("--size", default="320x240")
ap.add_argument("--fps", type=int, default=15)
ap.add_argument("--port", type=int, default=8090)
a = ap.parse_args()

tmp = tempfile.mkdtemp()
subprocess.run(["ffmpeg", "-loglevel", "error", "-f", "lavfi", "-i", f"testsrc2=size={a.size}:rate={a.fps}",
                "-t", "4", "-q:v", "7", os.path.join(tmp, "f%03d.jpg")], check=True)
FRAMES = [open(p, "rb").read() for p in sorted(glob.glob(os.path.join(tmp, "*.jpg")))]


class Handler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path.startswith("/snapshot"):
            f = FRAMES[int(time.time() * a.fps) % len(FRAMES)]
            self.send_response(200)
            self.send_header("Content-Type", "image/jpeg")
            self.send_header("Content-Length", str(len(f)))
            self.end_headers()
            self.wfile.write(f)
            return
        self.send_response(200)
        self.send_header("Content-Type", "multipart/x-mixed-replace; boundary=frame")
        self.end_headers()
        try:
            i = 0
            while True:
                f = FRAMES[i % len(FRAMES)]
                self.wfile.write(b"--frame\r\nContent-Type: image/jpeg\r\nContent-Length: %d\r\n\r\n" % len(f) + f + b"\r\n")
                i += 1
                time.sleep(1 / a.fps)
        except (BrokenPipeError, ConnectionResetError):
            pass

    def log_message(self, *args):
        pass


ip = socket.gethostbyname(socket.gethostname()) if False else subprocess.run(
    ["ipconfig", "getifaddr", "en0"], capture_output=True, text=True).stdout.strip() or "localhost"
print(f"{len(FRAMES)} frames, {a.size} at {a.fps} fps")
print(f"  stream:   http://{ip}:{a.port}/stream.mjpg")
print(f"  snapshot: http://{ip}:{a.port}/snapshot.jpg   (Ctrl-C to stop)")
http.server.ThreadingHTTPServer(("0.0.0.0", a.port), Handler).serve_forever()
