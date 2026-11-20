# v0.4.0 button qualification and update plan — UNRUN

**All phases are UNRUN.** This plan describes future qualification of the [five-button wizard](button-calibration.md) after screen setup. No physical serial, USB, SWD, flash, reset/power or RF operation was performed for this candidate. Earlier installed images and retained display candidates remain separate evidence. This document grants no device ownership or permission to execute an update.

Bind the selected artifacts to the exact source commit and the app BIN/ELF, loader BIN and combined BIN SHA256 values in [offline validation](validation.md#v040-button-calibration--offline-candidate). Before any future action, record the actual artifact hashes again and the exact unit/port, compatible MCU/flash/pins, sole power source, exclusive serial/mailbox ownership and recovery access. Stop on an identity, geometry or hash mismatch; do not substitute a branch name or an earlier update plan.

| Planned phase | Evidence required | State |
| --- | --- | --- |
| Prepare / inspect | Trusted source and image hashes; offline ELF/BIN/vector/metadata inspection; exact unit/port and ownership; retain SWD recovery access | UNRUN |
| App-only update | Follow unchanged [USB update procedure](usb-update.md), including GET, requested zero and exact `boot 1 08002000 0000dc00 00000040` geometry before erase; record writes, CRC and start replies | UNRUN |
| Defaults | `version 0.4.0`, GET zero, `buttons` → `buttons 1`, `buttons get` → `buttons none`, display trial 41 and 62 free slots after update | UNRUN |
| Screen setup | Complete [screen identification](display-identification.md) on the selected v0.4.0 image before calibration; photographs of all borders and explicit chosen trial/save/readback | UNRUN |
| Five holds | Exact LEFT, +, -, RIGHT, OK prompts; independently record PA1 samples/cadence/noise while held, displayed medians and stable-release transitions; no pending/step/applied change | UNRUN |
| Rejection / cancel | Short/interrupted/noisy holds, wrong/duplicate/overlapping key and near-release bands, partial session, Ctrl-C/EOF, invalid commands, ADC error and deadline exit; previous calibration/defaults retained | UNRUN |
| Explicit save | First cancel the full captured set and verify unchanged storage; repeat and separately exercise host `SAVE BUTTONS` and on-device one-second OK/release confirmation; wait for completion and verify all five stored values | UNRUN |
| Main firmware | After separately authorized restart/power testing, saved bands drive normal PLUS/MINUS/step/pending/OK; UNKNOWN gaps stay inert; fresh release after every exit; saved display roles/alignment preserved | UNRUN |
| Ownership / timing | Active profile refuses start, acquisition cancels before ownership, direct SET and mailbox apply cancel; wrong/stale profile tokens cannot latch; lease/idle/total/capture deadlines and TX pressure preserve applied state | UNRUN |
| Persistence faults | Under separately authorized power-interruption testing, interrupt every record phase, independently read flash and distinguish previous valid from fully committed new; no loader/app/metadata-header mutation; software fault models alone do not qualify this | UNRUN |
| Capacity / update | Changed button save costs two slots, unchanged set costs none, interrupted attempts consume space, one remaining slot refuses a changed button set; a separately authorized app update clears both configurations | UNRUN |
| Host platforms | Deliberately owned Windows/macOS/Linux CH340 use, exclusive open, bounded prompts, signal/disconnect handling and no replay/reopen; portable mocks/PTYS are software evidence only | UNRUN |
| RF / electrical | Independent attenuation, continuity and signal-timing/supply measurements; a wizard response or ADC median is not calibrated RF evidence | UNRUN |

The updater enters the loader, erases app/metadata and starts the new app; it is a device operation. Do not run it automatically to reclaim slots. Never erase metadata alone, replace the installed loader over UART, widen app ranges or downgrade protection. Lost save acknowledgment means unknown completion: re-establish exact identity and ownership, read stored values, then decide whether a retry is appropriate. No blind retry, alternate device or automatic restart is part of this plan. Physical ADC cadence/noise, band margins, real flash endurance/interrupted-power behavior and permanent busy-flash recovery remain unqualified.
