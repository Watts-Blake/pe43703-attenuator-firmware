# Installation on a stock STM32F103C8T6 + PE43703 board

One unit has completed standard-driver RDP removal, actual power cycling, post-POR identity checks, image flash/verification and a permutation-0 mailbox/RF comparison. USB serial testing remains pending. These results apply to that unit; verify wiring and power on every board before installing.

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

## Build and select the explicit OpenOCD profile

Build dependencies are in the README. Install OpenOCD with native ST-Link support and the Cortex-M/memory-AP target drivers. Two reviewed configurations are supported; select one deliberately for every plan:

| Profile | Interface file | Transport |
| --- | --- | --- |
| `native-swd` (default, newer native configuration) | `interface/stlink.cfg`, selecting `adapter driver st-link` | `swd` |
| `native-dapdirect` (upstream OpenOCD 0.12.0) | `interface/stlink-dap.cfg` | `dapdirect_swd` |

There is no HLA, automatic transport change or alternative-adapter fallback. Inspect the installed files/version before execution. Tests execute copies of the actual included configurations with physical commands modeled; they do not establish physical compatibility. Both profiles finish configuration at 100 kHz, defer core examination and disable the included target-write hook before attachment. All server ports are disabled.

Build and inspect offline:

```sh
make all test inspect
mkdir -p .local
```

## Record protected-unit identity without trying to read its UID

Keep a private record of the unit label, the exact ST-Link serial, the sole physically wired target and its MCU marking. Programmer serial is not MCU UID. Protected system-memory reads, including UID and flash density, can fail; no preerase UID is required or attempted. The initial identity policy instead requires an operator to confirm a single known target wired to the selected adapter, plus exact DPIDR, full DBGMCU_IDCODE and OBR/WRPR observations. If this physical identity cannot be established, stop before erase.

First generate a peripheral-read plan **offline**, selecting the appropriate profile:

```sh
python3 tools/install.py stage-identity --serial "$STLINK_SERIAL" \
  --profile native-swd --output .local/identity.tcl
```

After reviewing it and arranging deliberate device access, the following command **does contact the programmer and target**:

```sh
openocd -f .local/identity.tcl
```

It requests only DPIDR, IDCODE, OBR and WRPR reads through a memory-only target. Record its exact output privately as `EXACT_DPIDR`, `EXACT_IDCODE`, `PROTECTED_OBR` and `EXACT_WRPR`. There is no halt/reset, UID/density read, flash-driver probe or configured MCU write. Debug attachment still has physical effects not excluded by offline testing. Require the supported DPIDR, medium-density STM32F103 IDCODE and OBR RDPRT=1/OPTERR=0; do not force IDs or invent values.

## Stage protection removal offline

Use the actual recorded identifiers and physical confirmation. These variable names are placeholders, not a qualified device identity:

```sh
python3 tools/install.py stage --purpose remove-rdp --elf build/attenuator.elf \
  --loader-elf build/bootloader.elf \
  --directory .local/erase-plan --device STM32F103C8T6 \
  --unit "$UNIT_LABEL" --serial "$STLINK_SERIAL" --profile native-swd \
  --dpidr "$EXACT_DPIDR" --idcode "$EXACT_IDCODE" \
  --obr "$PROTECTED_OBR" --wrpr "$EXACT_WRPR" \
  --physical-confirmation sole-wired-target-confirmed
```

This validates both ELF/BIN pairs and creates a 64 KiB combined loader/app/CRC image, ELF copies, a schema-3 hash manifest, `remove-rdp.tcl` and `post-rdp-status.tcl`. It does **not** open USB/SWD. It cannot generate programming phases and refuses a supplied UID. Keep the directory private: plans, confirmations, transcripts and receipts contain unit/programmer identity. Review the complete plans and image hashes before execution.

## Irreversible protection removal — separate opt-in action

