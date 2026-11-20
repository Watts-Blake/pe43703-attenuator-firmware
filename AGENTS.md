# Project agent memory

- Build and offline checks: `make all test inspect`; dependencies and product limits are in README.md and docs/design.md.
- `firmware/core.h` defines the RAM mailbox ABI; docs/mailbox.md defines single-writer publication and acknowledgment rules.
- Loader/app layout, app-only USB updates and recovery: docs/usb-update.md; build/combined.bin is the initial SWD image.
- `tools/install.py stage` and `tools/mailbox.py` generate plans offline. Installation `execute` and OpenOCD commands are device operations; keep them outside builds/tests. See docs/install.md.
- Hardware qualification is recorded in docs/validation.md; do not equate a completed command with an RF measurement.
- Diagnostic commands, two-unit observations and unqualified follow-ups: docs/diagnostics.md (PA1 ladder) and docs/oled-probe.md (OLED roles/controller uncertainty). Both diagnostics are inactive at boot; future applied-state display and USB-profile precedence requirements are in docs/design.md.
- This repository is public. Generated plans contain unit/programmer identity and belong in ignored local directories, never commits.
- `tools/attctl.py` controls two USB units and timed profiles; `docs/attctl.md` defines stable topology binding, exclusive ownership and bounded zero cleanup. PTY tests run with `python3 -m unittest discover -s tests -p test_attctl.py -v`.
- Portable set/get and USB updates use `tools/serial_port.py` with pyserial (`requirements.txt`); setup, shared Linux locking and platform qualification limits are in `docs/host-tools.md`. Offline coverage: `python3 -m unittest discover -s tests -p test_serial_port.py -v`.

## Maintaining this file

Keep knowledge useful to almost every future session. Prefer pointers to authoritative files/commands over duplicating implementation details. Rewrite or prune existing entries instead of accumulating a work log. Preserve this standard whenever editing this file.
