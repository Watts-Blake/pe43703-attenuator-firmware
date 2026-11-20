# Offline tests directory

- `test_core.c` exercises attenuation frame/GPIO events, parsing and coherent mailbox exchange. `test_boot.c` exercises update framing, simulated flash failures, commit ordering and protected-region bounds.
- `test_tools.py` models identity-bound installation and SWD/mailbox exchanges. `test_usb.py` checks image/update behavior, including POSIX serial I/O against a local pseudo-terminal.
- Tests must never launch a real OpenOCD process or access physical USB/UART/SWD hardware. Simulations and command acknowledgments cannot establish RF performance; see `docs/validation.md`.
- `test_profile_protocol.py` links `profile_bridge.c` to production firmware and runs production attctl through local PTYs, including lease expiry between keep and SET; it compiles a host-only shared library with `CC_HOST` (default `cc`).
- `test_ui.c` covers post-latch publication, button filtering/release, leases, fixed OLED frames, diagnostic deadlines and queued UART interaction; `test_adc.c` also models the nonblocking sampler.
- `test_display_config.c` models mixed display/button records, torn/corrupt writes and unchanged loader validity/update geometry. `test_button_cal.c` covers stable holds/releases, confirmation and rejection; `test_button_calibrate.py` drives the production ADC/UI/config path through PTYs, including saved bands after simulated boot, local/host confirmation races and actual helper exit/port-lock release with a full blocking stdout pipe. `test_display_identify.py` tests portable prompts/failures and the production UI/config parser through local PTYs via `display_bridge.c`; OLED tests decode all 96 held trials.
- Run `make all test inspect` from the repository root. `fixtures/README.md` records the pinned upstream OpenOCD fixtures and their licensing; preserve those fixtures as upstream material.
- `test_attctl.py` exercises USB control with local PTYs, synthetic sysfs, signals, ownership refusal, strict validation and independent zero cleanup; run it alone with `python3 -m unittest discover -s tests -p test_attctl.py -v`.

## Maintaining this file

Keep this guidance concise and point to authoritative files. Update existing entries when behavior changes; do not accumulate task logs.
