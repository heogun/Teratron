#include "machine.h"
#include <string.h>

static uint8_t iram_read(const ttl_machine_t* m, uint32_t a) {
    uint16_t w = m->cpu.rom[a / 2];
    return (uint8_t)(a & 1 ? w : w >> 8);
}

static void iram_write(ttl_machine_t* m, uint32_t a, uint8_t v) {
    uint16_t* w = &m->cpu.rom[a / 2];
    *w = a & 1 ? (uint16_t)((*w & 0xff00) | v)
               : (uint16_t)((*w & 0x00ff) | ((uint16_t)v << 8));
}

static uint8_t key_read(ttl_machine_t* m) {
    if (!(m->key_status & TTL_KEY_READY)) return 0;
    m->key_status &= (uint8_t)~TTL_KEY_READY;
    return m->key_data;
}

uint8_t ttl_read(ttl_machine_t* m, uint16_t a) {
    switch (a) {
    case TTL_MEMORY_BANK: return m->memory_bank;
    case TTL_EXEC_BANK: return m->exec_bank;
    case TTL_WINDOW: return m->window;
    case TTL_KEY_DATA: return key_read(m);
    case TTL_KEY_STATUS: return m->key_status;
    case TTL_RESERVED_FF05: return 0;
    case TTL_FLASH_STATUS: return (m->flash.remaining ? 1 : 0) | (m->flash.error ? 2 : 0);
    case TTL_PORT_LO: return (uint8_t)m->port_address;
    case TTL_PORT_HI: return (uint8_t)(m->port_address >> 8);
    case TTL_PORT_TARGET: return m->port_target;
    case TTL_PORT_DATA: {
        uint32_t p = (uint32_t)m->memory_bank * TTL_BANK_SIZE + m->port_address;
        return m->port_target == TTL_WINDOW_IRAM ? iram_read(m, p)
             : m->port_target == TTL_WINDOW_FLASH ? flash_read(&m->flash, p) : 0xff;
    }
    default: break;
    }
    uint32_t physical = (uint32_t)m->memory_bank * TTL_BANK_SIZE + a;
    if (m->window == TTL_WINDOW_IRAM) return iram_read(m, physical);
    if (m->window == TTL_WINDOW_FLASH) return flash_read(&m->flash, physical);
    return m->cpu.ram[a];
}

void ttl_write(ttl_machine_t* m, uint16_t a, uint8_t v) {
    switch (a) {
    case TTL_MEMORY_BANK: m->memory_bank = v & 7; return;
    case TTL_EXEC_BANK: m->exec_bank = v & 3; return;
    case TTL_WINDOW: if (v <= TTL_WINDOW_FLASH) m->window = v; return;
    case TTL_KEY_DATA: return;
    case TTL_KEY_STATUS:
        m->key_status &= (uint8_t)~(v & (TTL_KEY_FRAME_ERROR | TTL_KEY_OVERRUN));
        return;
    case TTL_RESERVED_FF05: return;
    case TTL_FLASH_STATUS: if (v & 2) m->flash.error = false; return;
    case TTL_PORT_LO: m->port_address = (m->port_address & 0xff00) | v; return;
    case TTL_PORT_HI: m->port_address = (m->port_address & 0xff) | ((uint16_t)v << 8); return;
    case TTL_PORT_TARGET:
        if (v == TTL_WINDOW_IRAM || v == TTL_WINDOW_FLASH) m->port_target = v;
        return;
    case TTL_PORT_DATA: {
        uint32_t p = (uint32_t)m->memory_bank * TTL_BANK_SIZE + m->port_address;
        if (m->port_target == TTL_WINDOW_IRAM) iram_write(m, p, v);
        else if (m->port_target == TTL_WINDOW_FLASH) flash_write(&m->flash, p, v);
        return;
    }
    default: break;
    }
    uint32_t physical = (uint32_t)m->memory_bank * TTL_BANK_SIZE + a;
    if (m->window == TTL_WINDOW_IRAM) iram_write(m, physical, v);
    else if (m->window == TTL_WINDOW_FLASH) flash_write(&m->flash, physical, v);
    else m->cpu.ram[a] = v;
}

static uint8_t bus_read(void* ctx, uint16_t a) { return ttl_read(ctx, a); }
static void bus_write(void* ctx, uint16_t a, uint8_t v) { ttl_write(ctx, a, v); }
static uint8_t bus_input(void* ctx) { return key_read(ctx); }
static uint16_t bus_fetch(void* ctx, uint16_t pc) {
    ttl_machine_t* m = ctx;
    return m->cpu.rom[(uint32_t)m->exec_bank * TTL_BANK_SIZE + pc];
}

