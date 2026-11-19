# Validation record

## Current loader/app candidate — offline validation only

Built with `arm-none-eabi-gcc (GCC) 16.2.0`, Cortex-M3 Thumb, freestanding C11, `-Os`, warnings as errors, no runtime library. `make all test inspect` passes. Initial SP for both images is `0x20004ff8`; both linkers exclude the top eight RAM bytes and reserve at least 2 KiB stack. All 59 vectors in each actual ELF/BIN pair are checked, with zero reserved core entries. No undefined symbols or heap/runtime calls.

| Image | Flash bytes / sections | Thumb entry | SHA256 of BIN |
| --- | --- | --- | --- |
| Loader | 2,460 = 236 vectors + 2,224 text; zero data/BSS | `0x0800025d` | `01abab39ac7172075801edff3e89fcf3d41eff83d46efa5844186ad47079819d` |
| App | 2,156 = 236 vectors + 1,920 text; 128-byte mailbox, zero data/BSS | `0x08002139` | `9c9c86529cb9516b4a86ce64df147cb97d003bf304975eeec0e89de8afb31e36` |
| Combined loader/app/metadata | 65,536 bytes, including erased-value padding | Loader entry | `381e377c7c9e32c4cfebb4c18603ef085e4c5d7f96774336d4e51a76b9bf93ae` |

ELF SHA256: loader `bd1cba2c28f558d08b6599977282b3b464c22bc8b99a283a37aa5320f05ee1a0`; app `ab490f05f578a96ab93366f52b4fc1a3153550c4563b1c4c571ee08ef8bd43cc`.
HEX SHA256: loader `1082fa00a444d7f125510a672f2751ef835ba7d9338df024128c0ef14f7b4be1`; app `d7c0a1fd5f6a987f13e059ddce0a2571d2336bec6852419193e19aa7b8b1daee`; combined `1b5a31ccdeba2ada04defefd76cf52ccaf73f5cd52db216d2a05eda0e1f9e083`.

Disassembly confirms loader page/halfword range and alignment checks before flash unlock; `strh` programming and readback; EOP/error checks and lock restoration; loader BRR=69; app BRR=625; UART TC wait before request/reset or app handoff; VTOR=`0x08002000`, DSB/ISB, and the final MSP load/Thumb branch. App boot still issues mapping-0 zero before command reception. Compiler stack reports 304 bytes for loader reset, 144 for its parser and at most 48 in its CRC/validation helpers; the deepest static path is below 640 bytes. App reset is 120 bytes, below 256 for its deepest static path. These are static estimates, not measured stack high-water marks.

The existing C GPIO/parser/mailbox suite passes, including the new bootloader command issuing zero before its acknowledgment/request flag. A second C executable models flash and fails every erase/program operation in turn, checking invalidation, commit-last behavior, no loader writes, invalid-app run refusal and explicit recovery. It tests malformed/range/alignment/overflow input, sequential writes, incomplete lines, receive errors, CRC/vector corruption and actual generated combined-image validity. It independently validates Python/zlib's metadata CRC. Twenty-nine Python test methods include the installation model described below, deterministic bundle layout, malformed app rejection before transport, bounded serial failures and the real POSIX serial adapter against a local pseudo-terminal with fragmented replies and exact uploaded bytes. No physical serial port is used. Both C executables also pass AddressSanitizer and UndefinedBehaviorSanitizer with halt-on-error enabled.

The current images have **not** yet been flashed or verified on the unit. Required next checks: independent review, combined-image SWD flash/verify and issued zero, then CH340 set/get and one app-only USB update with post-update zero. Prebuilt release remains gated on those hardware checks. Historical RF evidence below applies to the earlier image, not this relocated candidate.

## Earlier application — single-unit evidence

The initial candidate was built with `arm-none-eabi-gcc (GCC) 16.2.0`, using Cortex-M3 Thumb, `-Os`, freestanding C11, warnings as errors, section garbage collection and no runtime library. Debug paths are remapped to the source root. Reproduce with `make all test inspect`. Different compiler versions can change image hashes.

