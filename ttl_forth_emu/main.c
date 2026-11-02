#include "machine.h"
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(const char* name) {
    fprintf(stderr,
        "Usage: %s (--flash IMAGE | --demo) [--cycles N] [--scan HEX]\n"
        "       [--power-fail-at N] [--checkpoint IMAGE] [--frame IMAGE.ppm]\n"
        "IMAGE is exactly 524288 bytes, big-endian instruction words.\n"
        "Cycles exclude DMA boot. --scan supplies one raw PS/2 set-2 byte.\n"
        "--checkpoint is a halted debugger I-RAM save, not firmware.\n", name);
}

static bool number(const char* s, unsigned base, uint64_t* result) {
    if (!*s || *s == '-') return false;
    errno = 0;
    char* end;
    unsigned long long value = strtoull(s, &end, (int)base);
    if (errno || *end) return false;
    *result = (uint64_t)value;
    return true;
}

static bool load_flash(ttl_machine_t* m, const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    bool ok = fread(m->flash.bytes, 1, FLASH_SIZE, f) == FLASH_SIZE;
    if (ok) ok = fgetc(f) == EOF && !ferror(f);
    if (fclose(f)) ok = false;
    return ok;
}

static bool save_flash(ttl_machine_t* m, const char* path) {
    /* Write beside the destination and rename only after a complete write. */
    size_t size = strlen(path) + 5;
    char* temp = malloc(size);
    if (!temp) return false;
    snprintf(temp, size, "%s.tmp", path);
    FILE* f = fopen(temp, "wbx");
    bool ok = f != NULL;
    if (f) {
        ok = fwrite(m->flash.bytes, 1, FLASH_SIZE, f) == FLASH_SIZE;
        if (fclose(f)) ok = false;
        if (ok && rename(temp, path)) ok = false;
        if (!ok) remove(temp);
    }
    free(temp);
    return ok;
}

static bool save_frame(ttl_machine_t* m, const char* path) {
    FILE* f = fopen(path, "wb");
    if (!f) return false;
    bool ok = fprintf(f, "P6\n%u %u\n255\n", m->vga.width, m->vga.height) > 0;
    for (uint32_t i = 0; ok && i < m->vga.width * m->vga.height; ++i)
        ok = fwrite(m->vga.pixels + i * 4, 1, 3, f) == 3;
    if (fclose(f)) ok = false;
    return ok;
}

int main(int argc, char** argv) {
    const char *image = NULL, *checkpoint = NULL, *frame = NULL;
    bool demo = false, scan_given = false;
    uint64_t cycles = 100000, scan = 0, fail_at = UINT64_MAX;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--help")) { usage(argv[0]); return 0; }
        if (!strcmp(argv[i], "--demo")) { demo = true; continue; }
        if (i + 1 >= argc) { usage(argv[0]); return 2; }
        const char* option = argv[i++];
        const char* value = argv[i];
        if (!strcmp(option, "--flash")) image = value;
        else if (!strcmp(option, "--checkpoint")) checkpoint = value;
        else if (!strcmp(option, "--frame")) frame = value;
        else if (!strcmp(option, "--cycles")) {
            if (!number(value, 10, &cycles)) { usage(argv[0]); return 2; }
        } else if (!strcmp(option, "--power-fail-at")) {
            if (!number(value, 10, &fail_at)) { usage(argv[0]); return 2; }
        } else if (!strcmp(option, "--scan")) {
            if (!number(value, 16, &scan) || scan > 255) { usage(argv[0]); return 2; }
            scan_given = true;
        } else { usage(argv[0]); return 2; }
    }
    if ((image != NULL) == demo) { usage(argv[0]); return 2; }
    ttl_machine_t* m = malloc(sizeof(*m));
    if (!m) return 1;
    if (!ttl_init(m)) { free(m); return 1; }
    int result = 1;
    if (demo) {
        /* LD $42; ST AC,[$00]; LD $00,Y; JMP Y:$00; delay-slot NOP. */
        const uint8_t code[] = {0x00,0x42, 0xc2,0x00, 0x14,0x00, 0xe0,0x00, 0x02,0x00};
        memcpy(m->flash.bytes, code, sizeof(code));
    } else if (!load_flash(m, image)) {
        fprintf(stderr, "Cannot read exact 512 KiB flash image: %s\n", image);
        goto done;
    }
    ttl_run(m, FLASH_SIZE);
    if (scan_given) ttl_inject_scan(m, (uint8_t)scan);
    for (uint64_t i = 0; i < cycles; ++i) {
        if (i == fail_at) ttl_power_fail(m);
        ttl_tick(m);
    }
    if (checkpoint && (!ttl_checkpoint(m) || !save_flash(m, checkpoint))) {
        fprintf(stderr, "Checkpoint failed (flash busy or file write failed).\n");
        goto done;
    }
    if (frame && !save_frame(m, frame)) {
        fprintf(stderr, "Cannot write framebuffer: %s\n", frame);
        goto done;
    }
    printf("ticks=%" PRIu64 " cpu_cycles=%" PRIu64
           " pc=%04x ac=%02x ram[0]=%02x power_fail=%u\n",
           m->ticks, m->cpu.cycles, m->cpu.pc, m->cpu.ac,
           m->cpu.ram[0], (unsigned)m->power_fail);
    result = 0;
done:
    ttl_shutdown(m);
    free(m);
    return result;
}
