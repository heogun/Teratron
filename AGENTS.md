# Workspace boundaries

- There is no root build. `gigatron_emu/` is the stock C/C++ emulator; `ttl_forth_emu/` is the independent modified-machine hardware emulator (see its `AGENTS.md` and provisional interface in `README.md`). `gigatron_rom/` contains ROM assembly, applications, compilers, and independent contributed projects. Run commands in the indicated subtree.
- `hardware_specification.md` specifies a **modified FORTH-native design**, not the stock machine implemented by the emulator/ROM sources. For hardware design, preserve its discrete 74HC/DIP-only constraint (no FPGA/PLD/MCU helpers), writable instruction RAM bootstrapped from flash, and pure-TTL PS/2 input instead of serial controller circuitry. Do not assume the reference schematics already implement these changes.
- `Gigatron Rev. 2012 OSH/` contains reference KiCad sources and fabrication outputs; the root PDFs are additional reference material.
- The modified machine uses fixed **64KiB unbanked data RAM**; flash persists **I-RAM only**, not data RAM. These user-confirmed changes are reflected in `hardware_specification.md`.

## Emulator

Commands below run from the workspace root:

```sh
cmake -S gigatron_emu -B gigatron_emu/build -DGIGAEMU_BUILD_RAYLIB_DEMO=OFF
cmake --build gigatron_emu/build --target gigatron_core
cmake --build gigatron_emu/build --target gigatron_sokol_imgui
```

- Requires CMake 3.16+, C11, and C++20. Raylib is enabled by default and fetched from GitHub at configure time (tag `5.5`); disable it as above to avoid that download. To build it, configure with `-DGIGAEMU_BUILD_RAYLIB_DEMO=ON` and build target `gigatron_raylib`.
- Actual executables are `build/frontend/sokol_imgui/gigatron_sokol_imgui` and `build/frontend/raylib/gigatron_raylib`, **not** `gigatron_emu` as the README suggests. Launch from the corresponding executable directory: both load `roms/gigatron.rom` relative to the working directory. CMake copies bundled ROMs there; rebuilding `gigatron_rom/` does not update the emulator's bundled ROM automatically.
- `core/` is frontend-independent C. The real GUI entrypoints are `frontend/sokol_imgui/main.cpp` and `frontend/raylib/main.c`; changes to input or execution flow may need both.
- Preserve per-cycle order: CPU, VGA, audio, then loader when active. Input is active-low; the loader owns `cpu.in_reg` while active, so gamepad updates must not overwrite it.
- CPU initialization randomizes RAM; reset only resets registers/counters, not RAM. Do not assume zero-filled or repeatable memory in checks.
- No emulator test target is defined. A core-only build is a focused compile check, not a GUI or emulation correctness test.

## ROM and toolchain

Run these commands from `gigatron_rom/` (paths and interface bindings depend on that working directory):

- `make`: builds development `dev.rom` and links `gigatron.rom` to it on Unix. `make ROMv6.rom` builds the frozen release instead. Use Make rather than invoking ROM scripts directly: it supplies application-extension `PYTHONPATH` and the embedded asset list.
- `Core/*.asm.py` are executable Python assembly sources backed by `Core/asm.py`; generated `.rom` and `.lst` files are outputs, not the place to edit instructions.
- Focused GCL compilation: `python3 Core/compilegcl.py Apps/Blinky/Blinky.gcl <existing-output-directory>`. Default bindings come from root `interface.json`; `-x` additionally loads `Core/interface-dev.json` and emits `.gt1x`.
- `make test` runs the console reference emulator `Docs/gtemu` over roughly 30 seconds of emulated time, searching for hSync errors (`~`). It ends with `grep`, so **no matches produces a nonzero status**; inspect output rather than treating the exit code as a conventional test result.
- `make compiletest` recompiles all `Apps/*/*.gcl` **in place**, potentially overwriting supplied `.gt1` files. It is a regeneration check, not a read-only suite; compare outputs afterward.
- The maintained C compiler is separately built with `make -C Compilers/glcc`; its suite is `make -C Compilers/glcc test` (iterates bundled ROM versions). Root `make lcc` still points at legacy `Utils/lcc`, whereas archived LCC sources live under `Archive/lcc/`; do not use that target for GLCC.
- `Contrib/` projects have their own build/test requirements; root ROM verification does not cover them. Consult the relevant local README/Makefile before running their suites.
