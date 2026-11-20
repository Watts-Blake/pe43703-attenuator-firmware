# Firmware directory

- `core.c` / `core.h` implement attenuation frames, exact-value parsing and the RAM mailbox; `target.c` supplies STM32 registers, clocks, GPIO and USART1.
- `boot_core.c` / `boot_core.h` implement the bounded UART update protocol; `boot_target.c` supplies flash operations and app handoff. The loader must never erase/program its own region.
- `layout.h` and the linker scripts define the protected first 8 KiB, app base `0x08002000`, metadata page and reserved RAM. Keep startup vectors, stack and both linkers consistent.
- Boot issues requested zero before accepting commands. Default SI/CLK/LE is PB11/PB12/PB13; preserve all six mapping options, safe idle levels and complete latch transactions. Factory display trial 41 uses PA6/PA7/PA5/PA4 with SSD1306-compatible column zero; validated saved display trials affect only the OLED. Screen/button hardware qualification remains pending.
- `ui.c` / `screen.c` implement pending/apply buttons, post-latch rendering and bounded USB profile leases; `docs/design.md` defines expiry/disconnect/release semantics. Automatic PA1 sampling uses the nonblocking ADC state machine and serializes with raw diagnostics.
- `display_id.c` owns held alignment sessions; `display_config.c` validates the append-only metadata-tail journal. Runtime saves never erase. Keep the loader’s first 20 metadata bytes, full app range and updater erase behavior unchanged; app updates clear the journal. See `docs/display-identification.md`.
- From the repository root run `make all test inspect`; hardware limits and primary references are in `docs/design.md`, mailbox publication in `docs/mailbox.md`, and update recovery in `docs/usb-update.md`.

## Maintaining this file

Keep this guidance concise and point to authoritative files. Update existing entries when behavior changes; do not accumulate task logs.
