# [System Specification] 8/16-Bit FORTH-Native Modified Harvard TTL Computer

## 0. Design Constraints & Rules

* **Allowed Components:** Standard 74HC series TTL logic ICs and DIP-packaged components exclusively.
* **Strict Prohibitions:** FPGA, CPLD, GAL, PLD, microcontrollers (such as ATtiny or Arduino auxiliary chips), and any SoC solutions are **strictly forbidden**. All control logic, decoding, and sequencing must be implemented using discrete TTL logic.
* **Primary Language Support:** The system natively supports and executes **FORTH** with runtime word compilation directly on the hardware.

---

## 1. Architectural Overview

* **Base Architecture:** Derived from the Gigatron TTL Microcomputer.
* **Modified Harvard Architecture:** 
  * Replaces traditional read-only control ROM with an **Instruction RAM (I-RAM)** coupled with **Flash Storage**, enabling Von Neumann-like runtime flexibility (e.g., defining and compiling new FORTH words on the fly).
   * Retains physical separation between instruction and data memory paths. CPU byte access to single-port I-RAM requires explicit access arbitration.
* **Performance Target:** Gigatron-equivalent **6.25 MHz**, 160ns clock cycle, with a 1-Clock / 1-Instruction goal. This is a requirement/target, not a verified timing result for the modified circuit; the runtime fetch/data schedule is still an architectural open item.

---

## 2. Memory Subsystem

* **Fixed Components:**
   * **Data RAM:** One AS6C1008-55PCN (128K x 8-bit, 55ns CMOS SRAM), using **64KiB only**, with A16 tied to ground. Data RAM is unbanked and volatile; CE2 must be held active for accesses.
   * **Instruction RAM:** Two AS6C4008-55PCN (each 512K x 8-bit, 55ns) in parallel, providing **512K words x 16 bits (1MiB)**, with separate opcode/operand byte lanes.
   * **Storage/Boot Flash:** Two SST39SF040-70-4C-PHE (each 512K x 8-bit, 70ns), sharing address lines and providing separate opcode/operand byte lanes, not tied-together data outputs.

Both I-RAM and Flash have **512K words x 16 bits (1MiB)**, using their full capacity. The 16-bit PC addresses 64K instruction words within a bank; three upper word-address bits select eight banks. The current emulator/firmware still uses a **512KiB logical I-RAM image** and has not yet been updated to this hardware capacity. Its image size, bank count, and DMA behavior must be aligned in a separate implementation change.

Address Space & Banking:

The core CPU uses 16-bit data addresses (64KiB) and a 16-bit instruction-word PC (64K words per bank). Data RAM uses its address space directly, without banking. I-RAM and Flash each use chip/word address lines A0–A18; a **single shared 74HC161** holds the upper bank address bits A16–A18, replacing the earlier 74HC273 proposal. It counts during boot and uses synchronous parallel load for software bank selection during normal operation.

Both Flash and I-RAM share a unified banking framework.

The shared 74HC161 is an **upper-address banking register/counter**, not a latch pair for the full 16-bit lower address. Do not add two PORT_LO/PORT_HI packages to the hardware BOM based on the emulator's provisional MMIO interface. Four additional 74HC157 packages select the lower 16 memory address bits between PC and CPU data address; boot reuses PC, so no third boot address source or dedicated lower-address DMA counter is specified. The original four data-RAM address multiplexers remain separate. A linear byte address's low bit selects the lane; it is not the paired chips' word-address A0. Flash command addresses are each chip's own address space. Separate execution/access bank storage and its software interface remain to be settled.

Use two 74HC245 byte-lane bridges between the CPU's 8-bit data bus and the two 8-bit instruction-memory lanes, with only the selected lane bridge enabled on a CPU read. Retain one additional 74HC138 for memory-access decoding. Direction is derived from CPU read/write control, not an assumed dedicated Gigatron R/W pin. Ordinary stores continue to data RAM; only explicitly selected accesses reach I-RAM or Flash. Reserve one XOUT LED bit as Flash write permission; it is not itself a write strobe. Exact port addresses, decoding equations, bus control matrix, and phase scheduling are documented as open items below.

Flash stores **I-RAM only**. Data RAM is neither backed up to Flash nor restored at boot.

---

3. I/O & Peripherals
VGA Graphics: Time-shared pixel pumping driven by CPU microcode during Active Video intervals, maintaining 160x120 retro graphics without dedicated graphic controllers.

Mono Audio: XOUT expansion register coupled with a Resistor Ladder DAC for 4-voice wavetable software sound mixing.

PS/2 Keyboard Input (Pure TTL):

Built entirely without helper microcontrollers.

Hardware Receiver Buffer: Dual 74HC595 shift registers configured with hardwired bit-slicing to route clean 8-bit scan codes directly onto the CPU data bus (D0∼D7).

Receiver control baseline: one 74HC161 counts the 11 frame bits, with completion feedback disabling its count-enable. Parity checking is **omitted**, in both hardware and software; the parity bit remains part of the received 11-bit frame. Completion must latch the data only once and preserve it until CPU consumption.

**PS/2 is on hold by user decision.** Preserve its chosen component budget: two 74HC595, one 74HC161, one shared 74HC14, and two NPN transistors (one inhibit, one watchdog discharge). Existing 74HC00/74HC32 gates supply SR-latch and control logic, with no additional PS/2-specific logic package in this allocation. Exact models and passive values will be selected later; do not confuse this deferral with reopening the chosen component budget. The sequence, gate allocation requirements, and unverified timing conditions are recorded in `ttl_forth_emu/docs/hardware_design_status.md`.

Use an edge-retriggered RC watchdog for incomplete-frame recovery, with Schmitt conditioning and active timing-capacitor discharge. Enable it only while receiving a frame and not READY; both stuck-HIGH and stuck-LOW clock must be covered. Timeout resets an incomplete receiver, not a completed unread byte.

