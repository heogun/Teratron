#!/usr/bin/env python3
"""Loopback-only, dependency-free browser GUI; no host FORTH implementation."""
import argparse
import base64
from collections import deque
from http.server import BaseHTTPRequestHandler, HTTPServer
import json
from pathlib import Path
import secrets
from urllib.parse import urlparse
from machine import Machine


class Application:
    def __init__(self, machine, ticks=625000):
        self.machine = machine
        self.running = True
        self.queue = deque()
        self.ticks = ticks
        self.token = secrets.token_hex(24)

    def advance(self):
        if not self.running: return
        remaining = self.ticks
        while remaining:
            if self.queue and not self.machine.lib.ttl_key_pending(self.machine.handle):
                self.machine.lib.ttl_scan(self.machine.handle, self.queue.popleft())
            step = min(5000, remaining)
            self.machine.run(step)
            remaining -= step

    def state(self):
        state = self.machine.state()
        state.update(running=self.running, queued=len(self.queue), text=self.machine.transcript(),
                     diag=self.machine.byte("diag"), saved=self.machine.byte("saved"))
        # Downsample the actual VGA framebuffer, not the RAM text transcript.
        pixels = self.machine.pixels()
        small = bytearray()
        for row in range(0, 480, 4):
            line = pixels[row*640*4:(row+1)*640*4]
            for col in range(0, 640*4, 16): small.extend(line[col:col+4])
        state["pixels"] = base64.b64encode(small).decode("ascii")
        state["memory"] = bytes(self.machine.ram[0:256]).hex()
        return state

    def action(self, request):
        action = request.get("action")
        if action == "run": self.running = True
        elif action == "pause": self.running = False
        elif action == "step":
            self.running = False; self.machine.run(1)
        elif action == "reset":
            self.machine.reboot(); self.queue.clear()
        elif action == "power": self.machine.power_fail()
        elif action == "input":
            text = request.get("text")
            if not isinstance(text, str) or len(text) > 126:
                raise ValueError("Input must be at most 126 characters")
            codes = self.machine.encode_text(text + "\n")
            if len(self.queue) + len(codes) > 4096:
                raise ValueError("Keyboard event queue is full")
            # Host input queue is paced into the ONE-byte hardware receive latch.
            self.queue.extend(codes)
        elif action == "load":
            image = base64.b64decode(request.get("image", ""), validate=True)
            self.machine.load(image); self.queue.clear()
        else: raise ValueError("Unknown action")


def handler_for(app):
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *_): pass

        def valid_host(self):
            port = self.server.server_port
            return self.headers.get("Host") in (f"127.0.0.1:{port}", f"localhost:{port}")

        def reply(self, status, body, content_type="application/json"):
            self.send_response(status)
            self.send_header("Content-Type", content_type)
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.send_header("X-Content-Type-Options", "nosniff")
            self.send_header("Content-Security-Policy", "default-src 'self'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; img-src 'self' data:")
            self.end_headers(); self.wfile.write(body)

        def do_GET(self):
            if not self.valid_host():
                self.reply(403, b'{"error":"Invalid host"}'); return
            path = urlparse(self.path).path
            if path == "/":
                html = Path(__file__).with_name("index.html").read_text()
                self.reply(200, html.replace("SESSION_TOKEN", app.token).encode(), "text/html; charset=utf-8")
            elif path == "/api/state":
                if self.headers.get("X-TTL-Session") != app.token:
                    self.reply(403, b'{"error":"Invalid session"}'); return
                app.advance()
                self.reply(200, json.dumps(app.state()).encode())
            elif path == "/api/flash":
                if self.headers.get("X-TTL-Session") != app.token:
                    self.reply(403, b'{"error":"Invalid session"}'); return
                if app.machine.byte("status") == 4:
                    self.reply(409, b'{"error":"Firmware save in progress"}'); return
                self.reply(200, app.machine.flash(), "application/octet-stream")
            else: self.reply(404, b'{"error":"Not found"}')

        def do_POST(self):
            if not self.valid_host():
                self.reply(403, b'{"error":"Invalid host"}'); return
            if self.path != "/api/action":
                self.reply(404, b'{"error":"Not found"}'); return
            if self.headers.get("X-TTL-Session") != app.token:
                self.reply(403, b'{"error":"Invalid session"}'); return
            try:
                size = int(self.headers.get("Content-Length", "0"))
                if size < 1 or size > 750000: raise ValueError("Invalid request size")
                request = json.loads(self.rfile.read(size))
                if not isinstance(request, dict): raise ValueError("Expected an object")
                app.action(request)
                self.reply(200, b'{"ok":true}')
            except (ValueError, RuntimeError, TypeError) as exc:
                self.reply(400, json.dumps({"error":str(exc)}).encode())
    return Handler


def main():
    parser = argparse.ArgumentParser()
    root = Path(__file__).resolve().parents[1]
    parser.add_argument("--build", type=Path, default=root / "build")
    parser.add_argument("--port", type=int, default=8765)
    args = parser.parse_args()
    libraries = [args.build / name for name in ("libttl_bridge.dylib", "libttl_bridge.so", "ttl_bridge.dll")]
    library = next((path for path in libraries if path.exists()), None)
    if library is None: parser.error("Build ttl_forth_emu first; cannot find ttl_bridge shared library")
    machine = Machine(library, args.build / "firmware")
    server = None
    try:
        server = HTTPServer(("127.0.0.1", args.port), handler_for(Application(machine)))
        print(f"TTL FORTH GUI: http://127.0.0.1:{server.server_port}", flush=True)
        print("Ctrl+C to stop. SAVE runs target firmware; Export only downloads current flash.", flush=True)
        server.serve_forever()
    except KeyboardInterrupt: pass
    finally:
        if server: server.server_close()
        machine.close()


if __name__ == "__main__": main()
