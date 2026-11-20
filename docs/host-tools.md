# Windows, macOS and Linux host tools

`tools/display_identify.py` provides [guided OLED alignment and explicitly confirmed configuration](display-identification.md), `tools/attenuator.py` provides single-port set/get and `tools/usb_update.py` provides app-only updates using [pyserial](https://pyserial.readthedocs.io/en/latest/pyserial_api.html). They require Python 3.10+ and pyserial 3.5. Firmware builds still require the separate toolchain in the README. Import, `--help` and image inspection do not open ports; inspection does not require pyserial.

## Install and identify the port

Use an isolated Python environment. On macOS/Linux:

```sh
python3 -m venv .local/venv
.local/venv/bin/python -m pip install -r requirements.txt
```

On Windows PowerShell:

```powershell
py -3 -m venv .local/venv
.local/venv/Scripts/python.exe -m pip install -r requirements.txt
```

Use that environment's Python for subsequent commands. These instructions use `python` as shorthand for it.

| Host | Exact port example | Driver and identification |
| --- | --- | --- |
| Windows | `COM7` (including `COM10` and higher) | Identify the CH340 in Device Manager → Ports (COM & LPT). If no working driver is present, use WCH's CH340/CH341 Windows driver: https://www.wch-ic.com/downloads/CH341SER_EXE.html |
| macOS | `/dev/cu.wchusbserial-example` | Use the CH340's `/dev/cu.*` callout port. Check System Information → USB and the port names before/after connecting the intended unit. If a driver is needed, use WCH's current macOS package and its installation/approval instructions: https://www.wch-ic.com/downloads/CH34XSER_MAC_ZIP.html |
| Linux | `/dev/ttyUSB0` or a unique `/dev/serial/by-id/...` link | Usually provided by the kernel's `ch341` driver. Identify the USB device/topology and ensure authorized port and controller-lock permissions. CH340 units may lack unique serial numbers; a port name alone is not stable identity. |

The tools open only the supplied native port; they never scan, choose another device or accept serial URLs. Recheck identity after reconnecting. Close terminals and other controllers first. No concurrent SWD mailbox writer is permitted.

## Set and get

These are **device operations**, for the replacement app protocol only:

```sh
python tools/attenuator.py get --port COM7
python tools/attenuator.py set --port COM7 0
python tools/attenuator.py get --port /dev/cu.wchusbserial-example
python tools/attenuator.py set --port /dev/ttyUSB0 0
```

`set` accepts 0–31.75 dB in exact 0.25 dB steps, checks `ok <value>\r\n`, then requires matching `get` readback. Both commands print the requested value with two decimals only on success. This is firmware command readback, **not measured RF attenuation**. `--timeout 2` is the default per-request budget, shared by writes and the complete reply; allowed values are finite, greater than zero and at most 30 seconds. Values and timeouts are validated before opening the port. The link uses 115200 8N1 without flow control.

A failed acknowledgment, malformed/oversized response, disconnect or timeout exits unsuccessfully. Completion may be unknown; there is no automatic replay or zero cleanup. Inspect the connection and application state before another deliberate operation. This minimal CLI leaves the requested setting applied. For Linux timed profiles and bounded zero cleanup, use [attctl](attctl.md).

On Linux, these portable tools take the same `/run/lock/attctl.lock` as attctl **before opening/configuring a port**, retaining it through close. A busy or inaccessible lock fails closed; do not delete it or override permissions to bypass another controller. This global lock also serializes operations on different ports. The installed Linux attctl and its stronger topology, `/proc` holder checks, output bounds and cleanup remain unchanged. On POSIX, pyserial also requests a cooperative exclusive port lock; Windows uses pyserial's native exclusive open. POSIX locks do not exclude arbitrary terminal programs that ignore them. Close all other port users; macOS provides no attctl topology or Linux process-holder guarantee. Driver behavior may briefly toggle modem-control lines at open even though the tools request DTR/RTS inactive.

For app-only updates, follow [USB update/recovery](usb-update.md), including exact image-hash confirmation. USB updates cannot install the initial loader.

## Initial flashing prerequisites

Initial programming requires SWD and the **combined loader/app image**, not an app BIN at address zero. Follow [installation](install.md) for wiring, exact target identity, irreversible read-protection removal, actual power-on reset, separate verification and first boot. A CH340 driver is separate from the ST-Link driver; the USB bridge is not a DFU programmer.

**macOS:** install OpenOCD with `brew install open-ocd` (binary `openocd`); official formula: https://formulae.brew.sh/formula/open-ocd . Inspect the included native ST-Link scripts and select the matching explicit profile in the installation guide. Installing OpenOCD does not install the compiler/build dependencies.

**Windows — documented, not tested:** install the ST-Link driver **before connecting ST-LINK/V2 or V2-1** using ST's signed package and included instructions: https://www.st.com/en/development-tools/stsw-link009.html . Then choose one programming tool:

- **xPack OpenOCD:** follow https://xpack-dev-tools.github.io/openocd-xpack/docs/install/ . The manual option is the Windows archive matching the PC architecture, extracted with its `bin` directory added to `PATH`; check `openocd --version` and the installed native ST-Link configuration. Use the repository's staged, identity-guarded OpenOCD phases in [install.md](install.md), selecting the profile that matches the installed scripts. Use a staging path without spaces or shell metacharacters, such as `C:/attenuator/.local/program-plan`; generated Tcl normalizes Windows separators to forward slashes. The guide's shell variables are placeholders; use PowerShell variables such as `$STLINK_SERIAL` and single-line commands rather than POSIX backslash continuations. The execute wrapper invokes `openocd` from `PATH`.
- **STM32CubeProgrammer GUI:** download from https://www.st.com/en/development-tools/stm32cubeprog.html and follow ST's manual: https://www.st.com/resource/en/user_manual/um2237-stm32cubeprogrammer-software-description-stmicroelectronics.pdf . Select ST-LINK/SWD at a conservative clock (100 kHz), the exact adapter and the sole physically identified target. Check MCU identity and protection before changing anything. If protected, removing RDP in the option-byte interface mass-erases stock firmware irreversibly; stop on errors, then perform actual target POR and reconnect to verify RDP clear and identity. For the verified unprotected 64 KiB target, load the trusted `combined.bin` at `0x08000000` (or `combined.hex` with its embedded addresses), enable programming verification, program, independently compare the entire image, then deliberately start execution. This GUI route does **not** execute the repository's identity guards, confirmations or receipts; preserve the same identity, one-attempt/no-blind-retry and separate erase/verify/start discipline manually. Never substitute the app-only BIN at address zero. The CH340 UART loader is not ST's ROM UART bootloader; use this repository's USB updater for later app updates.

Neither Windows programming route has been executed for this project. Driver installation success or a transport acknowledgment is not board qualification or RF validation.

## Manual power-on reset without a switchable hub

After protection removal, the target requires real POR; a software reset, NRST pulse or resetting only the programmer's USB connection does not establish it. If no USB hub power switch is available, manually disconnect **every target power source** (CH340 USB, programmer-supplied 3.3 V and any other supply/backfeed path). Allow the target rail to discharge below the MCU's POR threshold, then reconnect the single chosen supply. LED extinction alone is not a voltage measurement. Do not connect programmer power and USB power together. Reconfirm target/programmer identity and perform the post-POR protection check before programming; see [install.md](install.md). Do not repeat unlock to substitute for POR.

## Offline validation limits

macOS and Linux checks use fake serial ports or local PTYs, including fragmented/malformed replies, timeouts, validation, readback mismatches and Linux controller-lock refusal. No hardware is opened by import/help/tests. Windows port names and platform branches are covered with fakes only: **Windows instructions are documented-not-tested**, not Windows qualification. No new physical flash, USB update, power-reset or RF measurement is claimed.
