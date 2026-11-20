# Host tools directory

- `install.py` and `mailbox.py` stage identity-bound plans; `swd.py` handles bounded OpenOCD execution. `image.py`, `pack_image.py` and `inspect_image.py` validate and assemble images.
- `usb_update.py` supports offline app inspection and explicit app-only UART updates. The combined image is for initial SWD installation; the app-only image belongs at the address defined in `firmware/layout.h`.
- Import, staging, inspection and tests must not open physical devices or downgrade protection. Keep destructive installation and UART update actions explicit, identity-checked, bounded and outside build/test targets. See `docs/install.md` and `docs/usb-update.md`.
- Generated unit/programmer plans stay in ignored local directories. From the repository root run `make all test inspect`; executable host-tool coverage lives in `tests/`.
- `display_identify.py` guides held OLED trials and `button_calibrate.py` guides the subsequent five-key wizard through `serial_port.py`; require exact capability negotiation, explicit save confirmation, bounded prompts and no replay after UART failure. Button confirmation watches device save progress; console backpressure must not block deadline handling or transport cleanup. Protocols and journal limits are in `docs/display-identification.md` and `docs/button-calibration.md`.
- `attctl.py` is the standalone Linux set/get/profile CLI; keep imports offline, topology binding exact, controller lock shared, and profile zero cleanup independent per unit. Firmware revision-2 leases bind each scheduled SET to its active token, with exact legacy refusal fallback and bounded renewal/expiry; direct set/get stays unchanged. See `docs/attctl.md`.

## Maintaining this file

Keep this guidance concise and point to authoritative files. Update existing entries when behavior changes; do not accumulate task logs.
