# RAM mailbox and permutation selection

The ELF exports `mailbox`, a 128-byte, 4-byte-aligned structure at `0x20000000`. `firmware/core.h` is the authoritative ABI. SWD preserves PA13/PA14; no display is needed. **Last applied means the serial command was issued, not measured RF attenuation.**

## Offline command staging

`tools/mailbox.py` checks the ARM ELF type, load ranges, entry/stack vector and mailbox symbol/section address/size before generating a Tcl file. Use the exact ELF that was flashed and the real MCU UID recorded after unprotection/POR. The generated file selects one exact ST-Link serial, checks DPIDR, full MCU IDCODE, every UID word and inactive protection, then compares all ELF flash load bytes before accessing mailbox RAM. Matching firmware alone does not identify a unit. It validates the live schema and reads stable generation snapshots with bounded retries.

Native attachment uses a memory-only AP0 target with core examination deferred and the included examine-end write hook disabled before initialization. The final speed is 100 kHz. It requests no core examination, halt/reset, MCU MMIO write, target verification algorithm or flash unlock. Adapter/debug-port activity still occurs; offline configuration tests cannot exclude opaque physical effects. Select `--profile native-dapdirect` for upstream OpenOCD 0.12.0's native configuration, or the default `native-swd` for the newer native configuration described in the installation guide. No transport fallback occurs.

```sh
python3 tools/mailbox.py --elf build/attenuator.elf --serial "$STLINK_SERIAL" \
  --idcode "$EXACT_IDCODE" --uid "$MCU_UID_WORDS" \
  --output .local/status.tcl status
python3 tools/mailbox.py --elf build/attenuator.elf --serial "$STLINK_SERIAL" \
  --idcode "$EXACT_IDCODE" --uid "$MCU_UID_WORDS" \
  --output .local/set.tcl set 18.25
python3 tools/mailbox.py --elf build/attenuator.elf --serial "$STLINK_SERIAL" \
  --idcode "$EXACT_IDCODE" --uid "$MCU_UID_WORDS" \
  --output .local/select.tcl select 2
python3 tools/mailbox.py --elf build/attenuator.elf --serial "$STLINK_SERIAL" \
  --idcode "$EXACT_IDCODE" --uid "$MCU_UID_WORDS" \
  --output .local/get.tcl get
```

These commands only write local files, refusing overwrites. They do not open USB or SWD. Keep generated files private because they contain unit identity. After authorization and review, execute one selected file with `openocd -f .local/status.tcl` (or the explicitly selected command file). `status` requests MCU identity/option, flash and RAM reads through the memory-only target; `get` publishes a mailbox GET request; `set` and `select` intentionally cause GPIO transactions. Never run these on stock firmware.

| Index | SI | CLK | LE |
| --- | --- | --- | --- |
| 0, boot hypothesis | PB11 | PB12 | PB13 |
| 1 | PB11 | PB13 | PB12 |
| 2 | PB12 | PB11 | PB13 |
| 3 | PB12 | PB13 | PB11 |
| 4 | PB13 | PB11 | PB12 |
| 5 | PB13 | PB12 | PB11 |

Changing mapping completes at the main loop boundary. Every transaction ends with all three wires low; mapping changes software masks while all wires remain low, waits, and retransmits the current value with the new mapping. No GPIO mode changes or LE rising edge are introduced by the mask switch itself. These guarantees apply to the selected logical mapping. An incorrect hypothesis can put clock/data pulses on the actual LE wire: establish physical wiring before qualifying behavior. All mappings remain untraced hypotheses.

After a write, status prints the acknowledgment sequence, status code, result value in quarter-dB units, result mapping and issued transaction count, plus live last value/mapping/count. The selected mapping is printed as explicit SI/CLK/LE PB pin numbers. Require this readback rather than assuming a selection succeeded. `issued=1` after a successful boot is the zero command; GET does not increment it. Status can report a clock/CPU fault; write plans refuse any state other than ready.

## Schema v1

All words are little-endian uint32, naturally aligned. The debugger owns only words 8–15. Firmware owns all other words. Do not write firmware-owned status/header fields or run multiple mailbox clients concurrently.

| Word offsets | Fields | Meaning |
| --- | --- | --- |
| 0–3 | magic, version, bytes, state | `0x50453433`, `1`, `128`; state 0 initializing, 1 ready, 2 clock fault, 3 CPU fault |
| 4–7 | generation, last_qdb, permutation, issued | Generation is even outside firmware result/live publication; last completed command in quarter dB, mapping 0–5 and transaction count |
| 8–11 | seq, command_magic, command_version, op | Published even nonzero sequence, command magic/version, op 1 GET / 2 SET / 3 SELECT |
| 12–15 | arg, reserved, check, seq_inverse | GET arg 0; SET arg 0–127; SELECT arg 0–5; reserved 0; checksum and sequence guard |
| 16–20 | ack, status, result_qdb, result_permutation, result_issued | Snapshot at acknowledgment; retained across later UART/live-status updates |
| 21–31 | unused | Reserved, zero at initialization |

Publication is a single-writer protocol:

1. Read a stable firmware result: generation before and after the read must be equal and even. Validate schema/state; no previous command may be outstanding.
2. Choose an even sequence greater than `ack`, normally `ack+2`. Sequence zero is unpublished; odd values are ignored. Never reuse a sequence. Stop at `0xfffffffe`; wrap needs a deliberate reset, not an automatic one.
3. Write `seq=0` first. Write the complete payload, `seq_inverse = ~new_seq`, and `check = 0xa5c35a3c XOR new_seq XOR command_magic XOR command_version XOR op XOR arg XOR reserved`. Complete those SWD writes before publishing the aligned sequence word **last**.
4. Firmware snapshots payload between sequence reads with memory barriers and checks sequence equality, inverse and checksum before any action. Unpublished, inconsistent, incomplete and repeated/older sequences produce no action or acknowledgment. A coherent new command with bad magic/version/op/range/reserved data is acknowledged with an error and no GPIO transaction.
5. Firmware makes generation odd, publishes result/status/ack and live state, then makes generation even. The client waits for its exact ack and checks status; a timeout is unknown completion, not permission to replay. Inspect status before any recovery.

Status codes: 0 success, 1 bad command magic, 2 bad command version, 3 unknown operation, 4 invalid attenuation/GET argument, 5 invalid permutation, 6 nonzero reserved word. A checksum mismatch is treated as incomplete publication, not an acknowledged semantic error. The checksum detects accidental partial writes; it is not authentication. One synchronous firmware loop serializes UART and mailbox commands. Use one control interface at a time when correlating results with measurements.
