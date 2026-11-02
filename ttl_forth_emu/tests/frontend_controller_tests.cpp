#include "controller.h"
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <fstream>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); std::exit(1); } } while (0)
int main(int argc, char** argv) {
    CHECK(argc == 2);
    FrontendController c;
    CHECK(c.load(argv[1]));
    CHECK(c.text(": SQUARE DUP * ;", true));
    CHECK(c.text("12 SQUARE .", true));
    for (unsigned i = 0; i < 500 && c.console.find("144 ") == std::string::npos; ++i) c.run(20000);
    CHECK(c.console.find("144 ") != std::string::npos);
    CHECK(c.byte(TTL_FW_ERROR) == 0 && c.queued() == 0);
    for (unsigned i = 0; i < 30; ++i) CHECK(c.text("WORDS", true));
    for (unsigned i = 0; i < 2500 && (c.queued() || c.byte(TTL_FW_STATUS) != 2 || c.byte(TTL_FW_INLEN)); ++i)
        c.run(20000);
    if (c.console.size() <= 4096 || c.console.find('\0') != std::string::npos)
        std::fprintf(stderr, "Transcript size=%zu nul=%zu queued=%zu status=%u error=%u\n",
            c.console.size(), c.console.find('\0'), c.queued(), c.byte(TTL_FW_STATUS), c.byte(TTL_FW_ERROR));
    CHECK(c.console.size() > 4096 && c.console.find('\0') == std::string::npos);
    const auto ticks = c.machine().ticks;
    c.running = false; c.run(1000); CHECK(c.machine().ticks == ticks);
    c.step(); CHECK(c.machine().ticks == ticks + 1);
    CHECK(!c.text(std::string(127, '1'), true));
    CHECK(!c.text("\xff", false));
    CHECK(c.queued() == 0);
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::string exported = std::string(argv[1]) + ".native-test-" + std::to_string(stamp) + ".flash";
    CHECK(c.export_flash(exported));
    {
        std::ifstream file(exported, std::ios::binary | std::ios::ate);
        CHECK(file && file.tellg() == FLASH_SIZE);
    }
    CHECK(c.load(exported));
    CHECK(std::remove(exported.c_str()) == 0);
    CHECK(c.reset()); CHECK(c.console.empty() && c.machine().booting);
    c.breakpoint_enabled = true; c.breakpoint_pc = 0; c.breakpoint_bank = 0;
    c.running = true; c.run(600000);
    CHECK(!c.running && c.machine().cpu.cycles == 0 && !c.machine().booting);
    c.step(); CHECK(c.machine().cpu.cycles == 1);
    std::puts("Native frontend controller checks passed.");
}
