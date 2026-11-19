# UART bootloader and USB app updates

The combined loader/app image passed SWD flash and independent verification, CH340 set/get and one same-image USB app update on one examined unit. The 2,156-byte app transferred in 34 chunks, the loader acknowledged CRC completion, and GET returned requested zero after app start. See the [qualification record](validation.md) and [v0.1.0 downloads](https://github.com/Watts-Blake/pe43703-attenuator-firmware/releases/tag/v0.1.0). Retain SWD access until those checks pass on the particular board. OLED/buttons remain unsupported.

## Images and memory

| Range / artifact | Purpose |
| --- | --- |
| `0x08000000..0x08001fff`, `bootloader.elf/.bin/.hex` | 8 KiB reserved loader; initial SWD installation only |
| `0x08002000..0x0800fbff`, `attenuator.elf/.bin/.hex` | Up to 55 KiB app; app vector table and VTOR at its base |
| `0x0800fc00..0x0800ffff` | 1 KiB metadata page; commit, byte size, CRC32, complemented size/CRC |
| `combined.bin/.hex` | Full 64 KiB loader/app/CRC image for the guarded SWD installer |
| `0x20004ff8..0x20004fff` | Reserved eight-byte RAM reset request; excluded from both linkers' RAM |

Use only the **app-only `attenuator.bin`** with the USB updater. Do not send `combined.bin`, HEX or ELF. The host validates app length/alignment, aligned RAM stack pointer and Thumb reset vector inside the supplied relocated image before opening the port. The loader independently checks the same bounds and CRC before committing or running it. CRC detects corruption; it is not a signature, board identification or authentication. Install only trusted code built for the matching board and loader layout.

## Update after initial SWD installation

Use Python 3.10+ on Linux or macOS; no Python packages are needed. Connect CH340 USB with the programmer's power disconnected. Identify the exact port from the operating system's USB inventory; prefer a stable, unique device path where available. Close terminal programs that hold the port. The updater does not scan for devices or choose a port automatically. No simultaneous SWD mailbox writer is permitted.

Inspect the app **offline**:

```sh
python3 tools/usb_update.py inspect --app build/attenuator.bin
```

Record its printed SHA256 as `APP_SHA256` and set `SERIAL_PORT` to the identified CH340 port. The following is a **device operation**: it requests 0 dB, resets the MCU into the loader, erases the app/metadata pages, writes the app and starts it after verification:

```sh
python3 tools/usb_update.py update --app build/attenuator.bin \
  --port "$SERIAL_PORT" --entry app \
  --confirm "UPDATE $SERIAL_PORT $APP_SHA256"
```

One command is outstanding at a time. The tool checks the exact loader version/geometry reply before erasing, checks each reply, commits only after full CRC verification, requests app start, then requires `get` to reply `0.00`. That response confirms a requested/issued zero command, not measured insertion loss. Keep stable power throughout the update; no application RF command service runs while the loader is active.

## Interrupted update and recovery

The following recovery behavior is implemented and covered by offline flash-failure models. Physical power-interruption recovery was not exercised on the examined unit. The successful USB test uploaded the same app image; it did not test a different-version upgrade or independently read back flash after USB.

The app command `bootloader` first issues zero, replies `ok bootloader` with CRLF, waits for UART transmit completion, writes a magic/complement pair in reserved RAM and requests a system reset. The loader consumes and clears this request. A normal reset jumps immediately to a valid app; there is no startup entry window. An invalid or interrupted app remains in the loader indefinitely, ready for a new **explicit operator-initiated** upload:

```sh
python3 tools/usb_update.py update --app build/attenuator.bin \
  --port "$SERIAL_PORT" --entry loader \
  --confirm "UPDATE $SERIAL_PORT $APP_SHA256"
```

The loader erases the metadata page **before** any app page. Each update starts from offset zero and requires sequential, aligned writes; the commit marker is written last after size, vector and CRC checks. A power interruption before invalidation can retain the previous valid app. Once invalidated, an incomplete update cannot boot. Flash/serial failures produce an error or remain in the loader; no automatic retry, reset, alternate port, protection downgrade or SWD fallback occurs in the tool. Retain the error and inspect the connection before deliberately starting another upload. A corrupt loader or a valid app that cannot accept `bootloader` requires SWD recovery; the loader cannot replace itself over UART.

While waiting in the loader, an unterminated line uses at most 159 bytes and has no effect until LF. Invalid/overlong/error lines are discarded through LF. The loader never starts an invalid app on an idle timeout. The host bounds every request to five seconds, or ten for begin/finish; a missing reply means unknown completion and stops the transfer. Firmware UART/flash waits use bounded poll counts; a flash-busy timeout parks without further flash actions. These counts are not calibrated wall-clock deadlines, and single-bank flash access stalls can extend execution while hardware is busy.

## Protocol version 1

115200 8N1, no flow control, one complete LF-terminated ASCII command at a time. Lowercase fixed-width hex fields; CR is ignored. Replies below use LF, except app replies use CRLF. Maximum payload is 64 bytes per write. All malformed commands return `err\n`; success returns `ok\n`.

| Command | Meaning |
| --- | --- |
| `hello` | Exact reply `boot 1 08002000 0000dc00 00000040\n` (version, app base, maximum bytes, chunk bytes) |
| `begin <size:8hex> <crc:8hex>` | Size 236..56320, multiple of four; invalidate metadata, erase required 1 KiB app pages |
| `write <offset:8hex> <data:hex>` | Offset equals bytes already received; 4..64 bytes, multiple of four; program halfwords, verify each |
| `finish` | Require complete size, valid initial SP/Thumb reset vector and CRC; commit metadata last, revalidate |
| `run` | Require valid committed app and no active transfer; acknowledge, wait for UART TC, reset UART configuration, relocate VTOR/MSP and jump |

CRC is reflected CRC-32/ISO-HDLC, polynomial `0xedb88320`, initial `0xffffffff`, final XOR `0xffffffff`, matching Python `zlib.crc32`. Both the protocol layer and hardware flash callbacks reject loader/out-of-flash addresses; neither exposes an arbitrary-address write/erase command. Hardware callbacks verify erased pages, halfword programming, EOP and PGERR/WRPRTERR. No option-byte code is present in the loader. These operations follow [ST PM0075 Rev 2 §2.3.2–2.3.4](https://www.st.com/resource/en/programming_manual/pm0075-stm32f10xxx-flash-memory-microcontrollers-stmicroelectronics.pdf).

The loader runs on reset HSI 8 MHz with USART1 BRR=69 (nominal 115942 baud, +0.64% before oscillator tolerance); the app uses its assumed HSE 8 MHz → 72 MHz clock and exact BRR=625. Loader/app UART communication passed on one examined board. Supply/temperature tolerance, measured clock accuracy, physical power-failure recovery and other boards remain unqualified. PA4–PA7 remain idle throughout. UART replies and CRC completion do not measure RF behavior.
