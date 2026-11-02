#ifndef TTL_MACHINE_H
#define TTL_MACHINE_H
#include "gigatron.h"
#include "vga.h"
#include "audio.h"
#include "flash.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TTL_MEMORY_SIZE (512u * 1024u)
#define TTL_DATA_SIZE 65536u
#define TTL_BANK_SIZE 65536u
#define TTL_IO_BASE 0xff00u
enum {
    TTL_MEMORY_BANK = TTL_IO_BASE, TTL_EXEC_BANK, TTL_WINDOW,
    TTL_KEY_DATA, TTL_KEY_STATUS, TTL_POWER_STATUS, TTL_FLASH_STATUS,
    TTL_PORT_LO, TTL_PORT_HI, TTL_PORT_DATA, TTL_PORT_TARGET
};
enum { TTL_WINDOW_DATA, TTL_WINDOW_IRAM, TTL_WINDOW_FLASH };
enum { TTL_KEY_READY = 1, TTL_KEY_FRAME_ERROR = 2, TTL_KEY_OVERRUN = 4 };
typedef struct {
    gigatron_t cpu;
    vga_t vga;
    audio_t audio;
    ttl_flash_t flash;
    uint8_t memory_bank, exec_bank, window;
    uint16_t port_address;
    uint8_t port_target;
    uint8_t key_data, key_status, key_bits;
    uint16_t key_shift;
    bool ps2_clock, power_fail, booting;
    uint32_t key_idle, boot_offset;
    uint64_t ticks;
} ttl_machine_t;

bool ttl_init(ttl_machine_t* m);
void ttl_shutdown(ttl_machine_t* m);
bool ttl_power_on(ttl_machine_t* m);
void ttl_tick(ttl_machine_t* m);
void ttl_run(ttl_machine_t* m, uint64_t cycles);
uint8_t ttl_read(ttl_machine_t* m, uint16_t address);
void ttl_write(ttl_machine_t* m, uint16_t address, uint8_t value);
void ttl_ps2_lines(ttl_machine_t* m, bool clock, bool data);
void ttl_inject_scan(ttl_machine_t* m, uint8_t scan);
void ttl_power_fail(ttl_machine_t* m);
/* Halted debugger convenience, NOT an automatic hardware save engine.
 * Uses flash command sequences/timing. Saves I-RAM only; data RAM is volatile. */
bool ttl_checkpoint(ttl_machine_t* m);
#ifdef __cplusplus
}
#endif
#endif
