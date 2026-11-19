# Validation record

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

Hardware evidence on one unit: the standard OpenOCD driver completed unprotection; an actual power cycle and post-POR RDP0/OPTERR0, real UID and 64 KiB density checks passed. The listed BIN was flashed and independently verified. With the core running, full image comparison in 64-byte reads and a fresh 128-byte mailbox snapshot passed. Mapping 0 acknowledged SET20.00 dB, a monotonic 20-second hold, SET0 and final GET0. The RF comparison is summarized in the README and is not a calibrated attenuation measurement. Firmware source/image bytes have not changed during these host-tool corrections.

Still unqualified: CH340/UART electrical communication and USB command replies; GPIO/UART continuity; measured oscillator and pad timing/voltage; display interface; physical insertion loss, attenuation accuracy and frequency range. Other mapping permutations are debug-only and unqualified. A BIN/HEX/checksum release waits for the USB serial test. The stock image has no backup or restoration path. Configuration/driver models do not establish absence of opaque adapter effects or real hardware behavior on other boards.
