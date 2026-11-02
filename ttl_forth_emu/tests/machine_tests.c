#include "machine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Unlike assert(), checks remain active in Release builds. */
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)

static ttl_machine_t* new_machine(void) {
    ttl_machine_t* m = malloc(sizeof(*m));
    CHECK(m && ttl_init(m));
    return m;
}
static void destroy(ttl_machine_t* m) { ttl_shutdown(m); free(m); }
static void boot(ttl_machine_t* m) { ttl_run(m, FLASH_SIZE); CHECK(!m->booting); }
static void unlock(ttl_flash_t* f, uint8_t command) {
    flash_write(f, 0x5555, 0xaa);
    flash_write(f, 0x2aaa, 0x55);
    flash_write(f, 0x5555, command);
}
static void finish(ttl_flash_t* f) { while (f->remaining) flash_tick(f); }

static void test_boot_and_banks(void) {
    ttl_machine_t* m = new_machine();
    m->flash.bytes[0] = 0x00; m->flash.bytes[1] = 0x42;
    m->flash.bytes[FLASH_SIZE - 2] = 0x12; m->flash.bytes[FLASH_SIZE - 1] = 0x34;
    ttl_run(m, FLASH_SIZE - 1);
    CHECK(m->booting && m->cpu.cycles == 0);
    ttl_tick(m);
    CHECK(!m->booting && m->cpu.rom[0] == 0x0042);
    CHECK(m->cpu.rom[TTL_MEMORY_SIZE / 2 - 1] == 0x1234);
    ttl_tick(m); CHECK(m->cpu.ac == 0x42 && m->cpu.cycles == 1);
    CHECK(m->cpu.ram_size == TTL_DATA_SIZE);
    ttl_write(m, 0x1234, 0x5a);
    ttl_write(m, 0xffff, 0xa5);
    ttl_write(m, TTL_MEMORY_BANK, 7);
    CHECK(ttl_read(m, 0x1234) == 0x5a && ttl_read(m, 0xffff) == 0xa5);
    ttl_write(m, TTL_WINDOW, TTL_WINDOW_IRAM);
    for (unsigned bank = 0; bank < 8; ++bank) {
        ttl_write(m, TTL_MEMORY_BANK, (uint8_t)bank);
        ttl_write(m, 0x200, 0x00);
        ttl_write(m, 0x201, (uint8_t)(bank + 0x20));
        CHECK(ttl_read(m, 0x201) == bank + 0x20);
    }
    for (unsigned bank = 0; bank < 4; ++bank) {
        ttl_write(m, TTL_EXEC_BANK, (uint8_t)bank);
        m->cpu.pc = 0x100; m->cpu.next_pc = 0x101;
        ttl_tick(m); CHECK(m->cpu.ac == bank * 2 + 0x20);
    }
    destroy(m);
}

static void test_cpu_bus_and_self_modify(void) {
    ttl_machine_t* m = new_machine(); boot(m);
    /* All accesses here execute as native instructions, not host-side writes. */
    m->cpu.rom[0] = 0x14ff; /* LD $ff,Y */
    m->cpu.rom[1] = 0x0001; /* LD 1,AC */
    m->cpu.rom[2] = 0xca02; /* ST AC,[Y,$02]: select I-RAM window */
    m->cpu.rom[3] = 0x1400; /* LD 0,Y */
    m->cpu.rom[4] = 0x0077;
    m->cpu.rom[5] = 0xca15; /* ST AC,[Y,$15]: patch word 10's immediate */
    m->cpu.rom[6] = 0xe00a; /* JMP $000a, with delay slot */
    m->cpu.rom[7] = 0x0066;
    m->cpu.rom[10] = 0x0011;
    ttl_run(m, 7); CHECK(m->cpu.pc == 7 && m->cpu.next_pc == 10);
    ttl_tick(m); CHECK(m->cpu.ac == 0x66);
    ttl_tick(m); CHECK(m->cpu.ac == 0x77 && m->cpu.rom[10] == 0x0077);
    ttl_write(m, TTL_WINDOW, TTL_WINDOW_DATA);
    m->cpu.pc = 20; m->cpu.next_pc = 21;
    m->cpu.rom[20] = 0xc220; /* ST AC,[$20] */
    m->cpu.rom[21] = 0x0120; /* LD [$20],AC */
    ttl_tick(m); m->cpu.ac = 0; ttl_tick(m);
    CHECK(m->cpu.ac == 0x77 && m->cpu.ram[0x20] == 0x77);
    m->cpu.rom[22] = 0xd042; /* ST $42,[$42],X: X gets old AC, not bus data. */
    m->cpu.rom[23] = 0x1c03; /* LD $03,OUT,[Y,X++], including immediate-bus X++. */
    ttl_tick(m); CHECK(m->cpu.x == 0x77 && m->cpu.ram[0x42] == 0x42);
    ttl_tick(m); CHECK(m->cpu.x == 0x78 && m->cpu.out == 3);
    destroy(m);
}

