/* Native desktop frontend. Rendering/input only: FORTH runs on ttl_machine. */
#define SOKOL_IMPL
#define SOKOL_METAL
#include "sokol_app.h"
#include "sokol_gfx.h"
#include "sokol_audio.h"
#include "sokol_glue.h"
#include "sokol_log.h"
#define SOKOL_IMGUI_IMPL
#include "imgui.h"
#include "util/sokol_imgui.h"
#include "controller.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <vector>
#include <mach-o/dyld.h>

static struct {
    std::unique_ptr<FrontendController> controller;
    sg_image image;
    sg_view view;
    sg_sampler sampler;
    bool graphics = false;
    bool keyboard = false;
    bool mute = false;
    bool smoke = false;
    bool smoke_queued = false;
    bool smoke_ok = false;
    bool export_on_exit = true;
    bool quit_after_save = false;
    unsigned smoke_frames = 0;
    unsigned memory_address = 0;
    int memory_kind = 0;
    float speed = 1.0f;
    double fraction = 0;
    char image_path[1024] = {};
    char export_path[1024] = "machine.flash";
    char input[128] = {};
} state;

static void draw_controls() {
    auto& c = *state.controller;
    auto& m = c.machine();
    if (ImGui::Button(c.running ? "Pause (F5)" : "Run (F5)")) c.running = !c.running;
    ImGui::SameLine(); if (ImGui::Button("Step (F6)")) c.step();
    ImGui::SameLine(); if (ImGui::Button("Power on (F9)")) c.reset();
    ImGui::SameLine();
    ImGui::BeginDisabled(c.byte(TTL_FW_STATUS) != 2 || c.byte(TTL_FW_MODE) || c.byte(TTL_FW_INLEN) || c.queued());
    if (ImGui::Button("Firmware SAVE")) c.text("SAVE", true);
    ImGui::EndDisabled();
    ImGui::SameLine(); ImGui::Checkbox("Mute", &state.mute);
    ImGui::SetNextItemWidth(130); ImGui::SliderFloat("Speed", &state.speed, 0.25f, 8.0f, "%.2fx");
    ImGui::SameLine(); ImGui::Text("PS/2 queued: %zu | %s", c.queued(), m.booting ? "DMA boot" : "CPU executing");
    ImGui::SetNextItemWidth(std::max(180.0f, ImGui::GetContentRegionAvail().x - 90));
    ImGui::InputText("##flash_path", state.image_path, sizeof(state.image_path));
    ImGui::SameLine(); if (ImGui::Button("Load flash")) c.load(state.image_path);
    ImGui::SetNextItemWidth(std::max(180.0f, ImGui::GetContentRegionAvail().x - 105));
    ImGui::InputText("##export_path", state.export_path, sizeof(state.export_path));
    ImGui::SameLine(); if (ImGui::Button("Export flash")) c.export_flash(state.export_path);
    ImGui::Checkbox("Export saved flash on normal exit", &state.export_on_exit);
    ImGui::SameLine(); ImGui::TextDisabled("%s", c.flash_dirty() ? "Chip changed; image file needs update" : "Image file matches chip");
    ImGui::TextWrapped("%s", c.message.c_str());
    ImGui::TextDisabled("SAVE runs CPU firmware. Export downloads current flash only. Reset clears data RAM. Drop a .flash file to load.");
    if (c.loaded && c.byte(TTL_FW_STATUS) == 4)
        ImGui::TextColored(ImVec4(1, 0.75f, 0.3f, 1), "Native flash erase/program/verify running. Keep power on; video refresh is paused.");
    ImGui::Separator();
}