Host inhibit uses one of the two NPN transistors with collector on PS/2 CLK and emitter at ground, plus a base resistor and base-emitter pull-down resistor. Shift/count on the original clock's falling edge through an inverter; finish/latch on the eleventh rising edge before asserting inhibit. Freeze the counter at 11 and preserve the output latch. Hold inhibit while data is pending and for a minimum-safe interval after CPU consumption, then release after receiver rearming. Never actively drive CLK high. Meet PS/2 inhibit timing (at least 100us), account for an already-starting frame and retransmission, and validate release edges. No host-to-keyboard command transmitter is specified.

Polled by the CPU during V-Blank periods.

Excluded Blocks: Serial controller ports and associated communication circuitry are completely removed to minimize chip count.

---

4. Bootstrap & Explicit Persistence
Boot-up (Load):

Upon power-on, reuse the CPU's four PC counters for the lower 16 word-address bits and the bank 74HC161 for the upper three bits. There are **zero dedicated lower-address DMA counter packages**. Copy both Flash byte lanes to I-RAM over eight banks (512K words total). The nominal one-word-per-tick copy duration would be about 83.9ms at 6.25MHz, excluding initialization/transition; this is not measured or timing-certified.

The selected boot-control baseline uses one 74HC74 package, holds instruction registers reset, and ORs opcode bit 1 with BootMode to present `0x02` (LD AC, AC) to all affected control paths. An additional 74HC32 and 74HC00 are allocated for glue logic; 74HC08 was discussed as optional, not mandatory. Half-cycle write pulses and a bank-Q3 completion indication are proposed mechanisms, not completed timing proofs. Exact first/last-word sequencing, write-disable/initialization/run handoff, and reset separation remain to be implemented. Do not claim PCB correctness from typical propagation delays or an RC transition pulse.

Explicit Save:

Only the explicit FORTH `SAVE` command programs I-RAM, including runtime-compiled words, into Flash, observing the SST39SF040 erase, Byte-Program and Software Data Protection protocols. Data RAM is never saved.

There is no power-failure detector, power-down interrupt, hold-up capacitor save circuit, or automatic shutdown save in the hardware/firmware. Without `SAVE`, sudden power loss leaves the previously stored Flash image unchanged; the next boot restores that image and discards volatile I-RAM/data RAM changes.

Power loss during `SAVE` is **undefined and outside the supported contract**. Flash corruption, loss of the boot image, or physical device damage are not covered; no atomicity, recovery, or old-image preservation is guaranteed in that case.

The simulator may write its already-programmed Flash array to a host image file at normal application exit. This is a host persistence feature, not automatic I-RAM-to-Flash programming and not a hardware requirement.

---

5. Optimization & Implementation Guidelines
Minimal Chip Count: Suppress logic bloat by utilizing techniques like Diode-Resistor Logic (DRL) for instruction decoding where appropriate.

Timing Margins: Ensure strict adherence to setup/hold requirements under a 6.25MHz (160ns cycle) timing budget using 55ns–70ns components.

## 6. Gigatron Circuit Reuse Baseline

Use `Gigatron-BillOfMaterials.xlsx` and `Gigatron Rev. 2012 OSH/Kicad/` as the implementation baseline. Preserve the reference files unchanged. Reuse the original circuit topology outside the game-controller interface and instruction-fetch/memory replacement boundary, with the specified RAM replacement and the minimum additional MMIO/DMA/PS/2 circuitry.

* Replace the reference 74HCT (and legacy KiCad 74LS labels) with corresponding **74HC** functions. The BOM is the part-selection reference; older netlist family labels are not a requirement to use LS parts.
* Reuse ALU, CPU registers, control decoding including diode logic, data-address multiplexers, clock/power-on reset, VGA resistor DAC/sync wiring, XOUT/audio DAC, and indicator circuits. Accommodate the 64KiB RAM address/chip-enable wiring; do not retain the old 32KiB limitation.
* Remove the DB9 game-controller interface and replace its input function with the pure-TTL PS/2 receiver. Do not use the BOM's ATtiny85-based Pluggy McPlugface adapter.
* Replace the EPROM fetch path with writable, two-lane I-RAM, paired Flash, and bootstrap/access arbitration. Preserve the PC/ISA behavior; instruction registers/buffers inside this boundary must be accounted for in the new circuit, not silently omitted.
* The original reset supervisor is power-on/reset circuitry only, never a power-failure SAVE trigger. Its final package must satisfy the project's packaging constraint.
* HC substitution is a design choice, **not proof of electrical equivalence**. Verify memory/peripheral VOH/VOL against HC VIH/VIL, fanout, oscillator behavior, diode-decoder levels, and worst-case timing. The local AS6C1008 datasheet guarantees VOH of only 2.4V at the specified -1mA load; direct drive of a 5V HC input cannot be certified from that guarantee alone. Resolve any level-margin issue within the HC/discrete-component constraint.

See [provisional chip accounting](ttl_forth_emu/docs/hardware_bom.md) for reference designators, reuse counts, and unresolved additions. This is a design baseline, not a completed or electrically verified schematic.

## 7. Decision Record and Remaining Architecture

[Hardware design status](ttl_forth_emu/docs/hardware_design_status.md) records the discussion's adopted decisions, PS/2 hold, rejected/superseded assumptions, and remaining non-value architectural questions. In particular, runtime single-port fetch/data access, execution versus access banking, the CPU-visible interface, RAM/MMIO overlap, bus control matrix, boot transition sequencing, XOUT ownership, and video-service policy are not yet finalized. Component selections/allocations must not be described as undecided merely because passive values or timing verification remain unfinished.
