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
  - External GitHub survey: [`CHECKM8_REFERENCES.md`](CHECKM8_REFERENCES.md)

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
- Result from both `src/log_white.txt` and `src/log_black.txt`: no `PWND`.
- Logs confirmed the payload now uses the global timeout:

```text
send_payload_chunks: sending 2016 bytes total (timeout=50 ms)
```

- Stage 4 still times out immediately on the first payload DNLOAD and returns as clean DFU.
- New signal in `src/log_black.txt`: attempt 2 exhausted stage 2 retries before reaching spray/patch, but current logs do not show the failed pad request values.

## Current Hypothesis

We need better visibility into the heap-shaping stages before changing payload logic again.

The code currently logs only "stage 3 complete" and "UAF triggered", but not:

- how many attempts `checkm8_stall()` needed
- whether each `no_leak` hole succeeded immediately or after retries
- what `pad_ret` was when the stage 2 guessed-send path did not get a STALL

Without this, multiple distinct failure modes look identical in the logs.

## Next Experiment

### v1.0.20

- Add INFO diagnostics for stage 2 pad failures when Linux guessed-send values are used.
- Add INFO diagnostics for A10/path-B spray:
  - `checkm8_stall` success attempts
  - each `no_leak` hole attempt count
  - final `leak/no_leak` loop attempt count
- Result from both `src/log_white.txt` and `src/log_black.txt`: no `PWND`.
- The diagnostics narrowed the failure:
  - stage 2 UAF triggers for guesses `128`, `64`, and `0`
  - stage 3 path B consistently succeeds
  - `checkm8_stall` succeeds in 3 attempts with `ret=0`, `abort=0 ms`
  - all 5 `no_leak` holes succeed in 1 attempt
  - final `leak/no_leak` succeeds in 1 attempt
- Therefore the remaining failure is concentrated in stage 4 payload execution/observation.

## Current Hypothesis

After stage 4, the code resets USB immediately after the payload DNLOAD timeout:

```text
send_payload_chunks: ... transfer completion unknown, payload may be executing
checkm8_exploit: stage 4 done -- triggering USB bus reset ...
```

If the timeout fires while the ROP chain/shellcode is still running, the immediate host reset may interrupt the very window where serial patching and descriptor creation happen.

## Next Experiment

### v1.0.21

- Add a short post-stage4 settle delay before `libusb_reset_device()`.
- Keep it small (`250ms`) so we test the race without returning to the earlier multi-second dead wait.
- Result from both `src/log_white.txt` and `src/log_black.txt`: no `PWND`.
- The delay appeared in logs and did not change the result:

```text
checkm8_exploit: stage 4 done -- waiting 250 ms before USB bus reset...
```

- Stage 2 and stage 3 still consistently succeed; stage 4 still times out on payload DNLOAD and returns clean DFU.

## Current Hypothesis

The shellcode may be crashing before it reaches the serial patch. In `gaster`'s `payload_notA9.S`, the shellcode copies the request handler with `memcpy()` before it appends `PWND:[checkm8]` to `gUSBSerialNumber`.

That means a crash in:

- `payload_dest`
- `payload_off`
- `payload_sz`
- `memcpy_addr`
- the handler-copy source/destination mapping

would produce exactly what we see: stage 4 timeout, then clean DFU serial.

## Next Experiment

### v1.0.22

- Diagnostic only: set `payload_sz=0` for A10/ROP-prefix payloads.
- This keeps the shellcode path but makes the handler-copy `memcpy()` a zero-length copy before the serial patch.
- If `PWND` appears, the remaining bug is in the handler copy or handler payload placement.
- If `PWND` still does not appear, the ROP jump/shellcode entry path is still the likely issue.
- Result from both `src/log_white.txt` and `src/log_black.txt`: no `PWND`.
- The diagnostic was active:

```text
assemble_payload: diagnostic handler copy disabled for A10 (payload_sz 248 -> 0)
assemble_payload: payload_dest=0x1800AFF08 payload_off=216 payload_sz=0
```

- Because disabling the handler copy did not change the serial, the failure is earlier than the handler-copy `memcpy()` or in the pre-serial pointer patching.

## Current Hypothesis

The A10 shellcode may not be reaching its serial patch block. The current notA9 shellcode performs these operations before touching `gUSBSerialNumber`:

- zero `dfu_handle_bus_reset`
- patch `dfu_handle_request`
- copy the checkm8 handler

`v1.0.22` removed the copy size, but it still left the pointer writes before the serial patch.

## Next Experiment

### v1.0.23

- Keep the same shellcode length and config layout, preserving all PC-relative literal offsets.
- For A10 only, replace the pre-serial request-handler patch/copy instructions with ARM64 NOPs.
- Also skip the final `patch_addr` write in this diagnostic shellcode so a late crash cannot erase the serial evidence.
- Expected result: if ROP enters the shellcode at all, the first meaningful action should be the serial marker and descriptor update.
- Result from both `src/log_white.txt` and `src/log_black.txt`: no `PWND`.
- The diagnostic shellcode was active:

```text
assemble_payload: A10 diagnostic serial-first shellcode active (pre-serial request patch/copy NOPed)
```

- Because the serial still does not change, either the ROP chain is not entering the shellcode or the stage 4 payload transfer is not actually completing before the timeout.

## Current Hypothesis

The single stage 4 payload `DFU_DNLOAD` returns `Operation timed out` at `offset=0` with the 50 ms gaster-style USB timeout. We have been treating that as "completion unknown", but with the serial-first shellcode it is now plausible that the timeout is happening before the 2016-byte DATA stage reliably reaches `insecure_memory_base`.

## Next Experiment

### v1.0.24

- Keep the A10 serial-first diagnostic shellcode.
- Increase only the stage 4 payload `DNLOAD` timeout to at least `1000ms`.
- Leave stage 2 timing unchanged, because the UAF setup depends on the short abort timing.
- Expected signal: if payload bytes were previously truncated by the 50 ms timeout, `send_payload_chunks` may return `2016` or at least produce a different post-stage4 behavior.
- Result from both `src/log_white.txt` and `src/log_black.txt`: no `PWND`.
- The payload send still timed out at `offset=0`, now after roughly one second:

