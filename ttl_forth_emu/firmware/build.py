#!/usr/bin/env python3
"""Build a real native-CPU firmware image and a token-threaded FORTH dictionary.

Python only assembles the image. Parsing, compilation, stack operations, drawing,
PS/2 decoding and flash persistence all run as Gigatron native instructions.
"""
import argparse
import json
from pathlib import Path
from assembler import Assembler

SIZE = 524288
DICT_BANK = 2
DICT_START = 0x100
DICT_LIMIT = 0xFD00
TOKENS = ["EXIT", "LIT", "CALL", "DUP", "DROP", "SWAP", "OVER", "+", "-", "*",
          "AND", "OR", "XOR", "INVERT", "=", "0=", "U<", ".", "EMIT", "CR",
          "DEPTH", "C@", "C!", "@", "!", ":", ";", "WORDS", "SAVE", "EXECUTE",
          "BRANCH", "0BRANCH", "IF", "ELSE", "THEN", "BEGIN", "UNTIL", "AGAIN", "RECURSE",
          "IC@", "IC!", "FC@", "FC!"]
TOKEN = {name: i + 1 for i, name in enumerate(TOKENS)}
INTERNAL = {"EXIT", "LIT", "CALL", "BRANCH", "0BRANCH"}
IMMEDIATE = {":", ";", "IF", "ELSE", "THEN", "BEGIN", "UNTIL", "AGAIN", "RECURSE"}

# Raw set-2 scan codes. ASCII conversion is performed by target firmware.
KEYS = dict(zip("ABCDEFGHIJKLMNOPQRSTUVWXYZ", [0x1c,0x32,0x21,0x23,0x24,0x2b,0x34,0x33,
    0x43,0x3b,0x42,0x4b,0x3a,0x31,0x44,0x4d,0x15,0x2d,0x1b,0x2c,0x3c,0x2a,0x1d,0x22,0x35,0x1a]))
KEYS.update(dict(zip("1234567890", [0x16,0x1e,0x26,0x25,0x2e,0x36,0x3d,0x3e,0x46,0x45])))
KEYS.update({" ":0x29,"\n":0x5a,"\b":0x66,"-":0x4e,"=":0x55,";":0x4c,
             "'":0x52,",":0x41,".":0x49,"/":0x4a,"[":0x54,"]":0x5b,"\\":0x5d,"`":0x0e})
SHIFTED = dict(zip("1234567890-=;',./[]\\`", '!@#$%^&*()_+:"<>?{}|~'))

# Compact, original 3x5 glyph patterns; each character occupies a 5x8 cell.
GLYPHS = {
    "A":"25755", "B":"65656", "C":"34443", "D":"65556", "E":"74647",
    "F":"74644", "G":"34553", "H":"55755", "I":"72227", "J":"11152",
    "K":"55655", "L":"44447", "M":"57755", "N":"57555", "O":"25552",
    "P":"65644", "Q":"25573", "R":"65655", "S":"34216", "T":"72222",
    "U":"55557", "V":"55552", "W":"55775", "X":"55255", "Y":"55222",
    "Z":"71247", "0":"75557", "1":"26227", "2":"61247", "3":"61216",
    "4":"55711", "5":"74616", "6":"34652", "7":"71222", "8":"25252",
    "9":"25316", "+":"02720", "-":"00700", "*":"05250", "/":"11244",
    ":":"02020", ";":"02024", ".":"00002", ",":"00024", "!":"22202",
    "?":"61202", "=":"07070", "<":"12421", ">":"42124", "@":"35743",
    "(":"12221", ")":"42224", "[":"32223", "]":"62226", "_":"00007",
    "'":"22000", '"':"55000", "#":"57575", "$":"37362", "%":"51245",
    "&":"25653", "|":"22222", "\\":"44211", "^":"25000", "~":"05200",
}


