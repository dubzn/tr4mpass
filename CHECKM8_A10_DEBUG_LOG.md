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

## Next Experiments (si v1.0.36 falla)

### v1.0.37 — King path completo
King no usa `(0x02, 0x03)` STALL overwrite. Usa un overwrite diferente que no produce STALL, evitando el problema EP0-HALT por completo.