```text
send_payload_chunks: raising payload timeout from 50 to 1000 ms
send_payload_chunks: offset=0 ret=-7 (Operation timed out)
```

- New signal: after the first clean failed verification, later retries left the USB stack in a bad state and the serial read returned a corrupted 6-byte string instead of the normal DFU serial. This is not success, but it is the first post-stage4 behavior change since the early diagnostics.

Observed corrupted serial reads:

```text
black: serial = "C...SvIe"  hex = [43 F0 53 76 49 65]
white: serial = "C0...~...\"" hex = [43 30 93 7E FE 5C]
```

Why this matters:

- Versions before this mostly returned to a clean DFU serial immediately after stage 4.
- `v1.0.24` still does not show `PWND`, but both devices stop behaving like clean DFU after the longer payload `DNLOAD`.
- The corrupted serial starts with `0x43` (`C`), matching the first byte of the original `CPID...` serial, but the rest is garbage. That suggests we may be disturbing the serial descriptor/string path or USB descriptor state rather than simply crashing before any effect.
- This is therefore progress in diagnosis: stage 4 is now visibly changing host/device behavior after the payload request.
- It also means follow-up attempts can contaminate the evidence. For future tests, pay special attention to the first verification immediately after stage 4, before retries run additional setup/reset logic.

## Current Hypothesis

The 1000 ms payload request may be getting far enough to disturb iBoot/USB state, but we still do not perform the two follow-up `DFU_DNLOAD` requests that `gaster` sends after payload:

- 16-byte zero suffix
- zero-length DNLOAD

`v1.0.15` tried the full finalizer before the later shellcode and timeout diagnostics; it only produced long waits. Retesting a bounded version now gives us a cleaner answer without another 25-second dead wait.

## Next Experiment

### v1.0.25

- Keep the A10 serial-first diagnostic shellcode.
- Keep the 1000 ms payload DATA-stage timeout.
- For ROP-prefix chips, send a bounded gaster-style suffix and zero-length DNLOAD after the payload.
- Skip the three status polls for ROP-prefix chips in this diagnostic, because earlier logs showed they only consume time after EP0 is already wedged.
- Result from both `src/log_white.txt` and `src/log_black.txt`: no `PWND`.
- The bounded finalizer was active, but both follow-up requests timed out:

```text
bounded finalize for ROP-prefix chip (timeout=1000 ms, no status polls)
send_dfu_finalize: suffix send failed: Operation timed out
send_dfu_finalize: zero-length send failed: Operation timed out
```

- Unlike `v1.0.24`, this run returned clean DFU serials on every verification. The bounded finalizer did not preserve or improve the corrupted-serial signal.

## Important Correction

The `serial-first` diagnostic introduced in `v1.0.23` NOPed too much of the shellcode prologue. It skipped not only the handler patch/copy path, but also:

```text
ldr x2, =dfu_handle_bus_reset
str xzr, [x2]
```

That means any successful in-memory serial patch could be erased by the host bus reset before verification, because iBoot's normal bus-reset handler was still active. This makes `v1.0.23` through `v1.0.25` weaker than intended as serial-patch diagnostics.

## Next Experiment

### v1.0.26

- Keep the A10 serial-first diagnostic, but preserve the `dfu_handle_bus_reset = NULL` write.
- NOP only the `dfu_handle_request` patch and handler-copy setup/call.
- Keep the 1000 ms payload DATA-stage timeout.
- Skip the bounded finalizer again for ROP-prefix chips, since `v1.0.25` only timed out and removed the `v1.0.24` corrupted-serial signal.
- Result from both `src/log_white.txt` and `src/log_black.txt`: no `PWND`.
- The corrected diagnostic was active:

```text
assemble_payload: A10 diagnostic serial-first shellcode active (bus-reset null preserved, request patch/copy NOPed)
```

- Both devices returned clean DFU serials on every verification. Preserving `dfu_handle_bus_reset = NULL` did not expose a hidden serial patch.

## Current Hypothesis

The serial-first diagnostic may be too synthetic. It proves that this diagnostic form does not visibly patch the serial, but it also removes the handler patch/copy behavior that `gaster` normally performs before serial update. We have not yet tested the full gaster-style notA9 shellcode together with the newer 1000 ms stage 4 payload timeout.

## Next Experiment

### v1.0.27

- Restore the full A10/notA9 shellcode path.
- Keep the 1000 ms payload DATA-stage timeout from `v1.0.24+`.
- Keep ROP-prefix finalizer skipped because `v1.0.25` showed bounded suffix/zero-length DNLOAD only time out.
- This tests the closest-to-gaster shellcode behavior under the only payload timing that produced a different USB/serial state.

- Result from both `src/log_white.txt` and `src/log_black.txt`: no `PWND`.
- Full shellcode + 1000 ms payload timeout returned clean DFU serials on every verification.
- Unlike `v1.0.24`, no corrupted-serial signal appeared. That earlier signal likely came from the artificial serial-first shellcode, not from payload timing alone.

## Important Correction

`v1.0.12` fixed the overwrite *fields* (`io_buffer/io_len` zeroed, `callback=nop_gadget`, `next=insecure_memory_base`) but the stage 4 code still sent the overwrite through the wrong USB request:

```text
bmReqType=0x02, bReq=0x03, wIndex=0x80   /* SET_FEATURE HALT -- stage 3 stall probe */
```

`gaster` sends the overwrite with:

```text
bmReqType=0x00, bReq=0x00, wValue=0, wIndex=0
```

The STALL we logged as "overwrite landed in freed io_buffer" was therefore very likely just the EP0 halt/STALL from the wrong request, not confirmation that the callback structure was written. Additional gaster divergences still present in our tree:

- missing `heap_pad_0/heap_pad_1` in `checkm8_overwrite_t` (48 bytes sent instead of 64)
- missing the pre-payload `DFU_DNLOAD` of `EP0_MAX_PACKET_SZ` (64 bytes) between overwrite STALL and payload
- ROP-prefix finalizer skipped since `v1.0.15`/`v1.0.25`, while `gaster` always runs suffix + zero-length DNLOAD + status polls

## Next Experiment

### v1.0.28

- Send overwrite via gaster's `(0,0,0,0)` control transfer.
- Add `heap_pad_0/heap_pad_1` magic values to the A10 overwrite blob.
- Send the 64-byte pre-payload `DFU_DNLOAD` after overwrite STALL.
- Restore gaster-style DFU finalize for ROP-prefix chips.
- Drop the 1000 ms payload timeout diagnostic; use normal `usb_timeout` again.

### v1.0.28

- Result from both logs: no `PWND`. Regressed vs earlier attempts:
  - overwrite `(0,0,0,0)` 64 bytes → STALL
  - pre-payload DNLOAD → immediate PIPE/STALL
  - payload DNLOAD → timeout at offset 0
  - `send_dfu_finalize` → ~25s of timeouts per attempt (suffix, zero-length, 3 status polls)
  - clean DFU serial on every verification

## Opus analysis review (`OPUS_ANALYSIS.txt`)