static void draw_display() {
    auto& c = *state.controller;
    ImGui::Text("VGA framebuffer - actual CPU OUT signal (click for keyboard input)");
    const ImVec2 available = ImGui::GetContentRegionAvail();
    float height = std::max(120.0f, std::min(available.x * 0.75f, available.y * 0.57f));
    ImGui::Image(simgui_imtextureid_with_sampler(state.view, state.sampler), ImVec2(height / 0.75f, height));
    if (ImGui::IsItemClicked()) state.keyboard = true;
    ImGui::Checkbox("Send keyboard to machine", &state.keyboard);
    ImGui::SameLine(); ImGui::TextDisabled("ASCII / Enter / Backspace; calculation and SAVE pause cooperative video.");
    ImGui::Text("Firmware RAM transcript (debug view, not a UART)");
    ImGui::BeginChild("transcript", ImVec2(0, std::max(70.0f, ImGui::GetContentRegionAvail().y - 90)), true);
    const bool follow = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 2;
    ImGui::TextUnformatted(c.console.data(), c.console.data() + c.console.size());
    if (follow) ImGui::SetScrollHereY(1);
    ImGui::EndChild();
    ImGui::SetNextItemWidth(std::max(100.0f, ImGui::GetContentRegionAvail().x - 65));
    bool enter = ImGui::InputText("##command", state.input, sizeof(state.input), ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine(); enter |= ImGui::Button("Enter");
    if (enter && c.text(state.input, true)) { state.input[0] = 0; ImGui::SetKeyboardFocusHere(-1); }
    if (ImGui::Button(": SQUARE DUP * ;")) std::snprintf(state.input, sizeof(state.input), ": SQUARE DUP * ;");
    ImGui::SameLine(); if (ImGui::Button("12 SQUARE .")) std::snprintf(state.input, sizeof(state.input), "12 SQUARE .");
    ImGui::SameLine(); if (ImGui::Button("WORDS")) c.text("WORDS", true);
}

static void draw_debugger() {
    auto& c = *state.controller;
    auto& m = c.machine();
    ImGui::Text("CPU - 6.25 MHz | 64 KiB unbanked RAM");
    ImGui::Text("PC $%04X  next $%04X  bank %u", m.cpu.pc, m.cpu.next_pc, m.exec_bank);
    ImGui::Text("AC $%02X  X $%02X  Y $%02X", m.cpu.ac, m.cpu.x, m.cpu.y);
    ImGui::Text("OUT $%02X  XOUT $%02X", m.cpu.out, m.cpu.outx);
    ImGui::Text("CPU cycles %llu | board ticks %llu", (unsigned long long)m.cpu.cycles, (unsigned long long)m.ticks);
    ImGui::Text("DMA %u / %u | Flash busy %u ticks", m.boot_offset, FLASH_SIZE, m.flash.remaining);
    const uint16_t ir = m.cpu.rom[(uint32_t)m.exec_bank * TTL_BANK_SIZE + m.cpu.pc];
    ImGui::Text("IR $%04X  OP %u MODE %u BUS %u D $%02X", ir, ir >> 13, (ir >> 10) & 7, (ir >> 8) & 3, ir & 255);
    ImGui::Text("Port bank %u address $%04X target %u", m.memory_bank, m.port_address, m.port_target);
    ImGui::Text("PS/2 status $%02X", m.key_status);
    ImGui::Text("FW status %u error %u compile %u diag $%02X", c.byte(TTL_FW_STATUS), c.byte(TTL_FW_ERROR), c.byte(TTL_FW_MODE), c.byte(TTL_FW_DIAG));
    ImGui::Text("Dictionary head $%04X here $%04X", c.cell(TTL_FW_HEAD), c.cell(TTL_FW_HERE));
    ImGui::Separator();
    ImGui::Checkbox("PC breakpoint", &c.breakpoint_enabled);
    ImGui::SameLine(); ImGui::SetNextItemWidth(75);
    ImGui::InputScalar("PC", ImGuiDataType_U16, &c.breakpoint_pc, nullptr, nullptr, "%04X", ImGuiInputTextFlags_CharsHexadecimal);
    ImGui::SameLine(); ImGui::SetNextItemWidth(45);
    ImGui::InputScalar("Bank", ImGuiDataType_U8, &c.breakpoint_bank);
    c.breakpoint_bank &= 3;
    ImGui::TextDisabled("Step bypasses the breakpoint once. During DMA, step advances one boot byte.");
    ImGui::Separator();
    if (ImGui::BeginTabBar("debug_tabs")) {
        if (ImGui::BeginTabItem("Stacks")) {
            ImGui::Text("Data stack: %u cells (bottom -> top)", c.byte(TTL_FW_SP) / 2);
            for (unsigned i = 0; i + 1 < std::min<unsigned>(c.byte(TTL_FW_SP), 240); i += 2) {
                const auto value = c.cell(0x200 + i);
                ImGui::Text("%3u  $%04X  %6d", i / 2, value, (int)(int16_t)value);
            }
            ImGui::Separator();
            ImGui::Text("VM return stack: %u cells", c.byte(TTL_FW_RP) / 2);
            for (unsigned i = 0; i + 1 < std::min<unsigned>(c.byte(TTL_FW_RP), 240); i += 2)
                ImGui::Text("%3u  IP $%04X", i / 2, c.cell(0x300 + i));
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Memory")) {
            ImGui::Combo("Physical memory", &state.memory_kind, "Data RAM\0I-RAM\0Flash\0");
            ImGui::SetNextItemWidth(100);
            ImGui::InputScalar("Byte address", ImGuiDataType_U32, &state.memory_address, nullptr, nullptr, "%05X", ImGuiInputTextFlags_CharsHexadecimal);
            const unsigned size = state.memory_kind ? TTL_MEMORY_SIZE : TTL_DATA_SIZE;
            state.memory_address = std::min(state.memory_address, size - 256) & ~15u;
            ImGui::TextDisabled("Physical bytes, read-only. No MMIO read side effects.");
            for (unsigned row = 0; row < 16; ++row) {
                const unsigned start = state.memory_address + row * 16;
                char line[100]; int offset = std::snprintf(line, sizeof(line), "%05X: ", start);
                for (unsigned col = 0; col < 16; ++col) {
                    unsigned address = start + col;
                    uint8_t byte = state.memory_kind == 0 ? m.cpu.ram[address]
                        : state.memory_kind == 2 ? m.flash.bytes[address]
                        : (uint8_t)(address & 1 ? m.cpu.rom[address / 2] : m.cpu.rom[address / 2] >> 8);
                    offset += std::snprintf(line + offset, sizeof(line) - offset, "%02X ", byte);
                }
                ImGui::TextUnformatted(line);
            }
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

static void init() {
    try { state.controller = std::make_unique<FrontendController>(); }
    catch (const std::exception& error) { std::fprintf(stderr, "%s\n", error.what()); sapp_request_quit(); return; }
    sg_desc desc = {}; desc.environment = sglue_environment(); desc.logger.func = slog_func; sg_setup(&desc);
    simgui_desc_t gui = {}; gui.logger.func = slog_func; simgui_setup(&gui);
    saudio_desc sound = {}; sound.sample_rate = AUDIO_SAMPLE_RATE; sound.num_channels = 1; sound.logger.func = slog_func;
    saudio_setup(&sound);
    sg_image_desc image = {}; image.width = VGA_WIDTH; image.height = VGA_HEIGHT;
    image.pixel_format = SG_PIXELFORMAT_RGBA8; image.usage.stream_update = true;
    state.image = sg_make_image(&image);
    sg_view_desc view = {}; view.texture.image = state.image; state.view = sg_make_view(&view);
    sg_sampler_desc sampler = {}; sampler.min_filter = sampler.mag_filter = SG_FILTER_NEAREST;
    sampler.wrap_u = sampler.wrap_v = SG_WRAP_CLAMP_TO_EDGE; state.sampler = sg_make_sampler(&sampler);
    state.graphics = true;
    ImGui::GetIO().IniFilename = nullptr; // Do not write imgui.ini into the launch directory.
    state.controller->load(state.image_path);
}

static void frame() {
    if (!state.controller || !state.graphics) return;
    auto& c = *state.controller;
    auto& m = c.machine();
    const double elapsed = std::clamp(sapp_frame_duration(), 0.0, 0.05);
    if (c.running) {
        state.fraction += elapsed * GIGATRON_HZ * state.speed;
        uint64_t ticks = state.smoke ? 500000 : std::min<uint64_t>((uint64_t)state.fraction, 2000000);
        state.fraction -= std::min<double>(state.fraction, (double)ticks);
        c.run(ticks);
    } else state.fraction = 0;
    if (state.quit_after_save) {
        const auto result = c.prepare_close(state.export_path, state.export_on_exit);
        if (result != FrontendController::CloseResult::Busy) {
            state.quit_after_save = false;
            if (result == FrontendController::CloseResult::Ready) sapp_quit();
            else c.running = false; /* Let the user correct the export path. */
        }
    }
    if (state.smoke) {
        ++state.smoke_frames;
        if (c.loaded && c.byte(TTL_FW_STATUS) == 2 && !state.smoke_queued) {
            c.text(": SQUARE DUP * ;", true); c.text("12 SQUARE .", true); state.smoke_queued = true;
        }
        if (state.smoke_queued && c.console.find("144 ") != std::string::npos && (c.byte(TTL_FW_DIAG) & 16)) {
            state.smoke_ok = true; sapp_request_quit();
        } else if (state.smoke_frames > 120) sapp_request_quit();
    }
    // Push API avoids racing the emulator's audio ring from an audio callback.
    if (saudio_isvalid()) {
        float samples[1024]; int expected = std::min(saudio_expect(), 1024);
        uint32_t count = expected > 0 ? audio_read_samples(&m.audio, samples, (uint32_t)expected) : 0;
        if (state.mute) std::fill(samples, samples + count, 0.0f);
        if (count) saudio_push(samples, (int)count);
    }
    sg_image_data data = {}; data.mip_levels[0].ptr = m.vga.pixels;
    data.mip_levels[0].size = (size_t)VGA_WIDTH * VGA_HEIGHT * 4;
    sg_update_image(state.image, &data);
    simgui_frame_desc_t gui = {}; gui.width = sapp_width(); gui.height = sapp_height();
    gui.delta_time = sapp_frame_duration(); gui.dpi_scale = sapp_dpi_scale(); simgui_new_frame(&gui);
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::Begin("TTL FORTH", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
    draw_controls();
    if (ImGui::BeginTable("layout", 2, ImGuiTableFlags_Resizable)) {
        ImGui::TableSetupColumn("Display", ImGuiTableColumnFlags_WidthStretch, 2);
        ImGui::TableSetupColumn("Debugger", ImGuiTableColumnFlags_WidthStretch, 1);
        ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); draw_display();
        ImGui::TableSetColumnIndex(1); draw_debugger(); ImGui::EndTable();
    }
    ImGui::End();
    sg_pass pass = {}; pass.swapchain = sglue_swapchain();
    pass.action.colors[0].load_action = SG_LOADACTION_CLEAR;
    pass.action.colors[0].clear_value = {0.06f, 0.08f, 0.11f, 1};
    sg_begin_pass(&pass); simgui_render(); sg_end_pass(); sg_commit();
}

static void cleanup() {
    if (state.graphics) {
        saudio_shutdown(); sg_destroy_view(state.view); sg_destroy_sampler(state.sampler); sg_destroy_image(state.image);
        simgui_shutdown(); sg_shutdown();
    }
    state.controller.reset();
    if (state.smoke) {
        std::fprintf(stderr, "Native GUI smoke test: %s\n", state.smoke_ok ? "PASS" : "FAIL");
        if (!state.smoke_ok) std::exit(1);
    }
}

static void event(const sapp_event* ev) {
    simgui_handle_event(ev);
    if (!state.controller) return;
    auto& c = *state.controller;
    if (ev->type == SAPP_EVENTTYPE_QUIT_REQUESTED && !state.smoke) {
        const auto result = c.prepare_close(state.export_path, state.export_on_exit);
        if (result == FrontendController::CloseResult::Busy) {
            sapp_cancel_quit(); state.quit_after_save = true; c.running = true;
            c.message = "Finishing explicit SAVE before closing (no automatic I-RAM save).";
        } else if (result == FrontendController::CloseResult::Error) {
            sapp_cancel_quit();
        }
        return;
    }
    if (ev->type == SAPP_EVENTTYPE_FILES_DROPPED && sapp_get_num_dropped_files()) {
        const char* path = sapp_get_dropped_file_path(0);
        std::snprintf(state.image_path, sizeof(state.image_path), "%s", path); c.load(path);
    }
    if (ev->type == SAPP_EVENTTYPE_KEY_DOWN && !ev->key_repeat) {
        switch (ev->key_code) {
        case SAPP_KEYCODE_F5: c.running = !c.running; return;
        case SAPP_KEYCODE_F6: c.step(); return;
        case SAPP_KEYCODE_F9: c.reset(); return;
        default: break;
        }
    }
    if (!state.keyboard || ImGui::GetIO().WantTextInput || (ev->modifiers & (SAPP_MODIFIER_CTRL | SAPP_MODIFIER_SUPER))) return;
    if (ev->type == SAPP_EVENTTYPE_CHAR && ev->char_code >= 32 && ev->char_code < 127)
        c.text(std::string(1, (char)ev->char_code));
    if (ev->type == SAPP_EVENTTYPE_KEY_DOWN) {
        if (ev->key_code == SAPP_KEYCODE_ENTER) c.text("\n");
        if (ev->key_code == SAPP_KEYCODE_BACKSPACE) c.text("\b");
    }
}

sapp_desc sokol_main(int argc, char** argv) {
    uint32_t size = 1024;
    std::vector<char> executable(size);
    if (_NSGetExecutablePath(executable.data(), &size)) {
        executable.resize(size);
        _NSGetExecutablePath(executable.data(), &size);
    }
    std::error_code error;
    auto actual_path = std::filesystem::weakly_canonical(executable.data(), error);
    if (error) actual_path = std::filesystem::absolute(argv[0]);
    const auto directory = actual_path.parent_path();
    const auto profile = directory / "machine.flash";
    std::snprintf(state.export_path, sizeof(state.export_path), "%s", profile.string().c_str());
    std::filesystem::path image = directory / "firmware/forth.flash";
    if (!std::filesystem::exists(image)) image = TTL_DEFAULT_FLASH;
    bool explicit_image = false;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--smoke-test")) state.smoke = true;
        else { image = argv[i]; explicit_image = true; }
    }
    if (!state.smoke && !explicit_image && std::filesystem::exists(profile)) image = profile;
    if (state.smoke) state.export_on_exit = false; /* Smoke tests must not touch a user profile. */
    std::snprintf(state.image_path, sizeof(state.image_path), "%s", image.string().c_str());
    sapp_desc desc = {}; desc.init_cb = init; desc.frame_cb = frame; desc.cleanup_cb = cleanup; desc.event_cb = event;
    desc.width = 1360; desc.height = 960; desc.window_title = "TTL FORTH - Native Emulator";
    desc.enable_dragndrop = true; desc.max_dropped_files = 1; desc.max_dropped_file_path_length = 1024;
    desc.high_dpi = true; desc.logger.func = slog_func;
    return desc;
}
