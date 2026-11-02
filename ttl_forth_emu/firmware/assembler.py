"""Small two-pass assembler for the existing Gigatron native ISA (no VM opcodes)."""
from dataclasses import dataclass


@dataclass
class Ref:
    label: str
    high: bool = False


class Assembler:
    def __init__(self):
        self.words = []
        self.labels = {}
        self.variables = {}
        self.serial = 0

    @property
    def pc(self):
        return len(self.words)

    def var(self, name, size=1):
        if name not in self.variables:
            address = 0x20 + sum(n for _, n in self.variables.values())
            if address + size > 0x100:
                raise ValueError("zero-page exhausted")
            self.variables[name] = (address, size)
        return self.variables[name][0]

    def unique(self, prefix="L"):
        self.serial += 1
        return f"{prefix}_{self.serial}"

    def label(self, name):
        if name in self.labels:
            raise ValueError(f"duplicate label: {name}")
        self.labels[name] = self.pc

    def ins(self, opcode, operand=0):
        self.words.append((opcode, operand))

    def nop(self): self.ins(0x02)
    def imm(self, value): self.ins(0x00, value)
    def ld(self, address): self.ins(0x01, address)
    def st(self, address): self.ins(0xC2, address)
    def x(self, value): self.ins(0x10, value)
    def y(self, value): self.ins(0x14, value)
    def lx(self, address): self.ins(0x11, address)
    def ly(self, address): self.ins(0x15, address)
    def add(self, value): self.ins(0x80, value)
    def addm(self, address): self.ins(0x81, address)
    def sub(self, value): self.ins(0xA0, value)
    def subm(self, address): self.ins(0xA1, address)
    def and_(self, value): self.ins(0x20, value)
    def orm(self, address): self.ins(0x41, address)
    def indirect(self): self.ins(0x0D)
    def store_indirect(self): self.ins(0xCE)

    def put(self, address, value):
        self.imm(value); self.st(address)

    def copy(self, src, dst):
        self.ld(src); self.st(dst)

    def set16(self, address, value):
        self.put(address, value & 255); self.put(address + 1, value >> 8)

    def copy16(self, src, dst):
        self.copy(src, dst); self.copy(src + 1, dst + 1)

    def jump(self, label):
        self.y(Ref(label, True)); self.ins(0xE0, Ref(label)); self.nop()

    def branch(self, condition, label):
        """Far conditional branch. Preserve AC, X; Y is scratch."""
        # The short inverted branch and its target must stay in one PC page.
        while (self.pc & 255) > 248:
            self.nop()
        skip = self.unique("skip")
        inverse = {"eq": 3, "ne": 4, "lt": 5, "ge": 2, "gt": 6, "le": 1}[condition]
        self.ins(0xE0 | (inverse << 2), Ref(skip)); self.nop()
        self.jump(label)
        self.label(skip)

    def call(self, name):
        ret = self.var(f"ret_{name}", 2)
        accumulator = self.var("call_ac")
        after = self.unique("return")
        self.st(accumulator)
        self.imm(Ref(after)); self.st(ret)
        self.imm(Ref(after, True)); self.st(ret + 1)
        self.ld(accumulator)
        self.jump(name)
        self.label(after)

    def ret(self, name):
        address = self.var(f"ret_{name}", 2)
        self.ly(address + 1); self.ins(0xE1, address); self.nop()

    def inc16(self, address):
        done = self.unique()
        self.ld(address); self.add(1); self.st(address); self.branch("ne", done)
        self.ld(address + 1); self.add(1); self.st(address + 1)
        self.label(done)

    def resolve(self):
        if self.pc > 0x8000:
            raise ValueError("firmware overlaps table byte bank")
        result = bytearray()
        for opcode, operand in self.words:
            if isinstance(operand, Ref):
                value = self.labels[operand.label]
                operand = (value >> 8) if operand.high else (value & 255)
            if not 0 <= operand <= 255:
                raise ValueError(f"operand out of range: {operand}")
            result.extend((opcode, operand))
        return result
