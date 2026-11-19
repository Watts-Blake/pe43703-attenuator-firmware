# Host tools directory

- `install.py` and `mailbox.py` stage identity-bound plans; `swd.py` handles bounded OpenOCD execution. `image.py`, `pack_image.py` and `inspect_image.py` validate and assemble images.
- `usb_update.py` supports offline app inspection and explicit app-only UART updates. The combined image is for initial SWD installation; the app-only image belongs at the address defined in `firmware/layout.h`.
- Import, staging, inspection and tests must not open physical devices or downgrade protection. Keep destructive installation and UART update actions explicit, identity-checked, bounded and outside build/test targets. See `docs/install.md` and `docs/usb-update.md`.
- Generated unit/programmer plans stay in ignored local directories. From the repository root run `make all test inspect`; executable host-tool coverage lives in `tests/`.

## Maintaining this file

Keep this guidance concise and point to authoritative files. Update existing entries when behavior changes; do not accumulate task logs.