static void test_flash(void) {
    ttl_flash_t* f = malloc(sizeof(*f)); CHECK(f); flash_init(f);
    flash_write(f, 0x100, 0); CHECK(f->bytes[0x100] == 0xff);
    unlock(f, 0xa0); flash_write(f, 0x100, 0x5a);
    CHECK(f->remaining == FLASH_PROGRAM_CYCLES && f->bytes[0x100] == 0xff);
    uint8_t a = flash_read(f, 0x100), b = flash_read(f, 0x100);
    CHECK(((a ^ b) & 0x40) && (a & 0x80));
    for (unsigned i = 1; i < FLASH_PROGRAM_CYCLES; ++i) flash_tick(f);
    CHECK(f->bytes[0x100] == 0xff);
    flash_tick(f); CHECK(flash_read(f, 0x100) == 0x5a);
    unlock(f, 0xa0); flash_write(f, 0x100, 0xff); finish(f);
    CHECK(f->error && f->bytes[0x100] == 0x5a); /* No 0 -> 1 programming. */
    unlock(f, 0xa0); flash_write(f, 0x200, 0xf0); finish(f);
    CHECK(f->bytes[0x200] == 0xf0);
    f->bytes[0x1000] = 0;
    unlock(f, 0x80); flash_write(f, 0x5555, 0xaa);
    flash_write(f, 0x2aaa, 0x55); flash_write(f, 0x100, 0x30);
    CHECK(f->remaining == FLASH_ERASE_CYCLES); finish(f);
    CHECK(f->bytes[0x100] == 0xff && f->bytes[0x1000] == 0);
    unlock(f, 0x90); CHECK(flash_read(f, 0) == 0xbf && flash_read(f, 1) == 0xb7);
    flash_write(f, 0, 0xf0); CHECK(flash_read(f, 0) == 0xff);
    unlock(f, 0x80); unlock(f, 0x10); finish(f);
    CHECK(f->bytes[0x1000] == 0xff);
    free(f);
}

static void test_ps2_and_power(void) {
    ttl_machine_t* m = new_machine(); boot(m);
    ttl_inject_scan(m, 0xe0);
    CHECK(ttl_read(m, TTL_KEY_STATUS) == TTL_KEY_READY);
    ttl_inject_scan(m, 0x75);
    CHECK(m->key_status & TTL_KEY_OVERRUN);
    CHECK(ttl_read(m, TTL_KEY_DATA) == 0xe0);
    CHECK(ttl_read(m, TTL_KEY_DATA) == 0);
    ttl_write(m, TTL_KEY_STATUS, TTL_KEY_OVERRUN); CHECK(!m->key_status);
    ttl_inject_scan(m, 0xf0);
    m->cpu.rom[0] = 0x0300; /* LD IN,AC */
    ttl_tick(m); CHECK(m->cpu.ac == 0xf0 && !m->key_status);
    for (unsigned i = 0; i < 11; ++i) {
        ttl_ps2_lines(m, true, false); ttl_ps2_lines(m, false, false);
    }
    CHECK(m->key_status & TTL_KEY_FRAME_ERROR);
    ttl_write(m, TTL_KEY_STATUS, TTL_KEY_FRAME_ERROR);
    ttl_ps2_lines(m, true, false); ttl_ps2_lines(m, false, false);
    ttl_run(m, 12500); CHECK(m->key_status & TTL_KEY_FRAME_ERROR);
    ttl_power_fail(m); CHECK(ttl_read(m, TTL_POWER_STATUS) == 1);
    ttl_write(m, TTL_POWER_STATUS, 0); CHECK(m->power_fail);
    CHECK(ttl_power_on(m)); CHECK(!m->power_fail && !m->key_status);
    destroy(m);
}

