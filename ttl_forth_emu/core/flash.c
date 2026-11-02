#include "flash.h"
#include <string.h>

void flash_init(ttl_flash_t* f) {
    memset(f, 0, sizeof(*f));
    memset(f->bytes, 0xff, sizeof(f->bytes));
}

uint8_t flash_read(ttl_flash_t* f, uint32_t a) {
    a &= FLASH_SIZE - 1;
    if (f->remaining) {
        f->toggle = !f->toggle;
        /* DQ7 data polling and DQ6 toggle while busy. */
        return (uint8_t)((f->bytes[a] & 0x3f) |
               (f->toggle ? 0x40 : 0) | ((~f->value) & 0x80));
    }
    if (f->id_mode) {
        if ((a & 0xffff) == 0) return 0xbf;
        if ((a & 0xffff) == 1) return 0xb7;
        return 0xff;
    }
    return f->bytes[a];
}

static bool unlock_address(uint32_t a, uint32_t expected) {
    return (a & 0x7fff) == expected;
}

void flash_write(ttl_flash_t* f, uint32_t a, uint8_t v) {
    a &= FLASH_SIZE - 1;
    if (f->remaining) return;
    /* Program data, including F0, must not be mistaken for a reset command. */
    if (f->sequence == 3) {
        f->error = (f->bytes[a] & v) != v;
        f->address = a;
        f->length = 1;
        f->value = v;
        f->remaining = FLASH_PROGRAM_CYCLES;
        f->sequence = 0;
        return;
    }
    if (v == 0xf0) {
        f->id_mode = false;
        f->sequence = 0;
        return;
    }
    switch (f->sequence) {
    case 0:
        f->sequence = unlock_address(a, 0x5555) && v == 0xaa ? 1 : 0;
        break;
    case 1:
        f->sequence = unlock_address(a, 0x2aaa) && v == 0x55 ? 2 : 0;
        break;
    case 2:
        f->sequence = 0;
        if (!unlock_address(a, 0x5555)) break;
        if (v == 0xa0 && !f->id_mode) f->sequence = 3;
        else if (v == 0x80 && !f->id_mode) f->sequence = 4;
        else if (v == 0x90) f->id_mode = true;
        break;
    case 4:
        f->sequence = unlock_address(a, 0x5555) && v == 0xaa ? 5 : 0;
        break;
    case 5:
        f->sequence = unlock_address(a, 0x2aaa) && v == 0x55 ? 6 : 0;
        break;
    case 6:
        f->sequence = 0;
        if (v == 0x30 || (v == 0x10 && unlock_address(a, 0x5555))) {
            f->address = v == 0x30 ? a & ~(FLASH_SECTOR_SIZE - 1) : 0;
            f->length = v == 0x30 ? FLASH_SECTOR_SIZE : FLASH_SIZE;
            f->value = 0xff;
            f->error = false;
            f->remaining = v == 0x30 ? FLASH_ERASE_CYCLES : FLASH_CHIP_ERASE_CYCLES;
        }
        break;
    default: f->sequence = 0; break;
    }
}

void flash_tick(ttl_flash_t* f) {
    if (!f->remaining || --f->remaining) return;
    if (f->length == 1) f->bytes[f->address] &= f->value;
    else memset(f->bytes + f->address, 0xff, f->length);
}
