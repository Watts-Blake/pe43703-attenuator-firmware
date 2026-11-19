# Test fixtures directory

- `README.md` identifies the OpenOCD snapshot and its licensing. `openocd-012/` contains unchanged upstream Tcl/configuration fixtures used by the offline installation model.
- Preserve upstream bytes and license files; adapt the model in `../test_tools.py` rather than silently changing fixture behavior. Do not invoke these scripts against hardware during tests.
- Run `make all test inspect` from the repository root; fixtures are data, not a separate build target.

## Maintaining this file

Keep this guidance concise and point to authoritative files. Update existing entries when behavior changes; do not accumulate task logs.