| Artifact | Result |
| --- | --- |
| ELF SHA256 | `f50bc78e965a9da5dc93c75926035bf60424fc129e606048fd44fc7b51e266e0` |
| BIN SHA256 | `7969ab660db24d54a753c183e00c23f80cd5753b0134709ff9bcb4d88c2c1d84` |
| HEX SHA256 | `0b5e411ffff1c496398b3c575cd82f31fe02a0ad9f1f9ce4903a67f6e0157515` |
| Flash load image | 1,984 bytes: `.isr_vector` 236, `.text` 1,748 |
| Static RAM | `.mailbox` 128 bytes at `0x20000000`; `.data`/`.bss` zero |
| Entry / initial stack | Thumb `0x08000131` / `0x20005000` |
| Vector table | All 59 medium-density entries checked, including zero reserved core slots |
| Stack | Compiler reports 128 bytes for Reset_Handler; deepest static call chain below 256 bytes, versus linker reservation of 2,048 bytes |

`tools/inspect_image.py` parses ELF load segments, vectors and mailbox section/symbol and compares the BIN byte for byte with the ELF flash image. `arm-none-eabi-objdump -d` was inspected for the GPIO BSRR store masked to PB11–PB13, SysTick's 72-ticks/µs down-counter loop, bounded oscillator/PLL/switch polls, flash-latency write/readback, clock prescalers, BRR=625, boot transaction before UART enable, SR-before-DR error handling, and `dmb` instructions around mailbox publication. There are no undefined symbols or runtime/heap calls. This is compiled-image inspection, not target execution or pad timing measurement.

Executable offline coverage:

- C bus simulator: all 128 quarter-dB values across all six mappings (768 combinations), 16 rising clock edges, sampled bit order, reserved/address zeros, setup/hold/pulse timing, LE after the frame and idle-low completion. All 36 mapping transitions and boot's requested zero transaction are also exercised.
- C parser/UART: all 128 formatted values round-trip; boundaries, non-quarter values, signs/exponents/nonfinite input, excessive digits, CR/LF/CRLF, bad characters, receive errors, overflow/discard and recovery are exercised. Rejected input leaves the issued command unchanged.
- C mailbox: unpublished and partial payload prefixes, odd sequence, inverse/checksum mismatch, duplicate/old/exhausted sequences, every semantic error, acknowledgment/result publication, retained results after UART/live updates, all six mapping selections and invalid values.
- Twenty-four Python test methods cover image/plan corruption, exact parsing, explicit execution confirmation and the two-stage identity policy. Generated Tcl runs under a local memory/driver model while sourcing the actual unmodified OpenOCD target/interface/helper scripts. The model reproduces the original examine-end write, then verifies deferred memory-only attachment, suppression of that hook and effective 100 kHz for both native profiles. It checks status/GET/SET/all mappings, wrong-MCU IDCODE/UID with an identical image, unavailable post-POR UID, wrong image/schema/state, coherent-read limits and sequence/pending-command guards.
- Installation tests execute protected peripheral identity collection without UID/density access, explicit physical-identity erase, post-POR UID collection, and UID-bound flash/verify/start. They inject OBR/WRPR read failure before mutation, deferred examination invalidation on reset, driver busy/erase/program/bus failures, failed verification, exit-zero error transcripts, missing success evidence and subprocess timeout. The model prohibits protected raw-option reads, verifies the standard driver path, allows only known marked probe-density diagnostics, and rejects image mismatches at transfer boundaries. Mapping selection requires explicit debug opt-in. Raw transcripts/unknown completion are retained and a consumed attempt refuses replay. OpenOCD is never launched by tests; printed device-phase events come from injected local runners.

Hardware evidence on one unit: the standard OpenOCD driver completed unprotection; an actual power cycle and post-POR RDP0/OPTERR0, real UID and 64 KiB density checks passed. The listed BIN was flashed and independently verified. With the core running, full image comparison in 64-byte reads and a fresh 128-byte mailbox snapshot passed. Mapping 0 acknowledged SET20.00 dB, a monotonic 20-second hold, SET0 and final GET0. The RF comparison is summarized in the README and is not a calibrated attenuation measurement. The current loader/app build changes these image bytes and therefore requires fresh target qualification.

Still unqualified: CH340/UART electrical communication and USB command replies; GPIO/UART continuity; measured oscillator and pad timing/voltage; display interface; physical insertion loss, attenuation accuracy and frequency range. Other mapping permutations are debug-only and unqualified. A BIN/HEX/checksum release waits for current-image flash/verify, USB set/get and a USB app update. The stock image has no backup or restoration path. Configuration/driver models do not establish absence of opaque adapter effects or real hardware behavior on other boards.
