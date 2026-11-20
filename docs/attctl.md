# Timed USB control with attctl

`tools/attctl.py` is a Python 3.9+ standard-library Linux CLI for two CH340
attenuators. It sends `set` and `get`, plus the invalid framing marker `!\n` during zero
cleanup; it never resets or updates firmware.
Replies confirm issued attenuation commands, not RF measurements.

Install the executable as `/usr/local/bin/attctl` and a private, administrator-owned
configuration as `/etc/attctl.json`. Example configuration (replace the example
USB topology paths after checking your own `/sys/class/tty/ttyUSB*/device`):

```json
{
  "version": 1,
  "devices": {
    "A": {"usb_path": "2-3.4", "vid": "1a86", "pid": "7523"},
    "B": {"usb_path": "2-3.5", "vid": "1a86", "pid": "7523"}
  }
}
```

A/B identify physical USB sockets. CH340 adapters without serial numbers cannot
identify a particular attenuator after it is exchanged at the same socket.
TTY numbers may change; each command resolves the configured topology via sysfs.
Missing/duplicate paths, a different VID/PID, or re-enumeration during a command
cause failure. Reconfirm the configuration after changing hub wiring.

```sh
sudo install -m 0755 tools/attctl.py /usr/local/bin/attctl
# Create /etc/attctl.json using the verified local topology, before use.
sudo /usr/local/bin/attctl A get
sudo /usr/local/bin/attctl B get
sudo /usr/local/bin/attctl both set 0
sudo /usr/local/bin/attctl A set 12.25
sudo /usr/local/bin/attctl profile schedule.json > schedule.jsonl
```

An SSH caller can run the same installed command through its existing SSH route.
Use `--config /path/to/config.json` before the subcommand to select another
configuration. All invocations share `/run/lock/attctl.lock`, irrespective of
configuration; never delete or replace that file while controllers might run.
The account needs serial access, lock-file access, and visibility of other
processes' file descriptors in `/proc`. An authorized root invocation is usually
needed for complete ownership visibility. Inaccessible process descriptors cause
refusal. The CLI also takes a serial-device advisory lock and `TIOCEXCL`, and
checks for raw pre-existing openers before configuring or writing a port. It does
not steal existing locks or holders. Other controllers must honor exclusive
ownership; privileged software can bypass kernel tty exclusion.

`set` accepts 0..31.75 dB in exact 0.25 dB steps and requires the exact formatted
`ok <value>\r\n` reply. `get` requires a valid formatted value. Direct `set`
leaves that value applied. `both set` is sequential: failure on one unit can leave
a partial result, reported with a nonzero exit. Use a profile when automatic zero
cleanup is needed.

A profile explicitly specifies both values and the hold duration at every step:

```json
{
  "version": 1,
  "steps": [
    {"A": "12.00", "B": "0.00", "duration": 30},
    {"A": "22.00", "B": "0.00", "duration": 60}
  ]
}
```

The complete file is validated before either port opens: version 1, exactly the
shown keys, 1..1024 steps, at most 65536 bytes, durations 0.05..3600 seconds in
0.001-second increments, and total duration at most 7200 seconds. Numbers must
be finite; dB values may also be decimal strings. Duplicate/unknown keys and
invalid quarter steps are errors. A 1200-second schedule fits these limits.
Durations begin after both acknowledgments and use the monotonic clock. Delays
extend the schedule; late steps never trigger a catch-up burst.

## Firmware profile leases

For v0.2.0, profiles first send the side-effect-free `profile` capability query to each owned unit. The exact legacy `err\r\n` reply retains the old profile path, appropriate for older apps with inert buttons. A `profile 2\r\n` reply enables the [token/lease protocol](design.md#usb-profile-ownership). Malformed replies, timeouts and acquisition failures stop the profile; they do not trigger a legacy fallback. Mixed old/new firmware is supported. Any other capability revision, including the earlier `profile 1` development reply, is refused rather than treated as legacy firmware.

Both leases are acquired before the first profile setting. Each supported unit is renewed before its step setting and receives `profile set <token> <dB>`, never a legacy SET for a scheduled value. Firmware validates expiry and the matching token atomically with dispatch to the latch, so a host pause between keep and SET cannot overwrite a local apply after expiry. Supported leases are renewed during holds about once per second. The firmware lease lasts 10 seconds; failed/expired renewals or token-bound settings stop execution without replay or reacquisition. Cleanup still independently attempts zero with legacy SET/GET on both exclusively owned ports (even after lease loss), then releases its own leases and closes the ports. Unlock errors are reported and fail the run. The shared host lock remains held throughout cleanup and close.

The firmware suppresses local pending edits, step changes and OK while a lease is active; a one-shot USB set does not acquire a lease. Older attctl versions and generic serial controllers retain set/get compatibility but cannot identify a running profile to v0.2.0 and therefore cannot exclude local buttons without adopting this protocol. Direct CLI set/get commands remain unchanged and send no lease traffic.

On disconnect, host crash or SIGKILL the firmware cannot observe USB directly through CH340; lack of renewal expires the lock after 10 seconds. Expiry keeps applied attenuation unchanged, discards pending edits and requires a debounced button release before local actions. It does not guarantee zero after host failure. Tokens prevent stale release/renew commands from touching another lease; they are not authentication, and all existing exclusive ownership rules still apply.

Stdout is JSONL with UTC: `applied` records each acknowledged unit/value, `step`
records both actual acknowledged values and the requested duration, `held`
records elapsed monotonic seconds, and `cleanup` records each zero outcome.
Partial application is recorded before starting the next unit when output is writable.
Output writes have a 0.1-second deadline and a 4096-byte record bound. A stalled
pipe is a logging failure; cleanup proceeds even if both stdout and stderr are
full. Cleanup/error reporting occurs after both independent zero attempts, so
reporting cannot prevent either attempt. Records may be missing or partial when
the output sink fails; a nonzero exit never claims a complete log. Exit status is 0 on
success, 1 on validation/runtime/ownership/cleanup failure, or 2 on CLI syntax
error. An interrupted profile exits nonzero even if cleanup succeeds. Log write
failure also fails the profile and starts cleanup; cleanup reporting falls back
to stderr if stdout is unavailable.

On normal profile completion, SIGINT/SIGTERM/SIGHUP, or an error after ports are
owned, the CLI independently attempts `set 0.00` and `get` on each owned unit,
with bounded replies. It keeps the shared lock until cleanup and close finish.
A failed unit does not prevent the other attempt. Recovery invalidates partial
framing before sending zero; it never retries an old nonzero command. A port
that was never acquired, changed identity, or gained a foreign holder is not
written during cleanup. Each request has a 2-second deadline, a 64-byte reply
limit, and bounded process-holder checks; cleanup has no retry loop.

**Zero cannot be guaranteed after disconnect, SIGKILL, host/USB power loss,
firmware failure, or loss of port ownership.** Inspect every cleanup outcome and
restore zero deliberately once safe ownership is established. Acknowledgments
are firmware command confirmations, not physical attenuation readback.

Offline coverage: `python3 -m unittest discover -s tests -p test_attctl.py -v`.
Tests use local PTYs and synthetic sysfs trees; no physical device is opened.
