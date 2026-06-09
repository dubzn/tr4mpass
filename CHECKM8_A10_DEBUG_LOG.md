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
| H6 | El payload DATA stage puede llegar aunque libusb devuelva timeout | `v1.0.39`: `actual=2016` en 5/6 intentos; `actual=128` en 1/6 | Mantener diagnóstico `actual_length`; si hace falta, confirmar con `usbmon` | DATA completo pero STATUS timeout desplaza el problema a ejecución/finalize |
| H7 | Drift de versión/docs está ensuciando conclusiones | `v1.0.38` corrió impreso como `1.0.36`; `COMP_CHANGES.md` estaba viejo | Bump de `CHECKM8_EXPLOIT_VERSION` y actualizar docs antes de hardware | **Hecho en v1.0.39** |
| H8 | El finalize DFU o el timing post-payload perturba ejecución/observación | `actual=2016` pero suffix/zlen fallan y serial vuelve limpio; `v1.0.40` corrido con `finalize mode 'gaster'` todavía no prueba esto | `v1.0.40`: `CHECKM8_FINALIZE_MODE=skip`; luego delay post-payload | Si PWND aparece, finalize/timing era parte del bloqueo |

## Test Plan (one by one)

1. **H7 / higiene primero:** bump real de versión antes de otra corrida y asegurar que el log diga el experimento correcto. **Hecho en código v1.0.39.**
2. **H6 / payload actual_length:** correr `v1.0.39` con `CHECKM8_EP0_RECOVERY=none` y mirar `status=... actual=...` del payload DNLOAD. **Hecho: 5/6 con `actual=2016`, 1/6 parcial `128`.**
3. **H8 / finalize/timing:** correr `v1.0.40` con `CHECKM8_FINALIZE_MODE=skip`, sin otro cambio. **Pendiente; los logs actuales siguen con `finalize mode 'gaster'`.**
4. **H8 / delay:** si sigue limpio, repetir con `CHECKM8_POST_PAYLOAD_DELAY_MS=500`.
5. **H5 / control gaster:** correr upstream `0x7ff/gaster` en el mismo host/cable/puerto.
6. **H4 / control King:** correr upstream `pgarba/King` en el mismo host/cable/puerto.
7. **H2/H6 / traza USB:** si gaster/King divergen de tr4mpass, capturar `usbmon` en overwrite→payload para ver STATUS/finalize/reset.
8. **Port King completo:** solo si King logra `PWND` o muestra una señal USB cualitativamente mejor que gaster/tr4mpass.

## Next Experiment

### v1.0.39 — Payload transfer diagnostic + explicit EP0 recovery mode

**Código aplicado:** 
- `CHECKM8_EXPLOIT_VERSION` ahora imprime `1.0.39`.
- `send_payload_chunks()` usa un control transfer async diagnóstico para loguear:
  - `ret`
  - `transfer->status`
  - `transfer->actual_length`
  - `completed`
  - `cancelled` (`v1.0.40` lo renombra a `cancel_requested` para evitar confusión)
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

**Resultado con logs actualizados (`src/log_white.txt`, `src/log_black.txt`):**

No hubo `PWND`, pero sí obtuvimos una señal nueva y valiosa:

- `white`: los 3 intentos llegaron a stage 4 con `actual=2016` en el payload DNLOAD.
- `black`: intento 1 quedó parcial en `actual=128`; intentos 2 y 3 llegaron a `actual=2016`.
- En todos los casos el resultado libusb siguió siendo `ret=-7 (Operation timed out)` y `status=TIMED_OUT`.
- Finalize siguió fallando: suffix/zero-length DNLOAD timeoutean o, en el primer intento de `black`, suffix da `Pipe error`.
- Después del reset post-stage4 el serial vuelve limpio, `len=98`, sin `PWND`.

**Lectura:** H6 cambia de forma. Ya no parece correcto decir que el payload "no llega" en general: en 5/6 intentos el DATA stage reportó `actual=2016`. El problema queda después de transferir DATA: STATUS phase/finalize, ejecución ROP/shellcode, o reset/observación del `PWND`.

**Nota de logging:** `cancelled=1` en este diagnóstico significa que el host pidió cancelar tras vencer el timeout de espera; no niega `actual=2016`. En `v1.0.40` el campo se llama `cancel_requested`.

## Next Experiment

### v1.0.40 — Separate payload DATA from DFU finalize/reset timing

**Hipótesis nueva:** después de `actual=2016`, el suffix/zero-length finalize inmediato podría estar perturbando una ejecución que ya recibió el payload, o simplemente agregando ruido antes del reset. Queremos aislarlo sin volver al loop de `RESETEP`/reopen.

**Código aplicado:**
- `CHECKM8_EXPLOIT_VERSION` imprime `1.0.40`.
- El log diagnóstico de payload ahora muestra `submit=...` y `cancel_requested=...`.
- `CHECKM8_FINALIZE_MODE`:
  - `gaster` (default): conserva suffix + zero-length + status polls acotados.
  - `skip`: no envía finalize después del payload.
- `CHECKM8_POST_PAYLOAD_DELAY_MS`:
  - default `0`.
  - si se define, espera ese tiempo después del payload y antes de finalize/return a reset.

**Prueba recomendada 1, una variable por vez:**

```bash
unset CHECKM8_EP0_RECOVERY
unset CHECKM8_PAYLOAD_TIMEOUT_MS
export CHECKM8_FINALIZE_MODE=skip
unset CHECKM8_POST_PAYLOAD_DELAY_MS
sudo ./tr4mpass ...
```

**Señal esperada:**
- Si aparece `PWND`, el finalize era dañino o innecesario en nuestro path Linux/A10.
- Si sigue `actual=2016` + serial limpio, repetir con delay post-payload:

```bash
unset CHECKM8_PAYLOAD_TIMEOUT_MS
export CHECKM8_FINALIZE_MODE=skip
export CHECKM8_POST_PAYLOAD_DELAY_MS=500
sudo ./tr4mpass ...
```

Eso separa "no finalizar" de "darle más tiempo a ROP/shellcode antes del bus reset".

**Resultado con logs actuales (`src/log_white.txt`, `src/log_black.txt`):**

Todavía no prueba H8. Los dos logs muestran:

- `checkm8_exploit: version 1.0.40`
- `v1.0.40: EP0 recovery mode 'none'`
- `v1.0.40: finalize mode 'gaster'`

O sea: se corrió la versión nueva, pero no el experimento de `CHECKM8_FINALIZE_MODE=skip`.

**Qué sí aportan estos logs:**

- `black`: 3/3 intentos llegaron a stage 4 con `actual=2016`.
- `white`: intento 1 llegó con `actual=2016`; intento 2 quedó parcial con `actual=192`; intento 3 ni siquiera llegó a stage 4 porque falló en stage 1 reset con:

```text
dfu_dnload: block 0 failed: Pipe error
checkm8_stage_reset: DFU_DNLOAD(suffix) failed
```

- En todos los intentos que sí llegaron a stage 4, el patrón siguió igual:
  - overwrite STALL esperado
  - payload `ret=-7` con `status=TIMED_OUT`
  - `actual` mayormente completo (`2016`) y una vez parcial (`192`)
  - finalize suffix/zlen timeout
  - bus reset 250 ms
  - serial DFU limpio, sin `PWND`

**Lectura actualizada:**

