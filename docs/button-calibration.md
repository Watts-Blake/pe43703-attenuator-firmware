# Five-button calibration (v0.4.0 candidate)

**Hardware UNRUN.** Finish [screen identification](display-identification.md) first so every prompt is readable. Calibration measures the PA1 resistor ladder on compatible five-button boards; it does not identify arbitrary wiring, change pin mappings, calibrate RF attenuation or run automatically at boot. The installed firmware starts at requested zero. Until a complete valid record is explicitly saved, normal buttons use the existing [factory bands](diagnostics.md#button-processing-in-v020).

## Guided host helper

Use Python 3.10+ and `requirements.txt` following [cross-platform setup and exclusive ownership](host-tools.md). Close other serial controllers and ensure no SWD mailbox writer is active. Select the exact known port; the helper never scans or reopens a failed session. These are **device-operation examples for a future deliberately owned session**, not build/test commands:

```powershell
python tools/button_calibrate.py --port COM7
```

```sh
python tools/button_calibrate.py --port /dev/cu.wchusbserial-example
python tools/button_calibrate.py --port /dev/ttyUSB0
```

The helper requires exactly `buttons 1\r\n`, prints any previous saved medians, then asks for `START`. Legacy firmware, unsupported revisions and malformed replies stop before starting the wizard. The screen prompts **HOLD LEFT**, **HOLD +**, **HOLD -**, **HOLD RIGHT**, **HOLD OK** in that order. Release all keys before the first hold. Hold each key steadily until its captured ADC median appears, then release it before the next prompt. The host prints progress and all five captured medians/bands.

After the final stable release, the OLED shows **HOLD OK TO SAVE / 1 SECOND THEN RELEASE**. Either deliberately hold the captured OK key continuously for at least one second and release it stably, or type exactly `SAVE BUTTONS` at the helper's confirmation prompt. These are explicit save actions; capturing the fifth key alone never saves. Do not perform both confirmations at once. The helper verifies stored values after completion. Cancellation, incorrect confirmation, EOF, Ctrl-C or a prompt timeout before a save sends no save request. A cancellation cannot undo a record already committed.

Every prompt allows at most 90 seconds. The firmware enforces 120 seconds without a completed release/capture and ten minutes total, regardless of GET/status polling or TX pressure. A capture has its own three-second deadline. The helper polls status with one outstanding command, bounds each request by `--timeout` (default two seconds, maximum 30), bounds save completion to five seconds and bounds the active session to ten minutes. Ctrl-C during capture cancels with the same exclusive connection. No unattended/automatic save flag exists.

## Sampling and accepted bands

The wizard uses the production nonblocking ADC1 sampler, serialized with diagnostics. Normal sampling is requested every 5 ms. Calibration accepts at most one observation per 10 ms; a gap over 30 ms invalidates an in-progress capture and resets release/confirmation stability. Actual hardware cadence remains unqualified. Release requires 11 observations at ADC 0–32 spanning at least 100 ms. It is required before the first hold, between keys and after the confirmation hold. An interrupted hold, ADC failure or out-of-range sample ends the session.

Each capture collects exactly 31 observations spanning at least 300 ms, sorts them and takes the middle value. The full sample spread must be no more than 24 counts; noisy holds terminate with `err noise`. Every saved button band is the median ±32 ADC counts, clipped at 4095. Medians must be 97–4095, so there are more than 32 unused counts between release and the first band. Every pair must have more than 32 unused counts between its bands (median separation greater than 96). Duplicate/overlapping/too-close buttons terminate with `err bands`; there is no guessed ordering by ADC voltage. All five positions are required, in the explicit prompted order.

Normal firmware uses only validated saved bands, with its existing median, debounce and fresh-release gating. Readings outside release and all accepted bands remain UNKNOWN and perform no key action. A corrupt/unsupported/incomplete record is ignored; the preceding valid calibration or factory defaults remain. Calibration keys never edit pending attenuation, step or applied attenuation. After any exit a fresh normal debounced release is required.

## Ownership, failure and cancellation

An active profile, display-identification session or legacy display diagnostic refuses a new wizard with `err busy`. A wizard refuses competing display starts and fixed OLED tests. Profile acquisition cancels the wizard before taking ownership. Valid direct SET, token-bound profile SET and external mailbox applied changes retain their existing behavior and cancel calibration. No calibration operation itself calls the RF controller. Zero boot, atomic token-bound profile SET and profile lease behavior are unchanged.

Raw ADC diagnostics, `oledprobe` and loader entry cancel calibration; receive errors, discarded/overlong lines, malformed calibration commands and parser errors cancel uncommitted work. GET/status queries cannot prolong a session. ADC sampling is suspended during flash programming, then resumed. Cancellation/deadlines restore the saved display trial and normal applied-state screen independently of TX availability; no unsolicited calibration reply is emitted. Display configuration and button configuration are saved independently, and neither session can commit the other's transaction.

The CH340 provides no disconnect notification. On UART failure the helper closes without replay, reopening, automatic save or fallback commands; firmware deadlines clear an abandoned session. If an explicit save lost its acknowledgment, completion is unknown: a previous valid record or a fully committed new record can be used at the next boot. Establish identity/ownership in a new session, query `buttons get` and all five `buttons stored K` values, then decide whether another operation is needed. A failed flash write latches saving off until restart; this tool never restarts the device. Permanent single-bank flash faults and physical interrupted-power recovery remain unqualified.

## Exact protocol revision 1

115200 8N1, CR/LF/CRLF, one outstanding request; all replies end CRLF. `K` is exactly one of `L P M R O`, meaning LEFT, PLUS, MINUS, RIGHT, OK. `NNNN` is four decimal digits in 0000–4095. Maximum reply including CRLF is 15 bytes, fitting the existing UART queue contract.

| Command | Reply / meaning |
| --- | --- |
| `buttons` | Exactly `buttons 1`; side-effect-free capability query |
| `buttons get` | `buttons none` (factory defaults) or `buttons saved` (validated complete record) |
| `buttons stored K` | `value K NNNN` from persisted configuration, or `err config` if unavailable; never starts a session |
| `buttons start` | `ok buttons`; explicit release/hold wizard, or `err busy` without disturbing an owner |
| `buttons status` | `release K`, `hold K`, `value K NNNN`, `confirm`, `saving`, `saved`, `idle`, or a terminal error below |
| `buttons value K` | `value K NNNN` for a captured key in the active session; partial or inactive reads fail |
| `buttons save` | `ok saving` only after all five captures and final release, while awaiting confirmation; poll status then verify stored values |
| `buttons cancel` | `ok cancel`; idempotently stops uncommitted work |

Terminal statuses are `err timeout`, `err hold`, `err noise`, `err bands`, `err sample`, `err save`. Invalid operations return `err buttons` and cancel. A captured value stays visible until stable release, then status advances; the helper retrieves every median before confirmation. `saved` acknowledges software commit/readback, not physical power-fail or RF qualification. The device's explicit OK hold/release uses these same validation and save paths.

## Journal compatibility and limits

The [unchanged metadata-tail geometry](display-identification.md#persistent-record-and-flash-compatibility) has 62 allocation slots of 16 bytes, starting at `0x0800fc20`; loader metadata bytes 0–19 and gap bytes 20–31 remain untouched. Existing v0.3 display records retain their exact version-1 16-byte encoding and cost one slot. A version-2 button record costs **two adjacent slots (32 bytes)**. It contains little-endian u32 value 2 at offset 0 (u16 version plus u16 reserved zero), five u16 medians at offsets 4–13, reserved u16 zero at 14, continuation marker `0x42434e54` at 16, CRC-32/ISO-HDLC over bytes 0–19 at 20, CRC complement at 24, and commit `0x42544e32` at 28, written last. Boot requires exact header/reserved/marker/CRC/complement/commit plus disjoint safe bands.

Scanning selects the latest valid physical record independently for display and buttons. It ignores torn/corrupt/unknown versions and appends beyond every programmed slot, including holes and partial attempts. The continuation marker cannot be read as a version-1 display header. A v0.3 reader ignores button records and still finds its latest display record; a v0.4 reader accepts existing display-only journals and uses default buttons. CRC detects corruption, not hostile modification or incorrect user labeling.

No runtime erase or garbage collection is added. A changed button save programs at most 16 verified halfwords in separate main-loop polls, with the existing RAM-resident flash routine. An unchanged complete set is a no-op. Cancellation reserves both allocated slots during that boot; after restart only physically programmed slots are consumed. Mixed saves and interrupted attempts share capacity: at most 31 changed button saves on an otherwise empty journal, fewer after display saves. One remaining slot can save a display choice but cannot hold a button record. Full/faulted stores refuse changes and retain known-good values. This bound avoids runtime erase wear; it is not a measured flash-endurance claim.

Every app update, including the same image, erases the complete metadata page and resets **both** display and buttons to factory defaults. Repeat screen identification, then calibration if needed. Never erase metadata separately to recover slots: that invalidates the app. Loader code/protocol, 55 KiB app maximum, image/vector geometry, mailbox ABI and reserved reset RAM are unchanged. Existing set/get, profile 2, ADC, screen-ID protocol 1 and update tools remain compatible. See the [UNRUN qualification/update plan](button-qualification.md); offline interruption models cannot qualify physical power failure.
