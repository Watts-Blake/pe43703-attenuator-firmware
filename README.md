# PE43703 USB attenuator firmware

**Display changes but attenuation doesn't? USB/serial commands have no effect, the PC software doesn't work, or the `wv` command gets no reply?** This project develops replacement firmware for a LiebeWH-style OLED digital programmable attenuator: the aluminium, SMA-to-SMA, STM32F103C8T6 + PE43703 board listed as a USB-programmable 0–31.75 dB, 0.25 dB-step, 6 GHz digital RF attenuator.

**Status: the current loader/app image passed SWD flash, independent verification, USB serial commands and one same-image USB app update on one examined unit.** [v0.1.0 downloads](https://github.com/Watts-Blake/pe43703-attenuator-firmware/releases/tag/v0.1.0) include combined, app-only and loader BIN/HEX files plus checksums. Display/buttons remain unsupported. These results do not establish compatibility with every similar board or calibrated RF performance. The advertised frequency range is a product description, not a measured claim.

In earlier SWD-mailbox tests of the preceding app image, a phone reported about −85 dBm RSRP before a 20.00 dB command, then −103 dBm about 5 and 15 seconds afterward. After the zero command it returned to −85 dBm at both follow-up samples. This roughly 18 dB reported change supports control with SI=PB11, CLK=PB12, LE=PB13. Phone RSRP and the test enclosure do not establish calibrated attenuation accuracy, insertion loss or performance across the advertised frequency range.

A separate 31.75 dB command produced about −108 dBm RSRP at samples roughly 7, 19 and 27 seconds into its 30-second hold, from a −85 dBm baseline. After zero, readings returned to −86 and −85 dBm roughly 10 and 20 seconds later. Reported SNR fell from about 23.8 dB to 2.4–3.0 dB and recovered. The approximately 23 dB RSRP drop does not establish 31.75 dB attenuation accuracy. An enclosure leakage floor was an initial interpretation of this earlier observation, rather than an established cause.

In a later closed-enclosure USB staircase with the released loader/app, each setting below was acknowledged and held for 15 seconds, in the listed order, before a final `set 0` / `get` confirmed requested zero. A phone reported:

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

The relative observed response was within about 1 dB of the requested change through roughly 22–25 dB. At the highest settings, RSRP flattened near −110 dBm while reported SNR fell from about 21 dB toward 0 dB. This saturation is consistent with the phone measurement/noise floor. Phone RSRP/SNR alone cannot identify the limiting path or exclude RF leakage; these results do not establish calibrated attenuation accuracy, insertion loss or frequency response.

## Board and original-firmware symptoms

The examined board has an OLED, five front-panel buttons, a micro-USB CH340 serial bridge, a TTL UART header, a lower RF board with an STM32F103C8T6 and PE43703, and a removable display/button board connected through a 2×5 header. Similar-looking products can have different wiring or firmware.

On the examined unit, stock serial command attempts produced display changes without a demonstrated change in RF attenuation, and no acknowledgment was received. A phone's reported RSRP remained unchanged with the display at 31.75 dB; a manual front-panel setting followed by OK did produce an attenuation indication. Phone RSRP is not a calibrated measurement of insertion loss. These observations apply to that unit and those attempts; they do not establish the behavior of every firmware revision or every possible stock command. The stock firmware is readout-protected. **No original firmware backup exists; stock restoration is unavailable.**

Known formats investigated or attempted at 115200 8N1 include:

| Format | Example requesting 31.75 dB | Exact hexadecimal bytes | Result/limit |
| --- | --- | --- | --- |
| ATT Windows tool / community `wv` text | `wv03175\n` | `77 76 30 33 31 37 35 0a` | Tool sends LF; expects `ok\r\n`. Attempts did not establish RF control or an acknowledgment on the examined unit. |
| Community binary integer + hundredths + CRLF | bytes, not ASCII hex | `1f 4b 0d 0a` | No reply in the retained attempt; physical application unknown. Display alone is insufficient. |

The text format comes from static inspection of the bundled tool in [att6000_control](https://github.com/emptemp/att6000_control/tree/a65e9aa50e81866d0176bfb1503adc4a10c41ce3). It is not a manufacturer protocol specification for every board. This replacement implements the new protocol below, without compatibility with those old formats.

## Replacement behavior

- Boot requests and latches the **0.00 dB command** before accepting commands. This does not establish physical 0 dB or remove the PE43703's inherent insertion loss.
- An 8 KiB UART bootloader at `0x08000000`; application at `0x08002000`, CRC commit page at `0x0800fc00`. App-only USB updates cannot address loader pages. See [USB update instructions](docs/usb-update.md).
- USART1 on PA9/PA10, 115200 8N1: `set <dB>` replies `ok <dB>` after the complete serial transaction; `get` returns the last command issued. Neither is RF readback.
- PE43703 address zero, full 16-bit frame, quarter-dB steps, LSB first, rising CLK sampling and an LE pulse. No display or button driver in v1; use the display detached for initial qualification. PA4–PA7 stay at reset input levels unless the development OLED probe is explicitly requested.
- Normal operation uses mapping 0: SI=PB11, CLK=PB12, LE=PB13. Boot always selects it; the production UART interface cannot change it. Alternative mappings remain available only through the SWD debug mailbox with explicit `--debug-select`. Read status before testing; GPIO-to-chip continuity remains untraced.
- Assumed 8 MHz HSE crystal → 72 MHz CPU/APB2, 36 MHz APB1. HSE is a design assumption, not a measured oscillator frequency. Clock startup fails closed with a readable mailbox fault; no attenuation command is accepted in that state.

| Pins | v1 use | Qualification |
| --- | --- | --- |
| PB11/PB12/PB13 | SI/CLK/LE in selectable order | Mapping 0 functionally tested on one unit; connections not continuity-traced |
| PA4–PA7 | Inputs by default; opt-in OLED role probe | Display roles remain unqualified; see [probe profile](docs/oled-probe.md) |
| PA9 / PA10 | USART1 TX / RX | CH340 command/reply and app update tested on one unit; traces not continuity-checked |
| PA13 / PA14 | SWDIO / SWCLK | Preserve SWD; no debug-port remap |

[Hardware installation guide](docs/install.md) · [Mailbox and mapping selection](docs/mailbox.md) · [Design references and limits](docs/design.md)

## Build and offline checks

Install GNU Arm Embedded tools (`arm-none-eabi-gcc`, binutils), a C11 host compiler, GNU Make, Python 3.10+ and Tcl (`tclsh`). No SDK, heap, runtime library or Python package installation is needed.

```sh
make all
make test
make inspect
```

ELF, BIN, HEX, map and disassembly files are written to `build/` for both loader and app. `combined.bin`/`.hex` contain the loader, app and committed CRC metadata for initial SWD installation; `attenuator.bin` is the app-only USB update. The 64 KiB flash layout reserves 8 KiB for the loader, 55 KiB for app and 1 KiB for metadata. RAM reserves eight bytes for the reset request and at least 2 KiB for stack. Tests use simulated GPIO/flash, a Tcl model and a local pseudo-terminal. They never launch OpenOCD or contact a physical USB, UART or SWD device. [Validation notes](docs/validation.md) describe coverage and what remains untested.

## USB serial use

Connect the board's CH340 micro-USB port, select its exact serial port in a terminal at **115200, 8 data bits, no parity, 1 stop bit, no flow control**, and send one command at a time. Wait for the complete response before sending the next command.

```text
set 18.25
ok 18.25
get
18.25
set 0
ok 0.00
```

Commands accept CR, LF or CRLF. Values must be 0–31.75 dB in exact 0.25 dB steps, with one or two digits before the optional decimal point and up to two after it. Attenuation responses always use two decimal places and CRLF. Invalid syntax, range, overlong input or UART errors return `err` at the line boundary without applying that line. A partial line occupies at most 31 bytes and waits for its terminator. An overlong/bad line is discarded through its terminator; then a fresh command can be sent. There is no command echo, unsolicited boot banner or persistent setting storage.

**v0.1.1 development diagnostic:** `adc` (alias `diag adc`) returns one raw PA1 sample as `adc NNNN\r\n`, or `err adc\r\n` on a bounded ADC failure. It is inactive until requested and does not change attenuation. This addition is offline-tested and absent from the published v0.1.0 images; button thresholds and the display interface remain unqualified. `version` reports `version 0.1.1`. See [input diagnostics](docs/diagnostics.md). `oledprobe` starts a finite visual role sweep with indexed USB output; a valid SET cancels it. See [exact trial mapping and limits](docs/oled-probe.md).

**Observed on one unit:** `get` returned `0.00\r\n`; `set 20.00` returned `ok 20.00\r\n`. After a monotonic 20-second hold, `set 0` returned `ok 0.00\r\n` and `get` returned `0.00\r\n`, with the display/button board connected.

**USB updates:** the CH340 is a USB-to-UART bridge. One same-image, 2,156-byte app update passed through the UART loader in 34 chunks, with CRC-completion acknowledgment and `get` returning zero after app start. This exercised the update path, without a version change or independent post-update SWD readback. CRC detects corruption; it is not authentication. After initial SWD installation, use the [app-only update guide](docs/usb-update.md). Keep SWD access until command and app-update checks pass on the particular unit.

## FAQ

### Why doesn't my USB attenuator change attenuation?

On the examined STM32F103 attenuator, the stock display and serial path did not establish that a new value was latched into the PE43703. A displayed number or accepted host write is not an RF measurement. Board variants, framing, UART wiring, firmware behavior and measurement conditions all matter. The replacement issues the complete RF control transaction and replies afterward. The single-unit functional results above passed; continuity, calibrated RF performance and other board variants remain unqualified.

### Can I restore the original firmware after removing protection?

No backup is available. Removing STM32F1 read protection erases the original firmware. The installation guide and tools separate that irreversible step from flashing and verification.

### Will the OLED and five buttons work?

Published v0.1.0 omits display/button support and makes no writes on PA4–PA7. Development diagnostics add an explicit [OLED role probe](docs/oled-probe.md); ordinary operation still leaves the display idle and buttons inert. Initial SWD tests used a detached display; USB qualification passed with the display/button board reconnected. The display interface has not been identified.

### Is `get` a measurement?

No. It returns the last requested attenuation for which firmware issued a complete transaction. PE43703 has no RF measurement/readback channel in this interface. Confirm RF performance later with appropriate inline measurement equipment.

### Does this work on every LiebeWH / ATT6000 / 6 GHz USB attenuator?

No such compatibility claim is made. Match the MCU, RF chip, oscillator, logic voltage, board traces and boot wiring before installing. The six GPIO permutations support investigation; they do not prove the candidate pins are connected to the PE43703.

## License

[MIT](LICENSE) for this project's firmware, tools and documentation. The unmodified OpenOCD test fixtures retain their [upstream licenses](tests/fixtures/openocd-012/COPYING); see their [provenance](tests/fixtures/README.md). Third-party datasheets and linked tools retain their own licenses; no vendor firmware is included.
