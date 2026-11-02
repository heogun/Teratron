#ifndef TTL_FRONTEND_CONTROLLER_H
#define TTL_FRONTEND_CONTROLLER_H
#include "machine.h"
#include "forth_abi.h"
#include <cstdint>
#include <deque>
#include <memory>
#include <string>

class FrontendController {
public:
    FrontendController();
    ~FrontendController();
    FrontendController(const FrontendController&) = delete;
    FrontendController& operator=(const FrontendController&) = delete;
    bool load(const std::string& path);
    bool export_flash(const std::string& path);
    bool reset();
    bool text(const std::string& text, bool newline = false);
    void run(uint64_t ticks);
    void step();
    ttl_machine_t& machine() { return *board; }
    const ttl_machine_t& machine() const { return *board; }
    uint8_t byte(unsigned address) const { return board->cpu.ram[address]; }
    uint16_t cell(unsigned address) const;
    bool loaded = false;
    bool running = true;
    bool breakpoint_enabled = false;
    uint16_t breakpoint_pc = 0;
    uint8_t breakpoint_bank = 0;
    size_t queued() const { return keys.size(); }
    std::string message;
    std::string console;
private:
    void observe();
    std::unique_ptr<ttl_machine_t> board;
    std::deque<uint8_t> keys;
    uint16_t observed = 0x4000;
    uint64_t next_key = 0;
};
#endif