- H6 sigue teniendo sentido: el DATA stage llega muchas veces, así que el problema no parece ser simplemente "el host no manda payload".
- H8 sigue teniendo sentido como hipótesis de trabajo, pero estos logs no la confirman ni la debilitan porque el path ejecutado fue el mismo `gaster finalize` de antes.
- El `actual=192` en `white` muestra que hay algo de variabilidad host/device en el corte del transfer, pero no alcanza para explicar por sí solo la ausencia de `PWND`, porque también fallan varios intentos con `actual=2016`.
- El fallo de stage 1 en `white` intento 3 parece ruido adicional del estado USB tras retries; no cambia la lectura principal sobre stage 4.

**Conclusión operativa:** la hipótesis H8 es razonable, pero el run actual fue un re-baseline de `v1.0.40`, no el experimento aislado. El próximo dato útil sigue siendo correr exactamente `CHECKM8_FINALIZE_MODE=skip` y, si hace falta, luego `CHECKM8_POST_PAYLOAD_DELAY_MS=500`.

---

## Re-análisis Profundo (2026-06-01) — Después de v1.0.36

### Logs analizados: log_black.txt y log_white.txt (v1.0.36, ~13:21–13:22)

#### Patrón observado (idéntico en black y white, 3/3 intentos):

```
v1.0.36: reopen handle on same addr to clear host-side EP0 HALT...
v1.0.36: handle reopened (same device, no bus reset) -- overwrite preserved
send_payload_chunks: offset=0 ret=-7 (Operation timed out)
```

**El reopen del handle libusb (sin bus reset) NO limpia el EP0 HALT del kernel.**
La conclusión es definitiva: el estado HALT se mantiene en el `usbcore`/xHCI del kernel, no en el handle de libusb. Cerrar y reabrir el fd no resetea el driver.

---

### Mapa definitivo de versiones vs comportamiento EP0

| Versión | Estrategia post-overwrite | Resultado DNLOAD | Serial |
|---------|--------------------------|-----------------|--------|
| v1.0.33 y anteriores | Sin EP0 recovery | ret=-7 timeout | limpio |
| v1.0.34 | bus reset completo | **ret=2016 ✅** (DFU 0x06→0x07→0x08) | limpio |
| v1.0.35 | CLEAR_HALT EP0 | ret=-5 NOT_FOUND + ret=-7 | limpio |
| v1.0.36 | reopen handle mismo device | ret=-7 timeout | limpio |

**Conclusión única del mapa:** El único mecanismo que entregó el payload es el bus reset (v1.0.34). Pero tras recibir 2016 bytes con DFU completado, el serial sigue limpio.

---

### ¿Por qué v1.0.34 (bus reset) NO pwneó el device?

**Dos hipótesis en competencia:**

#### H-A: El bus reset destruye el overwrite antes que el ROP ejecute
- El BootROM recibe el bus reset → llama `dfu_handle_bus_reset()` → limpia/reinicia DFU state machine
- La overwrite en el freed io_buffer sobrevive en RAM pero el BootROM ya no la invoca como callback (reinicia la pipeline DFU)
- El payload de 2016 bytes llega → el BootROM lo procesa como firmware normal → DFU states (MANIFEST_SYNC→MANIFEST→MANIFEST_WAIT_RESET) son el ciclo DFU normal
- El ROP nunca ejecuta → serial limpio

#### H-B: El overwrite sobrevive al bus reset, el ROP ejecuta, pero algo falla en el shellcode
- `dfu_handle_bus_reset()` NO limpia el freed io_buffer (depende de la implementación del BootROM)
- El BootROM reinicia DFU y eventualmente procesa el overwrite callback → ROP ejecuta
- El payload llega (2016 bytes), el DFU completa normalmente (la DFU finalization vía nop_gadget devuelve al loop normalmente)
- El ROP ejecuta pero alguno de los gadgets falla: `write_ttbr0`, `tlbi`, `exec_addr`, o el shellcode ARM64

**Diferenciador clave para distinguir H-A vs H-B:**
- Si H-A: la USB addr post-stage4 es la MISMA que durante stage4 (sin reset del device). ✅ Observado en logs
- Si H-B: la USB addr post-stage4 podría cambiar si el shellcode dispara un bus reset interno (pero el shellcode de gaster nulifica `dfu_handle_bus_reset` antes de hacer el bus reset del serial...)

**El MD anterior dice:** "En el log black, el addr post-stage4 es el mismo que el addr con el que entramos al stage4". Esto es consistente con H-A, pero no la confirma porque en H-B la addr también podría ser la misma si el shellcode no dispara un reset USB propio.

---

### Análisis del código: ¿qué haría el ROP si ejecutara?

El ROP chain para A10 es:
```
callback[0]: write_ttbr0(insecure_memory_base)  → cambia TTBR0 al page table en SRAM
callback[1]: tlbi(0)                             → TLB invalidate
callback[2]: exec_addr(0)                        → salta al shellcode en SRAM
callback[3]: write_ttbr0(ttbr0_addr)            → restaura TTBR0 original
callback[4]: tlbi(0)                             → TLB invalidate
callback[5]: ret_gadget(0)                       → return
```

El shellcode ARM64 (120 bytes) en `exec_addr=0x1820B0610`:
1. Nulifica `dfu_handle_bus_reset` (SRAM ptr)
2. Parchea `dfu_handle_request` para instalar el nuevo handler en `payload_dest`
3. `memcpy` del handle code al `payload_dest`
4. Espera que `gUSBSerialNumber` no sea nulo
5. Copia string " PWND:[checkm8]" via `ldp/stp`
6. Llama `usb_create_string_descriptor`
7. Escribe resultado en `usb_serial_number_string_descriptor`
8. Escribe `0xD2800000` en `patch_addr` (= `0x1020074AC`)
9. `ret`

**Punto crítico del ROP:**
`exec_addr = insecure_memory_base + ARM_16K_TT_L2_SZ + rop_prefix_sz`
= `0x1800B0000 + 0x2000000 + (ttbr0_sram_off + 2*8)`
= `0x1800B0000 + 0x2000000 + 0x600 + 16`
= `0x1820B0610`

El shellcode está en `0x1820B0610` **bajo el nuevo TTBR0 que apunta a `insecure_memory_base`**. Esto es la virtual address bajo el nuevo mapping. La dirección **física** del shellcode es `insecure_memory_base + rop_prefix_sz = 0x1800B0610` (dentro del payload que enviamos).

Esto parece correcto. El problema podría ser que el shellcode en la SRAM no existe correctamente porque **el bus reset vacía la SRAM de DFU** antes de que el ROP ejecute.

---

### Hipótesis de trabajo más probable (H-A refinada)

El bus reset en v1.0.34 entrega el payload pero activa `dfu_handle_bus_reset()` en el BootROM. Esta función:
1. Puede re-inicializar el io_buffer pool (liberando la region donde está la overwrite)
2. O simplemente cambia el DFU state machine a estado IDLE

Si el BootROM resetea la DFU state machine a IDLE tras el bus reset, el overwrite callback que estaba apuntando al nop_gadget no será invocado — porque la state machine fue reiniciada antes de que se procese el overwrite. Los 2016 bytes del payload llegan al DFU IDLE state como nuevo firmware → DFU completa normalmente (MANIFEST_SYNC→MANIFEST→MANIFEST_WAIT_RESET).

---

### El path correcto: `CHECKM8_EP0_RECOVERY=bus-reset` + `CHECKM8_FINALIZE_MODE=skip`

El código actual (v1.0.40) ya tiene todo para probar esto:

