# Identify and save OLED alignment (v0.3.0 candidate)

**Hardware UNRUN.** This candidate adds a guided visual comparison and persistent display configuration. It does not identify controller silicon, calibrate buttons or measure RF attenuation. The earlier two-unit observation is SSD1306-compatible column-zero alignment, not definitive chip identification. Both examined units use PA6=SCK, PA7=MOSI, PA5=D/C, PA4=RES; those remain the factory default (trial 41). See [observations and controller references](oled-probe.md#observed-wiring-on-two-units).

SSD1306 has 128-column RAM; SH1106 has 132-column RAM, commonly with a 128-column visible window starting at column 2. A wrong offset shifts or clips an edge. Module size alone (including 0.91, 0.96 or 1.3 inches) cannot identify a controller or even establish a 128×64 panel. Only compatible 128×64 panels with the documented electrical assumptions are in scope. A readable label is insufficient evidence to choose a configuration.

## Guided helper on Windows, macOS and Linux

Install Python 3.10+ and `requirements.txt` in an isolated environment as described in [host setup](host-tools.md). Identify the exact CH340 port and close other controllers/terminals. No simultaneous SWD mailbox writer is permitted. The helper uses the established exclusive serial transport, including the shared Linux controller lock, POSIX cooperative exclusive lock and Windows native exclusive open. It never scans ports. Windows and macOS behavior is covered by portable fakes; physical use of this helper on every platform is UNRUN.

These commands are **device operations**, shown for a future deliberately owned session; they are not build/test commands:

```powershell
python tools/display_identify.py --port COM7
```

```sh
python tools/display_identify.py --port /dev/cu.wchusbserial-example
python tools/display_identify.py --port /dev/ttyUSB0
```

1. The helper requires the exact reply `display 1\r\n` to the capability query. Legacy `err`, other revisions and malformed replies stop before any pattern or write. It reads the saved trial and remaining journal slots.
2. SSD1306 initialization with column zero is shown first on the saved pin roles. Examine all four continuous outer edges, the short inner edge columns and the asymmetric markers. Press Enter in the helper to show SH1106 initialization with column two on those same pins.
3. Type `SSD1306`, `SH1106` or `neither` when asked which had a complete border. If both appear correct, choose `neither` and investigate; the helper makes no automatic silicon inference.
4. The selected trial is shown again. Only the exact confirmation `SAVE NN` (the displayed two-digit trial) permits persistence. The helper waits for save completion and checks the saved configuration. Normal rendering then uses that choice. `neither`, `cancel`, EOF, Ctrl-C or a prompt timeout saves nothing before an explicit save request.

```text
Complete (all four edges):        Wrong offset (example; direction varies):
+--------------------------+      +--------------------------
|:: L                      |      |:: L
|       SSD1306             |      |       SH1106
|           41          R :|      |           89          R
+--------------------------+      +--------------------------
 ^ continuous left/right edges     ^ an edge is missing or shifted

Actual pattern: outer columns 0/127, rows 0/63; short inner markers
at columns 1/2 (upper left) and 125/126 (lower right), plus page numbers.
```

The schematic illustrates clipping, not an exact panel photograph. Compare both patterns on the particular board. The label describes the trial's command family, not the installed chip. Neither comparison changes pending or applied attenuation.

## Optional different-wiring sweep

Use `--sweep` **explicitly** only when the display connections are compatible 3.3 V OLED inputs on PA4–PA7. The helper cannot verify electrical compatibility:

```sh
python tools/display_identify.py --port /dev/ttyUSB0 --sweep
```

This reuses [all 96 existing role/family indices](oled-probe.md#stable-index-mapping), holding each labeled trial until Enter advances it. Type `yes` only for a complete pattern, then confirm `SAVE NN` on the repeated trial. No automatic winner is selected. C mode treats the fourth pin as CS with reset externally held high; R mode treats it as RES with CS externally active. This does not probe arbitrary GPIOs. A session can end before all 96 trials; a new explicit session starts at 00. Manual users can use `display try NN` within a sweep to revisit a recorded index.

## Bounded sessions and ownership

A frame remains held until an explicit `next`/`try`, a fresh debounced front-key press, finish/cancel, a valid external SET, profile acquisition, or a safety deadline. A key only advances the trial; it cannot apply attenuation or save. Release debounce is required between keys and before local attenuation editing resumes. Use the helper's prompts without pressing front keys; an unexpected trial change makes it stop.

Each session has a **120-second inactivity deadline** and an independent **600-second total deadline**, including frame setup and save. Only start, next, try, save or an accepted advance key refresh inactivity. GET/status polling cannot keep it alive. Each host prompt has a 90-second deadline, and the helper bounds rendering/save waits to five seconds and every serial request to the configured `--timeout` (default two seconds, maximum 30). Holding the serial port open while waiting for a user does not block firmware UART processing. Deadline cleanup proceeds even when TX has no space; no unsolicited identification replies are queued.

An active USB profile rejects a new identification session; profile acquisition cancels an existing session before taking ownership. Direct SET, token-bound profile SET and mailbox applied changes retain their existing behavior and cancel identification. Diagnostics never issue an RF transaction. A malformed diagnostic command, bad/overlong UART line or receive error cancels the session. Cancel/error/timeout restores the previously saved display configuration without altering applied attenuation. Existing `oledtest` (fixed factory pattern) and finite `oledprobe` remain available; [legacy semantics](oled-probe.md) are preserved. Starting the legacy probe cancels identification.

The CH340 provides no disconnect notification. On a failed UART session the helper closes without replay, reopening or fallback commands; firmware restores normal rendering within 120 seconds of the last advance, subject to its total deadline. Before an explicit save, this retains the previous choice. If a confirmed save loses its acknowledgment or power is interrupted, completion is unknown: either the previous valid record or a fully committed new record is used. Read `display get` in a new deliberately owned session before deciding to retry. Cancellation cannot undo an already committed save.

## Exact protocol revision 1

115200 8N1, CR/LF/CRLF, one outstanding command. Replies use CRLF. `NN` is exactly two decimal digits, 00–95; it is a trial index, not a pin mask. No command changes RF attenuation. Status does not acknowledge physical rendering.

| Command | Reply / behavior |
| --- | --- |
| `display` | Exactly `display 1`; side-effect-free capability query |
| `display get` | `config NN SS`; saved trial and remaining slots 00–62 |
| `display start` | `ok display`; compare S then H on saved roles, starting at saved index modulo 48 |
| `display sweep` | `ok display`; explicit all-role session, starting at 00 |
| `display status` | `trial NN W` while drawing, `trial NN R` when held; `saving`, `saved NN`, `idle`, `err save` or `err timeout` |
| `display next` | `ok next`; advance a completed frame (S/H pair cycles, sweep wraps after 95) |
| `display try NN` | `ok try`; show the exact index (same roles only in pair mode; any index in sweep mode) |
| `display save NN` | `ok saving`; only the current completed trial is accepted; explicit persistent save, then automatic normal-display restoration |
| `display finish` | `ok finish`; close without saving, restore normal display |
| `display cancel` | `ok cancel`; stop any uncommitted save, restore normal display; idempotent |

Busy starts return `err busy` without disturbing the owner. Invalid display operations return `err display` and cancel. Store refusal/failure returns `err save`. Query `status` until save completion, then verify `get`; do not treat `ok saving` as durable completion. Queries and finish/cancel do not open an inactive session. There is no speculative automatic choice, controller readback, authentication or button calibration in this protocol.

## Persistent record and flash compatibility

The actual 64 KiB geometry in `firmware/layout.h` and the loader is unchanged: loader `0x08000000..0x08001fff`, app `0x08002000..0x0800fbff` (55 KiB), metadata page `0x0800fc00..0x0800ffff` (1 KiB). Loader validity uses only the first **20 bytes** of metadata and the CRC over the committed app size. The journal occupies the unused metadata tail `0x0800fc20..0x0800ffff`: 62 records of 16 bytes. Bytes 20–31 remain untouched. No app capacity is removed; loader code, hello geometry, app maximum, mailbox ABI and reserved reset RAM are unchanged.

Each little-endian record contains version 1 (u16), trial (u16), trial complement (u16), reserved zero (u16), CRC-32/ISO-HDLC over those first eight bytes (u32), then commit `0x44535031` (u32), written last. Boot accepts only exact version/reserved/complement/CRC/commit and an index below 96. A corrupt record can never supply unchecked GPIO bits. Scanning selects the last valid physical record; no valid record falls back to factory trial 41. Interrupted/corrupt records are skipped; a previous valid record remains usable. CRC is corruption detection, not authentication.

Only explicit save programs flash, at most eight halfwords in separate main-loop polls, with erased checks, readback and a bounded busy poll. The target program routine and literal pool execute from copied RAM, with HSI retained. A detected write failure disables further saves until restart and restores the known-good RAM configuration; the tool does not restart the device. As with the existing single-bank loader, a permanently busy/faulted flash can stall later flash fetches; offline bounded models do not establish recovery from that hardware failure. Normal rendering continues to service UART between programming calls; clients still wait for each reply rather than pipeline commands.

There is **no runtime erase** and no garbage collection. Save of the already selected value consumes no slot. Other saves and interrupted attempts consume slots; when full, changed saves refuse without mutation. This bounds wear and avoids sharing erase ownership with app commit metadata. The existing updater already erases the entire metadata page before updating app pages, so **every app update clears display configuration**, including a same-image update. Re-run identification afterward if the factory default does not match. Initial combined images contain erased journal bytes. Do not invent a config-only metadata erase: it would invalidate the app. Exhaustion is reported for deliberate maintenance using the existing update procedure, never an automatic update/erase.

v0.3.0 distinguishes this user-facing configuration feature from the offline v0.2.0 candidate. Loader protocol remains version 1 and existing set/get, profile 2, ADC, bootloader and update tools remain compatible. New firmware, persistence and host-helper physical behavior require the [UNRUN qualification plan](display-qualification.md) before a hardware claim.
