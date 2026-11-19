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