```bash
# En Linux con el device en DFU:
export CHECKM8_EP0_RECOVERY=bus-reset   # igual que v1.0.34 que entregó el payload
export CHECKM8_FINALIZE_MODE=skip       # NO enviar suffix/zlen/status tras el payload
export CHECKM8_POST_PAYLOAD_DELAY_MS=500  # 500ms para que el ROP ejecute
sudo ./tr4mpass
```

**Por qué esto puede funcionar:**
- El bus reset es necesario para limpiar EP0 (solo opción conocida que funciona)
- Si el ROP **sí** ejecuta en v1.0.34, el shellcode puede tardar en correr
- El `send_dfu_finalize` actual (suffix + zlen + 3×status) manda 3 transfers más que podrían interferir con el ROP en ejecución
- Con `FINALIZE_MODE=skip` + delay, damos tiempo al ROP antes del bus reset de verificación
- Si PWND aparece → el problema era el finalize interfiriendo con el ROP
- Si sigue sin PWND → el problema es en el ROP chain o el bus reset destruye el overwrite (H-A)

---

### Si H8 falla también: Siguiente paso es KING PATH

El código ya tiene `CHECKM8_KING_PATH` compilable que usa `(0x00, 0x00, 0, 0)` en vez de `(0x02, 0x03, 0, 0x80)` para el overwrite. El KING PATH no produce STALL, por lo que EP0 queda disponible inmediatamente para el payload DNLOAD sin necesidad de bus reset.

```bash
# Compilar con King path:
make clean && make CFLAGS="-DCHECKM8_KING_PATH"
# luego testear
sudo ./tr4mpass
```

El riesgo del King path es que el BootROM puede rechazar el request `(0x00, 0x00, 0, 0)` en lugar de escribirlo al freed io_buffer, o puede escribirlo pero en un offset diferente al esperado.

---

## Próximos experimentos pendientes de hardware

### Experimento 1 (prioridad alta): bus-reset + skip finalize + delay
```bash
export CHECKM8_EP0_RECOVERY=bus-reset
export CHECKM8_FINALIZE_MODE=skip
export CHECKM8_POST_PAYLOAD_DELAY_MS=500
sudo ./tr4mpass
```
**Señales esperadas:**
- Si PWND → el finalize era el problema (el ROP sí ejecutaba en v1.0.34)
- Si actual=2016 + serial limpio → H-A es correcta (bus reset destruye overwrite)

### Experimento 2 (si exp1 falla): KING PATH
```bash
make clean && make EXTRA_CFLAGS="-DCHECKM8_KING_PATH"
sudo ./tr4mpass
```
**Señales esperadas:**
- Si no hay STALL y el payload DNLOAD funciona → excelente (sin bus reset needed)
- Si hay STALL inesperado → el BootROM rechaza el request `(0,0,0,0)` en este estado

---

## Re-análisis Crítico (2026-06-01 ~01:00) — Logs con nuevos experimentos

### Runs ejecutados:

| Device | Run 1 | Run 2 |
|--------|-------|-------|
| White  | `EP0_RECOVERY=none` + `FINALIZE=gaster` (~00:50) | `KING_PATH` (~00:53) |
| Black  | `KING_PATH` (~00:55) | `EP0_RECOVERY=bus-reset` + `FINALIZE=skip` + `DELAY=500ms` (~00:58) |

---

### 🔴 KING PATH — Confirmado inútil (6/6 intentos fallidos)

**En ambos devices, el overwrite via `(0x00, 0x00, 0, 0)` produce STALL en todos los intentos:**
```
send_overwrite [KING PATH]: ret=-9 (Pipe error)
send_overwrite [KING PATH]: unexpected STALL -- EP0 will be halted (King path ineffective)
```
**Conclusión: El KING PATH no es aplicable aquí.** El BootROM en este estado (DFU post-heap-spray con freed io_buffer) responde con STALL a cualquier OUT request en EP0, incluyendo el `(0x00, 0x00, 0, 0)`. El STALL no es específico del `(0x02, 0x03)` — es el comportamiento del BootROM cuando el overwrite toca la estructura de control DFU correctamente. El KING PATH funciona en otras implementaciones donde el BootROM acepta el OUT sin STALL.

Además: tras el STALL del King Path, el device entra en un estado degradado grave:
- `set configuration failed, errno=110` (Connection timed out)
- Todos los string descriptors fallan con timeout por ~30 segundos
- El serial leído post-intento es basura: `[43 40 4F 40 04 59]` = `"C@O@.Y"` (WHITE) y `[43 E0 17 B4 C4 60]` (BLACK) — **¡esto es memoria corrupta del BootROM!**

**Esto es una señal positiva:** la corrupción del serial indica que el overwrite SÍ llega al freed io_buffer. El BootROM está leyendo datos corruptos de la zona UAF. La memoria fue modificada pero el ROP no ejecutó (o ejecutó y crasheó).

---

### 🟡 WHITE `EP0_RECOVERY=none` — HALLAZGO CRÍTICO: `actual=2016, completed=1`

```
send_payload_chunks: offset=0 ret=-7 (Operation timed out)
  submit=0 status=TIMED_OUT actual=2016 completed=1 cancel_requested=1
```

**`actual=2016` + `completed=1` SIN bus reset.** Esto significa que el payload de 2016 bytes se transfirió completamente a nivel libusb/usbcore **incluso sin bus reset**. El `ret=-7` es porque el STATUS phase (la fase de confirmación tras el DATA stage) no volvió dentro del timeout de 1000ms — pero el DATA stage ya completó.

Esto contradice nuestra hipótesis previa de que "sin bus reset el kernel bloquea el SETUP packet porque EP0 está halted". En realidad:
- El DATA stage del DNLOAD sí llega al BootROM
- El BootROM recibe los 2016 bytes
- El STATUS phase (ACK del BootROM que confirma recepción) nunca llega → libusb cancela a los 1000ms

**¿Por qué el STATUS phase no llega?** El BootROM está ejecutando el callback (nop_gadget → ROP chain) y no puede responder al STATUS phase porque el DFU task está ocupado ejecutando el ROP. O bien, el BootROM crashea durante el ROP y ya no puede responder.

**El bus reset en v1.0.34 NO era necesario para entregar los 2016 bytes** — ya los entregábamos sin él. Lo que v1.0.34 logró diferente fue que el STATUS phase completó (ret=2016 en lugar de ret=-7), lo que implica que con bus reset el BootROM respondió al STATUS. Pero con bus reset el ROP no ejecutó porque `dfu_handle_bus_reset()` limpió el estado.

---

### 🟠 Conclusión revisada — El problema es el ROP, no la entrega del payload

**Cronología real de lo que ocurre:**
1. El overwrite llega → STALL ✅
2. El payload de 2016 bytes llega (DATA stage) → `actual=2016, completed=1` ✅  
3. El BootROM recibe el callback (nop_gadget) e intenta ejecutar el ROP
4. **El ROP falla o el BootROM crashea** → no puede responder al STATUS phase
5. Libusb cancela después del timeout → `ret=-7`
6. Nosotros mandamos bus reset de verificación → el device reaparece en DFU limpio

**La USB plumbing está completa y funciona.** El problema es en el ROP chain o en el shellcode.

---

### 🔍 ¿Por qué falla el ROP?

