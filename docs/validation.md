# Offline validation record

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
- Ten Python test methods: executable ELF corruption/range/schema checks, exact integer parsing, image/plan tamper rejection and deliberate execution confirmation with an injected mock runner. Generated Tcl is executed with simulated RAM and stubbed debug commands to check mailbox status/GET/SET/all mappings, coherent-read retry exhaustion, wrong image/schema/state, outstanding command refusal and sequence exhaustion. Installation Tcl is exercised for all four phases, including confirmation/IDCODE/UID/protection rejection before reset/flash. OpenOCD is never launched by tests.

Still unqualified: actual MCU/clock/boot wiring and power sequencing; control/UART continuity and mapping; display interface; active-RDP UID readability; installed programmer/OpenOCD behavior; erase/option reload, flash/verify and first boot; UART electrical communication; pad timing/voltage; physical insertion loss, attenuation accuracy, frequency range and RF safety. No hardware action, release publication, firmware restoration capability or physical 0 dB result is implied by these checks. Independent offline review is required before arranging separate attended hardware work.