static void test_checkpoint(void) {
    ttl_machine_t* m = new_machine(); boot(m);
    m->cpu.rom[0] = 0x0042;
    m->cpu.rom[TTL_MEMORY_SIZE / 2 - 1] = 0x1234;
    m->cpu.ram[0] = 0xab; m->cpu.ram[TTL_DATA_SIZE - 1] = 0xcd;
    uint64_t cycles = m->cpu.cycles;
    CHECK(ttl_checkpoint(m)); CHECK(m->cpu.cycles == cycles);
    CHECK(m->flash.bytes[0] == 0 && m->flash.bytes[1] == 0x42);
    CHECK(m->flash.bytes[FLASH_SIZE - 1] == 0x34);
    CHECK(ttl_power_on(m)); boot(m);
    CHECK(m->cpu.rom[0] == 0x0042 && m->cpu.rom[TTL_MEMORY_SIZE / 2 - 1] == 0x1234);
    CHECK(m->cpu.ram[0] == 0 && m->cpu.ram[TTL_DATA_SIZE - 1] == 0);
    unlock(&m->flash, 0xa0); flash_write(&m->flash, 0, 0);
    CHECK(!ttl_checkpoint(m) && !ttl_power_on(m));
    destroy(m);
}

static void test_peripherals(void) {
    ttl_machine_t* m = new_machine(); boot(m);
    m->cpu.rom[0] = 0x00f0; /* LD $f0,AC */
    m->cpu.rom[1] = 0x18c3; /* LD $c3,OUT: rising HSYNC latches AC. */
    m->cpu.rom[2] = 0x1800; /* Falling VSYNC completes frame. */
    ttl_run(m, 2); CHECK(m->cpu.outx == 0xf0);
    ttl_tick(m); CHECK(vga_frame_ready(&m->vga));
    m->cpu.rom[3] = 0xe003; m->cpu.rom[4] = 0x0200;
    ttl_run(m, 2000);
    CHECK(audio_available_samples(&m->audio) > 0);
    float samples[8]; CHECK(audio_read_samples(&m->audio, samples, 8) == 8);
    CHECK(samples[0] > 0);
    m->vga.row = m->vga.min_row; m->vga.col = m->vga.min_col;
    m->vga.prev_out = m->cpu.out = 0xc0;
    m->vga.pixel_index = m->vga.width * m->vga.height * 4;
    vga_tick(&m->vga); /* Malformed video must remain memory-safe. */
    destroy(m);
}

static void port_address(ttl_machine_t* m, uint16_t address) {
    ttl_write(m, TTL_PORT_LO, (uint8_t)address);
    ttl_write(m, TTL_PORT_HI, (uint8_t)(address >> 8));
}

static void port_write(ttl_machine_t* m, uint16_t address, uint8_t value) {
    port_address(m, address); ttl_write(m, TTL_PORT_DATA, value);
}

static void test_ports(void) {
    ttl_machine_t* m = new_machine(); boot(m);
    ttl_write(m, 0x1234, 0x42);
    ttl_write(m, TTL_MEMORY_BANK, 7);
    ttl_write(m, TTL_PORT_TARGET, TTL_WINDOW_IRAM);
    port_write(m, 0xffff, 0xa5);
    CHECK(ttl_read(m, TTL_PORT_DATA) == 0xa5);
    CHECK((m->cpu.rom[TTL_MEMORY_SIZE / 2 - 1] & 255) == 0xa5);
    port_write(m, TTL_IO_BASE, 0x5a); /* MMIO-shadowed I-RAM byte. */
    CHECK(ttl_read(m, TTL_PORT_DATA) == 0x5a);
    CHECK(ttl_read(m, 0x1234) == 0x42 && m->window == TTL_WINDOW_DATA);
    ttl_write(m, TTL_PORT_TARGET, TTL_WINDOW_FLASH);
    port_write(m, 0x5555, 0xaa); port_write(m, 0x2aaa, 0x55);
    port_write(m, 0x5555, 0xa0); port_write(m, 0xffff, 0x5a);
    CHECK(m->flash.remaining == FLASH_PROGRAM_CYCLES);
    ttl_run(m, FLASH_PROGRAM_CYCLES);
    CHECK(ttl_read(m, TTL_PORT_DATA) == 0x5a);
    CHECK(m->flash.bytes[FLASH_SIZE - 1] == 0x5a);
    destroy(m);
}

int main(int argc, char** argv) {
    const char* selected = argc > 1 ? argv[1] : "all";
    if (!strcmp(selected, "all") || !strcmp(selected, "boot")) test_boot_and_banks();
    if (!strcmp(selected, "all") || !strcmp(selected, "cpu")) test_cpu_bus_and_self_modify();
    if (!strcmp(selected, "all") || !strcmp(selected, "flash")) test_flash();
    if (!strcmp(selected, "all") || !strcmp(selected, "ps2")) test_ps2_and_power();
    if (!strcmp(selected, "all") || !strcmp(selected, "checkpoint")) test_checkpoint();
    if (!strcmp(selected, "all") || !strcmp(selected, "peripherals")) test_peripherals();
    if (!strcmp(selected, "all") || !strcmp(selected, "ports")) test_ports();
    puts("All machine checks passed.");
    return 0;
}