El ROP chain para A10 hace:
1. `write_ttbr0(insecure_memory_base)` → cambia TTBR0
2. `tlbi(0)` → TLB invalidate
3. `exec_addr(0)` → salta al shellcode
4. (shellcode ejecuta, debería parchear serial)
5. `write_ttbr0(ttbr0_addr)` → restaura TTBR0
6. `tlbi(0)` → TLB invalidate
7. `ret_gadget` → return

**Candidatos al fallo:**

**A) La secuencia ROP está mal ensamblada.** `usb_rop_callbacks` con 6 callbacks y `ROP_MAX_BLOCK_SZ=0x50=80` genera bloques de 80 bytes. Con 6 callbacks (ceil(6/5)=2 grupos), el ROP prefix total es:
- `rop_prefix_sz = ttbr0_sram_off + 2*8 = 0x600 + 16 = 0x610 bytes`
- El payload se empieza en `insecure_memory_base + 0x610 = 0x1800B0610`

**B) El `exec_addr` apunta a la dirección virtual `0x1820B0610`** bajo el nuevo TTBR0. Pero el nuevo TTBR0 mapea `insecure_memory_base` (SRAM física `0x1800B0000`). La VA `0x1820B0610` bajo el nuevo TTBR0 debe mapear a la misma dirección física donde está el shellcode. Si el mapping es `0x1820B0000 → físico 0x1800B0000`, entonces `0x1820B0610` → físico `0x1800B0610`. ¿Y el shellcode físicamente está en `0x1800B0610`? El payload buffer que enviamos tiene:
- `[0..0x60F]` = ROP prefix (1552 bytes = `rop_prefix_sz`)
- `[0x610..0x610+120]` = shellcode ARM64

**Sí**, el shellcode está en el offset correcto. El mapping parece correcto.

**C) Las TTBR0 entries en el ROP prefix son incorrectas.** El payload pone en `buf[ttbr0_vrom_off]` y `buf[ttbr0_sram_off]` los page table entries. Pero `ttbr0_vrom_off=0x400` y `ttbr0_sram_off=0x600`. La TTBR0 en A10 usa **16KB granule** por lo tanto el TTBR0 apunta a una tabla de 1024×8=8192 bytes. Los entries en `0x400` y `0x600` dentro del buffer que enviamos se usarán como page table entries para el BootROM. Si estos entries no están correctamente alineados o son incorrectos para la dirección de exec_addr, el CPU haría un translation fault al saltar a `0x1820B0610`.

**D) El nop_gadget en `0x10000CC6C` no es realmente un gadget que pueda usarse como callback.** Si el BootROM invoca `callback(arg1, arg2)` y el nop_gadget crashea (bad instruction, stack alignment, etc.), el BootROM muere antes de ejecutar el ROP.

---

### Próximo experimento: Verificar integridad del ROP con log extendido

El próximo paso debería ser verificar si el ROP al menos inicia. La señal más clara sería:
- Si `write_ttbr0` ejecuta pero `exec_addr` falla → el device se resetea inesperadamente (diferente addr USB)
- Si todo falla antes de `write_ttbr0` → el device sigue en la misma addr USB (lo que observamos)

**La USB addr post-stage4 en WHITE es siempre 54** — la misma durante todo el run. Esto sugiere que el ROP falla muy temprano (antes de que el shellcode haga nada que cambie el estado USB).

### Próximo experimento: EP0_RECOVERY=bus-reset + FINALIZE=skip + DELAY=2000ms

La teoría: si el ROP ejecuta parcialmente pero es lento (como el TTBR0 switch + TLB invalidate pueden tomar tiempo), 500ms puede no ser suficiente. Probar 2000ms:
```bash
export CHECKM8_EP0_RECOVERY=bus-reset
export CHECKM8_FINALIZE_MODE=skip
export CHECKM8_POST_PAYLOAD_DELAY_MS=2000
sudo ./tr4mpass
```

Pero dado que `actual=2016 completed=1` ya funciona SIN bus reset, la siguiente prioridad es depurar el ROP chain en sí mismo — verificar los page table entries y el nop_gadget.

---

## v1.0.41 — Fix del spin loop en el shellcode ARM64 (2026-06-01 ~01:26)

### Root cause identificado: `cbnz w1, #-0x8` loop infinito

El análisis cruzado de los nuevos logs reveló el bug real:

**El shellcode ARM64 (en `shellcode.h` offset 64) contenía:**
```asm
ldr  x0, =gUSBSerialNumber    ; x0 = 0x180083CF8
add  x0, x0, #1               ; x0 = 0x180083CF9
ldrb w1, [x0]                 ; w1 = byte at 0x180083CF9
cbnz w1, #-0x8               ; ← SPIN LOOP: loop while w1 != 0
```

**Por qué nunca salía (en Linux):**
- `gUSBSerialNumber+1 = 0x180083CF9` contiene el segundo byte del serial string actual: `'P'` (de `"CPID:8010..."`) = 0x50 ≠ 0
- El loop gira INFINITAMENTE porque ese byte nunca se vuelve cero
- En macOS/IOKit, una re-inicialización USB del host después de entregar el payload limpia momentáneamente ese byte → el loop sale
- En Linux, nunca ocurre esa limpieza → el loop nunca sale

**Evidencia de los logs que confirmó esta hipótesis:**
- `actual=2016 completed=1` ← payload llega completo ✅
- STATUS phase toutea a los 1000ms ← BootROM ejecutando (el spin loop) ✅
- USB addr siempre la misma (54, 57, etc.) ← BootROM no crashea, está girando ✅
- Serial siempre limpio ← shellcode nunca llega a `stp x2, x3, [x0]` (write PWND) ✅

**Fix aplicado:**
```diff
- 0xC1, 0xFF, 0xFF, 0x35,  /* cbnz w1, #-0x8  ← spin loop infinito */
+ 0x1F, 0x20, 0x03, 0xD5,  /* nop              ← sin loop, proceed  */
```

Con `nop`, el shellcode procede inmediatamente a:
1. `adr x1, PWND_STR` → x1 apunta a `" PWND:[checkm8]"` en el config struct
2. `ldp x2, x3, [x1]` → carga 16 bytes del string PWND
3. `stp x2, x3, [x0]` → escribe en `gUSBSerialNumber+1` (SRAM writable via entry 0xC0)
4. `blr usb_create_string_descriptor(gUSBSerialNumber)` → actualiza el descriptor USB
5. `strb w0, [usb_serial_number_string_descriptor]` → actualiza el índice del descriptor
6. `str w0, [patch_addr]` → parchea 0x1020074AC con `0xD2800000`
7. `ret` → el BootROM continúa normalmente

### Qué esperar de v1.0.41

Con `EP0_RECOVERY=none` (default):
- Overwrite STALL ✅
- Payload 2016 bytes → `actual=2016 completed=1` ✅
- Shellcode escribe PWND en gUSBSerialNumber+1
- El STATUS phase sigue timeouteando (el shellcode demora en ejecutar antes de que el BootROM pueda responder)
- Bus reset de verificación → serial debería mostrar `PWND:[checkm8]`

### Comando de prueba (sin variables de entorno extra necesario):
```bash
# En Linux con device en DFU:
sudo ./tr4mpass
```
El default es `EP0_RECOVERY=none` + `FINALIZE=gaster`. Si el shellcode parchea el serial correctamente, el bus reset de verificación debería ver `PWND:[checkm8]` en el serial string.

