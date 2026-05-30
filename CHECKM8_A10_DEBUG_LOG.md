# A10 checkm8 Debug Log

This document tracks the A10 / CPID `0x8010` checkm8 experiments so we do not repeat loops.

## Current Target

- Device: A10, iBoot `2696.0.0.1.33`
- Expected success marker: `PWND:[checkm8]` in the USB serial
- Main logs used:
  - `src/log_white.txt`
  - `src/log_black.txt`
- Most useful source references:
  - `gaster`: readable C checkm8 implementation, including A10 offsets, ROP, payload layout, and stage flow
  - `ipwndfu`: original checkm8 reference
  - `palera1n`: not useful for low-level comparison; it delegates the exploit to embedded `checkra1n`

## Important Reference Findings

- `gaster` uses A10 TTBR0 entries:
  - `0x1000006A5` at `ttbr0_vrom_off`
  - `0x60000100000625` at `ttbr0_vrom_off + 8`
  - `0x60000180000625` at `ttbr0_sram_off`
  - `0x1800006A5` at `ttbr0_sram_off + 8`
- `gaster` sets `notA9.patch_addr += ARM_16K_TT_L2_SZ` for A10-class ROP-prefix chips, producing the `0x102...` VROM alias.
- `gaster`'s A10 overwrite leaves `io_buffer` and `io_len` zeroed; it only sets `callback = nop_gadget` and `next = insecure_memory_base`.
- `palera1n` does not contain a readable checkm8 implementation. It extracts and runs an embedded `checkra1n` binary.

## Version History

### v1.0.0

- Added explicit exploit version logging.
- Logs still showed DFU serial without `PWND`, meaning the device returned as normal DFU.

### v1.0.1

- Realigned the notA9 config layout after checking shellcode literal loads.
- Goal was to stop the shellcode from reading wrong config fields.
- Result: still no `PWND`.

### v1.0.2

- Moved the patched function pointer target to `dfu_handle_request` instead of treating the shellcode as a bus-reset handler.
- Result: still no `PWND`.

### v1.0.3

- Removed post-stage-4 `libusb_reset_device` based on the theory that bus reset reinitialized the serial.
- Result: Linux/xHCI became stuck after stage 4, with descriptor timeouts and stale EP0 state.

### v1.0.4

- Added wait/polling after stage 4 and skipped DFU finalization for ROP-prefix chips.
- Result: device still did not return as `PWND`.

### v1.0.5

- Changed TTBR0 entry `0x80` to be writable.
- Later comparison with `gaster` showed this was likely a wrong direction.

### v1.0.6

- Changed TTBR0 entry `0x81` to be executable.
- Later comparison with `gaster` showed this also diverged from the working reference.

### v1.0.7

- Reverted TTBR0 entries to `gaster` values.
- Changed final shellcode store to `str xzr` and targeted `dfu_handle_bus_reset`.
- Reintroduced post-stage-4 USB reset after nulling bus reset handler.
- Result: improvement in USB behavior. After stage 4 the serial could be read cleanly again, but it was still normal DFU serial, not `PWND`.

### v1.0.8

- Removed the pre-payload close/reopen after overwrite STALL.
- Sent payload immediately after overwrite on the same handle.
- Result: no `PWND`. Failure still occurred around the payload DNLOAD timeout.

### v1.0.9

- Padded the A10 payload transfer from `2016` to `2048` bytes so it matched `DFU_MAX_TRANSFER_SZ`.
- Log confirmed `data=2016 transfer=2048 pad=32`.
- Result: no `PWND`. This ruled out the simple `io_len=2048` vs `payload=2016` mismatch.

### v1.0.10

- Added `HH:MM:SS` timestamps to the central logger.
- Result: no exploit behavior change; logs showed stage 4 and reset complete very quickly, returning to clean DFU.

### v1.0.11

- Restored `gaster`-style `patch_addr += ARM_16K_TT_L2_SZ` for ROP-prefix chips.
- Log confirmed `patch=0x1020074AC`.
- Result: no `PWND`; still returns as clean DFU.

### v1.0.12

