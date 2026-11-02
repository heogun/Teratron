#include "controller.h"
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <stdexcept>
#include <vector>

FrontendController::FrontendController() : board(std::make_unique<ttl_machine_t>()) {
    if (!ttl_init(board.get())) throw std::runtime_error("Cannot initialize TTL machine");
}
FrontendController::~FrontendController() { ttl_shutdown(board.get()); }
uint16_t FrontendController::cell(unsigned address) const {
    return static_cast<uint16_t>(byte(address) | (byte(address + 1) << 8));
}
bool FrontendController::load(const std::string& path) {
    if (board->flash.remaining || (loaded && byte(TTL_FW_STATUS) == 4)) {
        message = "Cannot load while firmware SAVE is running"; return false;
    }
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file || file.tellg() != FLASH_SIZE) {
        message = "Flash image must be exactly 512 KiB: " + path; return false;
    }
    std::vector<uint8_t> data(FLASH_SIZE);
    file.seekg(0);
    if (!file.read(reinterpret_cast<char*>(data.data()), data.size())) {
        message = "Cannot read flash image: " + path; return false;
    }
    std::copy(data.begin(), data.end(), board->flash.bytes);
    loaded = true;
    if (!reset()) return false;
    message = "Loaded " + path;
    return true;
}
bool FrontendController::export_flash(const std::string& path) {
    if (!loaded || board->flash.remaining || byte(TTL_FW_STATUS) == 4) {
        message = "Wait for firmware SAVE to finish before exporting"; return false;
    }
    const std::string temp = path + ".tmp";
    // Exclusive temporary file: never overwrite an unrelated existing .tmp.
    FILE* f = std::fopen(temp.c_str(), "wbx");
    if (!f) { message = "Cannot create temporary export: " + temp; return false; }
    bool ok = std::fwrite(board->flash.bytes, 1, FLASH_SIZE, f) == FLASH_SIZE;
    if (std::fclose(f)) ok = false;
    if (ok && std::rename(temp.c_str(), path.c_str())) ok = false;
    if (!ok) std::remove(temp.c_str());
    message = ok ? "Exported current flash (run SAVE to persist I-RAM first): " + path
                 : "Flash export failed: " + path;
    return ok;
}
bool FrontendController::reset() {
    if (!loaded) { message = "Load an image first"; return false; }
    if (board->flash.remaining || byte(TTL_FW_STATUS) == 4) {
        message = "Cannot reset while firmware SAVE is running"; return false;
    }
    if (!ttl_power_on(board.get())) return false;
    keys.clear(); console.clear(); observed = 0x4000; next_key = 0;
    message = "Power-on: flash -> I-RAM; data RAM cleared";
    return true;
}
bool FrontendController::text(const std::string& input, bool newline) {
    if (!loaded) { message = "Load an image first"; return false; }
    if (newline && input.size() > 126) { message = "FORTH input limit is 126 characters"; return false; }
    std::deque<uint8_t> encoded;
    const std::string characters = input + (newline ? "\n" : "");
    for (unsigned char ch : characters) {
        if (ch >= 'a' && ch <= 'z') ch = static_cast<unsigned char>(ch - 'a' + 'A');
        if (ch >= 128 || !ttl_fw_ascii_scan[ch]) {
            message = "Only supported ASCII / PS/2 set-2 input is accepted"; return false;
        }
        if (ttl_fw_ascii_shift[ch]) encoded.push_back(0x12);
        encoded.push_back(ttl_fw_ascii_scan[ch]);
        if (ttl_fw_ascii_shift[ch]) { encoded.push_back(0xf0); encoded.push_back(0x12); }
    }
    if (keys.size() + encoded.size() > 4096) { message = "Keyboard input queue is full"; return false; }
    keys.insert(keys.end(), encoded.begin(), encoded.end());
    return true;
}
void FrontendController::observe() {
    const uint16_t cursor = cell(TTL_FW_OUTPTR);
    if (cursor < 0x4000 || cursor >= 0x5000) return;
    // CPU updates the little-endian pointer in multiple instructions. Do not
    // interpret a transient xxFF -> xx00 low-byte carry as a full ring wrap.
    if (cursor < observed && !(observed >= 0x4f00 && cursor < 0x4100)) return;
    while (observed != cursor) {
        console.push_back(static_cast<char>(byte(observed)));
        if (++observed == 0x5000) observed = 0x4000;
    }
    if (console.size() > 32768) console.erase(0, console.size() - 32768);
}
void FrontendController::run(uint64_t ticks) {
    if (!loaded || !running) return;
    while (ticks && running) {
        if (!board->booting && !keys.empty() && !(board->key_status & TTL_KEY_READY) && board->ticks >= next_key) {
            ttl_inject_scan(board.get(), keys.front()); keys.pop_front(); next_key = board->ticks + 2000;
        }
        const uint32_t chunk = static_cast<uint32_t>(std::min<uint64_t>(ticks, 2000));
        for (uint32_t i = 0; i < chunk; ++i) {
            if (breakpoint_enabled && !board->booting && board->exec_bank == breakpoint_bank && board->cpu.pc == breakpoint_pc) {
                running = false; message = "PC breakpoint reached"; break;
            }
            ttl_tick(board.get());
        }
        ticks -= chunk;
        observe();
    }
}
void FrontendController::step() {
    running = false;
    if (loaded) { ttl_tick(board.get()); observe(); }
}