**Si el serial aparece corrupto** (no exactamente PWND pero tampoco el serial limpio) → el shellcode ejecutó pero algún paso falló (usb_create_string_descriptor, etc.). En ese caso probar con `CHECKM8_POST_PAYLOAD_DELAY_MS=200` para darle más tiempo al shellcode antes del bus reset.

**Si el serial sigue limpio** → el problema está antes del shellcode: en el ROP chain (func_gadget, write_ttbr0, o tlbi).

---

## v1.0.41 — Resultados (2026-06-01 ~11:08) — Spinloop fix NO fue el root cause

### Estado: MISMO comportamiento que v1.0.40

```
send_payload_chunks: offset=0 ret=-7 (Operation timed out)
  submit=0 status=TIMED_OUT actual=2016 completed=1 cancel_requested=1
checkm8_verify_pwned: serial = "CPID:8010..." (len=98)
PWND marker not found in serial
```

El fix del `cbnz → nop` no cambió nada observable. El serial sigue limpio en los 3 intentos de ambos devices. Esto confirma que **el ROP chain falla antes de llegar al shellcode** — el shellcode nunca ejecuta, independientemente del spinloop.

Nota: los logs aún dicen `v1.0.40: EP0 recovery mode` — hay strings hardcodeados en `checkm8_patch.c` que debemos actualizar (cosmético, no funcional).

### Nueva observación: addr cambia entre stage3 y stage4 en WHITE

```
[white, attempt 1] stage 3: addr 12 → stage 4: addr 13  (device reset entre stages)
[white, attempt 2] stage 3: addr 13 → stage 4: addr 14
[white, attempt 3] stage 3: addr 14 → stage 4: addr 15
```

Esto indica que el **device hace un hardware reset al final de stage 3** en white (líneas 60-67 del log: `reset failed errno=22`, `File doesn't exist`, `addr 13`). El black se mantiene en `addr 8` todo el run.

Este reset espontáneo en stage 3 puede ser problemático: si el heap spray completa el reset y el BootROM reinicializa parcialmente, el freed io_buffer puede ya no estar en la posición esperada cuando llegue el overwrite en stage 4. Sin embargo el overwrite sigue funcionando (STALL correcto) así que el UAF se mantiene.

---

## Análisis Root Cause: ¿Por qué falla el ROP chain?

### El flujo real después del overwrite:

1. Overwrite lanza → `nop_gadget` queda como callback, `insecure_memory_base` como `next`
2. Nosotros enviamos payload DNLOAD de 2016 bytes → `actual=2016, completed=1`
3. El BootROM recibe el payload y completa el DFU_DNLOAD DATA stage
4. El BootROM invoca el callback → `nop_gadget()` ejecuta (retorna inmediatamente)
5. El BootROM sigue el `next` pointer → llega a `insecure_memory_base = 0x1800B0000`
6. El BootROM intenta invocar el callback del struct en `0x1800B0000` → `func_gadget`
7. **¿Qué hace `func_gadget` exactamente?** ← PREGUNTA CRÍTICA SIN RESPONDER

### Hipótesis primaria: `func_gadget` no encuentra los argumentos en los offsets correctos

`usb_rop_callbacks` coloca los datos en el buffer así:
```
buf + offsetof(dfu_callback_t, callback) = buf[0x20]:
  block0 (80 bytes): [(func_gadget|next_addr) × 5 entries]  buf[0x20..0x6F]
  block1 (80 bytes): [(arg|func) × 5 entries]               buf[0x70..0xBF]
```

El BootROM llega a `insecure_memory_base = 0x1800B0000` y trata el struct como `dfu_callback_t`:
- `callback` field es a offset `+0x20` dentro del struct → `buf[0x20]` = `func_gadget = 0x10000CC4C` ✅

El `func_gadget = 0x10000CC4C` es llamado. Para que el func_gadget funcione correctamente, necesita leer el `arg` y el `func` real desde una posición específica relativa al struct base `0x1800B0000`.

**La posición del `arg` (write_ttbr0 arg = insecure_memory_base) y del `func` (write_ttbr0 = 0x1000003E4) es `buf[0x70..0x7F]`.**

¿Cómo sabe func_gadget dónde está este par (arg, func)? La implementación del gadget en gaster asume una estructura de memoria específica. Si el gadget usa un registro que apunta a la base del struct (`0x1800B0000`) y luego hace `ldr x0, [base + 0x70]` y `ldr x1, [base + 0x78]`, funciona. Pero si usa un offset diferente (relativo al stack o a otro registro), el layout puede no matchear.

### Hipótesis secundaria: `offsetof(dfu_callback_t, callback)` está mal calculado

En nuestro código, `usb_rop_callbacks` empieza en `buf + offsetof(dfu_callback_t, callback)`. Si `dfu_callback_t.callback` está a offset 0x20 en nuestro struct pero en el BootROM real está a un offset diferente (ej: 0x18 o 0x28), el `func_gadget` quedaría en la posición incorrecta.

En gaster, `dfu_callback_t` se define como:
```c
typedef struct {
    uint32_t endpoint;  // +0
    uint32_t pad_0;     // +4
    uint64_t io_buffer; // +8
    uint32_t status;    // +16
    uint32_t io_len;    // +20
    uint32_t ret_cnt;   // +24
    uint32_t pad_1;     // +28
    uint64_t callback;  // +32 = 0x20
    uint64_t next;      // +40 = 0x28
} dfu_callback_t;      // total = 48 bytes
```

Offset 0x20 = 32 bytes. Nuestro struct tiene 32+8=40 bytes de header antes de `callback`. Esto coincide con gaster exactamente ✅ — pero vale confirmar que el BootROM A10 usa exactamente esta misma estructura.

### Hipótesis terciaria: el struct en insecure_memory_base no empieza donde el BootROM espera

Cuando el BootROM sigue `next = 0x1800B0000`, ¿trata toda la dirección como la BASE del struct `dfu_callback_t`, o como un puntero DENTRO del struct (ej: apunta al campo `next` del siguiente struct en la lista)?

Si `next` apunta al campo `next` del siguiente struct (no a su base), entonces el struct base sería `0x1800B0000 - offsetof(next) = 0x1800B0000 - 0x28 = 0x1800AFFD8`. En ese caso:
- El campo `callback` del siguiente struct estaría en `0x1800AFFD8 + 0x20 = 0x1800AFFF8`
- `buf[0x1800AFFF8 - 0x1800B0000] = buf[-8]` → ¡FUERA DEL BUFFER! → crash garantizado

Esta hipótesis es crítica pero podemos descartarla si gaster está verificado como funcional en A10 (que sí lo está).

### Hipótesis cuaternaria: el BootROM de A10 no usa el `next` pointer de esta forma

En algunos SoC, la lista de callbacks USB no se itera automáticamente en el firmware — el mecanismo exacto de `next` puede diferir. Si el A10 BootROM no sigue el `next` pointer automáticamente después de invocar el callback, el ROP chain nunca empieza.

---

## Próximos experimentos propuestos

### Experimento 1: Verificar `func_gadget` con payload simplificado (RECOMENDADO)
Reemplazar el `next = insecure_memory_base` en el overwrite por algo que al menos cause un crash observable. Por ejemplo: poner un valor conocido como `0xDEADBEEFDEADBEEF` y ver si el BootROM se cuelga de forma diferente.

### Experimento 2: Agregar dump del payload en el log
Imprimir los primeros 80 bytes del payload ensamblado en hex para verificar que `func_gadget = 0x10000CC4C` queda en `buf[0x20]` como se espera.