def build(output):
    a = Assembler()
    # Stable RAM ABI for debug tools. No host service reads/writes execution state.
    names = ["status", "error", "mode", "sp", "rp", "cx", "cy", "outptr:2", "inlen",
             "parse", "tokstart", "toklen", "saved", "diag", "shift", "released", "extended",
             "head:2", "here:2", "pending:2", "oldhere:2", "ip:2", "found:2", "flags",
             "p:2", "bank", "target", "w", "ch", "v:2", "b:2", "tmp:2", "work:2",
             "entry:2", "link:2", "index", "count", "digit", "negative", "started",
             "glyph", "glyphbase:2", "px", "py", "pixel", "vrow", "vcount",
             "saveptr:2", "savebank", "control", "ctrl:2", "ctype", "definition:2"]
    v = {}
    for item in names:
        name, _, size = item.partition(":")
        v[name] = a.var(name, int(size or 1))
    def ld(n): a.ld(v[n])
    def st(n): a.st(v[n])
    def put(n, value): a.put(v[n], value)
    def put16(n, value): a.set16(v[n], value)
    def cp(src, dst): a.copy(v[src], v[dst])
    def cp16(src, dst): a.copy16(v[src], v[dst])
    def inc(n): a.ld(v[n]); a.add(1); a.st(v[n])
    def dec(n): a.ld(v[n]); a.sub(1); a.st(v[n])
    def mmwrite(reg, value): a.y(255); a.imm(value); a.ins(0xca, reg)
    def mmvar(reg, name): a.y(255); ld(name); a.ins(0xca, reg)
    def mmread(reg): a.y(255); a.ins(0x09, reg)
    def say(text):
        for char in text:
            put("ch", ord(char)); a.call("emit")
    def ptr(n): cp16(n, "p")
    def read_dict(n):
        ptr(n); put("bank", DICT_BANK); put("target", 1); a.call("memread")
    def write_dict():
        cp16("here", "p"); put("bank", DICT_BANK); put("target", 1)
        a.call("memwrite"); a.inc16(v["here"])
    def compile_byte(value): put("w", value); write_dict()
    def emit_v(): cp("v", "ch"); a.call("emit")
    def error(code): put("error", code); a.jump("abort")
    def token_label(name): return "prim_" + str(TOKEN[name])

    # Dictionary headers and code are permanently stored in I-RAM, not data RAM.
    dictionary = bytearray([0xff] * 65536)
    here, head = DICT_START, 0
    for name in TOKENS:
        if name in INTERNAL: continue
        entry = here
        raw = name.encode("ascii")
        data = bytes((head & 255, head >> 8, len(raw) | (0x80 if name in IMMEDIATE else 0)))
        data += raw + bytes((TOKEN[name], TOKEN["EXIT"]))
        dictionary[here:here+len(data)] = data
        here += len(data); head = entry
    dictionary[:6] = bytes((head & 255, head >> 8, here & 255, here >> 8, 0x54, 0x46))

    a.label("boot")
    put("status", 1); put("cy", 0x80); put16("outptr", 0x4000)
    a.call("clear_screen")
    # Non-destructive RAM/I-RAM tests and actual flash software-ID commands.
    a.y(0x7f); a.imm(0xa5); a.ins(0xca, 0xfe); a.ins(0x09, 0xfe)
    a.sub(0xa5); a.branch("ne", "fatal"); put("diag", 1)
    put16("p", 0x100); put("bank", 3); put("target", 1); put("w", 0x5a)
    a.call("memwrite"); a.call("memread"); a.sub(0x5a); a.branch("ne", "fatal")
    put("w", 0xff); a.call("memwrite"); put("diag", 3)
    put("savebank", 0); a.imm(0x90); a.call("flash_command")
    put16("p", 0); put("bank", 0); put("target", 2); a.call("memread")
    a.sub(0xbf); a.branch("ne", "fatal")
    put16("p", 1); a.call("memread"); a.sub(0xb7); a.branch("ne", "fatal")
    put16("p", 0); put("w", 0xf0); a.call("memwrite"); put("diag", 7)
    # Reload the persistent dictionary roots from I-RAM on every power-on.
    for i, name in enumerate(("head", "here")):
        for j in range(2):
            put16("p", i * 2 + j); put("bank", DICT_BANK); put("target", 1)
            a.call("memread"); a.st(v[name] + j)
    say("TTL FORTH\nOK\n")
    a.jump("idle")
    a.label("fatal"); put("error", 0xff); put("status", 0xff); a.jump("fatal_halt")
    a.label("fatal_halt"); a.jump("fatal_halt")

    # Stage 1/2: real OUT-driven video, polled PS/2 and text rendering.
    a.label("idle"); put("status", 2)
    a.call("service_power")
    mmread(4); a.and_(1); a.branch("ne", "key")
    a.call("video"); a.jump("idle")
    a.label("key")
    mmread(3); st("digit"); ld("diag"); a.ins(0x40, 8); st("diag")
    ld("digit"); a.sub(0xf0); a.branch("ne", "not_break")
    put("released", 1); a.jump("idle")
    a.label("not_break")
    ld("digit"); a.sub(0xe0); a.branch("ne", "not_extended")
    put("extended", 1); a.jump("idle")
    a.label("not_extended")
    ld("digit"); a.sub(0x12); a.branch("eq", "shift_key")
    ld("digit"); a.sub(0x59); a.branch("eq", "shift_key")
    ld("released"); a.orm(v["extended"]); a.branch("eq", "normal_key")
    put("released", 0); put("extended", 0); a.jump("idle")
    a.label("shift_key")
    ld("released"); a.ins(0x60, 1); st("shift"); put("released", 0); a.jump("idle")
    a.label("normal_key")
    cp("digit", "p"); ld("shift"); a.add(0x90); a.st(v["p"]+1)
    put("bank", 1); put("target", 1); a.call("memread"); st("ch")
    a.branch("eq", "idle")
    ld("ch"); a.sub(10); a.branch("eq", "enter")
    ld("ch"); a.sub(8); a.branch("eq", "backspace")
    ld("inlen"); a.sub(126); a.branch("eq", "idle")
    a.lx(v["inlen"]); a.y(5); ld("ch"); a.store_indirect()
    inc("inlen"); a.call("emit"); a.jump("idle")
    a.label("backspace")
    ld("inlen"); a.branch("eq", "idle"); dec("inlen")
    ld("cx"); a.branch("eq", "idle"); a.sub(5); st("cx")
    put("ch", 32); a.call("draw"); a.jump("idle")
    a.label("enter")
    a.call("emit"); put("parse", 0); put("status", 3); put("error", 0)
    a.jump("interpret")

    # Stage 3: the input parser and compiler execute entirely on the native CPU.
    a.label("interpret")
    ld("parse"); a.subm(v["inlen"]); a.branch("eq", "line_done")
    a.lx(v["parse"]); a.y(5); a.indirect(); a.sub(32)
    a.branch("ne", "token_start"); inc("parse"); a.jump("interpret")
    a.label("token_start"); cp("parse", "tokstart"); put("toklen", 0)
    a.label("token_scan")
    ld("parse"); a.subm(v["inlen"]); a.branch("eq", "token_ready")
    a.lx(v["parse"]); a.y(5); a.indirect(); a.sub(32); a.branch("eq", "token_ready")
    inc("parse"); inc("toklen"); ld("toklen"); a.sub(16); a.branch("eq", "long_token")
    a.jump("token_scan")
    a.label("long_token"); error(1)
    a.label("token_ready")
    ld("mode"); a.sub(1); a.branch("eq", "new_definition")
    a.call("lookup"); ld("found"); a.orm(v["found"]+1); a.branch("eq", "number")
    ld("mode"); a.sub(2); a.branch("ne", "execute_found")
    ld("flags"); a.and_(0x80); a.branch("ne", "execute_found")
    a.call("check_room"); compile_byte(TOKEN["CALL"])
    cp("found", "w"); write_dict(); a.copy(v["found"]+1, v["w"]); write_dict()
    a.jump("interpret")
    a.label("execute_found")
    cp16("found", "ip"); put("rp", 0); put16("v", 0); a.call("rpush")
    a.jump("next")
    a.label("number")
    a.call("parse_number")
    ld("mode"); a.sub(2); a.branch("eq", "compile_number")
    a.call("push"); a.jump("interpret")
    a.label("compile_number")
    a.call("check_room"); compile_byte(TOKEN["LIT"])
    cp("v", "w"); write_dict(); a.copy(v["v"]+1, v["w"]); write_dict()
    a.jump("interpret")
    a.label("new_definition")
    cp16("here", "oldhere"); cp16("here", "pending"); a.call("check_room")
    cp("head", "w"); write_dict(); a.copy(v["head"]+1, v["w"]); write_dict()
    cp("toklen", "w"); write_dict(); put("index", 0)
    a.label("copy_name")
    ld("tokstart"); a.addm(v["index"]); a.st(v["tmp"]); a.lx(v["tmp"]); a.y(5)
    a.indirect(); st("w"); write_dict(); inc("index")
    ld("index"); a.subm(v["toklen"]); a.branch("ne", "copy_name")
    cp16("here", "definition"); put("mode", 2); a.jump("interpret")
    a.label("line_done")
    put("inlen", 0)
    ld("mode"); a.branch("ne", "idle")
    say(" OK\n"); a.jump("idle")
    a.label("abort")
    ld("mode"); a.branch("eq", "abort_not_compiling")
    cp16("oldhere", "here")
    a.label("abort_not_compiling")
    put("mode", 0); put("sp", 0); put("rp", 0); put("control", 0); put("inlen", 0)
    say("?\n"); a.jump("idle")

    # Threaded code lives in I-RAM byte bank 2. CALL/EXIT use a target return stack.
    a.label("next")
    a.call("service_power")
    a.call("fetch"); st("digit")
    for name in TOKENS:
        ld("digit"); a.sub(TOKEN[name]); a.branch("eq", token_label(name))
    error(2)
    a.label(token_label("EXIT"))
    a.call("rpop"); cp16("v", "ip"); ld("ip"); a.orm(v["ip"]+1)
    a.branch("eq", "interpret"); a.jump("next")
    a.label(token_label("LIT"))
    a.call("fetch"); st("v"); a.call("fetch"); a.st(v["v"]+1)
    a.call("push"); a.jump("next")
    a.label(token_label("CALL"))
    a.call("fetch"); st("found"); a.call("fetch"); a.st(v["found"]+1)
    cp16("ip", "v"); a.call("rpush"); cp16("found", "ip"); a.jump("next")
    a.label(token_label("DUP")); a.call("pop"); a.call("push"); a.call("push"); a.jump("next")
    a.label(token_label("DROP")); a.call("pop"); a.jump("next")
    a.label(token_label("SWAP"))
    a.call("pop"); cp16("v", "b"); a.call("pop"); cp16("v", "work")
    cp16("b", "v"); a.call("push"); cp16("work", "v"); a.call("push"); a.jump("next")
    a.label(token_label("OVER"))
    a.call("pop"); cp16("v", "b"); a.call("pop"); cp16("v", "work"); a.call("push")
    cp16("b", "v"); a.call("push"); cp16("work", "v"); a.call("push"); a.jump("next")
    for name in ("+", "-", "AND", "OR", "XOR", "=", "U<", "*"):
        a.label(token_label(name)); a.call("pop"); cp16("v", "b"); a.call("pop")
        if name == "+": a.call("add16")
        elif name == "-": a.call("sub16")
        elif name in ("AND", "OR", "XOR"):
            opcode = {"AND":0x21, "OR":0x41, "XOR":0x61}[name]
            for offset in range(2):
                a.ld(v["v"]+offset); a.ins(opcode, v["b"]+offset); a.st(v["v"]+offset)
        elif name == "=":
            a.call("sub16"); ld("v"); a.orm(v["v"]+1); a.call("bool_zero")
        elif name == "U<":
            a.call("unsigned_less"); a.call("bool_nonzero")
        else:
            cp16("v", "work"); put16("v", 0)
            loop = a.unique(); done = a.unique(); a.label(loop)
            ld("b"); a.orm(v["b"]+1); a.branch("eq", done)
            # Save count in tmp; add16 consumes b, so temporarily replace it.
            cp16("b", "tmp"); cp16("work", "b"); a.call("add16"); cp16("tmp", "b")
            ld("b"); a.branch("ne", loop+"_low")
            a.ld(v["b"]+1); a.sub(1); a.st(v["b"]+1)
            a.label(loop+"_low"); ld("b"); a.sub(1); st("b"); a.jump(loop); a.label(done)
        a.call("push"); a.jump("next")
    a.label(token_label("INVERT")); a.call("pop")
    for offset in range(2): a.ld(v["v"]+offset); a.ins(0x60, 255); a.st(v["v"]+offset)
    a.call("push"); a.jump("next")
    a.label(token_label("0=")); a.call("pop"); ld("v"); a.orm(v["v"]+1)
    a.call("bool_zero"); a.call("push"); a.jump("next")
    a.label(token_label("DEPTH")); ld("sp"); st("count"); put16("v", 0)
    a.label("depth_loop"); ld("count"); a.branch("eq", "depth_done")
    a.sub(2); st("count"); inc("v"); a.jump("depth_loop")
    a.label("depth_done"); a.call("push"); a.jump("next")
    a.label(token_label("EMIT")); a.call("pop"); emit_v(); a.jump("next")
    a.label(token_label("CR")); put("ch", 10); a.call("emit"); a.jump("next")
    a.label(token_label(".")); a.call("pop"); a.call("print_number"); a.jump("next")
    for name in ("C@", "@", "C!", "!"):
        a.label(token_label(name)); a.call("pop"); cp16("v", "tmp")
        if name.endswith("!"): a.call("pop")
        a.lx(v["tmp"]); a.ly(v["tmp"]+1)
        if name.endswith("@"): a.indirect(); st("v"); a.put(v["v"]+1, 0)
        else: ld("v"); a.store_indirect()
        if len(name) == 1:
            a.inc16(v["tmp"]); a.lx(v["tmp"]); a.ly(v["tmp"]+1)
            if name == "@": a.indirect(); a.st(v["v"]+1)
            else: a.ld(v["v"]+1); a.store_indirect()
        if name.endswith("@"): a.call("push")
        a.jump("next")
    # Atomic port transactions: NEXT itself uses these latches, so assembling a
    # transaction from separate generic C! words would lose the port settings.
    for name in ("IC@", "IC!", "FC@", "FC!"):
        a.label(token_label(name))
        a.call("pop"); a.ld(v["v"]+1); a.branch("ne", "port_error")
        ld("v"); a.and_(0xf8); a.branch("ne", "port_error"); cp("v", "bank")
        a.call("pop"); cp16("v", "p")
        put("target", 1 if name[0] == "I" else 2)
        if name.endswith("!"):
            a.call("pop"); cp("v", "w"); a.call("memwrite")
        else:
            a.call("memread"); st("v"); a.put(v["v"]+1, 0); a.call("push")
        a.jump("next")
    a.label("port_error"); error(8)
    a.label(token_label(":"))
    ld("mode"); a.branch("ne", "compile_error"); put("mode", 1)
    cp16("here", "oldhere"); a.jump("next")
    a.label(token_label(";"))
    ld("mode"); a.sub(2); a.branch("ne", "compile_error")
    ld("control"); a.branch("ne", "compile_error")
    a.call("check_room"); compile_byte(TOKEN["EXIT"]); cp16("pending", "head")
    a.call("commit"); put("mode", 0); a.jump("next")
    a.label("compile_error"); error(3)
    a.label(token_label("SAVE"))
    ld("mode"); a.branch("ne", "compile_error"); a.call("save"); a.jump("next")
    a.label(token_label("EXECUTE"))
    a.call("pop"); cp16("v", "found"); cp16("ip", "v"); a.call("rpush")
    cp16("found", "ip"); a.jump("next")
    a.label(token_label("WORDS"))
    cp16("head", "entry")
    a.label("words_loop")
    ld("entry"); a.orm(v["entry"]+1); a.branch("eq", "words_done")
    read_dict("entry"); st("link"); a.inc16(v["entry"])
    read_dict("entry"); a.st(v["link"]+1); a.inc16(v["entry"])
    read_dict("entry"); a.and_(0x7f); st("count"); a.inc16(v["entry"])
    a.label("words_name")
    read_dict("entry"); st("ch"); a.call("emit"); a.inc16(v["entry"])
    dec("count"); a.branch("ne", "words_name")
    put("ch", 32); a.call("emit"); cp16("link", "entry"); a.jump("words_loop")
    a.label("words_done"); a.jump("next")

    # Branch operands are absolute byte addresses in dictionary bank 2.
    for name in ("BRANCH", "0BRANCH"):
        a.label(token_label(name))
        a.call("fetch"); st("found"); a.call("fetch"); a.st(v["found"]+1)
        if name == "0BRANCH":
            a.call("pop"); ld("v"); a.orm(v["v"]+1); a.branch("ne", "next")
        cp16("found", "ip"); a.jump("next")
    # Control-flow compiler stack: 16 two-byte patch addresses in RAM $0700.
    for name in ("IF", "ELSE", "THEN", "BEGIN", "UNTIL", "AGAIN"):
        a.label(token_label(name))
        ld("mode"); a.sub(2); a.branch("ne", "compile_error"); a.call("check_room")
        if name == "IF":
            compile_byte(TOKEN["0BRANCH"]); cp16("here", "ctrl"); put("ctype", 1); a.call("cpush")
            compile_byte(0); compile_byte(0)
        elif name == "ELSE":
            a.call("cpop"); ld("ctype"); a.sub(1); a.branch("ne", "compile_error")
            cp16("ctrl", "work"); compile_byte(TOKEN["BRANCH"])
            cp16("here", "ctrl"); put("ctype", 1); a.call("cpush"); compile_byte(0); compile_byte(0)
            cp16("work", "ctrl"); a.call("patch")
        elif name == "THEN":
            a.call("cpop"); ld("ctype"); a.sub(1); a.branch("ne", "compile_error"); a.call("patch")
        elif name == "BEGIN": cp16("here", "ctrl"); put("ctype", 2); a.call("cpush")
        else:
            a.call("cpop"); ld("ctype"); a.sub(2); a.branch("ne", "compile_error")
            compile_byte(TOKEN["0BRANCH"] if name == "UNTIL" else TOKEN["BRANCH"])
            cp("ctrl", "w"); write_dict(); a.copy(v["ctrl"]+1, v["w"]); write_dict()
        a.jump("next")
    a.label(token_label("RECURSE"))
    ld("mode"); a.sub(2); a.branch("ne", "compile_error"); a.call("check_room")
    compile_byte(TOKEN["CALL"]); cp("definition", "w"); write_dict()
    a.copy(v["definition"]+1, v["w"]); write_dict(); a.jump("next")

    # Helper routines. Return slots are private to each non-recursive helper.
    a.label("port_setup")
    mmvar(0, "bank"); mmvar(10, "target"); mmvar(7, "p")
    a.y(255); a.ld(v["p"]+1); a.ins(0xca, 8); a.ret("port_setup")
    a.label("memread"); a.call("port_setup"); mmread(9); a.ret("memread")
    a.label("memwrite"); a.call("port_setup"); mmvar(9, "w"); a.ret("memwrite")
    a.label("fetch"); read_dict("ip"); st("w"); a.inc16(v["ip"]); ld("w"); a.ret("fetch")
    for prefix, page, pointer, pair in (("",2,"sp","v"),("r",3,"rp","v"),("c",7,"control","ctrl")):
        a.label(prefix+"push")
        ld(pointer); a.sub(0xf0 if prefix != "c" else 32); a.branch("eq", "stack_error")
        if prefix == "c":
            a.lx(v[pointer]); a.y(8); ld("ctype"); a.store_indirect()
        a.lx(v[pointer]); a.y(page); ld(pair); a.store_indirect(); inc(pointer)
        a.lx(v[pointer]); a.y(page); a.ld(v[pair]+1); a.store_indirect(); inc(pointer)
        a.ret(prefix+"push")
        a.label(prefix+"pop")
        ld(pointer); a.branch("eq", "stack_error")
        dec(pointer); a.lx(v[pointer]); a.y(page); a.indirect(); a.st(v[pair]+1)
        dec(pointer); a.lx(v[pointer]); a.y(page); a.indirect(); st(pair)
        if prefix == "c":
            a.lx(v[pointer]); a.y(8); a.indirect(); st("ctype")
        a.ret(prefix+"pop")
    a.label("stack_error"); error(4)

    # Carry/borrow detection without a carry register: compare unsigned bytes by
    # XORing their sign bits; subtract only when both operands share the sign bit.
    def less_bytes(left, right, result):
        different, yes, done = a.unique(), a.unique(), a.unique()
        a.ld(left); a.ins(0x61, right); a.and_(0x80); a.branch("ne", different)
        a.ld(left); a.subm(right); a.branch("lt", yes); a.put(result, 0); a.jump(done)
        a.label(different); a.ld(left); a.and_(0x80); a.branch("eq", yes)
        a.put(result, 0); a.jump(done)
        a.label(yes); a.put(result, 1); a.label(done)
    # Dedicated scratch bytes avoid clobbering operands and caller temporaries.
    carry = a.var("carry"); old = a.var("old")
    a.label("add16")
    a.copy(v["v"], old); ld("v"); a.addm(v["b"]); st("v")
    less_bytes(v["v"], old, carry)
    a.ld(v["v"]+1); a.addm(v["b"]+1); a.addm(carry); a.st(v["v"]+1); a.ret("add16")
    a.label("sub16")
    less_bytes(v["v"], v["b"], carry)
    ld("v"); a.subm(v["b"]); st("v")
    a.ld(v["v"]+1); a.subm(v["b"]+1); a.subm(carry); a.st(v["v"]+1); a.ret("sub16")
    a.label("unsigned_less")
    high_equal = a.unique(); compare_done = a.unique()
    a.ld(v["v"]+1); a.subm(v["b"]+1); a.branch("eq", high_equal)
    less_bytes(v["v"]+1, v["b"]+1, carry); a.jump(compare_done)
    a.label(high_equal); less_bytes(v["v"], v["b"], carry)
    a.label(compare_done); a.ld(carry); a.ret("unsigned_less")
    for name, condition in (("bool_zero", "eq"),("bool_nonzero","ne")):
        yes, done = a.unique(), a.unique()
        a.label(name); a.branch(condition, yes); put16("v", 0); a.jump(done)
        a.label(yes); put16("v", 0xffff); a.label(done); a.ret(name)

    a.label("lookup"); cp16("head", "entry")
    a.label("lookup_entry")
    ld("entry"); a.orm(v["entry"]+1); a.branch("eq", "lookup_missing")
    read_dict("entry"); st("link"); a.inc16(v["entry"])
    read_dict("entry"); a.st(v["link"]+1); a.inc16(v["entry"])
    read_dict("entry"); st("flags"); a.and_(0x7f); a.subm(v["toklen"])
    a.branch("ne", "lookup_next"); a.inc16(v["entry"]); put("index", 0)
    a.label("lookup_compare")
    read_dict("entry"); st("w")
    ld("tokstart"); a.addm(v["index"]); st("digit"); a.lx(v["digit"]); a.y(5)
    a.indirect(); a.subm(v["w"]); a.branch("ne", "lookup_next")
    a.inc16(v["entry"]); inc("index"); ld("index"); a.subm(v["toklen"])
    a.branch("ne", "lookup_compare")
    cp16("entry", "found"); a.ret("lookup")
    a.label("lookup_next"); cp16("link", "entry"); a.jump("lookup_entry")
    a.label("lookup_missing"); put16("found", 0); a.ret("lookup")

    a.label("parse_number"); put16("v", 0); put("index", 0); put("negative", 0)
    a.lx(v["tokstart"]); a.y(5); a.indirect(); a.sub(45); a.branch("ne", "parse_digit")
    put("negative", 1); inc("index"); ld("index"); a.subm(v["toklen"])
    a.branch("eq", "bad_number")
    a.label("parse_digit")
    ld("tokstart"); a.addm(v["index"]); st("digit"); a.lx(v["digit"]); a.y(5)
    a.indirect(); a.sub(48); st("digit"); a.branch("lt", "bad_number")
    a.sub(10); a.branch("ge", "bad_number")
    cp16("v", "work"); cp16("v", "b"); a.call("add16") # 2n
    cp16("v", "b"); a.call("add16") # 4n
    cp16("v", "b"); a.call("add16") # 8n
    cp16("work", "b"); a.call("add16"); a.call("add16") # 10n
    cp("digit", "b"); a.put(v["b"]+1, 0); a.call("add16")
    inc("index"); ld("index"); a.subm(v["toklen"]); a.branch("ne", "parse_digit")
    ld("negative"); a.branch("eq", "number_done"); a.call("negate")
    a.label("number_done"); a.ret("parse_number")
    a.label("bad_number"); error(5)
    a.label("negate")
    cp16("v", "b"); put16("v", 0); a.call("sub16"); a.ret("negate")

    a.label("print_number")
    a.ld(v["v"]+1); a.branch("ge", "positive_number")
    put("ch", 45); a.call("emit"); a.call("negate")
    a.label("positive_number"); put("started", 0)
    for divisor in (10000,1000,100,10,1):
        loop, done, output_digit = a.unique(), a.unique(), a.unique()
        put16("b", divisor); put("digit", 0); a.label(loop)
        a.call("unsigned_less"); a.branch("ne", done)
        a.call("sub16"); inc("digit"); a.jump(loop)
        a.label(done)
        if divisor != 1:
            ld("digit"); a.orm(v["started"]); a.branch("eq", output_digit)
        ld("digit"); a.add(48); st("ch"); a.call("emit"); put("started", 1)
        a.label(output_digit)
    put("ch", 32); a.call("emit"); a.ret("print_number")

    a.label("check_room")
    a.ld(v["here"]+1); a.sub(0xfd); a.branch("eq", "dictionary_full"); a.ret("check_room")
    a.label("dictionary_full"); error(6)
    a.label("commit")
    for i, name in enumerate(("head", "here")):
        for j in range(2):
            put16("p", i * 2 + j); put("bank", DICT_BANK); put("target", 1)
            a.copy(v[name]+j, v["w"]); a.call("memwrite")
    a.ret("commit")
    a.label("patch")
    cp16("ctrl", "p"); put("bank", DICT_BANK); put("target", 1)
    cp("here", "w"); a.call("memwrite"); a.inc16(v["p"])
    a.copy(v["here"]+1, v["w"]); a.call("memwrite"); a.ret("patch")

    # Native software flash driver: no ttl_checkpoint or host callback involved.
    command = a.var("command")
    a.label("flash_command"); a.st(command)
    cp("savebank", "bank"); put("target", 2)
    put16("p", 0x5555); put("w", 0xaa); a.call("memwrite")
    put16("p", 0x2aaa); put("w", 0x55); a.call("memwrite")
    put16("p", 0x5555); a.copy(command, v["w"]); a.call("memwrite")
    a.ret("flash_command")
    poll = a.var("flash_poll")
    a.label("flash_wait")
    # SST39SF040 has no dedicated BUSY pin. Poll DQ6 twice through the actual
    # flash data port instead of relying on the emulator's debug BUSY register.
    a.call("memread"); a.st(poll); a.call("memread"); a.ins(0x61, poll)
    a.and_(0x40); a.branch("ne", "flash_wait"); a.ret("flash_wait")
    a.label("service_power")
    mmread(5); a.branch("eq", "power_serviced")
    ld("saved"); a.branch("ne", "power_serviced")
    a.call("save"); put("saved", 1)
    a.label("power_serviced"); a.ret("service_power")
    a.label("save")
    put("status", 4); put("savebank", 0)
    a.imm(0x80); a.call("flash_command"); a.imm(0x10); a.call("flash_command")
    a.call("flash_wait"); put16("saveptr", 0)
    a.label("save_byte")
    cp16("saveptr", "p"); cp("savebank", "bank"); put("target", 1)
    a.call("memread"); st("pixel"); a.sub(0xff); a.branch("eq", "save_next")
    a.imm(0xa0); a.call("flash_command")
    cp16("saveptr", "p"); cp("savebank", "bank"); put("target", 2)
    cp("pixel", "w"); a.call("memwrite"); a.call("flash_wait")
    a.label("save_next")
    a.inc16(v["saveptr"]); ld("saveptr"); a.orm(v["saveptr"]+1)
    a.branch("ne", "save_byte"); inc("savebank"); ld("savebank"); a.sub(8)
    a.branch("ne", "save_byte")
    # Read back every byte: firmware must not report a successful partial save.
    put("savebank", 0); put16("saveptr", 0)
    a.label("verify_byte")
    cp16("saveptr", "p"); cp("savebank", "bank"); put("target", 1)
    a.call("memread"); st("pixel"); put("target", 2); a.call("memread")
    a.subm(v["pixel"]); a.branch("ne", "flash_error")
    a.inc16(v["saveptr"]); ld("saveptr"); a.orm(v["saveptr"]+1)
    a.branch("ne", "verify_byte"); inc("savebank"); ld("savebank"); a.sub(8)
    a.branch("ne", "verify_byte")
    put("status", 3); a.ret("save")
    a.label("flash_error"); error(7)

    # Character output: a RAM transcript for the debugger AND target-rendered pixels.
    a.label("emit")
    a.lx(v["outptr"]); a.ly(v["outptr"]+1); ld("ch"); a.store_indirect()
    a.inc16(v["outptr"]); a.ld(v["outptr"]+1); a.sub(0x50)
    out_ok = a.unique(); a.branch("ne", out_ok); a.put(v["outptr"]+1, 0x40); a.label(out_ok)
    ld("ch"); a.sub(10); a.branch("eq", "emit_newline")
    a.call("draw"); ld("cx"); a.add(5); st("cx"); a.sub(160)
    a.branch("ne", "emit_return")
    a.label("emit_newline"); put("cx", 0); ld("cy"); a.add(8); st("cy"); a.sub(0xf8)
    a.branch("ne", "emit_return"); a.call("clear_screen"); put("cy", 0x80)
    a.label("emit_return"); a.ret("emit")
    a.label("draw")
    # glyphbase = $8000 + ASCII * 5 (table byte bank 1).
    ld("ch"); st("glyphbase"); a.put(v["glyphbase"]+1, 0x80)
    for _ in range(4):
        ld("glyphbase"); a.addm(v["ch"]); st("glyphbase")
        no_carry = a.unique(); a.subm(v["ch"]); a.and_(0x80)
        # General 8-bit addition carry test; avoid changing the FORTH operands.
        less_bytes(v["glyphbase"], v["ch"], carry)
        a.ld(carry); a.branch("eq", no_carry); a.ld(v["glyphbase"]+1)
        a.add(1); a.st(v["glyphbase"]+1); a.label(no_carry)
    for col in range(5):
        cp16("glyphbase", "p"); put("bank", 1); put("target", 1)
        a.call("memread"); st("glyph"); a.inc16(v["glyphbase"])
        for row in range(7):
            black, pixel_ready = a.unique(), a.unique()
            ld("glyph"); a.and_(0x80); a.branch("eq", black)
            put("pixel", 0xff); a.jump(pixel_ready)
            a.label(black); put("pixel", 0xc0); a.label(pixel_ready)
            ld("cx"); a.add(col); st("px"); ld("cy"); a.add(row); st("py")
            a.lx(v["px"]); a.ly(v["py"]); ld("pixel"); a.store_indirect()
            ld("glyph"); a.addm(v["glyph"]); st("glyph")
    a.ret("draw")
    a.label("clear_screen"); put("vrow", 0x80)
    a.label("clear_page"); a.ly(v["vrow"]); a.x(0)
    for _ in range(256): a.ins(0xdc, 0xc0)
    inc("vrow"); ld("vrow"); a.sub(0xf8); a.branch("ne", "clear_page")
    a.ret("clear_screen")

    # Cooperative video service. Each active line is 200 native ticks, with 24
    # sync, 12 back-porch, 160 pixel and 4 front-porch ticks. Evaluation/save may
    # pause refresh; this is not a cycle-budgeted multitasking ROM kernel.
    a.label("video")
    def out(value): a.ins(0x18, value)
    def timed_line(label, next_label, low=0x80, setup=None, pixels=False):
        a.label(label); start = a.pc; out(low)
        if setup: setup()
        while a.pc - start < 24: a.nop()
        if a.pc - start != 24: raise ValueError("video sync budget exceeded")
        out(low | 0x40)
        for _ in range(11): a.nop()
        if pixels:
            for _ in range(160): a.ins(0x1d)
        else:
            for _ in range(160): a.nop()
        while a.pc - start < 197: a.nop()
        a.jump(next_label)
        if a.pc - start != 200: raise ValueError("video line budget exceeded")
    # First falling VSYNC marks a new frame. 2 sync + 31 blank precede row 34.
    for i in range(33):
        timed_line("video"+str(i), "video"+str(i+1) if i < 32 else "video_active0",
                   low=0 if i < 2 else 0x80)
    # One block paints four physical scanlines for each 160-pixel logical row.
    # vrow is initialized before entering the video function via a trampoline.
    for line in range(4):
        def setup(line=line):
            if line == 0:
                inc("vrow"); ld("vrow"); a.sub(0xf8); a.branch("eq", "video_tail0")
            a.ly(v["vrow"]); a.x(0)
        timed_line("video_active"+str(line), "video_active"+str((line+1)%4), setup=setup, pixels=True)
    for i in range(12):
        timed_line("video_tail"+str(i), "video_tail"+str(i+1) if i < 11 else "video_return")
    a.label("video_return"); ld("diag"); a.ins(0x40, 16); st("diag"); a.ret("video")
    # Route calls through initialization while preserving the generated line labels.
    video_start = a.labels["video"]
    a.labels["video"] = a.pc
    put("vrow", 0x7f); a.y(video_start >> 8); a.ins(0xe0, video_start & 255); a.nop()

    image = bytearray([0xff] * SIZE)
    code = a.resolve(); image[:len(code)] = code
    image[0x20000:0x30000] = dictionary
    # Table pages are read through the ordinary instruction-RAM data port.
    font = bytearray(128 * 5)
    for char, rows in GLYPHS.items():
        for col in range(3):
            font[ord(char)*5+col] = sum(((int(row, 16) >> (2-col)) & 1) << (7-i) for i,row in enumerate(rows))
    image[0x18000:0x18000+len(font)] = font
    for shifted in range(2):
        table = bytearray(256)
        for char, scan in KEYS.items():
            mapped = SHIFTED.get(char, char) if shifted else char
            table[scan] = ord(mapped)
        image[0x19000+shifted*256:0x19100+shifted*256] = table
    output.mkdir(parents=True, exist_ok=True)
    (output / "forth.flash").write_bytes(image)
    abi = {name: address for name,(address,_) in a.variables.items()}
    abi.update({"dictionary_bank":DICT_BANK,"dictionary_limit":DICT_LIMIT,"tokens":TOKEN,
                "code_words":len(code)//2,"labels":a.labels,"keys":KEYS,"shifted":SHIFTED})
    (output / "forth.json").write_text(json.dumps(abi, indent=2)+"\n")
    header = ["/* Generated by firmware/build.py; do not edit. */",
              "#ifndef TTL_FORTH_ABI_H", "#define TTL_FORTH_ABI_H"]
    for name, (address, _) in a.variables.items():
        header.append(f"#define TTL_FW_{name.upper()} {address}")
    scans, shifts = [0] * 128, [0] * 128
    for char, scan in KEYS.items(): scans[ord(char)] = scan
    for normal, shifted in SHIFTED.items():
        scans[ord(shifted)] = KEYS[normal]; shifts[ord(shifted)] = 1
    for name, values in (("ttl_fw_ascii_scan", scans), ("ttl_fw_ascii_shift", shifts)):
        header.append(f"static const unsigned char {name}[128] = {{" + ",".join(map(str, values)) + "};")
    header.extend(("#endif", ""))
    (output / "forth_abi.h").write_text("\n".join(header))
    print(f"Native firmware: {len(code)//2} words; dictionary: {here} bytes; {len(a.variables)} RAM symbols")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, default=Path(__file__).resolve().parent.parent / "build" / "firmware")
    build(parser.parse_args().output)
