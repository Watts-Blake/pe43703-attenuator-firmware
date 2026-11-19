# Installation on a stock STM32F103C8T6 + PE43703 board

This is a staged procedure for future attended hardware qualification. The replacement has only been built and tested offline. Do not treat the existence of these commands as evidence that the board has been flashed, that the pin mapping is correct, or that the RF path works.

## Equipment and disassembly

Use an ST-Link V2 with a genuine regulated 3.3 V power output (a clone can work), four female jumper wires, a multimeter with continuity mode, a small screwdriver, and optionally a four-pin 2.54 mm male header and soldering iron. A carefully secured press-fit header can be used for temporary contact; loose wires can interrupt programming. Some programmer pins are voltage-reference inputs rather than power outputs: check the programmer's own pinout and power capability.

1. Unplug the attenuator's USB cable and every other power source. Disconnect RF cables before opening the shell.
2. Remove the case screws and open the aluminium shell without straining the SMA connectors.
3. Lift the upper OLED/button board off its 2×5 connector. Do not pull the OLED ribbon. Set the upper board aside for initial tests.
4. Find the four-hole header beside the STM32 on the lower RF board. Solder the optional header, or secure temporary press-fit pins with power off.

Functional sketches, **not oriented PCB artwork or a traced schematic**:

```text
Upper board                       Lower RF board
+-------------------------+       +-----------------------------------+
| OLED                    |       | SMA -- PE43703 RF path -- SMA      |
| five front-panel buttons|       |           STM32F103C8T6             |
|          2 x 5 connector| <---> | 2 x 5        four-hole SWD header   |
+-------------------------+       | micro-USB -- CH340 -- UART?        |
                                  | TTL UART header (trace to confirm)|
                                  +-----------------------------------+

Reported working header order (logical numbering only):
  hole 1       hole 2       hole 3       hole 4 (square pad)
  3.3 V        GND          SWDIO        SWCLK
```

The order above comes from a successful SWD connection and reported wiring on one unit. It does not establish the physical orientation on yours or continuity of holes 3/4 to the MCU. A square pad does not establish the electrical role by itself. Do not infer orientation from the sketches.

## Verify wiring with power off

