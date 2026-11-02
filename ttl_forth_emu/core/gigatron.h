/**
 * Gigatron TTL Microcomputer Emulator Core
 * 
 * A pure C implementation of the Gigatron TTL computer.
 * Based on the JavaScript reference implementation.
 */

#ifndef GIGATRON_H
#define GIGATRON_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Default configuration */
#define GIGATRON_HZ             6250000     /* 6.25 MHz clock */
#define GIGATRON_ROM_SIZE       (1 << 16)   /* 64K x 16-bit ROM */
#define GIGATRON_RAM_SIZE       (1 << 16)   /* 64K x 8-bit data RAM */

/* OUT register bit definitions */
#define GIGATRON_OUT_HSYNC      0x40        /* Horizontal sync (active low) */
#define GIGATRON_OUT_VSYNC      0x80        /* Vertical sync (active low) */

/**
 * Gigatron CPU state
 */
typedef struct gigatron_t {
    /* Clock frequency */
    uint32_t hz;
    
    /* ROM (instruction memory) */
    uint16_t* rom;
    uint32_t rom_size;
    uint32_t rom_mask;
    
    /* RAM (data memory) */
    uint8_t* ram;
    uint32_t ram_size;
    uint32_t ram_mask;
    
    /* Registers */
    uint16_t pc;        /* Program counter */
    uint16_t next_pc;   /* Next program counter */
    uint8_t ac;         /* Accumulator */
    uint8_t x;          /* X register */
    uint8_t y;          /* Y register */
    uint8_t out;        /* Output register (active low HSYNC/VSYNC, 6-bit color) */
    uint8_t outx;       /* Extended output register (active low audio sample) */
    uint8_t in_reg;     /* Raw fallback IN value; the board supplies a PS/2 read callback. */
    
    /* Cycle counter for timing */
    uint64_t cycles;
    /* Optional board bus. Kept out of the original gigatron_emu tree. */
    void* bus_context;
    uint8_t (*read_data)(void*, uint16_t);
    void (*write_data)(void*, uint16_t, uint8_t);
    uint16_t (*fetch_instruction)(void*, uint16_t);
    uint8_t (*read_input)(void*);
} gigatron_t;

/**
 * Gigatron configuration options
 */
typedef struct gigatron_config_t {
    uint32_t hz;                /* Clock frequency (default: 6250000) */
    uint32_t rom_address_width; /* Instruction storage width (default: 16, maximum: 18) */
    uint32_t ram_address_width; /* RAM address width in bits (default: 16, maximum: 16) */
} gigatron_config_t;

/**
 * Get default configuration
 */
gigatron_config_t gigatron_default_config(void);

/**
 * Initialize the Gigatron with the given configuration.
 * Allocates ROM and RAM memory.
 * Returns true on success, false on failure.
 */
bool gigatron_init(gigatron_t* cpu, const gigatron_config_t* config);

/**
 * Shutdown and free allocated memory.
 */
void gigatron_shutdown(gigatron_t* cpu);

/**
 * Reset CPU to power-on state.
 * Does not clear RAM (power-on contents on real hardware are undefined).
 */
void gigatron_reset(gigatron_t* cpu);

/**
 * Advance simulation by one clock cycle.
 */
void gigatron_tick(gigatron_t* cpu);

/**
 * Advance simulation by multiple clock cycles.
 */
void gigatron_run(gigatron_t* cpu, uint32_t cycles);

/**
 * Load ROM from memory buffer.
 * Buffer should contain big-endian 16-bit words.
 * Returns the number of words loaded.
 */
size_t gigatron_load_rom(gigatron_t* cpu, const uint8_t* data, size_t size);

/**
 * Load ROM from file.
 * Returns true on success, false on failure.
 */
bool gigatron_load_rom_file(gigatron_t* cpu, const char* filename);

/**
 * Set fallback IN value; the board's read_input callback takes precedence.
 */
static inline void gigatron_set_input(gigatron_t* cpu, uint8_t value) {
    cpu->in_reg = value;
}

/**
 * Get output register value
 */
static inline uint8_t gigatron_get_output(gigatron_t* cpu) {
    return cpu->out;
}

/**
 * Get extended output register value (audio)
 */
static inline uint8_t gigatron_get_outx(gigatron_t* cpu) {
    return cpu->outx;
}

/**
 * Check if HSYNC is active (active low in hardware)
 */
static inline bool gigatron_hsync_active(gigatron_t* cpu) {
    return (cpu->out & GIGATRON_OUT_HSYNC) == 0;
}

/**
 * Check if VSYNC is active (active low in hardware)
 */
static inline bool gigatron_vsync_active(gigatron_t* cpu) {
    return (cpu->out & GIGATRON_OUT_VSYNC) == 0;
}

/**
 * Get 6-bit color value from output register
 * Returns: BBGGRR (2 bits each)
 */
static inline uint8_t gigatron_get_color(gigatron_t* cpu) {
    return cpu->out & 0x3F;
}

#ifdef __cplusplus
}
#endif

#endif /* GIGATRON_H */
