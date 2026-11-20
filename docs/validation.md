# Validation record

## Two-unit diagnostic observations

The v0.1.1 diagnostic app produced legible OLED trial numbers 41 and 89 on two examined units. Both use PA6=SCK, PA7=MOSI, PA5=D/C, PA4=RES in reset mode R. SSD1306-family column-0 and SH1106-family column-2 sequences both render. Controller silicon remains unidentified, and the proposed fixed-pattern alignment comparison is UNRUN. See [OLED observations and limits](oled-probe.md#observed-wiring-on-two-units).

Known held-button presses on both units produced distinct PA1 ladder plateaus: LEFT 4092–4093, PLUS 2028–2048, MINUS 1016–1021, RIGHT 809–820 and OK 1352–1368. One 1092 transition remains UNKNOWN. The [ADC observation table](diagnostics.md#observed-pa1-front-button-ladder) separates these raw observations from proposed unqualified thresholds; the faster sampler/debounce proposal is unimplemented. Captures ran at about 10 Hz per unit. Buttons remain inert.

One examined unit successfully updated from v0.1.0 to the 4,736-byte v0.1.1 app through GET → bootloader entry, exact geometry-before-erase, 74 sequential chunks and CRC completion, followed by expected `version 0.1.1` and `get` zero replies. The pre-update v0.1.0 app lacks `version`. Transmitted bytes reconstructed the intended app hash; no independent flash readback was performed after this update. See [compatibility and retained update gates](usb-update.md#v010-compatibility-and-validation).

These observations do not establish RF accuracy, electrical continuity/timing, calibrated ADC thresholds, other-board compatibility or normal display/button support. The [applied-state display and profile-precedence requirements](design.md#applied-state-display-and-local-control-requirements-unimplemented) describe future behavior only. No new hardware operation or firmware change accompanies this documentation update.

## Portable host tools — offline validation only

The pyserial set/get CLI and updater pass fake-serial and local PTY checks on macOS and Linux. The 18-method portable suite passes all methods on Linux; macOS passes 17 with the Linux controller-lock integration method skipped. It covers native port-name forwarding, bounded partial writes/fragmented reads, malformed replies, timeout/disconnect/no-replay behavior, exact SET acknowledgment plus matching GET, pre-open validation, offline import/help, synthetic app updates and Windows-style staged Tcl paths. Windows branches and COM names are mocked only: **Windows instructions are documented-not-tested**.

Linux also passes the existing 16-method attctl suite (including full output-pipe backpressure and independent cleanup), the updater PTY error method, and host C core/ADC/OLED models. `make all test inspect` could not complete because Arm GCC was unavailable; firmware images, image-dependent tests and boot model were not requalified in this host-tools change. Firmware and attctl source are unchanged. No physical device operation or RF measurement was performed.

## v0.1.1 development OLED probe — offline validation only

The current development app adds the explicit `oledprobe` role sweep described in [the profile](oled-probe.md). The following records its offline validation; later hardware observations are separated above. `make all test inspect` passes with Arm GCC 16.2.0: four C executables and 29 Python test methods run offline. The OLED executable also passes AddressSanitizer and UndefinedBehaviorSanitizer with halt-on-error enabled.

The app is 4,736 bytes (236 vectors + 4,500 text), with a 128-byte mailbox and no data/BSS. Thumb entry is `0x080021ad`, initial SP `0x20004ff8`, and all 59 vectors pass inspection. App BIN SHA256 is `c8698872a876c9a5366138e1d1dce98c7aeccfbf63de58f0cd880809d0095318`; ELF SHA256 is `80b635f1f605239b97844333a9faa1a846850577ee876100ea3d572415431af1`. Loader BIN/ELF remain byte-identical to v0.1.0 below. No release assets have been replaced.

The executable model decodes the production SPI pin events across all 96 trials, including every role assignment, controller/mode index, initialization/pump command, white RAM image, page/column address and numbered pattern. It checks bit order, setup/high/low delays, reset pulses, complete sweep across timer wrap, default inactivity, busy refusal, cancellation in every mapping, and deadline cleanup despite TX backpressure. The actual bounded TX queue is exercised through repeated full/wrap cycles and simulated SET reception while an index line is transmitting. SET ends probe writes and applies the requested value. Existing RF framing, mailbox, ADC and update tests also pass.

Disassembly confirms PA4–PA7-only GPIOA BSRR masks, idle-latch preload before output-mode enable, masked mode/latch restoration, the eight-bit SPI loop, finite trial/deadline checks, nonblocking queued TX and UART TC before loader reset. Boot zero still precedes UART enable. There are no undefined symbols or heap/runtime calls. Compiler stack reports and the static call paths stay below 768 bytes, versus the linker’s 2 KiB reservation; this is not a measured high-water mark. The later observations above establish working roles and legible output on two units; OLED controller identity, supply compatibility, pad timing and full alignment remain unqualified.

## Earlier v0.1.1 ADC-only candidate — offline validation only

The v0.1.1 development app adds explicit PA1 sampling and an app-version reply (`adc`, alias `diag adc`, plus `version`). This earlier ADC-only build has no hardware qualification; the later combined diagnostic app has the separate observations above. `make all test inspect` passes with Arm GCC 16.2.0; the ADC host test also passes AddressSanitizer and UndefinedBehaviorSanitizer with halt-on-error enabled. The three C executables and 29 Python methods run offline, without physical device access.

The app image is 2,960 bytes (236 vectors + 2,724 text), with a 128-byte mailbox and no data/BSS. Thumb entry is `0x08002141`, initial SP `0x20004ff8`, and all 59 vector entries pass inspection. App BIN SHA256 is `9b3b33d11cd4cdce875fcf67d5dcc45ad4922c6b0628a681e9b8186d22f2d356`; ELF SHA256 is `6b2e94ad531d002235316e4a82dac267971f8b25bb6f94c707fa20bce508eec4`. The loader BIN/ELF remain byte-identical to v0.1.0 below. There is no new release asset or hardware qualification implied by this build.

The new executable model runs the production ADC register sequence: all 4096 sample values, delayed self-clearing calibration flags, calibration-data discard, single-conversion EOC, PA1-only mode changes and cleanup. Injected reset-calibration/calibration/EOC timeouts and clock/ownership faults return bounded errors; the UART accepts a subsequent SET. Normal/malformed/overflow/receive-error traffic never accesses ADC. Successful sampling leaves requested attenuation, permutation and issued count unchanged. See [diagnostic limits](diagnostics.md).

Compiled-image inspection confirms 32-bit MMIO callbacks, 100-poll ADC waits, ADON/calibration/start ordering, 12-bit result masking and common power/reset/clock/GPIO cleanup. The diagnostic is reached only from a valid parser request; boot zero still precedes UART enable. There are no undefined symbols or runtime/heap calls. Compiler stack reports 144 bytes for app reset, 48 for the console and ADC sample helpers, 24 for the ADC wait helper, and 56 for the parser: the deepest static path remains below 384 bytes, versus the linker’s 2 KiB reservation. This is a static estimate, not a measured high-water mark.

## v0.1.0 loader/app — single-unit qualification

Built with `arm-none-eabi-gcc (GCC) 16.2.0`, Cortex-M3 Thumb, freestanding C11, `-Os`, warnings as errors, no runtime library. `make all test inspect` passes. Initial SP for both images is `0x20004ff8`; both linkers exclude the top eight RAM bytes and reserve at least 2 KiB stack. All 59 vectors in each actual ELF/BIN pair are checked, with zero reserved core entries. No undefined symbols or heap/runtime calls.

| Image | Flash bytes / sections | Thumb entry | SHA256 of BIN |
| --- | --- | --- | --- |
| Loader | 2,460 = 236 vectors + 2,224 text; zero data/BSS | `0x0800025d` | `01abab39ac7172075801edff3e89fcf3d41eff83d46efa5844186ad47079819d` |
| App | 2,156 = 236 vectors + 1,920 text; 128-byte mailbox, zero data/BSS | `0x08002139` | `9c9c86529cb9516b4a86ce64df147cb97d003bf304975eeec0e89de8afb31e36` |
| Combined loader/app/metadata | 65,536 bytes, including erased-value padding | Loader entry | `381e377c7c9e32c4cfebb4c18603ef085e4c5d7f96774336d4e51a76b9bf93ae` |

ELF SHA256: loader `bd1cba2c28f558d08b6599977282b3b464c22bc8b99a283a37aa5320f05ee1a0`; app `ab490f05f578a96ab93366f52b4fc1a3153550c4563b1c4c571ee08ef8bd43cc`.
HEX SHA256: loader `1082fa00a444d7f125510a672f2751ef835ba7d9338df024128c0ef14f7b4be1`; app `d7c0a1fd5f6a987f13e059ddce0a2571d2336bec6852419193e19aa7b8b1daee`; combined `1b5a31ccdeba2ada04defefd76cf52ccaf73f5cd52db216d2a05eda0e1f9e083`.

Disassembly confirms loader page/halfword range and alignment checks before flash unlock; `strh` programming and readback; EOP/error checks and lock restoration; loader BRR=69; app BRR=625; UART TC wait before request/reset or app handoff; VTOR=`0x08002000`, DSB/ISB, and the final MSP load/Thumb branch. App boot still issues mapping-0 zero before command reception. Compiler stack reports 304 bytes for loader reset, 144 for its parser and at most 48 in its CRC/validation helpers; the deepest static path is below 640 bytes. App reset is 120 bytes, below 256 for its deepest static path. These are static estimates, not measured stack high-water marks.

The existing C GPIO/parser/mailbox suite passes, including the new bootloader command issuing zero before its acknowledgment/request flag. A second C executable models flash and fails every erase/program operation in turn, checking invalidation, commit-last behavior, no loader writes, invalid-app run refusal and explicit recovery. It tests malformed/range/alignment/overflow input, sequential writes, incomplete lines, receive errors, CRC/vector corruption and actual generated combined-image validity. It independently validates Python/zlib's metadata CRC. Twenty-nine Python test methods include the installation model described below, deterministic bundle layout, malformed app rejection before transport, bounded serial failures and the real POSIX serial adapter against a local pseudo-terminal with fragmented replies and exact uploaded bytes. These offline tests use no physical serial port. Both C executables also pass AddressSanitizer and UndefinedBehaviorSanitizer with halt-on-error enabled.

The current combined image passed independent offline review, SWD flash and separate verification on one examined unit. After start, all 65,536 flash bytes were compared with the combined image through bounded reads, including loader, app, metadata and padding. The fresh mailbox reported requested zero on mapping 0 with one issued transaction; GET confirmed it.

With the display/button board reconnected and sole USB power, the CH340 link passed this exact sequence at 115200 8N1:

| Request | Exact reply | Observation |
| --- | --- | --- |
| `get\n` | `0.00\r\n` | Initial requested zero |
| `set 20.00\n` | `ok 20.00\r\n` | Held for 20.000110984 seconds using a monotonic deadline |
| `set 0\n` | `ok 0.00\r\n` | Returned to requested zero |
| `get\n` | `0.00\r\n` | Confirmed zero |

One same-image USB app update then passed: `bootloader` acknowledgment, exact loader version/geometry, 2,156 bytes in 34 sequential chunks (33 × 64 + 44), `finish` acknowledgment after CRC validation/commit, `run` acknowledgment, and app `get` replying `0.00\r\n`. The 40 transmitted request/reply pairs reconstruct the exact app BIN SHA256 listed above, with CRC32 `3b1d60e6`. Both serial sessions closed and the port was released. A later closed-enclosure USB test acknowledged 31.75 dB for 30.000111239 seconds, returned to zero with GET confirmation, then acknowledged 20.00 dB for 20.000108550 seconds and returned to confirmed zero again. The port was released; these UART results alone do not establish RF attenuation.

This was a same-image erase/write/CRC/start test. By itself it does not prove a different-version upgrade (recorded separately above), authenticated firmware, independent post-USB flash readback or physical interrupted-update recovery. CRC is an integrity check, not authentication. No SWD readback was performed after the USB update. Command acknowledgment and GET are not RF measurements.

[v0.1.0](https://github.com/Watts-Blake/pe43703-attenuator-firmware/releases/tag/v0.1.0) publishes the six BIN/HEX files and `SHA256SUMS`. All seven downloaded files were compared with the approved artifacts and all six image checksums passed. Assets were built from [6ed560c](https://github.com/Watts-Blake/pe43703-attenuator-firmware/commit/6ed560c7684230b8fd2d0c95c9c90999d3f3609f); the release target [5df8ea6](https://github.com/Watts-Blake/pe43703-attenuator-firmware/commit/5df8ea63036cb79eda40330bc25da7e1e4d4cf5d) has identical firmware, tools and Makefile. The earlier-application section records qualification of the preceding app image.

### Closed-enclosure USB staircase and phone observations

After the same-image USB update, an initial GET confirmed requested zero. The following nine settings were issued in order, each acknowledged and held for at least 15 seconds using a monotonic deadline. There were no intermediate zero commands. Final SET0/GET0 confirmed requested zero, then the serial port was closed and released.

| Requested dB | Reported RSRP (dBm) | Drop from initial zero (dB) |
| --- | --- | --- |
| 0.00 | −85 | 0 |
| 10.00 | −95 | 10 |
| 15.00 | −101 | 16 |
| 20.00 | −105 | 20 |
| 22.00 | −107 | 22 |
| 25.00 | −109 | 24 |
| 28.00 | −110 | 25 |
| 30.00 | −110 | 25 |
| 31.75 | −110 | 25 |

The relative observed response was within about 1 dB of the requested change through roughly 22–25 dB. At the highest settings, RSRP flattened near −110 dBm while reported SNR fell from about 21 dB toward 0 dB. This saturation is consistent with the phone measurement/noise floor. Phone RSRP/SNR alone cannot identify the limiting path or exclude RF leakage; these results do not establish calibrated attenuation accuracy, insertion loss or frequency response. The earlier enclosure-leakage interpretation in the README is historical, not a demonstrated explanation of either test.

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

Hardware evidence on one unit: the standard OpenOCD driver completed unprotection; an actual power cycle and post-POR RDP0/OPTERR0, real UID and 64 KiB density checks passed. The listed BIN was flashed and independently verified. With the core running, full image comparison in 64-byte reads and a fresh 128-byte mailbox snapshot passed. Mapping 0 acknowledged SET20.00 dB, a monotonic 20-second hold, SET0 and final GET0. The RF comparison is summarized in the README and is not a calibrated attenuation measurement. The current loader/app image has the separate qualification record above.

Still unqualified: GPIO/UART continuity; oscillator and pad timing/voltage measurements; UART operation across supply/temperature limits; OLED controller identity/full alignment and normal display/button operation; physical interrupted-update recovery; insertion loss, calibrated attenuation accuracy and frequency range. Other mapping permutations and other board variants remain unqualified. The one-unit USB results above do not remove those limits. The stock image has no backup or restoration path. Configuration/driver models do not establish absence of opaque adapter effects or real hardware behavior on other boards.