- Changed the A10/TLBI overwrite to match `gaster`:
  - left `io_buffer = 0`
  - left `io_len = 0`
  - set `callback = nop_gadget`
  - set `next = insecure_memory_base`
- Result from `src/log_white.txt`: no `PWND`; still times out on the first payload DNLOAD and returns as clean DFU.
- This means the non-gaster overwrite fields were not the primary cause.

## Current Hypothesis

With the overwrite now matching `gaster`, the remaining active divergence is the forced 2048-byte payload transfer. Our current code sends:

```c
data=2016 transfer=2048 pad=32
```

`gaster` sends `data_sz` exactly in its stage patch loop. The padding was useful to test the old `io_len=2048` hypothesis, but after `v1.0.12` it is no longer gaster-compatible.

## Next Experiment

### v1.0.13

- Send exact `data_sz` (`2016`) instead of padded `2048` for A10/ROP-prefix payloads.
- Keep the gaster-style zeroed `io_buffer/io_len` overwrite from `v1.0.12`.
- Add an `INFO` log of overwrite fields so the next run visibly confirms the zeroed fields.
- Result from both `src/log_white.txt` and `src/log_black.txt`: no `PWND`.
- Logs confirmed:
  - `data=2016 transfer=2016 pad=0`
  - `patch=0x1020074AC`
  - `io_buffer=0x0 io_len=0x0 callback=0x10000CC6C next=0x1800B0000`
- This rules out the padding and A10 overwrite mismatch as the primary cause.

## Current Hypothesis

The remaining high-confidence mismatch is the ARM64 notA9 shellcode itself. Our payload bytes were a hybrid sequence: it patched `dfu_handle_request` first and only zeroed `dfu_handle_bus_reset` at the end via a locally edited instruction. `gaster`'s `payload_notA9.S` zeroes `dfu_handle_bus_reset` first, then patches `dfu_handle_request`, and its config layout is:

```c
pwnd[2]
payload_dest
dfu_handle_bus_reset
dfu_handle_request
payload_off
payload_sz
memcpy_addr
gUSBSerialNumber
usb_create_string_descriptor
usb_serial_number_string_descriptor
patch_addr
```

## Next Experiment

### v1.0.14

- Replace the ARM64 notA9 shellcode instruction sequence with the exact `gaster` flow.
- Reorder `notA9_config_t` to match `gaster` exactly.
- Keep:
  - gaster TTBR0 values
  - `patch=0x1020074AC`
  - gaster-style zeroed overwrite
  - exact `2016` payload transfer
- Result from both `src/log_white.txt` and `src/log_black.txt`: no `PWND`; still returns as clean DFU.
- This makes shellcode/config layout much less likely to be the remaining mismatch.

## Current Hypothesis

The next active mismatch with `gaster` is DFU finalization after payload. Our code still skips finalization for ROP-prefix chips, logging:

```text
skipping DFU finalize (ROP-prefix chip -- device will re-enumerate naturally)
```

`gaster` always sends:

```c
DFU_DNLOAD 16 zero bytes
DFU_DNLOAD zero-length
GET_STATUS MANIFEST_SYNC / MANIFEST / MANIFEST_WAIT_RESET
```

even after the payload transfer used `transfer_ret = NULL`.

## Next Experiment

### v1.0.15

- Run `send_dfu_finalize()` for A10/ROP-prefix chips too.
- Log each finalize step at info/warn level so we can tell whether suffix, zero-length DNLOAD, or status checks answer.
- Result from both `src/log_white.txt` and `src/log_black.txt`: no `PWND`.
- Finalize did not progress the device:
  - suffix DNLOAD timed out
  - zero-length DNLOAD timed out
  - all three status checks timed out
- It only added about 25 seconds per attempt, then returned as clean DFU.

## Current Hypothesis

On Linux, stage 2 uses guessed async-transfer byte counts because usbfs does not report the cancelled transfer's actual length. The current macro attempt sequence tries guesses `128` then `64`, but `MAX_EXPLOIT_TRIES` is `2`, so we never test guess `0`.

Logs show:

```text
attempt 1: [Linux xHCI] Guessing sent=128
attempt 2: [Linux xHCI] Guessing sent=64
```

