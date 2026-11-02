/* Opaque, size-independent interface for the browser GUI and firmware tests.
 * There are deliberately no host-side FORTH/evaluation callbacks. */
#include "machine.h"
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#define API __declspec(dllexport)
#else
#define API __attribute__((visibility("default")))
#endif
API ttl_machine_t* ttl_create(void) {
    ttl_machine_t* m = malloc(sizeof(*m));
    if (m && !ttl_init(m)) { free(m); return NULL; }
    return m;
}
API void ttl_destroy(ttl_machine_t* m) { if (m) { ttl_shutdown(m); free(m); } }
API int ttl_load_image(ttl_machine_t* m, const uint8_t* data, size_t size) {
    if (size != FLASH_SIZE || m->flash.remaining) return 0;
    memcpy(m->flash.bytes, data, size);
    return ttl_power_on(m);
}
API void ttl_advance(ttl_machine_t* m, uint64_t cycles) { ttl_run(m, cycles); }
API void ttl_scan(ttl_machine_t* m, uint8_t scan) { ttl_inject_scan(m, scan); }
API int ttl_reboot(ttl_machine_t* m) { return ttl_power_on(m); }
API const uint8_t* ttl_data(ttl_machine_t* m) { return m->cpu.ram; }
API const uint8_t* ttl_image(ttl_machine_t* m) { return m->flash.bytes; }
API const uint8_t* ttl_pixels(ttl_machine_t* m) { return m->vga.pixels; }
API uint64_t ttl_counter(ttl_machine_t* m) { return m->cpu.cycles; }
API uint16_t ttl_program_counter(ttl_machine_t* m) { return m->cpu.pc; }
API uint8_t ttl_accumulator(ttl_machine_t* m) { return m->cpu.ac; }
API int ttl_key_pending(ttl_machine_t* m) { return !!(m->key_status & TTL_KEY_READY); }
