# PE43703 USB attenuator firmware

**Display changes but attenuation doesn't? USB/serial commands have no effect, the PC software doesn't work, or the `wv` command gets no reply?** This project develops replacement firmware for a LiebeWH-style OLED digital programmable attenuator: the aluminium, SMA-to-SMA, STM32F103C8T6 + PE43703 board listed as a USB-programmable 0–31.75 dB, 0.25 dB-step, 6 GHz digital RF attenuator.

**Status: the published v0.1.0 loader/app image passed SWD flash, independent verification, USB serial commands and one same-image USB app update on one examined unit.** [v0.1.0 downloads](https://github.com/Watts-Blake/pe43703-attenuator-firmware/releases/tag/v0.1.0) include combined, app-only and loader BIN/HEX files plus checksums. The v0.5.0 candidate adds [the Stacked Hero applied-value screen and saved A/B badge](docs/screen.md), building on [explicit five-button calibration](docs/button-calibration.md) after [guided OLED identification and saved alignment](docs/display-identification.md) on the offline v0.2.0 screen/buttons; all candidate hardware behavior remains UNRUN; the published v0.1.0 images omit them. These results do not establish compatibility with every similar board or calibrated RF performance. The advertised frequency range is a product description, not a measured claim.

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
- PE43703 address zero, full 16-bit frame, quarter-dB steps, LSB first, rising CLK sampling and an LE pulse. The v0.2.0 candidate uses the observed fixed OLED mapping and PA1 button ladder. Boot zero precedes screen initialization. See [display, pending/apply buttons and profile precedence](docs/design.md#applied-state-display-and-local-control). Published v0.1.0 omits display/buttons.
- Normal operation uses mapping 0: SI=PB11, CLK=PB12, LE=PB13. Boot always selects it; the production UART interface cannot change it. Alternative mappings remain available only through the SWD debug mailbox with explicit `--debug-select`. Read status before testing; GPIO-to-chip continuity remains untraced.
- Assumed 8 MHz HSE crystal → 72 MHz CPU/APB2, 36 MHz APB1. HSE is a design assumption, not a measured oscillator frequency. Clock startup fails closed with a readable mailbox fault; no attenuation command is accepted in that state.

| Pins | Use | Qualification |
| --- | --- | --- |
| PB11/PB12/PB13 | SI/CLK/LE in selectable order | Mapping 0 functionally tested on one unit; connections not continuity-traced |
| PA4–PA7 | v0.2.0 OLED: PA6 SCK, PA7 MOSI, PA5 D/C, PA4 RES | Column-zero SSD1306-compatible rendering observed on two units; see [probe observations](docs/oled-probe.md#observed-wiring-on-two-units) |
| PA9 / PA10 | USART1 TX / RX | CH340 command/reply and app update tested on one unit; traces not continuity-checked |
| PA13 / PA14 | SWDIO / SWCLK | Preserve SWD; no debug-port remap |

[Hardware installation guide](docs/install.md) · [Mailbox and mapping selection](docs/mailbox.md) · [Design references and limits](docs/design.md)

For stable two-unit USB control and timed schedules, see [attctl](docs/attctl.md). The Linux CLI verifies replies, shares an exclusive controller lock, and attempts independent zero cleanup when a profile ends.

## Build and offline checks

Install GNU Arm Embedded tools (`arm-none-eabi-gcc`, binutils), a C11 host compiler, GNU Make, Python 3.10+ and Tcl (`tclsh`). No SDK, heap or runtime library is needed. Serial tools and their tests additionally use `python3 -m pip install -r requirements.txt` (pyserial 3.5); use an isolated environment as described in [host setup](docs/host-tools.md).

```sh
make all
make test
make inspect
```

ELF, BIN, HEX, map and disassembly files are written to `build/` for both loader and app. `combined.bin`/`.hex` contain the loader, app and committed CRC metadata for initial SWD installation; `attenuator.bin` is the app-only USB update. The 64 KiB flash layout reserves 8 KiB for the loader, 55 KiB for app and 1 KiB for metadata, including the append-only display/button journal. Runtime saves never erase; app updates clear the journal without changing update geometry. RAM reserves eight bytes for the reset request and at least 2 KiB for stack. Tests use simulated GPIO/flash, a Tcl model and a local pseudo-terminal. They never launch OpenOCD or contact a physical USB, UART or SWD device. [Validation notes](docs/validation.md) describe coverage and what remains untested.

## USB serial use

For a minimal Python set/get CLI on Windows, macOS and Linux, see [host tools and CH340 drivers](docs/host-tools.md): `python tools/attenuator.py get --port COM7` or `python tools/attenuator.py set --port /dev/cu.wchusbserial-example 0`. USB updates use the same pyserial transport. Windows installation/flashing instructions are documented-not-tested.

Connect the board's CH340 micro-USB port, select its exact serial port in a terminal at **115200, 8 data bits, no parity, 1 stop bit, no flow control**, and send one command at a time. Wait for the complete response before sending the next command.

```text
set 18.25
ok 18.25
get
18.25
set 0
ok 0.00
```

Commands accept CR, LF or CRLF. Values must be 0–31.75 dB in exact 0.25 dB steps, with one or two digits before the optional decimal point and up to two after it. Attenuation responses always use two decimal places and CRLF. Invalid syntax, range, overlong input or UART errors return `err` at the line boundary without applying that line. A partial line occupies at most 31 bytes and waits for its terminator. An overlong/bad line is discarded through its terminator; then a fresh command can be sent. There is no command echo or unsolicited boot banner. Attenuation is not persisted; v0.5.0 saves only explicitly selected display, complete button configurations and the A/B label. App updates reset these settings.

**Input diagnostics (introduced in v0.1.1):** `adc` (alias `diag adc`) returns one raw PA1 sample as `adc NNNN\r\n`, or `err adc\r\n` on a bounded ADC failure. It is inactive until requested and does not change attenuation. This addition is offline-tested and absent from the published v0.1.0 images; two-unit [button ladder observations and candidate unqualified bands](docs/diagnostics.md#observed-pa1-front-button-ladder) are documented separately. `version` reports `version 0.5.0` in the current candidate. See [input diagnostics](docs/diagnostics.md). `oledprobe` starts a finite visual role sweep with indexed USB output; a valid SET cancels it. See [exact trial mapping and limits](docs/oled-probe.md).

**Observed on one unit:** `get` returned `0.00\r\n`; `set 20.00` returned `ok 20.00\r\n`. After a monotonic 20-second hold, `set 0` returned `ok 0.00\r\n` and `get` returned `0.00\r\n`, with the display/button board connected.

**USB updates:** the CH340 is a USB-to-UART bridge. One same-image, 2,156-byte app update passed through the UART loader in 34 chunks, with CRC-completion acknowledgment and `get` returning zero after app start. This exercised the update path, without a version change or independent post-update SWD readback. CRC detects corruption; it is not authentication. After initial SWD installation, use the [app-only update guide](docs/usb-update.md). A later [v0.1.0-to-v0.1.1 update](docs/usb-update.md#v010-compatibility-and-validation) succeeded using `get` → `bootloader`; v0.1.0 has no `version` query. Keep SWD access until command and app-update checks pass on the particular unit.

## FAQ

### Why doesn't my USB attenuator change attenuation?

On the examined STM32F103 attenuator, the stock display and serial path did not establish that a new value was latched into the PE43703. A displayed number or accepted host write is not an RF measurement. Board variants, framing, UART wiring, firmware behavior and measurement conditions all matter. The replacement issues the complete RF control transaction and replies afterward. The single-unit functional results above passed; continuity, calibrated RF performance and other board variants remain unqualified.

### Can I restore the original firmware after removing protection?

No backup is available. Removing STM32F1 read protection erases the original firmware. The installation guide and tools separate that irreversible step from flashing and verification.

### Will the OLED and five buttons work?

Published v0.1.0 omits display/buttons. The v0.2.0 candidate adds a fixed SSD1306-compatible screen showing APPLIED, PENDING, STEP, source and lock status. PLUS/MINUS edit pending attenuation, OK applies, and LEFT/RIGHT cycle the 0.25/1/5 dB step; values clamp at 0.00–31.75 dB. An active [USB profile lease](docs/attctl.md#firmware-profile-leases) suppresses local actions. New firmware behavior is offline-tested only; [two-unit OLED compatibility](docs/oled-probe.md#observed-wiring-on-two-units) and [PA1 codes](docs/diagnostics.md#observed-pa1-front-button-ladder) do not qualify thresholds or RF performance. [`tools/display_identify.py`](docs/display-identification.md) holds both labeled alignment patterns, optionally sweeps the pin roles and saves only an explicit confirmed choice. `tools/button_calibrate.py` then guides five stable holds and releases, shows medians, rejects overlapping bands and saves only after explicit confirmation; see [calibration and capacity limits](docs/button-calibration.md). The factory default stays trial 41 / column zero. `oledtest` is a bounded fixed-pattern alignment diagnostic; `oledprobe` retains the opt-in role sweep. See [complete control semantics](docs/design.md#applied-state-display-and-local-control).

### Is `get` a measurement?

No. It returns the last requested attenuation for which firmware issued a complete transaction. PE43703 has no RF measurement/readback channel in this interface. Confirm RF performance later with appropriate inline measurement equipment.

### Does this work on every LiebeWH / ATT6000 / 6 GHz USB attenuator?

No such compatibility claim is made. Match the MCU, RF chip, oscillator, logic voltage, board traces and boot wiring before installing. The six GPIO permutations support investigation; they do not prove the candidate pins are connected to the PE43703.

## License

[MIT](LICENSE) for this project's firmware, tools and documentation. The unmodified OpenOCD test fixtures retain their [upstream licenses](tests/fixtures/openocd-012/COPYING); see their [provenance](tests/fixtures/README.md). Third-party datasheets and linked tools retain their own licenses; no vendor firmware is included.
