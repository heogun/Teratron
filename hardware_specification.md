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
  * Retains physical separation between instruction and data memory paths to guarantee true 1-Cycle instruction execution.
* **Performance Target:** Equivalent operating frequency to Gigatron (**6.25 MHz**, 160ns clock cycle) ensuring a 1-Clock / 1-Instruction (1 MIPS+) execution model.

---

## 2. Memory Subsystem

* **Fixed Components:**
  * **Data RAM:** AS6C4008-55PCN (512K x 8-bit, 55ns CMOS SRAM), using **64KB only**, with A16, A17, and A18 tied to ground. Data RAM is unbanked and volatile.
  * **Storage/Boot Flash:** SST39SF040-70-4C-PHE (512K x 8-bit, 70ns Multi-Purpose Flash).

Address Space & Banking:

The core CPU utilizes a 16-bit address bus (64KB). Data RAM uses this address space directly, without banking. For Flash and I-RAM byte access, hardware memory banking (via 74HC273 latch registers) maps upper address lines (A16,A17,A18) to access the full 512KB capacity.

Both Flash and I-RAM share a unified banking framework.

Flash stores **I-RAM only**. Data RAM is neither backed up to Flash nor restored at boot.

---

3. I/O & Peripherals
VGA Graphics: Time-shared pixel pumping driven by CPU microcode during Active Video intervals, maintaining 160x120 retro graphics without dedicated graphic controllers.

Mono Audio: XOUT expansion register coupled with a Resistor Ladder DAC for 4-voice wavetable software sound mixing.

PS/2 Keyboard Input (Pure TTL):

Built entirely without helper microcontrollers.

Hardware Receiver Buffer: Dual 74HC595 shift registers configured with hardwired bit-slicing to route clean 8-bit scan codes directly onto the CPU data bus (D0∼D7).

Polled by the CPU during V-Blank periods.

Excluded Blocks: Serial controller ports and associated communication circuitry are completely removed to minimize chip count.

---

4. Bootstrap & Power-Down Persistence Sequence
Boot-up (Load):

Upon power-on, a hardware DMA/counter-based bootloader copies the contents of the SST39SF040 flash memory into the I-RAM.

Once transferred, CPU reset is released, executing native FORTH code directly from I-RAM at full 6.25 MIPS speed.

Power-down (Save):

Utilizes hold-up power capacitors or power-failure detection circuits to trigger a routine that backs up I-RAM, including runtime-compiled FORTH words, into the Flash memory storage, strictly observing the SST39SF040 Byte-Program algorithm and Software Data Protection (SDP) specs. Data RAM contents are discarded on power loss.

---

5. Optimization & Implementation Guidelines
Minimal Chip Count: Suppress logic bloat by utilizing techniques like Diode-Resistor Logic (DRL) for instruction decoding where appropriate.

Timing Margins: Ensure strict adherence to setup/hold requirements under a 6.25MHz (160ns cycle) timing budget using 55ns–70ns components.
