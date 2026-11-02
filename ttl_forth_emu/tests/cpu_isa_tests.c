/* Independent instruction-level truth table, cross-checked with Docs/gtemu.c.
 * Covers every 16-bit encoding with six register/PC edge states. */
#include "gigatron.h"
#include <stdio.h>
#include <stdlib.h>

typedef struct { unsigned reads; } input_t;
static uint8_t read_input(void* context) {
    input_t* in = context;
    ++in->reads;
    return 0xa5;
}
static uint8_t pattern(uint16_t a) { return (uint8_t)((a * 37u) ^ (a >> 8) ^ 0x69); }
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "IR=%04x state=%u: %s\n", ir, state, #x); exit(1); } } while (0)

int main(void) {
    gigatron_t cpu;
    if (!gigatron_init(&cpu, NULL)) return 1;
    for (unsigned i = 0; i < 65536; ++i) cpu.ram[i] = pattern((uint16_t)i);
    input_t input = {0};
    cpu.bus_context = &input; cpu.read_input = read_input;
    const uint8_t accumulators[] = {0, 1, 0x7f, 0x80, 0xff, 0xa5};
    const uint16_t delays[] = {1, 0x100, 0xffff, 0, 0x12ff, 0xff00};
    for (unsigned state = 0; state < 6; ++state) {
        for (unsigned ir = 0; ir < 65536; ++ir) {
            unsigned op = ir >> 13, mode = (ir >> 10) & 7, bus = (ir >> 8) & 3;
            uint8_t d = (uint8_t)ir, ac = accumulators[state];
            uint8_t x = state == 5 ? 255 : (uint8_t)(state * 73), y = (uint8_t)(state * 51);
            uint8_t out = state & 1 ? 0xff : 0, outx = 0x5a;
            uint16_t delay = delays[state], pc = (uint16_t)(delay - 1), address = d;
            if (op != 7) {
                if (mode == 1) address = x;
                if (mode == 2) address = (uint16_t)((y << 8) | d);
                if (mode == 3 || mode == 7) address = (uint16_t)((y << 8) | x);
            }
            uint8_t b = bus == 0 ? d : bus == 1 ? (op == 6 ? 0 : pattern(address)) : bus == 2 ? ac : 0xa5;
            uint8_t result = b, expected_ac = ac, expected_x = x, expected_y = y, expected_out = out;
            uint16_t next = (uint16_t)(delay + 1);
            if (op == 7) {
                unsigned select = (ac >> 7) + 2 * (ac == 0);
                if (!mode || (mode & (1u << select)))
                    next = (uint16_t)((!mode ? (uint16_t)y << 8 : delay & 0xff00) | b);
            } else {
                switch (op) {
                case 1: result = ac & b; break;
                case 2: result = ac | b; break;
                case 3: result = ac ^ b; break;
                case 4: result = (uint8_t)(ac + b); break;
                case 5: result = (uint8_t)(ac - b); break;
                case 6: result = ac; break;
                default: break;
                }
                if (mode < 4 && op != 6) expected_ac = result;
                if (mode == 4) expected_x = result;
                if (mode == 5) expected_y = result;
                if (mode >= 6 && op != 6) expected_out = result;
                if (mode == 7) expected_x = (uint8_t)(x + 1);
            }
            uint8_t expected_outx = (~out & expected_out & 0x40) ? ac : outx;
            cpu.pc = pc; cpu.next_pc = delay; cpu.rom[pc] = (uint16_t)ir;
            cpu.ac = ac; cpu.x = x; cpu.y = y; cpu.out = out; cpu.outx = outx; cpu.cycles = 43;
            input.reads = 0;
            gigatron_tick(&cpu);
            CHECK(cpu.pc == delay && cpu.next_pc == next && cpu.cycles == 44);
            CHECK(cpu.ac == expected_ac && cpu.x == expected_x && cpu.y == expected_y);
            CHECK(cpu.out == expected_out && cpu.outx == expected_outx);
            CHECK(input.reads == (bus == 3 ? 1u : 0u));
            CHECK(cpu.ram[address] == (op == 6 ? b : pattern(address)));
            if (op == 6) cpu.ram[address] = pattern(address);
        }
    }
    gigatron_shutdown(&cpu);
    puts("393216 instruction/state checks passed (ST/RAM bus has deterministic zero policy).");
    return 0;
}