bool ttl_init(ttl_machine_t* m) {
    if (!m) return false;
    memset(m, 0, sizeof(*m));
    gigatron_config_t config = {GIGATRON_HZ, 18, 16};
    if (!gigatron_init(&m->cpu, &config)) return false;
    if (!vga_init(&m->vga, &m->cpu)) {
        gigatron_shutdown(&m->cpu);
        return false;
    }
    if (!audio_init(&m->audio, &m->cpu)) {
        vga_shutdown(&m->vga);
        gigatron_shutdown(&m->cpu);
        return false;
    }
    m->cpu.rom_mask = 0xffff; /* 16-bit word PC; fetch latch adds A16/A17. */
    m->cpu.bus_context = m;
    m->cpu.read_data = bus_read;
    m->cpu.write_data = bus_write;
    m->cpu.fetch_instruction = bus_fetch;
    m->cpu.read_input = bus_input;
    flash_init(&m->flash);
    return ttl_power_on(m);
}

void ttl_shutdown(ttl_machine_t* m) {
    audio_shutdown(&m->audio);
    vga_shutdown(&m->vga);
    gigatron_shutdown(&m->cpu);
}

bool ttl_power_on(ttl_machine_t* m) {
    /* An in-flight flash operation must complete before a new boot. */
    if (m->flash.remaining) return false;
    memset(m->cpu.rom, 0, TTL_MEMORY_SIZE);
    memset(m->cpu.ram, 0, TTL_DATA_SIZE);
    gigatron_reset(&m->cpu);
    vga_reset(&m->vga);
    audio_reset(&m->audio);
    m->memory_bank = m->exec_bank = m->window = 0;
    m->port_address = 0;
    m->port_target = TTL_WINDOW_IRAM;
    m->key_data = m->key_status = m->key_bits = 0;
    m->key_shift = 0;
    m->key_idle = 0;
    m->ps2_clock = true;
    m->flash.sequence = 0;
    m->flash.id_mode = false;
    m->flash.error = false;
    m->boot_offset = 0;
    m->ticks = 0;
    m->booting = true;
    return true;
}

void ttl_tick(ttl_machine_t* m) {
    ++m->ticks;
    flash_tick(&m->flash);
    if (m->key_bits && ++m->key_idle >= 12500) {
        m->key_bits = 0;
        m->key_status |= TTL_KEY_FRAME_ERROR;
    }
    if (m->booting) {
        uint32_t a = m->boot_offset++;
        iram_write(m, a, m->flash.bytes[a]);
        if (m->boot_offset == FLASH_SIZE) m->booting = false;
        return;
    }
    gigatron_tick(&m->cpu);
    vga_tick(&m->vga);
    audio_tick(&m->audio);
}

void ttl_run(ttl_machine_t* m, uint64_t cycles) {
    while (cycles--) ttl_tick(m);
}

void ttl_ps2_lines(ttl_machine_t* m, bool clock, bool data) {
    bool falling = m->ps2_clock && !clock;
    m->ps2_clock = clock;
    if (!falling) return;
    /* Wait for an actual start bit; stray idle-high clocks must not desync us. */
    if (!m->key_bits && data) return;
    m->key_idle = 0;
    if (!m->key_bits) m->key_shift = 0;
    m->key_shift |= (uint16_t)data << m->key_bits++;
    if (m->key_bits != 11) return;
    m->key_bits = 0;
    unsigned parity = 0;
    for (unsigned i = 1; i <= 9; ++i) parity ^= (m->key_shift >> i) & 1;
    if ((m->key_shift & 1) || !(m->key_shift & 0x400) || parity != 1) {
        m->key_status |= TTL_KEY_FRAME_ERROR;
    } else if (m->key_status & TTL_KEY_READY) {
        m->key_status |= TTL_KEY_OVERRUN; /* Single hardware receive latch. */
    } else {
        m->key_data = (uint8_t)(m->key_shift >> 1);
        m->key_status |= TTL_KEY_READY;
    }
}

void ttl_inject_scan(ttl_machine_t* m, uint8_t scan) {
    unsigned parity = 1;
    for (unsigned i = 0; i < 8; ++i) parity ^= (scan >> i) & 1;
    uint16_t frame = (uint16_t)((scan << 1) | (parity << 9) | 0x400);
    for (unsigned i = 0; i < 11; ++i) {
        bool bit = (frame >> i) & 1;
        ttl_ps2_lines(m, true, bit);
        ttl_ps2_lines(m, false, bit);
    }
    ttl_ps2_lines(m, true, true);
}