Identify pin 1 on the STM32 LQFP48 package using its package marking, then number pins according to the [ST datasheet pin diagram](https://www.st.com/resource/en/datasheet/stm32f103c8.pdf). Require continuity on the actual board:

| Header function | Verify continuity to | Programmer connection |
| --- | --- | --- |
| 3.3 V, reported hole 1 | STM32 pin 48, VDD | Regulated 3.3 V output |
| GND, reported hole 2 | SMA outer body and circuit ground | GND |
| SWDIO, reported hole 3 | STM32 pin 34, PA13 | SWDIO |
| SWCLK, reported square hole 4 | STM32 pin 37, PA14 | SWCLK |

Check for shorts and unexpected connections before applying power. Keep the attenuator USB unplugged while the programmer supplies 3.3 V: use one power source. Do not connect a 5 V pin. This four-wire arrangement has no external reset wire; software reset/halt operations still affect the unit when deliberately executed.

Before changing firmware, verify the remaining board assumptions: genuine supported MCU, BOOT0 set for main-flash boot, 8 MHz HSE, PE43703 serial mode (P/S high), all parallel control inputs grounded, address straps A0–A2 low, compatible 3.3 V logic, and actual SI/CLK/LE/UART traces. The suggested GPIO assignments are not continuity evidence.

## Build and record identity

Install OpenOCD **0.12.0** with `interface/stlink.cfg` (HLA ST-Link) and `target/stm32f1x.cfg`, plus the build dependencies in the README. Other OpenOCD versions/configurations require review; the tools do not automatically switch interfaces or adapters.

Build and inspect offline:

```sh
make all test inspect
```

Keep a private record of the exact unit label, adapter serial, full DBGMCU_IDCODE and three 32-bit UID words from the selected MCU. The STM32F1 UID starts at `0x1ffff7e8`; format `--uid` as three eight-digit hexadecimal words concatenated **in register-read order**, not a byte-swapped string. Do not infer the UID from the adapter serial. Identity collection is a separately attended read operation, not part of the build or test suite. If UID cannot be read under the unit's current protection, stop and review the identity prerequisite; do not omit the check or substitute zeros. No tool scans for an alternative device.

## Stage plans offline

Use the actual independently recorded identifiers in the variables below. These are local placeholders, not identifiers of a qualified unit:

```sh
python3 tools/install.py stage --elf build/attenuator.elf \
  --directory .local/install-plan --device STM32F103C8T6 \
  --unit "$UNIT_LABEL" --serial "$STLINK_SERIAL" \
  --idcode "$EXACT_IDCODE" --uid "$MCU_UID_WORDS"
```

This creates immutable image copies, a hash manifest and separate `remove-rdp.tcl`, `flash.tcl`, `verify.tcl` and `start.tcl` plans. It does **not** execute OpenOCD. Keep the generated directory private because it contains unit identity. Review the image hashes, every plan and the installed OpenOCD driver before any execution. The execution wrapper rechecks the ELF, BIN, complete plan text, SHA256 hashes and exact confirmation. Tcl also checks an execution confirmation before opening the adapter, then checks full IDCODE, UID and option-byte error state before reset or flash operations. No fallback or retry is provided.

## Irreversible protection removal — separate opt-in action

**The examined stock unit has readout protection active. Removing it causes a mass erase. No stock backup exists and this operation cannot restore the original firmware.** See [ST PM0075 §2.4.1](https://www.st.com/resource/en/programming_manual/cd00283419-stm32f10xxx-flash-memory-microcontrollers-stmicroelectronics.pdf) and [OpenOCD `stm32f1x unlock`](https://openocd.org/doc/html/Flash-Commands.html).

Only after review, identity verification and deliberate authorization, an operator may run:

```sh
python3 tools/install.py execute --directory .local/install-plan --phase remove-rdp
```

Type the exact displayed `ERASE-IRREVERSIBLY` confirmation, which includes the unit, part, adapter serial, IDCODE, UID and replacement image hash. Any mismatch aborts. The underlying operation is `reset halt` then **`stm32f1x unlock 0`**, which removes read protection and mass-erases the device. Review the installed OpenOCD driver's handling of other option bytes; do not assume they are preserved. The plan stops afterward. There is no extra mass erase, automatic protection downgrade or automatic flash.

ST specifies a power-on reset to reload option bytes after SWD unprotection. Arrange this as a separate attended operation with the device owner; the tool does not power-cycle or attempt fallback resets. Confirm the same unit and inactive protection before proceeding. Do not replay an erase plan after an ambiguous timeout or partial failure.

## Flash, verify and first boot — separately authorized phases

After option reload and identity revalidation:

```sh
python3 tools/install.py execute --directory .local/install-plan --phase flash
python3 tools/install.py execute --directory .local/install-plan --phase verify
```

Each command requires its own full typed confirmation. `flash` resets/halts, writes the selected binary at `0x08000000` with page erase, verifies it and leaves the CPU halted. `verify` also resets/halts and verifies; it performs no unlock or flash write. Both refuse active RDP. Verification is a device operation, never part of `make inspect` or offline tests.

When ready for the initial boot, with the display detached and RF test setup separately arranged:

```sh
python3 tools/install.py execute --directory .local/install-plan --phase start
```

This requires a distinct `RESET-START` confirmation, verifies the image and issues `reset run`. Boot then requests the 0.00 dB command with default permutation 0. Use [mailbox status](mailbox.md) to read the chosen mapping, state and issued count. Neither flash verification nor mailbox success qualifies RF attenuation.

After hardware qualification, disconnect programmer power before reconnecting USB. Reassemble the case without pinching wires or the OLED ribbon, then use CH340 serial `set`/`get`. V1 omits display/button support. **USB firmware updates are future work when a bootloader is added; v1 updates require SWD.**

## Troubleshooting

- **Unable to connect:** check a single stable power source, common ground, secured contacts, exact adapter serial, SWD continuity and target configuration. On the examined unit, correcting swapped SWDIO/SWCLK enabled connection. That is one cause, not a universal diagnosis; power off before changing wires and verify continuity instead of swapping blindly.
- **Programmer reports about 3.2 V:** this is a voltage cue at the programmer, not proof of all supply rails, adequate current, oscillator operation or MCU health.
- **Protected/read errors:** active RDP blocks a stock flash dump. Do not infer that a backup exists, automatically unlock, repeat a failed erase, or substitute a different programmer/target.
- **Clock-fault mailbox state:** verify HSE frequency and board supply. No commands are accepted after bounded startup failure. Do not claim a successful attenuation latch.
- **Serial reply but unchanged RF:** read the selected mapping, trace wiring and check serial mode/address/logic prerequisites. A completed command is not RF readback. Qualification requires independent inline RF measurements.