**Removing RDP from the examined stock unit mass-erases its firmware. No stock backup exists; the original firmware cannot be restored from this repository.** Arrange a real target power-on reset after unprotection before starting. A software reset, NRST pulse or programmer USB reset is not proof that target power fell below the POR threshold. Use one known power source and a reviewed physical power procedure. See [ST PM0075 §2.4.1, p18](https://www.st.com/resource/en/programming_manual/cd00283419-stm32f10xxx-flash-memory-microcontrollers-stmicroelectronics.pdf).

Only after identity verification, review and deliberate authorization:

```sh
python3 tools/install.py execute --directory .local/erase-plan --phase remove-rdp
```

Type the full displayed `ERASE-IRREVERSIBLY` confirmation. It binds the unit label, part, adapter serial, DPIDR, full IDCODE, physical identity assertion, OBR/WRPR snapshot and image hash. Tcl checks confirmation before adapter open, then identity/options before intentional core examination and `reset halt`; it repeats the checks afterward.

The plan uses OpenOCD's standard `stm32f1x unlock 0`, after the guarded reset/halt sequence. It explicitly re-examines and halts the deferred core after reset. The driver performs option erase/programming and destructive unprotection. A configured 64 KiB bank overrides its family-size estimate: the driver's probe may log a blocked read at `0x1ffff7e0` and a 128 KiB fallback under active RDP. Only those known diagnostics inside the marked probe are allowed, and the configured 64 KiB result must be confirmed. Unexpected probe diagnostics stop before unlock.

The unlock handler can report errors while returning exit zero. Its captured erase/program diagnostics are fatal, and a missing `stm32x unlocked.` marker also stops the phase. Raw option flash at `0x1ffff800` is **not read before POR**: the former direct sequence failed there after option erase while RDP remained active. The plan logs pre-POR OBR, emits `UNLOCK_DRIVER_RETURNED_POR_REQUIRED` and stops. This marker means the driver returned successfully; authoritative RDP-clear verification is still required after real POR. Driver programming can use an SRAM loader. No automatic power-cycle, flash, retry or stock restoration follows. A partial failure can leave partial options or erased firmware; retain evidence and stop.

Every execute phase records one consumed attempt under `attempt-<phase>/` before adapter open. It retains stdout/stderr and a completion receipt. Exit zero alone is insufficient: error diagnostics or missing phase-success evidence leave completion unknown. An existing attempt directory prevents replay, including after a timeout. Do not delete it to retry an ambiguous destructive operation.

## Perform actual POR, check options, then record the UID

After the driver-returned/POR-required result, perform the separately arranged power-on reset. Reconfirm the sole physical target, exact adapter and stable power. Do not use repeated unlock or reset commands as substitutes.

Then run this **read-only device phase**, confirming `POR-COMPLETED-CHECK` only after actual POR:

```sh
python3 tools/install.py execute --directory .local/erase-plan --phase post-rdp-status
```

It checks DPIDR/IDCODE, inactive RDP/OPTERR=0 and preserved loaded OBR/WRPR. It prints `POST_POR_OBR=`; no raw option-flash read is used. It requires actual reported flash density 64 KiB and prints `POST_POR_UID=` followed by three 32-bit UID words. Record that exact string as `MCU_UID_WORDS`, in **register-read order**, without byte swapping. UID starts at `0x1ffff7e8`. Empty, unreadable, all-zero or all-one UID is rejected; never substitute the adapter serial. Record post-POR OBR (the original value with RDPRT cleared) as `UNPROTECTED_OBR`.

Stage a separate programming manifest **offline** with this actual UID:

```sh
python3 tools/install.py stage --purpose program --elf build/attenuator.elf \
  --loader-elf build/bootloader.elf \
  --directory .local/program-plan --device STM32F103C8T6 \
  --unit "$UNIT_LABEL" --serial "$STLINK_SERIAL" --profile native-swd \
  --dpidr "$EXACT_DPIDR" --idcode "$EXACT_IDCODE" --uid "$MCU_UID_WORDS" \
  --obr "$UNPROTECTED_OBR" --wrpr "$EXACT_WRPR"
```

This manifest can generate only `flash.tcl`, `verify.tcl` and `start.tcl`; it contains no protection-removal phase. Recheck both selected ELF/BIN pairs and the combined image hash against the reviewed replacement. The staged `candidate.bin` is the complete 64 KiB combined image, not the app-only BIN. All later programming and mailbox operations require exact full IDCODE/UID, adapter serial and image identity.

## Flash, verify and first boot — separate device phases

```sh
python3 tools/install.py execute --directory .local/program-plan --phase flash
python3 tools/install.py execute --directory .local/program-plan --phase verify
```

Each command requires its own full typed confirmation. Identity, inactive protection, exact options and actual density are checked before intentional core examination/reset. `flash` resets/halts, executes `flash write_image erase <staged-candidate.bin> 0x08000000 bin`, then `verify_image <staged-candidate.bin> 0x08000000 bin`, and leaves the CPU halted. `verify` resets/halts and runs only that image comparison. Neither phase removes protection. The wrapper invokes `openocd -f <reviewed-phase.tcl>` with its phase-specific confirmation. Verification is a device operation, never part of `make inspect` or offline tests.

With the display detached and the initial test setup arranged:

```sh
python3 tools/install.py execute --directory .local/program-plan --phase start
```

The distinct `RESET-START` confirmation permits verification followed by `reset run`. Boot requests 0.00 dB with default permutation 0. Use [mailbox status](mailbox.md) with the exact post-POR UID to read state, selected mapping and issued count. Neither image verification nor mailbox acknowledgment establishes RF attenuation.

After hardware qualification, disconnect programmer power before reconnecting USB. Reassemble without pinching wires or the OLED ribbon, then use CH340 serial `set`/`get`. V1 omits display/button support. **The current UART loader awaits target qualification.** After the combined image passes SWD verification and initial USB commands, test an app-only USB update before removing programmer access. See [USB update/recovery](usb-update.md). Never flash `attenuator.bin` at `0x08000000`; it is linked for `0x08002000` and requires the loader and metadata.

## Troubleshooting

- **Unable to connect:** check a single stable power source, common ground, secured contacts, exact adapter serial, SWD continuity and target configuration. On the examined unit, correcting swapped SWDIO/SWCLK enabled connection. That is one cause, not a universal diagnosis; power off before changing wires and verify continuity instead of swapping blindly.
- **Programmer reports about 3.2 V:** this is a voltage cue at the programmer, not proof of all supply rails, adequate current, oscillator operation or MCU health.
- **Protected/read errors:** active RDP blocks a stock flash dump. Do not infer that a backup exists, automatically unlock, repeat a failed erase, or substitute a different programmer/target.
- **Clock-fault mailbox state:** verify HSE frequency and board supply. No commands are accepted after bounded startup failure. Do not claim a successful attenuation latch.
- **Serial reply but unchanged RF:** read the selected mapping, trace wiring and check serial mode/address/logic prerequisites. A completed command is not RF readback. Qualification requires independent inline RF measurements.
