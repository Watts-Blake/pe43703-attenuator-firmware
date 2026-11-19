# Project agent memory

- Build and offline checks: `make all test inspect`; dependencies and product limits are in README.md and docs/design.md.
- `firmware/core.h` defines the RAM mailbox ABI; docs/mailbox.md defines single-writer publication and acknowledgment rules.
- Loader/app layout, app-only USB updates and recovery: docs/usb-update.md; build/combined.bin is the initial SWD image.
- `tools/install.py stage` and `tools/mailbox.py` generate plans offline. Installation `execute` and OpenOCD commands are device operations; keep them outside builds/tests. See docs/install.md.
- Hardware qualification is recorded in docs/validation.md; do not equate a completed command with an RF measurement.
- Input-only ADC diagnostics and their hardware limits: docs/diagnostics.md; firmware/adc.c owns ADC1 only during an explicit command.
- This repository is public. Generated plans contain unit/programmer identity and belong in ignored local directories, never commits.

## Maintaining this file

Keep knowledge useful to almost every future session. Prefer pointers to authoritative files/commands over duplicating implementation details. Rewrite or prune existing entries instead of accumulating a work log. Preserve this standard whenever editing this file.