Cross-checked against **current** [gaster main](https://github.com/0x7ff/gaster/blob/main/gaster.c) (not an older snapshot):

| Claim in Opus TXT | Verdict |
|-------------------|---------|
| Overwrite uses `(2, 3, 0, 0x80)` | **Correct** — `gaster.c` line ~1211 |
| `checkm8_overwrite_t` is 48 bytes (callback only) | **Correct** — no `heap_pad` in current main |
| No pre-payload `EP0_MAX_PACKET_SZ` DNLOAD before payload | **Correct** — payload loop follows overwrite STALL directly |
| v1.0.28 `(0,0,0,0)` + 64 bytes + pre-DNLOAD was wrong | **Correct** — matched an outdated gaster excerpt |
| EP0 wedged after overwrite STALL explains follow-on PIPE/timeouts | **Plausible** — matches v1.0.28 log pattern |
| Separate 2048-byte “leading block” vs our 2016-byte send | **Misleading** — gaster `calloc(DFU_MAX_TRANSFER_SZ + …)` but still DNLOADs `data_sz` bytes in chunks; our `data_sz=2016` layout matches |

`v1.0.28` was based on a wrong gaster revision (overwrite `(0,0,0,0)` and heap pads from an older fork/commit dump).

## Next Experiment

### v1.0.29

- Revert stage 4 USB path to **current gaster main**:
  - overwrite `(0x02, 0x03, 0, 0x80)`, **48** bytes
  - no pre-payload DNLOAD
  - skip ROP-prefix finalize on Linux (v1.0.28 finalize wedge)
- Keep full notA9 payload assembly (`exec_addr=0x1820B0610`, etc.)

### v1.0.29

- Result from both logs: no `PWND`. Stage 4 USB path matches current gaster main again:
  - overwrite `(0x02, 0x03, 0x80)` 48 bytes → STALL
  - payload 2016 bytes → timeout at offset 0 (50 ms)
  - finalize skipped → fast run (~1 s per attempt vs v1.0.28)
  - clean DFU serial every time
- Stage 2 Linux guesses per macro-attempt: `128`, `64`, `0` (white log).

## Reference: [verygenericname/gaster](https://github.com/verygenericname/gaster)

README says **"Fixes A10x"** (CPID `0x8011` / A10X), not **A10 Fusion (`0x8010`)**.

Compared to [0x7ff/gaster](https://github.com/0x7ff/gaster) main (May 2026):

| Area | verygenericname fork | Relevant for our `0x8010`? |
|------|----------------------|----------------------------|
| `notA9` config includes `dfu_handle_bus_reset` | Yes (same as upstream now) | Already in our `notA9_config_t` |
| Stage 4 overwrite `(2,3,0,0x80)` 48 bytes | Same | Already v1.0.29 |
| After payload: skip 16B + 0-len DNLOAD for `0x8011`/`0x8012` only | A10X/T2 fix | **Not** for `0x8010` — A10 still runs suffix + zero-length + status |
| `usb_timeout` default **5 ms** | gaster default | We used **50 ms** on Linux only — diverges from gaster stage 2 abort timing |
| iBoot-2696 / `0x8010` offsets | Same as ours | No new offsets |

Conclusion: the fork does **not** add a distinct A10 (`0x8010`) patch beyond current upstream gaster. It is still worth matching gaster’s **5 ms** `USB_TIMEOUT` default and running finalize with that timeout (not `5000 ms`).

### v1.0.30 (white + black logs, 2026-05-30)

- `usb_timeout=5 ms` applied on Linux; stages 1–3 OK (UAF guesses 128/64/0).
- Stage 4: overwrite STALL OK, payload 2016 B → `Operation timed out` at offset 0 (5 ms).
- Finalize suffix + zlen also timeout at 5 ms; **status polls still used `DFU_TIMEOUT` 5000 ms** → ~15 s extra per attempt (21:46:11 → 21:46:26 white).
- Serial: clean DFU, no `PWND:[checkm8]` on both devices (3 attempts each).

### v1.0.31 (branch `comp-changes`)

- `dfu_get_status_timeout()` for finalize polls when they run.
- Skip status polls when suffix or zero-length DNLOAD fails (EP0 wedged after payload).
- Faster iteration; exploit outcome unchanged vs v1.0.30 until a USB/payload hypothesis lands.
- Índice de rama y env vars: [`COMP_CHANGES.md`](COMP_CHANGES.md).

### v1.0.32 (branch `comp-changes`) — código

- `checkm8_payload_timeout_ms()` + env **`CHECKM8_PAYLOAD_TIMEOUT_MS`** (stage 4 payload DNLOAD only).
- Log al inicio: `usb_timeout=` y `payload_timeout=`; línea extra si el override está activo.

### v1.0.32 — prueba en hardware (white + black, 2026-05-30 ~23:01–23:03)

**Entorno:** sin `CHECKM8_PAYLOAD_TIMEOUT_MS` (baseline gaster: `usb_timeout=5`, `payload_timeout=5`). Rama `comp-changes`, binario **1.0.32**. Logs: `src/log_white.txt`, `src/log_black.txt`.

| Métrica | v1.0.30 | v1.0.32 (esta corrida) |
|---------|---------|-------------------------|
| Tiempo stage 4 → verify (intento 1) | ~15 s (status1–3 @ 5 s) | **~1 s** (23:01:58→23:01:59 white) |
| `send_dfu_finalize` | `status1/2/3 failed` cada ~5 s | `skipping status polls (finalize DNLOAD failed)` |
| PWND | No | **No** |
| Serial | limpio len=98 | limpio len=98 (sin corrupción) |
| Payload DNLOAD | timeout @ offset 0, 5 ms | igual |
| UAF (white) | 128 / 64 / 0 | 128 / 64 / 0 |

**Stages 1–3:** OK en ambos dispositivos (spray path B, 5 holes). White: tras reset post-spray, libusb `errno=2` un instante y re-open en addr+1 (84→85); exploit siguió.

**Stage 4 (igual que v1.0.30 en outcome):** overwrite STALL OK → payload 2016 B → `Operation timed out` @ offset 0 → suffix/zlen timeout → skip status polls → bus reset 250 ms → serial DFU normal.

**Progreso real:**

- **Sí (herramienta / iteración):** el fix v1.0.31 de finalize funciona; ya no se pierden ~15 s por intento. El log confirma `payload_timeout=5 ms` en la línea de arranque.
- **No (exploit):** aún no hay `PWND:[checkm8]`. No se ejecutó el experimento de payload largo (`CHECKM8_PAYLOAD_TIMEOUT_MS=1000`); esta corrida valida solo baseline 5 ms + finalize rápido.

**Interpretación:** el timeout inmediato en payload + EP0 colgado (finalize imposible) sugiere que el dispositivo sale del estado DFU “habitual” tras el overwrite/payload, pero el parche del serial no ocurre — coherente con ROP que no completa, payload truncado por timeout de 5 ms, o heap incorrecto.

**Nota:** esa corrida no probó payload 1000 ms (solo documentación + baseline). Ver **v1.0.33** abajo.

### v1.0.33 — resultado en hardware (white + black, 2026-05-31 ~10:23–10:25)

**Código activo:** `payload_timeout=1000 ms` (stage 4 solo), `usb_timeout=5 ms`. Log confirma `version 1.0.33` y `Linux A10 experiment: payload DNLOAD timeout 1000 ms`.

| Métrica | v1.0.32 (5 ms) | v1.0.33 (1000 ms) |
|---------|----------------|-------------------|
| Overwrite | STALL OK (48 B, 0x02/0x03) | STALL OK (48 B, 0x02/0x03) |
| Payload DNLOAD | timeout @ offset 0, 5 ms | timeout @ offset 0, 1000 ms |
| Finalize | skip (EP0 wedged) | skip (EP0 wedged) |
| PWND | No | **No** |
| Serial | limpio len=98 | limpio len=98 |
| Addr post-stage4 | igual | igual (no cambia) |

**Observación crítica:** El payload DNLOAD timeutea en offset=0 tanto con 5ms como con 1000ms. Esto descarta la hipótesis de truncado por timeout corto. El BootROM no está aceptando el transfer DATA stage en absoluto — EP0 sigue halted después del overwrite STALL sin importar el timeout del host.

**Comportamiento del addr USB:** El addr NO cambia después del stage 4 (100→100→101, no hay salto adicional post-payload). El shellcode no ejecuta.

## Síntesis OPUS + Composer (2026-05-30/31)

**Resumen actual:** Estamos perfectamente alineados con `gaster` main en stage 4:
- overwrite `(0x02, 0x03, 0, 0x80)` 48 bytes ✅
- `io_buffer=0, io_len=0, callback=nop_gadget, next=insecure_memory_base` ✅
- sin pre-payload DNLOAD ✅
- `exec_addr=0x1820B0610`, `patch=0x1020074AC` ✅

Sin embargo el payload DNLOAD sigue timuteando en offset=0 con cualquier timeout. **El overwrite STALL deja EP0 halted permanentemente** y el siguiente DFU_DNLOAD no alcanza el BootROM.

**Hipótesis raíz más probable:** On Linux/xHCI, el host EP0 queda en estado halted tras el STALL del overwrite. `libusb_control_transfer` no puede enviar el SETUP packet del DFU_DNLOAD porque el kernel bloquea nuevos transfers hacia un endpoint halted. El BootROM nunca ve los bytes del payload.

**La solución real** requiere o bien:
1. Un USB reset entre el overwrite STALL y el payload send (que es lo que hace gaster implícitamente en macOS via IOKit — IOKit resetea EP0 automáticamente tras STALL)
2. Usar la implementación de King (`(0,0,0,0)` + blob diferente) que puede evitar el STALL en primer lugar

## Current Hypothesis (post v1.0.33)

Linux `libusb` no hace CLEAR_FEATURE(ENDPOINT_HALT) automáticamente en EP0 después de un STALL, a diferencia de IOKit en macOS. Esto deja EP0 halted, causando que todos los DFU_DNLOAD subsiguientes fallen inmediatamente con timeout (el kernel rechaza el submit).

Solución candidata: llamar `libusb_reset_device()` **después** del overwrite STALL y **antes** del payload send. Esto envía un USB bus reset que limpia el estado halted del EP0 en el dispositivo, permitiendo que el próximo DFU_DNLOAD llegue al BootROM.

**Riesgo:** el bus reset puede disturbar el estado de la heap si el BootROM procesa el reset antes de que el callback de la overwrite sea invocado. Pero dado que el STALL ya confirmó que el overwrite llegó, el callback ya está en memoria — solo necesitamos que el BootROM lo ejecute.


### v1.0.34 — resultado en hardware (white + black, 2026-05-31 ~13:04–13:06)

**⚡ BREAKTHROUGH — payload delivered for the first time.**

| Métrica | v1.0.33 (sin reset) | v1.0.34 (con reset) |
|---------|---------------------|---------------------|
| Payload DNLOAD | timeout @ offset 0, 1000 ms | **ret=2016 (2016 bytes ENTREGADOS)** |
| Finalize suffix | timeout | **ret=16 ✅** |
| Finalize zero-len | timeout | **ret=0 ✅** |
| status1 | skip | **state=0x06 (MANIFEST_SYNC) ✅** |
| status2 | skip | **state=0x07 (MANIFEST) ✅** |
| status3 | skip | **state=0x08 (MANIFEST_WAIT_RESET) ✅** |
| PWND | No | **No** |
| Serial | limpio len=98 | limpio len=98 |

**La hipótesis EP0-HALT en Linux era correcta.** El bus reset entre overwrite STALL y payload send libera EP0 y el BootROM recibe los 2016 bytes completos. La DFU state machine completa el ciclo `MANIFEST_SYNC → MANIFEST → MANIFEST_WAIT_RESET` como se espera después de un DFU download exitoso.

**Nuevo problema:** El serial sigue limpio después del stage 4. El ROP chain se entrega pero el shellcode no parchea el serial string. Esto apunta a un fallo en el ROP chain o el shellcode, **no** en la plomería USB.

**Nota sobre `ret=2016 (Other error)`:** libusb reporta `Other error` pero transfirió los 2016 bytes. Esto es coherente con que el BootROM empezó a ejecutar el ROP callback durante o justo después del STATUS phase del DFU_DNLOAD — el dispositivo interrumpió el STATUS phase al entrar al callback, que libusb interpreta como error del transfer aunque el DATA stage fue exitoso.

**Nota sobre `usb_timeout=5 ms`:** Correcto — gaster usa 5ms por default (confirmado en `gaster.c` línea 1632: `usb_timeout = 5`). No es un bug.

## Current Hypothesis (post v1.0.34)

La plomería USB está resuelta. El payload se entrega. El problema está en el **ROP chain / shellcode**:

1. El bus reset entre overwrite STALL y payload send **destruye el heap state** antes de que el BootROM ejecute el callback:
   - El overwrite llega (STALL confirmado).
   - El bus reset llega al BootROM como un `dfu_handle_bus_reset` event.
   - **El BootROM puede limpiar el estado DFU** durante el bus reset, sobreescribiendo el freed io_buffer donde pusimos el overwrite.
   - El payload llega pero ya no hay overwrite → el callback original del BootROM ejecuta → DFU normal → serial limpio.

2. Alternativamente: el ROP chain sí ejecuta pero falla en algún paso (TTBR0 switch, exec_addr incorrecto, etc.).

**Diferenciador clave:** si el bus reset destruye el overwrite, el addr USB NO debería cambiar después del stage 4. Si el ROP ejecuta parcialmente, podríamos ver un addr DIFERENTE (el reset proveniente del shellcode).

**Observación:** En el log black, el addr post-stage4 es **el mismo** que el addr con el que entramos al stage 4 (105→105, 106→106, 107→107). Esto es consistente con la hipótesis 1: el bus reset destruye el overwrite antes del payload.

### v1.0.35 — resultado en hardware (white + black, 2026-05-31 ~13:11)

**❌ Fallido — EP0 CLEAR_HALT bloqueado por el Host Controller Driver.**

- `CLEAR_HALT EP0-OUT` y `CLEAR_HALT EP0-IN` devolvieron `ret=-5 (Entity not found / LIBUSB_ERROR_NOT_FOUND)`.
- El driver xHCI/usbcore no registra a EP0 en su mapa de endpoints halted ordinarios o bloquea operaciones directas de clearing halt en él.
- El EP0 host-side continuó halted, y el payload `send_payload_chunks: offset=0` falló por timeout (`Operation timed out`).

---

### v1.0.36 — Reabrir handle en la misma dirección USB sin Bus Reset

Implementado en `checkm8_patch.c` y compilado exitosamente.

**Concepto:**
El bloqueo de EP0 tras el STALL es puramente en el **lado del host** (un estado lógico que el controlador de host xHCI y el kernel mantienen al recibir un STALL en un transfer). El hardware físico del dispositivo no mantiene a EP0 halted (los STALLs en EP0 son transitorios para indicar que una petición no es soportada). 

En lugar de:
- Un bus reset físico (`libusb_reset_device()`) que destruye el heap state al disparar `dfu_handle_bus_reset()` en el BootROM.
- `libusb_clear_halt` que es rechazado por el kernel en EP0.

Hacemos un **Reopen del handle local**:
1. Guardamos la referencia de `libusb_device` mediante `libusb_ref_device()`.
2. Liberamos la interfaz y cerramos el handle de libusb `usb` viejo.
3. Reabrimos la comunicación usando `libusb_open()` con el mismo puntero `dev`.
4. Re-clamamos la interfaz 0.
5. Liberamos la referencia del dispositivo con `libusb_unref_device()`.

**Propósito:**
Esto obliga al driver del host (xHCI/usbcore) a reinicializar el anillo de transferencia (transfer ring) de EP0 y limpiar el estado halt lógico local, **sin enviar ningún tipo de señalización de reset por el bus USB al dispositivo**. El dispositivo retiene la heap intacta en su estado actual, con la overwrite callback intacta, y EP0 host-side queda perfectamente listo para transferir los 2016 bytes del payload.

---

## v1.0.36 — Resultado en hardware (white + black, 2026-05-31 ~13:22)

**❌ Fallido — EP0 HALT NO fue eliminado por el reopen del handle.**

El concepto de v1.0.36 era: cerrar y reabrir el handle de libusb sobre el mismo `libusb_device` sin enviar señalización USB al dispositivo, forzando al kernel a reinicializar el estado host-side de EP0.

Lo que muestran los logs:

```text
checkm8: [stage4] v1.0.36: reopen handle on same addr to clear host-side EP0 HALT...
checkm8: [stage4] v1.0.36: handle reopened (same device, no bus reset) -- overwrite preserved
checkm8: [stage4] send_payload_chunks: sending 2016 bytes total (timeout=1000 ms)
checkm8: [stage4] send_payload_chunks: offset=0 ret=-7 (Operation timed out)
```

El payload `DFU_DNLOAD` volvió a timeout inmediato (igual que antes de v1.0.34). El reopen de handle limpia el estado userspace de libusb pero **el kernel usbcore no reinicializa el transfer ring de EP0** — el estado HALT persiste en el driver xHCI. No se entregaron bytes.

Conclusión: para limpiar el EP0 HALT sin bus reset físico se necesita o bien una ioctl especial al kernel, o **evitar el STALL en primer lugar**.

## Current Hypothesis (post v1.0.36)

**El problema raíz es la elección del request para el overwrite.**

`gaster` usa `(0x02, 0x03, 0, 0x80)` → SET_FEATURE(ENDPOINT_HALT). El BootROM responde STALL porque SET_FEATURE en EP0-IN no es válido en DFU mode. Ese STALL es lo que deja EP0 halted en el host (Linux xHCI no hace CLEAR automáticamente).

La solución es enviar el overwrite usando el **mismo tipo de request que el pad de stage 2**: `(0x00, 0x00, 0, 0)` — control OUT genérico hacia el BootROM. Este request:

- Escribe los bytes del overwrite en el freed io_buffer (mismo mecanismo que el pad de stage 2)
- **No genera STALL** — el BootROM lo acepta como un OUT data stage genérico
- EP0 queda listo para el siguiente `DFU_DNLOAD` (payload) sin necesidad de ningún tipo de reset

Este es el "King path".

## Next Experiment

### v1.0.37 — King path: overwrite via `(0x00, 0x00, 0, 0)` sin STALL

**Código:** `checkm8_patch.c` compilado con `-DCHECKM8_KING_PATH`

- `send_overwrite()` usa `usb_ctrl_transfer(usb, 0x00, 0x00, 0, 0, blob, 48, USB_TIMEOUT_MS)`
- Si retorna `LIBUSB_ERROR_PIPE` (STALL inesperado): abort (King path inefectivo)
- Si retorna ≥ 0 o TIMEOUT: ACK → EP0 listo → enviar payload inmediatamente
- Sin handle reopen, sin bus reset

**Build:**
```bash
make clean && make EXTRA_CFLAGS=-DCHECKM8_KING_PATH
```

**Señales esperadas en el log:**
```text
send_overwrite [KING PATH]: sending 48 bytes via (0x00,0x00,0,0) -- no STALL expected
send_overwrite [KING PATH]: ACK -- overwrite landed without STALL, EP0 ready for payload
v1.0.37 [KING PATH]: no handle reopen needed -- EP0 not halted
send_payload_chunks: sending 2016 bytes total (timeout=1000 ms)
send_payload_chunks: offset=0 ret=2016   <-- payload entregado
```

**Posibles resultados:**

1. **PWND** — King path correcto; shellcode ejecuta y parchea serial.
2. **Payload entregado pero sin PWND** — mismo que v1.0.34; el overwrite llegó pero el ROP/shellcode falla.
3. **Overwrite STALL con King path** — el BootROM rechaza el `(0,0,0)` request en este estado DFU (inesperado).
4. **Overwrite ACK pero payload timeout** — el `(0,0,0)` no escribe al freed io_buffer (geometry incorrecta).

---

## v1.0.37 — Resultado en hardware (white, 2026-05-31 ~20:44)

**❌ Fallido — Resultado 3: King path `(0x00, 0x00, 0, 0)` también STALLa EP0.**

Líneas clave del log:

```text
send_overwrite [KING PATH]: sending 48 bytes via (0x00,0x00,0,0) -- no STALL expected
send_overwrite [KING PATH]: ret=-9 (Pipe error)
unexpected STALL -- EP0 will be halted (King path ineffective)
```

**Conclusión crítica: el STALL no depende del tipo de request.**

El STALL viene del BootROM procesando la escritura al freed io_buffer. El mismo mecanismo que produce el STALL en stage 2 con `(0x00, 0x00)` aplica también en stage 4. El tipo de request es irrelevante — lo que causa el STALL es la escritura a la heap corrupta que hace que el firmware quede en un estado que fuerza STALL de EP0.

**Nueva señal importante: serial corrupto después del intento 1:**

```text
checkm8_verify_pwned: serial = "C@??X" (len=6)
checkm8_verify_pwned: serial hex = [43 40 15 A2 E2 58]
```

Esto es idéntico a la señal de v1.0.24: `0x43` = 'C' (primer byte de "CPID:..."), resto garbage. El overwrite `(0x00, 0x00)` **sí está llegando al dispositivo y alterando memoria**, pero sin payload el ROP callback nunca ejecuta. El STALL deja EP0 bloqueado antes de que el payload llegue.

**Nota sobre el flujo de intentos:**
- Intento 1: STALL → device NO reset automáticamente (addr 11 → 11) → `set configuration failed, errno=110 (ETIMEDOUT)` → device wedged 5 segundos
- Intento 2: falla en stage 1 (EP0 todavía wedged del intento anterior)
- Intento 3: addr 12 (device finalmente reseteo) → mismo resultado STALL

## Current Hypothesis (post v1.0.37)

**El STALL de EP0 en Linux es inherente al mecanismo de overwrite del checkm8.** No es posible evitarlo cambiando el tipo de request. La diferencia con macOS es que IOKit envía automáticamente `CLEAR_FEATURE(ENDPOINT_HALT)` en EP0 después de un STALL, restaurando EP0 de forma transparente. Linux/libusb no hace esto.

Intentos previos de limpiar EP0 sin bus reset:
- `libusb_clear_halt(EP0)` → `LIBUSB_ERROR_NOT_FOUND` (v1.0.35) — libusb rechaza EP0 en su API de alto nivel
- Reopen handle → no limpia el transfer ring (v1.0.36)
- King path `(0x00, 0x00)` → también STALLa (v1.0.37)

**Opción no intentada:** Acceso directo al kernel vía ioctl `USBDEVFS_RESETEP` sobre el file descriptor de usbfs. Este ioctl resetea el estado del endpoint en el driver xHCI **sin enviar nada al dispositivo** — limpia el toggle bits y el flag HALT interno del driver. A diferencia de `libusb_clear_halt`, no intenta enviar `CLEAR_FEATURE` al dispositivo y no está bloqueado para EP0 en el kernel level.

## Next Experiment

### v1.0.38 — `USBDEVFS_RESETEP` directo sobre EP0 vía usbfs ioctl

**Concepto:**

Abrir `/dev/bus/usb/BUS/ADDR` directamente y llamar:
```c
int fd = open("/dev/bus/usb/003/011", O_RDWR);
unsigned int ep_out = 0x00;   // EP0 OUT
unsigned int ep_in  = 0x80;   // EP0 IN
ioctl(fd, USBDEVFS_RESETEP, &ep_out);
ioctl(fd, USBDEVFS_RESETEP, &ep_in);
close(fd);
```

`USBDEVFS_RESETEP` reinicia el endpoint en el kernel xHCI driver: limpia el halt flag interno y resetea el data toggle — sin enviar ningún packet USB al dispositivo. El BootROM nunca ve esta operación.

**Por qué puede funcionar:**
- El HALT de EP0 es un estado **host-side** en el transfer ring del kernel
- `libusb_clear_halt` falla porque intenta enviar `CLEAR_FEATURE(ENDPOINT_HALT)` al dispositivo (inválido para EP0) y su código de alto nivel lo rechaza
- `USBDEVFS_RESETEP` opera solo en el kernel driver, no envía nada al bus

**Build:** binario gaster path (sin `CHECKM8_KING_PATH`), con la limpieza de EP0 vía `USBDEVFS_RESETEP` insertada después del overwrite STALL y antes del payload.

**Señales esperadas:**
```text
send_overwrite: STALL received (expected) -- overwrite landed in freed io_buffer
v1.0.38: USBDEVFS_RESETEP EP0 OUT ret=0
v1.0.38: USBDEVFS_RESETEP EP0 IN  ret=0
send_payload_chunks: sending 2016 bytes total (timeout=1000 ms)
send_payload_chunks: offset=0 ret=2016    <-- payload entregado sin bus reset
```

**Si `USBDEVFS_RESETEP` falla con `EINVAL`/`ENODEV`:** el kernel xHCI rechaza reset de EP0 también. En ese caso, la siguiente opción es `USBDEVFS_CLEAR_HALT` con el ioctl directo (bypass completo de libusb).

**Resultado:** _pendiente de implementación y test en hardware_

---

## v1.0.38 — Resultado en hardware (white + black, 2026-05-31 ~23:26–23:28)

**Fallido — `USBDEVFS_RESETEP` no sirve para EP0 en este host/kernel.**

Ambos dispositivos ejecutaron el path gaster, no el King parcial:

```text
send_overwrite: sending 48 bytes (bmReqType=0x02, bReq=0x03, wIndex=0x80)
send_overwrite: ret=-9 (Pipe error)
v1.0.38: USBDEVFS_RESETEP EP0-OUT ret=-1 No such file or directory
v1.0.38: USBDEVFS_RESETEP EP0-IN  ret=-1 No such file or directory
v1.0.38: RESETEP failed -- falling back to handle reopen
```

El resultado fue idéntico en los 3 intentos de `white` y los 3 intentos de `black`:
- stage 2 OK con guesses Linux `128`, `64`, `0`
- stage 3 spray path B OK, `checkm8_stall` en 3 intentos, 5 holes OK
- overwrite STALL esperado OK
- `USBDEVFS_RESETEP` falla para EP0 OUT/IN con `ENOENT`
- fallback reopen funciona a nivel handle, pero no limpia el problema
- payload `DFU_DNLOAD` `2016 B` vuelve a timeoutear en `offset=0` tras `1000 ms`
- finalize suffix y zero-length DNLOAD también timeoutean
- serial post-stage4 vuelve limpio, `len=98`, sin `PWND`

**Conclusión:** H1 confirmada. El kernel no expone EP0 como endpoint reseteable vía `USBDEVFS_RESETEP`; el fallback al reopen reproduce `v1.0.36`, y el payload no llega.

**Nota de logging:** `checkm8: stage 4 complete -- payload delivered` es engañoso en este caso. El log correcto previo dice `completion unknown after Operation timed out at offset=0`; no hay evidencia de bytes entregados. Conviene cambiar ese mensaje antes de la próxima prueba.

---

## Research Correction (2026-06-01)

La hipótesis `USBDEVFS_RESETEP(EP0)` quedó más débil después de revisar documentación y código del kernel Linux.

Fuentes revisadas:
- Linux USB docs: `USBDEVFS_RESETEP` y `USBDEVFS_CLEAR_HALT` están documentados para endpoints `1..15`, bulk/interrupt, no para EP0/control.
- Linux `drivers/usb/core/devio.c`: `proc_resetep()` llama `findintfep()`, que busca el endpoint en descriptores de interfaz. EP0 normalmente no aparece en esos descriptores, por lo que esperamos `-ENOENT`/`-EINVAL`.
- Linux USB host API: endpoints de control no "haltan" como bulk/interrupt; reportan protocol stall con códigos similares.
- Patch histórico xHCI: el driver ya tiene lógica especial para stalls en control endpoints.

**Conclusión actual:** `v1.0.38` sigue siendo un diagnóstico útil, pero no debe tratarse como la solución más probable. Si falla con `ENOENT`/`EINVAL`, confirma que el kernel no expone reset/clear directo para EP0 vía usbfs. Si sorprendentemente devuelve `0`, el siguiente dato crítico es si el payload llega sin bus reset.

## Hypothesis Queue (2026-06-01)

Regla para los próximos pasos: probar una hipótesis por vez, con una versión/log inequívocos, y registrar siempre la primera verificación post-stage4 antes de contaminar el estado con retries.

| ID | Hipótesis | Evidencia actual | Prueba aislada | Señal esperada |
|----|-----------|------------------|----------------|----------------|
| H1 | `USBDEVFS_RESETEP` no sirve para EP0 | Docs/kernel dicen endpoints `1..15`; `libusb_clear_halt(EP0)` ya dio `NOT_FOUND` | Correr `v1.0.38` gaster-path y loguear `errno` real de EP0 OUT/IN | `ENOENT`/`EINVAL` y payload vuelve a timeout |
| H2 | El bloqueo no es un "HALT limpiable", sino estado xHCI/control-transfer/device tras overwrite | Control endpoints no haltan como bulk; xHCI maneja stalls control internamente | Capturar `usbmon` desde overwrite hasta payload; comparar si el SETUP/DATA del `DFU_DNLOAD` sale del host | Si no sale DATA: host/TD; si sale DATA: BootROM/device state |
| H3 | Bus reset entre overwrite y payload libera USB pero destruye heap/overwrite | `v1.0.34` entregó payload+finalize, pero serial limpio y addr sin reset de shellcode | No repetir como solución; usarlo solo como control "payload path healthy" | Payload `ret=2016`, finalize OK, sin PWND |
| H4 | El "King path" probado no fue King completo | `v1.0.37` solo cambió request type con overwrite 48 B; King usa overwrite grande + callback chain | Correr binario King upstream en el mismo host/cable; si funciona, portar flujo completo | King muestra `PWND:[checkm8]` o falla igual que tr4mpass |
| H5 | El problema es host USB/cable/controlador, no tr4mpass | gaster/King reportan sensibilidad a Linux/xHCI, hubs y cables | Correr upstream `gaster pwn` mismo host/puerto/dispositivo | Si gaster falla también: stack host; si gaster pwn: bug nuestro |
| H6 | El payload no llega realmente sin reset, aunque el código lo trate como "completion unknown" | `v1.0.33/36/37` timeoutean en offset 0 con 1000 ms | `usbmon` o instrumentación de transferencia con `actual_length`/status más cruda | Confirmar si hay DATA stage parcial/completo |
| H7 | Drift de versión/docs está ensuciando conclusiones | `v1.0.38` corrió impreso como `1.0.36`; `COMP_CHANGES.md` estaba viejo | Bump de `CHECKM8_EXPLOIT_VERSION` y actualizar docs antes de hardware | **Hecho en v1.0.39** |

## Test Plan (one by one)

1. **H7 / higiene primero:** bump real de versión antes de otra corrida y asegurar que el log diga el experimento correcto. **Hecho en código v1.0.39.**
2. **H6 / payload actual_length:** correr `v1.0.39` con `CHECKM8_EP0_RECOVERY=none` y mirar `status=... actual=...` del payload DNLOAD.
3. **H5 / control gaster:** correr upstream `0x7ff/gaster` en el mismo host/cable/puerto.
4. **H4 / control King:** correr upstream `pgarba/King` en el mismo host/cable/puerto.
5. **H2/H6 / traza USB:** si gaster/King divergen de tr4mpass, capturar `usbmon` en overwrite→payload para ver si el host emite el `DFU_DNLOAD` DATA stage.
6. **Port King completo:** solo si King logra `PWND` o muestra una señal USB cualitativamente mejor que gaster/tr4mpass.

## Next Experiment

### v1.0.39 — Payload transfer diagnostic + explicit EP0 recovery mode

**Código aplicado:** 
- `CHECKM8_EXPLOIT_VERSION` ahora imprime `1.0.39`.
- `send_payload_chunks()` usa un control transfer async diagnóstico para loguear:
  - `ret`
  - `transfer->status`
  - `transfer->actual_length`
  - `completed`
  - `cancelled`
- El log final ya no dice `payload delivered` cuando hay timeout; dice `payload path attempted`.
- `CHECKM8_EP0_RECOVERY` permite elegir una sola estrategia por corrida:
  - unset / `none`: no recuperación EP0, baseline gaster para medir `actual_length`.
  - `resetep`: intenta `USBDEVFS_RESETEP`, sin fallback automático.
  - `reopen`: solo close/open handle, reproducción explícita de v1.0.36.
  - `resetep-reopen`: reproduce v1.0.38 con fallback, si se necesita comparar.
  - `bus-reset`: reproduce v1.0.34, entrega payload pero probablemente destruye heap.

**Primera prueba recomendada:**

```bash
unset CHECKM8_EP0_RECOVERY
sudo ./tr4mpass ...
```

**Señales a buscar:**

```text
checkm8_exploit: version 1.0.39
v1.0.39: EP0 recovery mode 'none'
send_payload_chunks: offset=0 ret=-7 (...) status=... actual=...
send_payload_chunks: done -- completion unknown after ... (actual=... status=...)
```

Interpretación:
- `actual=0`: el payload no está saliendo/llegando como DATA stage; priorizar `usbmon`, gaster/King control, o host USB.
- `actual=2016` con timeout/status failure: el DATA stage llegó, pero falla STATUS/ejecución; volver a hipótesis ROP/shellcode/finalize.
- `actual` parcial: mirar tamaño exacto para inferir si xHCI corta en packet boundary.

**Resultado:** _pendiente de corrida en hardware._
