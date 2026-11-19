# OLED role probe (development)

`oledprobe` starts a finite visual diagnostic, inactive at boot. This development addition is offline-tested and has not been installed or qualified on hardware. Buttons remain inert, and ordinary `set`/`get` behavior stays authoritative. The published v0.1.0 binaries have no probe command.

The profile assumes PA4–PA7 connect only to 3.3 V-compatible OLED inputs and a 128×64 panel using its internal charge pump. It tries SSD1306 and SH1106 initialization without identifying the actual controller or wiring. These assumptions are not measured voltage, continuity or compatibility evidence for other boards. No RF pin or attenuation setting is changed by the probe.

Send `oledprobe\n` at 115200 8N1. Reply is `ok oledprobe\r\n`; requesting another sweep while active returns `err busy\r\n` without restarting it. Each trial emits an index line such as `oled 00 S C\r\n` before output begins. S means SSD1306; H means SH1106. C means SCK/MOSI/DC/CS with module reset assumed tied high. R means SCK/MOSI/DC/RES with module CS assumed tied active. Both use 8-bit serial data with a separate DC line; R does not select the controller’s 9-bit 3-wire serial protocol.

Each trial initializes the selected family, writes a full-white 128×64 RAM image at low contrast, holds the white RAM image for at least 100 ms after issuing display-on, then draws a border and the two-digit global trial number. It holds the numbered pattern until about 2 seconds from that trial’s index announcement. A complete normal sweep takes about 192 seconds plus bounded loop/transport overhead. The sequence stops after 96 trials, with an independent 210-second deadline. Completion emits `oled done\r\n`; deadline expiry emits `oled timeout\r\n` when UART output has space. Output backpressure cannot prevent deadline cleanup.

A valid `set <dB>` cancels the probe during command processing and applies that requested attenuation. `set 0` is the normal stop command. Mailbox SET/permutation changes and the `bootloader` command also cancel it. GET, ADC sampling and version queries can be used without restarting the sweep. Cancellation/completion attempts display-off on the current trial mapping and restores PA4–PA7’s previous GPIO modes/output latches. An incorrect mapping may leave a visible image; no later probe writes occur until another explicit request. Boot itself makes no OLED writes.

## Stable index mapping

For permutation p in 0–23 below, global index is `p + 24*mode + 48*family`, where mode C=0/R=1 and family S=0/H=1. Thus 00–23 are SSD1306/C, 24–47 SSD1306/R, 48–71 SH1106/C, 72–95 SH1106/R. The fourth pin’s role is CS in C mode and RES in R mode. Record the exact index, family and mode that displayed a pattern; a lit display alone does not distinguish compatible controllers.

| p | SCK | MOSI | DC | CS or RES |
|---|---|---|---|---|
| 00 | PA4 | PA5 | PA6 | PA7 |
| 01 | PA4 | PA5 | PA7 | PA6 |
| 02 | PA4 | PA6 | PA5 | PA7 |
| 03 | PA4 | PA6 | PA7 | PA5 |
| 04 | PA4 | PA7 | PA5 | PA6 |
| 05 | PA4 | PA7 | PA6 | PA5 |
| 06 | PA5 | PA4 | PA6 | PA7 |
| 07 | PA5 | PA4 | PA7 | PA6 |
| 08 | PA5 | PA6 | PA4 | PA7 |
| 09 | PA5 | PA6 | PA7 | PA4 |
| 10 | PA5 | PA7 | PA4 | PA6 |
| 11 | PA5 | PA7 | PA6 | PA4 |
| 12 | PA6 | PA4 | PA5 | PA7 |
| 13 | PA6 | PA4 | PA7 | PA5 |
| 14 | PA6 | PA5 | PA4 | PA7 |
| 15 | PA6 | PA5 | PA7 | PA4 |
| 16 | PA6 | PA7 | PA4 | PA5 |
| 17 | PA6 | PA7 | PA5 | PA4 |
| 18 | PA7 | PA4 | PA5 | PA6 |
| 19 | PA7 | PA4 | PA6 | PA5 |
| 20 | PA7 | PA5 | PA4 | PA6 |
| 21 | PA7 | PA5 | PA6 | PA4 |
| 22 | PA7 | PA6 | PA4 | PA5 |
| 23 | PA7 | PA6 | PA5 | PA4 |

## Protocol and bounds

Both serial encoders send MSB first and sample on rising clock, with 1 µs data setup/high/low and an extra 1 µs byte-ending interval. R mode pulses reset low 10 µs and waits 10 µs after releasing it. C mode leaves reset to the module. Each main-loop call emits at most one byte (25 µs explicit delay) or one 20 µs reset transition; holds use a wrap-safe millisecond state machine. These are software delay bounds under the working 72 MHz clock contract, not measured pad timing or total execution latency.

The exact init lists are in `firmware/oled.c`. SSD1306 uses page addressing, `8D 14` pump enable and column 0. SH1106 uses `AD 8B` internal DC-DC enable and column 2 for a 128-column window in its 132-column RAM. Both use 64 commons, normal RAM display, normal pixel polarity, reversed segment/common mapping and contrast 0x10. White fill uses RAM data rather than an entire-display-on override. Each image sends 8 pages of 3 positioning-command bytes and 128 data bytes: 1048 bytes. There is no framebuffer allocation, controller readback, automatic inference or unbounded GPIO scan.

Primary references are the manufacturer-authored [SSD1306 Rev1.1](https://cdn-shop.adafruit.com/datasheets/SSD1306.pdf), §8.1.3, command table and charge-pump appendix, and [SH1106 V2.6](https://www.displayfuture.com/Display/datasheet/controller/SH1106.pdf), serial interface, commands 10/11, DC-DC initialization and reset timing. The trial profile uses their command encodings; it does not establish the actual panel’s supply topology.

UART transmission uses a 64-byte queue, sending at most one ready byte per loop so an index line does not block RX. Replies and index lines stay whole and ordered. Space for a maximum 16-byte response is reserved before accepting a new received byte or announcing a trial; overrun errors retain the normal discard/recovery behavior. Clients should still send one command at a time and wait for its reply; unsolicited index lines must be recognized while the probe is active. A command can encounter an already queued index line before its response.

`make test` decodes production SPI events for all 96 trials, checks pin masks/bit order/control signals/reset timing/init lists/pump commands/page placement/white and numbered RAM output, and completes the full sweep across timer wrap. It checks cancellation in every mapping, busy/default/timeout behavior, hard cleanup under UART backpressure, bounded queue ordering and SET reception during an index line. Hardware behavior remains unqualified.
