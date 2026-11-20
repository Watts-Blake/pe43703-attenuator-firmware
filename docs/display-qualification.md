# v0.3.0 display qualification and update plan — UNRUN

This is an unexecuted plan for the display-identification follow-up. **No physical device, serial hardware, SWD, flash, power/reset or RF operation was performed for this candidate.** Existing v0.2.0 pending-qualification artifacts and released images are separate and are not superseded as hardware evidence. A future bench session requires its own exact unit/port identity, exclusive ownership, power arrangement and authorization; nothing in this document allocates a device or authorizes execution.

Build with `make all test inspect`. Record the exact source commit, compiler and SHA256 of app BIN/ELF, loader BIN and combined BIN from the selected build; see [offline validation](validation.md#v030-display-identification--offline-candidate). The qualification record must bind the actual images used to those hashes rather than an unbuilt branch name. Do not substitute an earlier staged plan or a different candidate image.

| Planned phase | Required evidence | State |
| --- | --- | --- |
| Preparation | Exact physical unit and CH340 port, compatible MCU/flash/pins, sole power source, exclusive serial/mailbox ownership, trusted image hashes and retained recovery access | UNRUN |
| App-only update | Offline image inspect; normal GET; explicit hash-bound update via the unchanged [USB update procedure](usb-update.md); exact `boot 1 08002000 0000dc00 00000040` before erase; each write/CRC/start reply | UNRUN |
| Post-update | `version 0.3.0`, GET requested zero, normal APPLIED/PENDING/source/lock screen, `display` exact `display 1`, `display get` factory `config 41 62` | UNRUN |
| Default / comparison | Photograph all four edges of normal screen and labeled trial 41/89 on each unit; record complete/clipped/ambiguous separately; compare with earlier SSD1306-compatible observations without declaring silicon identity | UNRUN |
| Held-session control | USB next/try/finish/cancel, fresh released front-key advance, no advance on held key; 120-second idle and 600-second total expiration; normal display and release gating restored | UNRUN |
| Explicit save | Cancel first to show no persisted mutation; repeat, explicitly confirm one visual choice, await `saved NN`, read `config NN SS`, record one consumed slot (or no-op for unchanged choice) | UNRUN |
| Ownership | Direct SET, token-bound profile SET and mailbox applied change cancel identification; profile acquire cancels it; active profile rejects start; applied attenuation changes only through the intended RF command | UNRUN |
| Persistence | Under separately authorized power-interruption testing, interrupt every record phase, read back journal and check previous/default or fully committed new record; never assume a software reset proves physical power-fail safety | UNRUN |
| Update compatibility | A separately authorized later app update erases the journal and restores factory 41; installed loader bytes/geometry remain unchanged; legacy host set/get and update entry still work | UNRUN |
| Optional wiring | Only a board with established compatible OLED-input wiring on PA4–PA7; explicit role sweep, photographs/trial mapping, save only after full-border confirmation; do not save an unobserved mapping | UNRUN |
| Host platforms | Physical Windows/macOS/Linux CH340 sessions, cancellation/EOF/disconnect and exclusive-open behavior; existing mocks/PTYS are software evidence only | UNRUN |
| RF / normal buttons | Independent attenuation measurement and unchanged zero boot, pending/apply, steps and profile ownership; guided ladder calibration is outside this change | UNRUN |

The existing updater sends zero and resets into the loader, erases app/metadata and starts the new app; it is a device operation, not an inspection. Do not execute an update to recover a journal slot automatically. Never erase metadata separately, widen app ranges, replace the installed loader over UART or downgrade protections. On any failure/unknown completion, retain the transcript and stop; no blind replay or alternate device is part of this plan.

A lost save acknowledgment can coexist with a fully committed record. Re-establish exact identity/ownership and read configuration before choosing another operation. A permanently busy single-bank flash is a hardware fault outside the software recovery claim. Hardware timings, flash endurance under actual supply conditions, physical power-failure behavior and RF accuracy remain unqualified.