### Experimento 3: Probar con `insecure_memory_base` directamente como callback (sin ROP prefix)
Si ponemos `callback = insecure_memory_base` directamente (en lugar de `nop_gadget` + `next`), el BootROM saltaría directamente al inicio del payload buffer. Si eso funciona, el problema está en la cadena nop_gadget → next → func_gadget.

### Experimento 4 (ya documentado en v1.0.40): Verificar `usb_timeout` en stage 2
El black log muestra `usb_timeout=5ms` en stage 2. Debería ser `usb_timeout=50ms`. Este valor sigue en 5ms aunque lo habíamos corregido — verificar si el fix llegó a compilarse.

**Acción inmediata: implementar Experimento 2 (hex dump del payload) para confirmar layout.**

---

## v1.0.42 — Resultados (2026-06-01 ~11:19-11:20) — ROP layout CONFIRMADO correcto

### El hex dump es el dato más importante hasta ahora:

```
ROP hdr dump buf[0x00..0x5F]:
[0x00] 00 00 00 00 00 00 00 00  ← endpoint=0, pad=0
[0x08] 00 00 00 00 00 00 00 00  ← io_buffer=0
[0x10] 00 00 00 00 00 00 00 00  ← status=0, io_len=0
[0x18] 00 00 00 00 00 00 00 00  ← ret_cnt=0, pad_1=0
[0x20] 4C CC 00 00 01 00 00 00  ← callback = 0x10000CC4C ✅ func_gadget
[0x28] 10 00 0B 80 01 00 00 00  ← next     = 0x1800B0010 (slot 0 chain ptr)
[0x30] 4C CC 00 00 01 00 00 00  ← func_gadget slot 1
[0x38] 20 00 0B 80 01 00 00 00  ← next slot 1
[0x40] 4C CC 00 00 01 00 00 00  ← func_gadget slot 2
[0x48] 30 00 0B 80 01 00 00 00  ← next slot 2
...
```

`buf[0x20]=0x10000CC4C` == `func_gadget` ✅ PERFECTO. El layout del ROP es idéntico a gaster.

**Conclusión del hex dump:** El ROP chain está ensamblado correctamente. El problema NO es el layout del buffer. El `func_gadget` recibe la ejecución pero algo falla cuando intenta despachar a `write_ttbr0`.

### Nuevo fallo en black intento 3: `checkm8_stall: exceeded 100 retries`

```
checkm8_stall: exceeded 100 retries, giving up
checkm8_exploit: attempt 3 failed, retrying...
```

El stall en stage 3 falló en el tercer intento del black. Esto es un síntoma de que el heap está en mal estado después de intentos anteriores. La acumulación de USB resets degrada el estado del BootROM en memoria.

### Dato nuevo: el BLACK pierde la USB addr entre stage3 y stage4 TAMBIÉN

```
[black attempt 1] stage3: addr 22 → stage4: addr 23  (reset entre stages)
[black attempt 2] stage3: addr 23 → stage4: addr 24
```

Igual que en el white. Ambos devices hacen un reset espontáneo al final de stage 3. Esto ocurría en v1.0.40 en el white pero ahora también ocurre en el black con el nuevo usb_timeout=50ms. El cambio de 5ms → 50ms puede estar afectando el timing del stage 3 reset.

### Estado del diagnóstico

| Componente | Estado |
|-----------|--------|
| UAF trigger (stage 2) | ✅ siempre funciona |
| Heap spray (stage 3) | ✅ funciona (excepto 3er intento degradado) |
| Overwrite STALL | ✅ siempre STALL correcto |
| Payload entregado | ✅ `actual=2016 completed=1` siempre |
| ROP layout en memoria | ✅ CONFIRMADO correcto via hex dump |
| `func_gadget` en `buf[0x20]` | ✅ `0x10000CC4C` ✅ |
| `func_gadget` despacha a `write_ttbr0` | ❓ DESCONOCIDO — investigando |
| Serial PWND | ❌ |

### Hipótesis actual más probable: func_gadget no puede leer sus args

El `func_gadget` en `0x10000CC4C` es llamado por el BootROM cuando procesa el struct en `insecure_memory_base`. El gadget necesita leer el par `(arg, func)` de `buf[0x70..0x7F]` (que contiene `arg=insecure_memory_base` y `func=write_ttbr0`). 

El gadget lee estos valores de un **offset fijo relativo a algún registro** — probablemente x19 o x20 que el BootROM usa para pasar la dirección del DFU callback struct. Si ese registro apunta a `0x1800B0000`, entonces:
- `arg` = `*(x19 + 0x70)` = `buf[0x70]` = `0x1800B0000` ✅
- `func` = `*(x19 + 0x78)` = `buf[0x78]` = `0x1000003E4` ✅

Pero si el BootROM pasa el struct pointer en un registro diferente, o en un offset distinto, el gadget leería basura.

Pero si el BootROM pasa el struct pointer en un registro diferente, o en un offset distinto, el gadget leería basura.

---

## Research Agent: func_gadget confirmado correcto (2026-06-01 ~11:26)

El research agent encontró la respuesta definitiva:

### func_gadget = `LDP X8, X10, [X0, #0x70]` / `MOV X0, X8` / `BLR X10`

- **Registro:** `x0` contiene el puntero al struct cuando el BootROM llama el callback
- **Offset:** `+0x70` para `arg`, `+0x78` para `func`
- **Math verificado:** para slot 0 con base `0x1800B0000` → `[0x1800B0000+0x70] = buf[0x70]` = `insecure_memory_base` (write_ttbr0 arg) ✅
- **Flujo completo confirmado:** cb0→write_ttbr0, cb1→tlbi, cb2→exec_addr(shellcode), cb3→write_ttbr0(restore), cb4→tlbi, cb5→ret_gadget

**El ROP chain es correcto en todos sus componentes.** Esto descarta las hipótesis 1, 2, 3 y 4.

---

## v1.0.43 — El bug encontrado: verificación destruía la evidencia (2026-06-01 ~11:32)

### Root cause REAL identificado: el bus reset de verificación borraba el PWND

**El exploit PROBABLEMENTE ya estaba funcionando** desde hace varias versiones. El serial PWND estaba siendo escrito por el shellcode, pero nosotros mandábamos un bus reset ANTES de leerlo, destruyendo la evidencia:

**Flujo anterior (buggy):**
```
payload delivery → 250ms wait → BUS RESET → re-enumerate → read serial
```

**Qué pasaba:**
- Shellcode corre → escribe PWND en `gUSBSerialNumber+1` → llama `usb_create_string_descriptor`
- Shellcode nullifica `dfu_handle_bus_reset`
- Nosotros mandamos bus reset → BootROM intenta llamar `dfu_handle_bus_reset` (= NULL)
- OPCIÓN A: crash → hardware reset → DFU clean re-enumerate → serial limpio siempre ❌
- OPCIÓN B: null check skips, USB se reinicializa → recrear serial descriptor desde `gUSBSerialNumber` (que sí tiene PWND) → pero el timing es raro

**gaster** verifica el serial con GET_DESCRIPTOR DIRECTO, SIN bus reset. El PWND serial vive en el descriptor table del BootROM en ese momento.

### Fix en v1.0.43:

```
payload delivery → 250ms wait → GET_DESCRIPTOR directo → if PWND: SUCCESS!
                                                        → if not: BUS RESET → verify
```

