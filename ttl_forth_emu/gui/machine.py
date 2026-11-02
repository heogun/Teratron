"""ctypes bridge. All FORTH execution remains in the emulated native CPU."""
import ctypes as C
import json
from pathlib import Path


class Machine:
    def __init__(self, library, firmware):
        self.lib = C.CDLL(str(Path(library).resolve()))
        signatures = {
            "ttl_create": (C.c_void_p, []),
            "ttl_destroy": (None, [C.c_void_p]),
            "ttl_load_image": (C.c_int, [C.c_void_p, C.c_void_p, C.c_size_t]),
            "ttl_advance": (None, [C.c_void_p, C.c_uint64]),
            "ttl_scan": (None, [C.c_void_p, C.c_uint8]),
            "ttl_reboot": (C.c_int, [C.c_void_p]),
            "ttl_signal_power_fail": (None, [C.c_void_p]),
            "ttl_data": (C.POINTER(C.c_uint8), [C.c_void_p]),
            "ttl_image": (C.POINTER(C.c_uint8), [C.c_void_p]),
            "ttl_pixels": (C.POINTER(C.c_uint8), [C.c_void_p]),
            "ttl_counter": (C.c_uint64, [C.c_void_p]),
            "ttl_program_counter": (C.c_uint16, [C.c_void_p]),
            "ttl_accumulator": (C.c_uint8, [C.c_void_p]),
            "ttl_key_pending": (C.c_int, [C.c_void_p]),
        }
        for name, (restype, args) in signatures.items():
            function = getattr(self.lib, name)
            function.restype, function.argtypes = restype, args
        self.handle = self.lib.ttl_create()
        if not self.handle:
            raise MemoryError("Cannot allocate machine")
        self.abi = json.loads((Path(firmware) / "forth.json").read_text())
        self.image = (Path(firmware) / "forth.flash").read_bytes()
        self.load(self.image)

    def close(self):
        if self.handle:
            self.lib.ttl_destroy(self.handle)
            self.handle = None

    def load(self, image):
        if len(image) != 524288:
            raise ValueError("Flash image must be exactly 512 KiB")
        raw = C.create_string_buffer(image)
        if not self.lib.ttl_load_image(self.handle, raw, len(image)):
            raise RuntimeError("Flash is busy")
        self.run(524288)

    @property
    def ram(self):
        return self.lib.ttl_data(self.handle)

    def byte(self, name): return self.ram[self.abi[name]]
    def cell(self, name):
        address = self.abi[name]
        return self.ram[address] | self.ram[address + 1] << 8

    def run(self, cycles): self.lib.ttl_advance(self.handle, cycles)

    def wait(self, predicate, budget=30000000):
        for _ in range((budget + 999) // 1000):
            if predicate(): return
            self.run(1000)
        raise TimeoutError(f"Firmware timeout: {self.state()}")

    def ready(self):
        self.wait(lambda: self.byte("status") == 2)

    def scan(self, code):
        self.wait(lambda: not self.lib.ttl_key_pending(self.handle))
        self.lib.ttl_scan(self.handle, code)
        # Run until the firmware has consumed AND handled this frame. The next
        # scan may not arrive before the single receive latch is free.
        self.run(1000)
        self.wait(lambda: not self.lib.ttl_key_pending(self.handle))

    def character(self, char):
        for scan in self.encode_text(char): self.scan(scan)

    def encode_text(self, text):
        result = []
        for char in text:
            result.extend(self.encode_character(char))
        return result

    def encode_character(self, char):
        char = char.upper()
        reverse = {v: k for k, v in self.abi["shifted"].items()}
        shifted = char in reverse
        key = reverse.get(char, char)
        if key not in self.abi["keys"]:
            raise ValueError(f"Unsupported PS/2 character: {char!r}")
        result = [0x12] if shifted else []
        result.append(self.abi["keys"][key])
        if shifted: result.extend((0xf0, 0x12))
        return result

    def command(self, text, budget=120000000):
        if len(text) > 126: raise ValueError("Input exceeds the firmware's 126-character line buffer")
        codes = self.encode_text(text + "\n")
        self.ready()
        for code in codes: self.scan(code)
        self.wait(lambda: self.byte("status") == 2 and self.byte("inlen") == 0, budget)

    def transcript(self):
        # Last turn of the target's 4 KiB diagnostic transcript ring.
        pointer = self.cell("outptr")
        return bytes(self.ram[0x4000:pointer]).decode("ascii", errors="replace")

    def stack(self):
        return [self.ram[0x200+i] | self.ram[0x201+i] << 8 for i in range(0, self.byte("sp"), 2)]

    def flash(self): return C.string_at(self.lib.ttl_image(self.handle), 524288)
    def pixels(self): return C.string_at(self.lib.ttl_pixels(self.handle), 640*480*4)
    def power_fail(self): self.lib.ttl_signal_power_fail(self.handle)
    def reboot(self):
        if not self.lib.ttl_reboot(self.handle): raise RuntimeError("Flash is busy")
        self.run(524288)

    def state(self):
        return {"status":self.byte("status"), "error":self.byte("error"),
                "pc":self.lib.ttl_program_counter(self.handle),
                "ac":self.lib.ttl_accumulator(self.handle), "cycles":self.lib.ttl_counter(self.handle),
                "mode":self.byte("mode"), "stack":self.stack()}
