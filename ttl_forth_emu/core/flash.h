#ifndef TTL_FLASH_H
#define TTL_FLASH_H
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define FLASH_SIZE (512u * 1024u)
#define FLASH_SECTOR_SIZE 4096u
/* Deterministic timing budget at 6.25 MHz; not an analog timing model. */
#define FLASH_PROGRAM_CYCLES 125u
#define FLASH_ERASE_CYCLES 156250u
#define FLASH_CHIP_ERASE_CYCLES 625000u
typedef struct {
    uint8_t bytes[FLASH_SIZE];
    unsigned sequence;
    bool id_mode, error, toggle;
    uint32_t remaining, address, length;
    uint8_t value;
} ttl_flash_t;
void flash_init(ttl_flash_t* f);
uint8_t flash_read(ttl_flash_t* f, uint32_t address);
void flash_write(ttl_flash_t* f, uint32_t address, uint8_t value);
void flash_tick(ttl_flash_t* f);
#ifdef __cplusplus
}
#endif
#endif