El log nuevo va a mostrar:
```
checkm8_exploit: [v1.0.43] trying direct serial read (no bus reset)...
checkm8_verify_pwned: serial = " PWND:[checkm8]..."
checkm8_exploit: SUCCESS via direct read attempt 1 (shellcode confirmed)
```

O si el read directo falla (EP0 confused):
```
checkm8_exploit: direct read failed (EP0 confused) -- trying bus-reset verify
```

**Este es el cambio más importante desde que empezamos a debuggear.**

### Qué buscar en los logs de v1.0.43:

1. **`direct read: PWND not found`** → exploit no funciona todavía (problema real en ROP)
2. **`direct read failed (EP0 confused)`** → EP0 está en mal estado post-STATUS-timeout; probar aumentar el delay antes del read
3. **`SUCCESS via direct read`** → ¡EXPLOTADO! 🎉

### Si sigue fallando con v1.0.43:

Agregar un delay adicional entre el payload timeout y el direct read:
```bash
CHECKM8_POST_PAYLOAD_DELAY_MS=500 sudo ./tr4mpass
```
O probar aumentar `POST_STAGE4_SETTLE_USEC` de 250ms a 500ms.

---

## v1.0.43 — Resultados (2026-06-01 ~11:37-11:41) — Device confirma ejecución post-payload

### Datos clave nuevos:

#### 1. Direct read retorna datos corruptos consistentes (NO el serial DFU normal)

```
BLACK: serial = "C@Y)e"   hex = [43 40 17 59 29 65]  (en los 3 intentos)
WHITE: serial = "C0??V"   hex = [43 30 86 04 BF 56]  (en los 3 intentos)
```

Esto NO es el serial normal `"CPID:8010 CPRV:11..."` ni es PWND. Son 6 bytes que libirecovery
lee del device luego de que todos los GET_DESCRIPTOR timeoutean. El EP0 no responde a ningún
string descriptor después del payload — prueba de que el USB stack del BootROM está en estado
modificado/corrupto post-ROP.

**Esta es la primera evidencia directa de que el ROP EJECUTA código.** El device no está en estado
DFU normal — el USB stack está roto de alguna manera.

#### 2. EP0 completamente no-responsivo después del payload

Todos los string descriptors (índices 1-8) timeoutean 2s cada uno → ~30s de espera total.
Solo la fallback de libirecovery devuelve algo (esos 6 bytes).

Esto es consistente con:
- El shellcode corrió pero write_ttbr0 dejó el USB stack en estado inconsistente, O
- El ROP crasheó a mitad (write_ttbr0 ok → tlbi ok → exec_addr fault), dejando
  EP0 bloqueado sin completar el STATUS del DNLOAD.

#### 3. Bus reset limpia el estado y devuelve serial normal

Después del bus reset, el device re-enumera normalmente con el serial DFU completo.
Esto confirma que el bus reset hace hardware USB reset en el BootROM — NO hay crash/restart del CPU.

---

### Nueva hipótesis principal: exec_addr bajo TTBR0 nuevo causa un permission fault

El ROP chain:
1. `write_ttbr0(0x1800B0000)` → TTBR0 apunta a nuestra page table ✅ (probablemente OK)
2. `tlbi(0)` → TLB invalido ✅ (OK)
3. `exec_addr(0)` → CPU hace BLR a `0x1820B0610`

Para que el BLR a `0x1820B0610` funcione, esa VA debe ser ejecutable bajo la nueva TTBR0.
La entry `0xC1` en la page table (en `buf[0x608]` = PA `0x1800B0608`) es `0x1800006A5`:
- AP=10 → EL1 read, EL0 no access
- PXN=0, UXN=0 → ejecutable en EL1

**Pero ¿`0x1820B0610` cae en la entry 0xC1?**

Con 16KB pages y L2 table (1 level de walk, T0SZ apropiado):
- Cada L2 entry cubre 32MB (`ARM_16K_TT_L2_SZ = 0x2000000`)
- Entry index = VA >> 25 (bits [35:25])
- Para `0x1820B0610`: index = `0x1820B0610 >> 25 = 0xC1` → entry 0xC1 ✅

Esto es correcto. La page table math parece OK según los cálculos preliminares.

**Research agent activo verificando esto en detalle.**

---

### Discrepancia encontrada: ¿qué recibe write_ttbr0?

En gaster_ref.c línea 1026-1029:
```c
callbacks[0] = { write_ttbr0, insecure_memory_base };  // TTBR0 := 0x1800B0000
callbacks[3] = { write_ttbr0, ttbr0_addr };             // TTBR0 := 0x1800A0000 (restore)
```

En nuestro código (checkm8_payload.c línea 272-278):
```c
callbacks[0].arg = chip->insecure_memory_base;   // 0x1800B0000 ✅ igual que gaster
callbacks[3].arg = chip->ttbr0_addr;             // 0x1800A0000 ✅ igual que gaster
```

Aparentemente correcto. El `write_ttbr0` recibe `insecure_memory_base` como TTBR0.

---

### Estado del diagnóstico v1.0.43

| Componente | Estado |
|-----------|--------|
| UAF trigger (stage 2) | ✅ siempre funciona |
| Heap spray (stage 3) | ✅ funciona |
| Overwrite STALL | ✅ siempre STALL correcto |
| Payload entregado | ✅ `actual=2016 completed=1` |
| ROP layout | ✅ confirmado correcto via hex dump |
| ROP ejecutando ALGO | ✅ EP0 queda roto post-payload (evidencia directa) |
| Shellcode ejecuta correctamente | ❌ no hay PWND, EP0 no responde |
| Direct read retorna serial correcto | ❌ retorna 6 bytes de basura |
| Investigando: page table math | ✅ confirmado correcto (research agent) |

---

## v1.0.44 — RESULTADO CRÍTICO: canary 0x41 NO apareció (2026-06-01 ~17:32-17:37)

### Comando ejecutado:
```bash
sudo CHECKM8_DIAG_SHELLCODE=1 ./tr4mpass
```

### Resultado:
```
BLACK: serial = [43 B0 AA 2B 46 5E]  — sin 0x41, en los 3 intentos
WHITE: serial = [43 50 27 6E 1B 59]  — sin 0x41, en los 3 intentos
```

**El canary 0x41414141 NO apareció en ningún intento de ningún device.**

### Implicación directa:

El shellcode de diagnóstico era ultra-simple (6 instrucciones, literal pool al +0x20, sin depender de notA9_config):
```asm
stp  x29, x30, [sp, #-0x10]!
ldr  x0, #+28              ; carga gUSBSerialNumber desde pool
movz w1, #0x4141
movk w1, #0x4141, lsl #16  ; w1 = 0x41414141
str  w1, [x0]              ; escribe en gUSBSerialNumber
ldp  x29, x30, [sp], #0x10
ret
```

Si hubiera llegado al shellcode, la primera instrucción útil `str w1, [x0]` habría escrito 0x41 en los primeros bytes del serial. **No lo hizo.**

### Conclusión definitiva:

**El ROP chain falla ANTES de llegar a exec_addr (callback[2]).**

Los candidatos:
1. `write_ttbr0` (callback[0]) — el gadget falla silenciosamente o no existe
2. `tlbi` (callback[1]) — provoca un fault que aborta la cadena
3. `func_gadget` mismo no está siendo invocado correctamente
4. La estructura `dfu_callback_t` en `insecure_memory_base` no está en el lugar correcto

---

### ¿Por qué el EP0 queda roto si el shellcode no ejecuta?

