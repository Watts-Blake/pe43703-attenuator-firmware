# Offline tests directory

- `test_core.c` exercises attenuation frame/GPIO events, parsing and coherent mailbox exchange. `test_boot.c` exercises update framing, simulated flash failures, commit ordering and protected-region bounds.
- `test_tools.py` models identity-bound installation and SWD/mailbox exchanges. `test_usb.py` checks image/update behavior, including POSIX serial I/O against a local pseudo-terminal.
- Tests must never launch a real OpenOCD process or access physical USB/UART/SWD hardware. Simulations and command acknowledgments cannot establish RF performance; see `docs/validation.md`.
- Run `make all test inspect` from the repository root. `fixtures/README.md` records the pinned upstream OpenOCD fixtures and their licensing; preserve those fixtures as upstream material.
- `test_attctl.py` exercises USB control with local PTYs, synthetic sysfs, signals, ownership refusal, strict validation and independent zero cleanup; run it alone with `python3 -m unittest discover -s tests -p test_attctl.py -v`.

## Maintaining this file

Keep this guidance concise and point to authoritative files. Update existing entries when behavior changes; do not accumulate task logs.
