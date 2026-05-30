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

## Current Hypothesis

The next strongest mismatch with `gaster` is the A10 overwrite structure. Our code has been setting:

```c
io_buffer = insecure_memory_base;
io_len = DFU_MAX_TRANSFER_SZ;
callback = nop_gadget;
next = insecure_memory_base;
```

`gaster` leaves `io_buffer` and `io_len` zero for the A10/TLBI path and only sets:

```c
callback = nop_gadget;
next = insecure_memory_base;
```

## Next Experiment

### v1.0.12

- Make the A10/TLBI overwrite match `gaster`:
  - leave `io_buffer = 0`
  - leave `io_len = 0`
  - set `callback = nop_gadget`
  - set `next = insecure_memory_base`
- Keep all other recent changes unchanged:
  - TTBR0 values from `gaster`
  - `patch=0x1020074AC`
  - padded 2048-byte payload transfer
  - post-stage-4 reset
  - timestamped logs

Expected log signal:

```text
checkm8_exploit: version 1.0.12
assemble_payload: ... patch=0x1020074AC ...
build_overwrite_64: io_buffer=0x0 io_len=0x0 callback=... next=...
```

