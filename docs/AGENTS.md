# Documentation directory

- `install.md` is the stock-unit hardware/SWD installation guide; `mailbox.md` defines the mailbox ABI exchange and mapping workflow; `usb-update.md` defines image layout, UART updates and recovery.
- `design.md` records design assumptions, primary references and applied-state display/source-lock/profile-lease behavior and qualification limits. `validation.md` separates offline checks, hardware observations and qualification limits; `diagnostics.md` and `oled-probe.md` own the two-unit PA1/OLED findings and unqualified follow-ups.
- Keep installation commands explicit about irreversible stock-firmware loss, exact identity and image roles. Keep pin hypotheses, issued commands and measured RF evidence distinct.
- Keep docs public-safe: use placeholders for unit/programmer identity and generic paths. Check links and command examples against the authoritative source; firmware validation runs from the repository root with `make all test inspect`.
- `attctl.md` documents Linux USB control, profile schema, topology identity, ownership and cleanup limits.

## Maintaining this file

Keep this guidance concise and point to authoritative files. Update existing entries when behavior changes; do not accumulate task logs.