If the actual cancelled transfer length is closer to `0`, stage 2 may report "UAF triggered" but leave the heap geometry wrong for stage 4.

## Next Experiment

### v1.0.16

- Re-skip ROP-prefix DFU finalization because `v1.0.15` showed it only times out.
- Increase `MAX_EXPLOIT_TRIES` from `2` to `3` so a single run covers Linux guesses `128`, `64`, and `0`.
- Result from both `src/log_white.txt` and `src/log_black.txt`: no `PWND`.
- All three Linux guesses were tested and behaved the same:
  - `Guessing sent=128`
  - `Guessing sent=64`
  - `Guessing sent=0`
- Each attempt reached stage 4, got the expected overwrite STALL, timed out on the 2016-byte payload transfer, then returned as clean DFU.

## Current Hypothesis

The ROP callback jump address differs from `gaster`.

Our logs show:

```text
assemble_payload: exec_addr=0x1800B0610
```

But `gaster` builds the execution callback as:

```c
insecure_memory_base + ARM_16K_TT_L2_SZ + ttbr0_sram_off + 2 * sizeof(uint64_t)
```

For A10 that is:

```text
0x1800B0000 + 0x2000000 + 0x610 = 0x1820B0610
```

This matters because the ROP chain switches TTBR0 to the crafted table before jumping. The second SRAM L2 alias is the executable mapping; jumping to the raw insecure memory address can fetch through the wrong descriptor.

## Next Experiment

### v1.0.17

- Change ROP-prefix `exec_addr` to match `gaster`:
  - from `insecure_memory_base + rop_prefix_sz`
  - to `insecure_memory_base + ARM_16K_TT_L2_SZ + rop_prefix_sz`
- Keep `MAX_EXPLOIT_TRIES=3` so the new jump is tested with guesses `128`, `64`, and `0`.
- Result from both `src/log_white.txt` and `src/log_black.txt`: no `PWND`.
- The logs confirmed the new jump address:

```text
assemble_payload: exec_addr=0x1820B0610
```

- All three Linux guesses still reached stage 4, timed out on the 2016-byte payload transfer, and returned as clean DFU.

## Current Hypothesis

The Linux async-transfer workaround is too broad.

`usb_ctrl_transfer_async_abort()` currently forces any cancelled transfer to return `0` on Linux. That was intended for the stage 2 OUT `DFU_DNLOAD`, where usbfs can report the full requested length even after cancellation and we use explicit guesses (`128`, `64`, `0`).

The same helper is also used by stage 3 IN `GET_DESCRIPTOR` leak/no-leak/stall probes. For those probes, forcing every cancelled IN transfer to `0` can make stage 3 report success even if the actual leak geometry is wrong.

## Next Experiment

### v1.0.18

- Keep the Linux cancelled-transfer `0` workaround only for OUT transfers.
- Return `transfer->actual_length` for cancelled IN transfers so stage 3 decisions are based on the real descriptor-transfer result.
- Result from both `src/log_white.txt` and `src/log_black.txt`: no `PWND`.
- Stage 3 still completed immediately and stage 4 behaved the same:
  - expected overwrite STALL
  - payload DNLOAD timeout
  - clean DFU serial after reset
- This suggests the broad cancelled-IN workaround was not the main blocker.

## Current Hypothesis

The payload DNLOAD path still diverges from `gaster`.

`gaster` sends the payload with:

```c
send_usb_control_request(handle, 0x21, DFU_DNLOAD, 0, 0, &data[i], packet_sz, NULL)
```

On Linux, that means:

- timeout is the global `usb_timeout`
- return status is intentionally ignored because `transfer_ret == NULL`

Our code used a fixed `500ms` timeout and still treated non-timeout/non-IO negative returns as hard failures.

## Next Experiment

### v1.0.19

- Send the payload with `checkm8_usb_timeout_ms()` instead of hardcoded `500ms`.
- Treat all negative payload DNLOAD results as non-fatal/unknown, matching `gaster`'s `transfer_ret=NULL` behavior.

Expected log signal:

```text
checkm8_exploit: version 1.0.19
send_payload_chunks: ... timeout=50 ms ...
```

