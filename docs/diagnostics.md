# PA1 diagnostics and front-button control

The v0.1.1 development app added an explicit PA1 input diagnostic, retained in v0.2.0. It has passed offline tests and produced distinct held-button readings on two examined units, as recorded below. Hardware thresholds and button actions remain unqualified; the v0.2.0 candidate implements them with offline coverage only. The published v0.1.0 binaries do not include this command. Buttons were inert in v0.1.1. The v0.2.0 candidate adds automatic button sampling and a normal screen; see [applied-state behavior](design.md#applied-state-display-and-local-control).

Send one command at a time at 115200 8N1, waiting for the full reply:

```text
adc
adc 2048
```

The example value is illustrative. `version` returns `version 0.2.0\r\n` in the current candidate to identify the app version; it does not identify a particular board. `adc` (alias `diag adc`) performs one ADC1 channel-1 conversion on PA1 and returns `adc NNNN\r\n`, with four decimal digits from `0000` to `4095`. It is a raw 12-bit input reading, not a voltage, button identity or RF measurement. This command itself does not decode keys or change attenuation; automatic sampling and actions are described below. Calibration/conversion timeout or an unexpected clock/ADC ownership state returns `err adc\r\n`. Malformed commands retain the existing `err\r\n` behavior.

Each explicit request serializes with the v0.2.0 automatic sampler and temporarily selects analog input on PA1 without pull-ups or output drive. It powers and calibrates ADC1, performs one conversion, then powers down/resets ADC1, restores the PA1 configuration and disables its clock. There is no DMA, ADC interrupt, continuous mode, external trigger, internal sensor or scan of other channels. PA4–PA7 and the PE43703 controller are untouched; requested attenuation, selected mapping and issued count stay unchanged. USB `set`/`get`, boot's requested zero and the loader protocol retain their behavior.

The existing 72 MHz app clock supplies ADC at 12 MHz through APB2/6. Acquisition uses 239.5 ADC cycles (21 µs including the 12.5-cycle conversion). Startup waits 2 µs; reset-calibration, calibration and conversion each allow at most 100 polls separated by 1 µs. A request therefore waits at most 302 µs explicitly, plus bounded register, formatting and call overhead. This is a software bound under the app's working-clock contract, not a measured hardware latency guarantee. The next command can be processed after the reply. Pipelining commands remains unsupported by the polling UART.

A stock-board register snapshot found PA1 configured as analog input, ADC1 enabled, channel-1 acquisition set to 239.5 cycles and a regular sequence of channels 1, 0, 0. The held-button observations below confirm PA1 as the front-button ladder input on two units; the snapshot alone did not identify button thresholds or prove the wiring. This app deliberately samples only channel 1. Board variants and unknown external source impedance still require hardware observation.

The implementation follows ST's [RM0008 Rev21](https://www.st.com/resource/en/reference_manual/cd00171190.pdf), §§11.3.1, 11.3.4, 11.4 and 11.12: ADC registers use 32-bit accesses, calibration follows power stabilization, and a second ADON-only write starts a regular single conversion. The manufacturer-authored manual was also checked through a [distributor-hosted copy](https://www.hestore.eu/en/prod_getfile.php?id=14113). [DS5319 Rev20](https://www.st.com/resource/en/datasheet/stm32f103c8.pdf) supplies the PA1/ADC12_IN1 pin assignment, 14 MHz ADC limit and startup characteristics.

`make test` executes the actual diagnostic register sequence against a host model. It checks all 4096 sample values, calibration ordering, stale calibration-data rejection, one-conversion completion, bounded stuck flags and cleanup, unexpected ownership/clock refusal, malformed/overflow/receive-error recovery, unchanged RF state and a subsequent `set`. These tests establish software behavior, not physical button mapping or ADC accuracy.

## Observed PA1 front-button ladder

Held presses with a known physical button order produced these stable raw 12-bit codes on two units. The ranges combine both units; they are observations, not calibrated acceptance thresholds.

| Button | Observed stable codes | Proposed inclusive band (UNQUALIFIED) |
| --- | --- | --- |
| RIGHT | 809–820 | 780–850 |
| MINUS | 1016–1021 | 970–1060 |
| OK | 1352–1368 | 1310–1410 |
| PLUS | 2028–2048 | 1980–2110 |
| LEFT | 4092–4093 | 4050–4095 |
| Released | Near zero in diagnostic captures | 0–32 |

All codes outside the proposed disjoint bands must remain UNKNOWN, rather than round to the nearest button. In particular, one observed 1092 sample during entry into an OK press is an UNKNOWN transition, not a stable OK level or another button. Near-full-scale LEFT is established by controlled held presses on these units; a high reading alone does not rule out an electrical fault.

These starting engineering bands are **implemented in the v0.2.0 candidate but remain hardware-unqualified**. Supply, noise, temperature, resistor variation, stable releases, holds and transitions on both units need validation before hardware qualification of button actions. Captures sampled each unit at about 10 Hz; sample timestamps bracket changes only to that resolution and are not exact electrical edges. No voltage or RF measurement follows from these codes.

## Button processing in v0.2.0

The candidate samples at a nominal 200 Hz without blocking on ADC flags, takes a five-sample median and requires the same classified candidate for at least 30 ms before a press or release. UNKNOWN gaps cannot generate actions or stand in for release. There is no additional hysteresis expansion into gaps. A failed conversion or a gap longer than 20 ms clears filter/debounce history. Actual cadence and electrical/noise margins remain unmeasured.

Emit one press per stable transition, require a debounced release before another key and omit autorepeat. PLUS/MINUS edit pending attenuation; OK applies; LEFT/RIGHT select the step. Clamp within 0.00–31.75 dB. Boot, a completed remote change and profile lease transitions all require release before local action. While a USB profile lease is active, buttons cannot edit pending, select a step or apply attenuation. Buttons are also suppressed during the OLED diagnostics. See [source/lock and ownership semantics](design.md#usb-profile-ownership).

Offline tests exercise every raw classification code, the observed plateaus, UNKNOWN 1092, short presses, held keys, release gating, pending/apply, all steps, clamping, external changes and lease expiry across timer wrap. ADC register models exercise calibration, conversion, cancellation and bounded faults without a delay loop in the automatic sampler. These are software checks, not hardware button or RF qualification.

The separate [OLED observations and role probe](oled-probe.md) document working PA4–PA7 roles on two units. SSD1306-compatible column-zero rendering is observed, but actual silicon, chip-select wiring and panel supply arrangement remain unidentified. The role probe suspends automatic button ADC sampling; explicit raw `adc` / `diag adc` requests remain available.
