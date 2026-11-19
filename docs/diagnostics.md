# v0.1.1 input diagnostics (development)

The v0.1.1 development app adds an explicit PA1 input diagnostic. It has passed offline tests; it has not yet been installed or qualified on a board. The published v0.1.0 binaries do not include this command. Buttons remain inert and the OLED remains blank.

Send one command at a time at 115200 8N1, waiting for the full reply:

```text
adc
adc 2048
```

The example value is illustrative. `version` returns `version 0.1.1\r\n` to identify the app version; it does not identify a particular board. `adc` (alias `diag adc`) performs one ADC1 channel-1 conversion on PA1 and returns `adc NNNN\r\n`, with four decimal digits from `0000` to `4095`. It is a raw 12-bit input reading, not a voltage, button identity or RF measurement. No thresholds, button actions or automatic sampling are implemented. Calibration/conversion timeout or an unexpected clock/ADC ownership state returns `err adc\r\n`. Malformed commands retain the existing `err\r\n` behavior.

ADC1 remains disabled and PA1 keeps its reset input configuration until an explicit valid command. Each request temporarily selects analog input on PA1 without pull-ups or output drive. It powers and calibrates ADC1, performs one conversion, then powers down/resets ADC1, restores the PA1 configuration and disables its clock. There is no DMA, ADC interrupt, continuous mode, external trigger, internal sensor or scan of other channels. PA4–PA7 and the PE43703 controller are untouched; requested attenuation, selected mapping and issued count stay unchanged. USB `set`/`get`, boot's requested zero and the loader protocol retain their behavior.

The existing 72 MHz app clock supplies ADC at 12 MHz through APB2/6. Acquisition uses 239.5 ADC cycles (21 µs including the 12.5-cycle conversion). Startup waits 2 µs; reset-calibration, calibration and conversion each allow at most 100 polls separated by 1 µs. A request therefore waits at most 302 µs explicitly, plus bounded register, formatting and call overhead. This is a software bound under the app's working-clock contract, not a measured hardware latency guarantee. The next command can be processed after the reply. Pipelining commands remains unsupported by the polling UART.

A stock-board register snapshot found PA1 configured as analog input, ADC1 enabled, channel-1 acquisition set to 239.5 cycles and a regular sequence of channels 1, 0, 0. This supports testing PA1 as a possible button-ladder input; it does not identify button thresholds or prove the wiring. This app deliberately samples only channel 1. Board variants and unknown external source impedance still require hardware observation.

The implementation follows ST's [RM0008 Rev21](https://www.st.com/resource/en/reference_manual/cd00171190.pdf), §§11.3.1, 11.3.4, 11.4 and 11.12: ADC registers use 32-bit accesses, calibration follows power stabilization, and a second ADON-only write starts a regular single conversion. The manufacturer-authored manual was also checked through a [distributor-hosted copy](https://www.hestore.eu/en/prod_getfile.php?id=14113). [DS5319 Rev20](https://www.st.com/resource/en/datasheet/stm32f103c8.pdf) supplies the PA1/ADC12_IN1 pin assignment, 14 MHz ADC limit and startup characteristics.

`make test` executes the actual diagnostic register sequence against a host model. It checks all 4096 sample values, calibration ordering, stale calibration-data rejection, one-conversion completion, bounded stuck flags and cleanup, unexpected ownership/clock refusal, malformed/overflow/receive-error recovery, unchanged RF state and a subsequent `set`. These tests establish software behavior, not physical button mapping or ADC accuracy.

The OLED controller, PA4–PA7 signal roles, reset/chip-select wiring and panel supply arrangement remain unidentified. There is no OLED initialization or probe command in this app. Those facts must be established before selecting an SSD1306 or SH1106 initialization sequence.
