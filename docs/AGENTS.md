# Documentation directory

- `install.md` is the stock-unit hardware/SWD installation guide; `mailbox.md` defines the mailbox ABI exchange and mapping workflow; `usb-update.md` defines image layout, UART updates and recovery.
- `design.md` records design assumptions and primary datasheet references. `validation.md` separates offline checks, single-unit observations and remaining qualification limits.
- Keep installation commands explicit about irreversible stock-firmware loss, exact identity and image roles. Keep pin hypotheses, issued commands and measured RF evidence distinct.
- Keep docs public-safe: use placeholders for unit/programmer identity and generic paths. Check links and command examples against the authoritative source; firmware validation runs from the repository root with `make all test inspect`.
- `attctl.md` documents Linux USB control, profile schema, topology identity, ownership and cleanup limits.

## Maintaining this file

Keep this guidance concise and point to authoritative files. Update existing entries when behavior changes; do not accumulate task logs.