El device SÍ muestra un estado post-ROP distinto al normal (serial de 6 bytes de basura). Esto podría explicarse por:

- El func_gadget se llama, ejecuta parcialmente (llama write_ttbr0), pero el STR en write_ttbr0 bajo la nueva TTBR0 falla con un data abort → el BootROM maneja el abort y aborta la DFU transfer → EP0 queda en estado de error

- O bien: write_ttbr0 sí corre (TTBR0 cambia) pero `tlbi` causa un fault porque la CPU necesita que la nueva página sea válida en ese momento (timing issue con el TLB invalidation) → abort → mismo resultado

---

### Hipótesis sobre `write_ttbr0`:

`write_ttbr0 = 0x1000003E4` para A10. Esta es una función del BootROM que escribe TTBR0_EL1 y hace ISB. Si existe como función en el BootROM, el BLR funciona. Si la dirección es incorrecta → CPU ejecuta código arbitrario del ROM → probable fault.

**Acción necesaria:** Verificar en gaster_ref.c y otras fuentes si `write_ttbr0 = 0x1000003E4` es correcto para iBoot-2696.0.0.1.33. Buscar disassembly de BootROM A10 para confirmar la dirección.

---

### ¿Qué hace func_gadget realmente?

`func_gadget = 0x10000CC4C` ejecuta:
```asm
LDP X8, X10, [X0, #0x70]   ; carga arg y func
MOV X0, X8
BLR X10                     ; llama func(arg)
```

Si el BootROM llama `func_gadget(struct_base)` con x0=struct_base, y struct_base es `insecure_memory_base = 0x1800B0000`, entonces:
- Lee arg = `*(0x1800B0000 + 0x70)` = `buf[0x70]` = `insecure_memory_base` (para write_ttbr0)
- Lee func = `*(0x1800B0000 + 0x78)` = `buf[0x78]` = `write_ttbr0`

Pero `buf[0x70]` y `buf[0x78]` están en **block1** del ROP chain. El bloque1 para el slot 0 comienza en `buf + offsetof(dfu_callback_t, callback) + 0x50` aproximadamente. Necesito verificar que los offsets del usb_rop_callbacks estén colocando los datos correctamente en `0x1800B0070` y `0x1800B0078`.

**Acción siguiente:** Agregar un dump completo de `buf[0x60..0xBF]` (la zona del block1) en el log para verificar que los punteros de write_ttbr0 y sus args están en las posiciones correctas.

---

### Estado actualizado del diagnóstico

| Componente | Estado |
|-----------|--------|
| UAF trigger (stage 2) | ✅ funciona |
| Heap spray (stage 3) | ✅ funciona |
| Overwrite STALL | ✅ siempre correcto |
| Payload entregado | ✅ `actual=1592 completed=1` |
| ROP layout (buf[0x20..0x5F]) | ✅ confirmado via hex dump |
| func_gadget en buf[0x20] | ✅ confirmado = 0x10000CC4C |
| next pointer en buf[0x28] | ✅ confirmado = 0x1800B0010 |
| Page table math | ✅ confirmado por research agent |
| **Shellcode ejecuta** | **❌ CANARY NO ENCONTRADO** |
| **Falla en:** | **write_ttbr0 o tlbi (callback[0..1])** |
| Próxima acción | Dump de buf[0x60..0xBF] + verificar write_ttbr0 addr |



---

## v1.0.45 — Block1 dump: todo correcto (2026-06-01 ~17:42-17:44)

### Datos del log:
```
assemble_payload: block1 dump buf[0x60..0xBF]:
  4C CC 00 00 01 00 00 00  A0 00 0B 80 01 00 00 00   ← slot4: func_gadget + next=0x1800B00A0
  00 00 0B 80 01 00 00 00  E4 03 00 00 01 00 00 00   ← cb0: arg=0x1800B0000, func=write_ttbr0
  00 00 00 00 00 00 00 00  34 04 00 00 01 00 00 00   ← cb1: arg=0, func=tlbi
  00 00 00 00 00 00 00 00  10 06 0B 82 01 00 00 00   ← cb2: arg=0, func=exec_addr=0x1820B0610
  00 00 0A 80 01 00 00 00  E4 03 00 00 01 00 00 00   ← cb3: arg=ttbr0_addr, func=write_ttbr0
  00 00 00 00 00 00 00 00  34 04 00 00 01 00 00 00   ← cb4: arg=0, func=tlbi

assemble_payload: cb0 arg=0x1800B0000 (want insec_mem=0x1800B0000) func=0x1000003E4 ✅
assemble_payload: cb1 arg=0x0 (want 0) func=0x100000434 ✅
```

### Conclusión del block1 dump:
**Todos los valores son correctos.** El ROP chain está ensamblado perfectamente. 
El fallo no está en el layout de datos.

### ¿Por qué el canary 0x41 no aparece si los datos son correctos?

Las únicas explicaciones restantes:

**Hipótesis A**: `write_ttbr0 = 0x1000003E4` no es una función válida en este BootROM.
El BLR a esa dirección ejecuta código arbitrario → crash antes del exec_addr.

**Hipótesis B**: `tlbi = 0x100000434` causa un exception level fault.
`TLBI VMALLE1` o similar puede no ser válido en EL3/EL1 según el modo del BootROM.

**Hipótesis C**: `func_gadget` hace `LDP [X0, #0x70]` pero X0 en el BootROM no es
`insecure_memory_base = 0x1800B0000`. Podría ser la dirección del struct en la heap real
que fue liberada, que NO es `0x1800B0000`.

---

## v1.0.46 — Experimento CHECKM8_SKIP_MMU (implementado 2026-06-06)

### Propósito:
Eliminar write_ttbr0 y tlbi del ROP chain. Si el canary 0x41 aparece con SKIP_MMU
pero no sin él → el problema está en write_ttbr0 o tlbi (Hipótesis A/B).
Si TAMPOCO aparece con SKIP_MMU → el problema está antes (Hipótesis C: func_gadget dispatch).

### Comandos a ejecutar en ambos devices:
```bash
# Test 1: DIAG + SKIP_MMU (exec_addr directo sin MMU switch)
sudo CHECKM8_DIAG_SHELLCODE=1 CHECKM8_SKIP_MMU=1 ./tr4mpass

# Test 2: Si canary aparece en Test 1, probar sin DIAG pero con SKIP_MMU
sudo CHECKM8_SKIP_MMU=1 ./tr4mpass
```

### Con SKIP_MMU, el ROP chain es:
```
cb0: exec_addr = 0x1800B0610 (raw insecure_memory_base + rop_prefix_sz, sin TTBR0 remap)
cb1..5: ret_gadget (no-op)
```

### Shellcode funciona sin MMU remap:
El shellcode de diagnóstico escribe a gUSBSerialNumber via `str w1, [x0]` donde
x0 = `0x180083CF8` (cargado desde el literal pool). Esa dirección es SRAM normal,
accesible en la VA del BootROM sin necesidad de remapear con nuestra page table.

### Resultado esperado:
| Resultado | Diagnóstico |
|-----------|-------------|
| Canary 0x41 aparece con SKIP_MMU | write_ttbr0/tlbi addr incorrecta |
| NO aparece con SKIP_MMU | func_gadget no llama cb0 correctamente |
| Canary aparece sin DIAG (PWND?) | write_ttbr0/tlbi problema, pero shellcode funciona sin MMU |

### Estado actual:
- Pendiente de prueba en dispositivo



