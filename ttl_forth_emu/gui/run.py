#!/usr/bin/env python3
"""Headless PS/2 console for the same native firmware used by the GUI."""
import argparse
from pathlib import Path
import sys
from machine import Machine


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build", type=Path, default=Path(__file__).resolve().parents[1] / "build")
    parser.add_argument("--eval", action="append", default=[], help="FORTH line; may be repeated")
    parser.add_argument("--load", type=Path, help="Previously saved raw flash image")
    parser.add_argument("--export", type=Path, help="Export current flash, without invoking debugger checkpoint")
    parser.add_argument("--power-fail", action="store_true", help="Signal power failure and wait for native save")
    args = parser.parse_args()
    library = next((args.build / name for name in ("libttl_bridge.dylib", "libttl_bridge.so", "ttl_bridge.dll")
                    if (args.build / name).exists()), None)
    if not library: parser.error("Build the emulator first")
    machine = Machine(library, args.build / "firmware")
    try:
        if args.load: machine.load(args.load.read_bytes())
        machine.ready()
        shown = 0
        def display():
            nonlocal shown
            text = machine.transcript()
            if len(text) < shown: shown = 0
            print(text[shown:], end="", flush=True)
            shown = len(text)
        display()
        if args.eval:
            for line in args.eval:
                machine.command(line, 250000000); display()
                if machine.byte("error"): return 1
        elif not args.power_fail and not args.export:
            for line in sys.stdin:
                machine.command(line.rstrip("\r\n"), 250000000); display()
        if args.power_fail:
            machine.power_fail()
            machine.wait(lambda: machine.byte("saved") == 1, 250000000)
            machine.ready()
        if args.export:
            args.export.write_bytes(machine.flash())
        return 0
    finally:
        machine.close()


if __name__ == "__main__": raise SystemExit(main())
