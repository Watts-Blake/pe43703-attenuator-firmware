# OpenOCD configuration test fixtures

These unmodified files let offline Tcl tests execute the actual included configuration, including its target-write hook and adapter-speed assignment. Physical/debugger commands are modeled; no OpenOCD process or target is opened by these tests.

Files other than `interface/stlink.cfg` are from [OpenOCD v0.12.0](https://github.com/openocd-org/openocd/tree/v0.12.0), using their original paths under `tcl/` (plus COPYING and LICENSES). The native `interface/stlink.cfg` is from [the Raspberry Pi OpenOCD cd4873400 revision](https://github.com/raspberrypi/openocd/blob/cd4873400/tcl/interface/stlink.cfg). The target/helper files at these two revisions are byte-identical. No host-specific configuration is included.

The fixture code retains its SPDX GPL-2.0-or-later notices and [upstream license](openocd-012/LICENSES/preferred/GPL-2.0). The license-rules document retains its own notice. This exception does not change the MIT license of the project firmware, authored tools or tests.

| Relative fixture | SHA256 |
| --- | --- |
| `COPYING` | `1b8f7e37ee5afbbf95c2a4d62b12b25232e29538692663b434318503a9a88419` |
| `LICENSES/license-rules.txt` | `0a37155eb8bc0cb94477a0cae4f030e39da9317374e8a9f597e9229fd09aff78` |
| `LICENSES/preferred/GPL-2.0` | `7d35bc44da7bb763022e3c2c32cfc0125dd222060bc50992420599d8e65f6c0a` |
| `interface/stlink-dap.cfg` | `cff9ba09cb4390ad0d4367ab185fdff6ac54c1c5b7f7ea9981c6af9de6d20a0d` |
| `interface/stlink.cfg` | `6d80f32294a750d968dde9322072509d2bb7b8a736416dbe2f72eed6fb074bea` |
| `mem_helper.tcl` | `01794a37e9a8bdc20e2304ee64e9d70549c0872f5f6c40fa032a165bcbba1fd2` |
| `target/stm32f1x.cfg` | `fe44c3ade7395db34a751c57401ebc2a176ed1ca793c47081cc9f06a8618125e` |
| `target/swj-dp.tcl` | `aad132008735bbafea3d18a304632c638f0fb99bfc19530c9f577eda2b10410a` |
