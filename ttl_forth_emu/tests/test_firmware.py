#!/usr/bin/env python3
"""Integration tests enter PS/2 input; they never alter firmware RAM or I-RAM."""
import argparse
import base64
from http.server import HTTPServer
import json
from pathlib import Path
import sys
import threading
import unittest
from urllib.error import HTTPError
from urllib.request import Request, urlopen
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "gui"))
from machine import Machine
from server import Application, handler_for

parser = argparse.ArgumentParser()
parser.add_argument("--library", required=True)
parser.add_argument("--firmware", required=True)
ARGS, REMAINING = parser.parse_known_args()


class FirmwareTests(unittest.TestCase):
    def setUp(self):
        self.m = Machine(ARGS.library, ARGS.firmware)
        self.addCleanup(self.m.close)
        self.m.ready()

    def command(self, text):
        self.m.command(text, 250000000)
        self.assertEqual(self.m.byte("error"), 0, (text, self.m.state(), self.m.transcript()))

    def test_diagnostics_video_and_real_ps2(self):
        self.assertEqual(self.m.byte("diag") & 7, 7)
        self.m.run(250000)
        self.assertTrue(self.m.byte("diag") & 16)
        pixels = self.m.pixels()
        self.assertIn(b"\xff\xff\xff\xff", pixels)
        # Pixel generation is done by target drawing + OUT instructions.
        self.command("123\b4 .")
        self.assertIn("124 ", self.m.transcript())
        self.assertTrue(self.m.byte("diag") & 8)

    def test_stack_arithmetic_and_memory(self):
        for text, expected in (
            ("255 1 +", [256]), ("65535 1 +", [0]), ("256 1 -", [255]),
            ("0 1 -", [65535]), ("127 255 +", [382]), ("128 127 -", [1]),
            ("12 12 *", [144]), ("-2 3 *", [65530]),
            ("1 2 SWAP OVER +", [2,3]), ("15 7 AND", [7]),
            ("8 7 OR", [15]), ("15 7 XOR", [8]), ("0 INVERT", [65535]),
            ("123 123 =", [65535]), ("123 124 =", [0]),
            ("0 0=", [65535]), ("1 0=", [0]),
            ("127 128 U<", [65535]), ("65535 0 U<", [0]),
            ("12345 24576 ! 24576 @", [12345]),
            ("90 24578 C! 24578 C@", [90]), ("DEPTH", [0]),
        ):
            with self.subTest(text=text):
                self.command(text)
                self.assertEqual(self.m.stack(), expected)
                self.command(" ".join("DROP" for _ in expected))
        self.command("-32768 . 32767 .")
        self.assertIn("-32768 32767 ", self.m.transcript())

    def test_runtime_compile_nested_control_recursion(self):
        self.command(": SQUARE DUP * ;")
        self.command(": FOURTH SQUARE SQUARE ;")
        self.command("3 FOURTH")
        self.assertEqual(self.m.stack(), [81]); self.command("DROP")
        self.command(": CHOOSE IF 111 ELSE 222 THEN ;")
        self.command("0 CHOOSE 1 CHOOSE")
        self.assertEqual(self.m.stack(), [222,111]); self.command("DROP DROP")
        self.command(": COUNT BEGIN 1 - DUP 0= UNTIL ;")
        self.command("5 COUNT")
        self.assertEqual(self.m.stack(), [0]); self.command("DROP")
        self.command(": FACT DUP 1 U< IF DROP 1 ELSE DUP 1 - RECURSE * THEN ;")
        self.command("5 FACT")
        self.assertEqual(self.m.stack(), [120]); self.command("DROP")
        self.command("WORDS")
        self.assertIn("FACT COUNT CHOOSE FOURTH SQUARE", self.m.transcript())
        # Definitions can span lines; uncommitted words are not published.
        self.command(": MULTILINE")
        self.assertEqual(self.m.byte("mode"), 2)
        self.command("40 2 + ;")
        self.command("MULTILINE")
        self.assertEqual(self.m.stack(), [42])

    def test_errors_abort_and_recovery(self):
        original = self.m.cell("head")
        for line in ("DROP", "UNKNOWN", ";", ": BAD IF ;", ": BAD BEGIN THEN ;"):
            with self.subTest(line=line):
                self.m.command(line)
                self.assertNotEqual(self.m.byte("error"), 0)
                self.assertEqual(self.m.byte("mode"), 0)
                self.assertEqual(self.m.cell("head"), original)
                self.assertEqual(self.m.stack(), [])
                self.command("1 2 + DROP")
        self.command(": GOOD 42 ;")
        self.command("GOOD")
        self.assertEqual(self.m.stack(), [42])

    def test_native_save_and_unsaved_power_cycle(self):
        self.command(": SQUARE DUP * ;")
        self.command(": FOURTH SQUARE SQUARE ;")
        # Write the last I-RAM byte through the firmware-visible bus ports. This
        # also verifies access to locations shadowed by legacy MMIO windows.
        self.command("90 65535 7 IC!")
        self.command("65535 7 IC@")
        self.assertEqual(self.m.stack(), [90]); self.command("DROP")
        before = self.m.lib.ttl_counter(self.m.handle)
        self.command("SAVE")
        self.assertGreater(self.m.lib.ttl_counter(self.m.handle) - before, 1000000)
        saved = self.m.flash()
        self.assertEqual(saved[-1], 90)
        self.command("12345 24576 !")
        self.m.reboot(); self.m.ready()
        self.assertEqual(self.m.ram[0x6000], 0)
        self.command("3 FOURTH")
        self.assertEqual(self.m.stack(), [81]); self.command("DROP")
        # No implicit save on a power cycle: preserve committed words, lose new ones.
        self.command(": VOLATILE 77 ;")
        self.assertEqual(saved, self.m.flash())
        self.m.reboot(); self.m.ready()
        self.m.command("VOLATILE")
        self.assertEqual(self.m.byte("error"), 5)
        self.assertEqual(saved, self.m.flash())
        self.command("3 FOURTH")
        self.assertEqual(self.m.stack(), [81])

    def test_unsaved_boots_from_original_flash(self):
        original = self.m.flash()
        self.command(": TEMP 77 ;")
        self.command("TEMP 12345 24576 !")
        self.assertEqual(self.m.stack(), [77])
        self.assertEqual(original, self.m.flash())
        self.m.reboot(); self.m.ready()
        self.assertEqual(original, self.m.flash())
        self.assertEqual(self.m.stack(), [])
        self.assertEqual(self.m.ram[0x6000], 0)
        self.m.command("TEMP")
        self.assertEqual(self.m.byte("error"), 5)

    def test_both_shift_keys(self):
        self.m.scan(0x12); self.m.scan(0x59)  # Both Shift keys held.
        self.m.scan(0xf0); self.m.scan(0x12)  # Release left, keep right held.
        self.m.scan(0x16)                    # Shift+1 -> !
        self.m.wait(lambda: self.m.byte("inlen") == 1)
        self.assertEqual(self.m.ram[0x500], ord("!"))
        self.m.scan(0xf0); self.m.scan(0x59)  # Release right.
        self.m.scan(0x16)
        self.m.wait(lambda: self.m.byte("inlen") == 2)
        self.assertEqual(self.m.ram[0x501], ord("1"))

    def test_gui_http_controls_and_ps2_pacing(self):
        app = Application(self.m, ticks=250000)
        server = HTTPServer(("127.0.0.1", 0), handler_for(app))
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            base = f"http://127.0.0.1:{server.server_port}"
            def get(path, token=True):
                headers = {"X-TTL-Session":app.token} if token else {}
                with urlopen(Request(base+path, headers=headers), timeout=10) as r:
                    return r.read()
            def post(action, **kw):
                data = json.dumps(dict(action=action, **kw)).encode()
                req = Request(base+"/api/action", data=data,
                              headers={"X-TTL-Session":app.token,"Content-Type":"application/json"})
                with urlopen(req, timeout=10) as r: return json.loads(r.read())
            self.assertIn(b"<canvas", get("/"))
            with self.assertRaises(HTTPError) as rejected: get("/api/state", token=False)
            self.assertEqual(rejected.exception.code, 403)
            post("pause")
            a = json.loads(get("/api/state")); post("step"); b = json.loads(get("/api/state"))
            self.assertEqual(b["cycles"], a["cycles"]+1)
            self.assertEqual(len(base64.b64decode(b["pixels"])), 160*120*4)
            post("input", text="123 456 + ."); post("run")
            for _ in range(60):
                state = json.loads(get("/api/state"))
                if "579 " in state["text"]: break
            self.assertIn("579 ", state["text"])
            self.assertEqual(self.m.byte("error"), 0)
            self.assertEqual(len(get("/api/flash")), 524288)
            post("reset")
            self.m.ready()
            self.assertEqual(self.m.stack(), [])
        finally:
            server.shutdown(); thread.join(); server.server_close()


if __name__ == "__main__": unittest.main(argv=[sys.argv[0]] + REMAINING)
