# Branch `comp-changes` — checkm8 A10 (CPID 0x8010)

Rama de trabajo para iterar el exploit checkm8 en **iPhone 7-class / iBoot-2696** sin mezclar con `main` hasta validar en hardware.

**Versión actual del exploit:** `1.0.32` (`CHECKM8_EXPLOIT_VERSION` en logs).

**Criterio de éxito:** serial USB con `PWND:[checkm8]` en DFU.

**Log de experimentos (detalle histórico):** [`CHECKM8_A10_DEBUG_LOG.md`](CHECKM8_A10_DEBUG_LOG.md)

**Referencias externas:** [`CHECKM8_REFERENCES.md`](CHECKM8_REFERENCES.md)

---

## Cambios incluidos en esta rama (vs `main`)

| Versión | Qué cambió | Por qué |
|---------|------------|---------|
| **1.0.30** | Linux `usb_timeout` default **5 ms** (gaster); finalize suffix/zlen con `checkm8_usb_timeout_ms()` | Alinear timing de abort UAF con gaster |
| **1.0.31** | `dfu_get_status_timeout()`; omitir 3× GETSTATUS si fallan suffix o DNLOAD 0-len | v1.0.30 perdía ~15 s/intento en polls con `DFU_TIMEOUT=5000` |
| **1.0.32** | `CHECKM8_PAYLOAD_TIMEOUT_MS` para stage 4 payload solo | Probar timeout largo (p. ej. 1000 ms) sin tocar stage 2 |

**Estado conocido (logs v1.0.30, white + black):** stages 1–3 OK; overwrite STALL OK; payload 2016 B → timeout; serial limpio sin PWND.

---

## Variables de entorno

| Variable | Default | Uso |
|----------|---------|-----|
| `USB_TIMEOUT` | `5` | Timeout global gaster (UAF abort, overwrite, finalize DNLOAD, payload si no hay override) |
| `USB_ABORT_TIMEOUT_MIN` | `0` | Piso del barrido de abort en stage 2 |
| `CHECKM8_PAYLOAD_TIMEOUT_MS` | *(igual que `USB_TIMEOUT`)* | Solo el `DFU_DNLOAD` del payload en stage 4 |

Ejemplos:

```bash
# Baseline gaster (rápido)
sudo ./tr4mpass …   # usb_timeout=5, payload_timeout=5

# Stage 2 gaster, payload largo (experimento v1.0.24)
export CHECKM8_PAYLOAD_TIMEOUT_MS=1000
sudo ./tr4mpass …

# UAF más lento en Linux (solo si stage 2 falla con 5 ms)
export USB_TIMEOUT=50
sudo ./tr4mpass …
```

---

## Cómo probar y registrar

1. Compilar en la máquina Linux con el iPhone en DFU.
2. Correr exploit; copiar salida a `src/log_white.txt` / `src/log_black.txt`.
3. Buscar en log:
   - `checkm8_exploit: version 1.0.32`
   - `usb_timeout=` y `payload_timeout=`
   - `send_dfu_finalize: skipping status polls` (esperado si EP0 colgado)
   - `checkm8_verify_pwned` → `PWND` o serial limpio
4. Anotar resultado en [`CHECKM8_A10_DEBUG_LOG.md`](CHECKM8_A10_DEBUG_LOG.md) (versión, env, dispositivo, 1–3 líneas de resultado).

---

## Próximos experimentos (orden sugerido)

1. **v1.0.32 + `CHECKM8_PAYLOAD_TIMEOUT_MS=1000`** — retest señal de serial corrupto (v1.0.24).
2. **`USB_TIMEOUT=50`** sin cambiar payload — comparar `UAF triggered (sent=…)`.
3. **Path King/ipwndfu** para `0x8010` — overwrite `0x5C0` + `bmRequestType=0,bReq=9` (código futuro en esta rama).
4. **Spray gaster #31** — `checkm8_no_leak` con `DFU_MAX_TRANSFER_SZ` solo en 8010 (flag/env).

---

## Archivos tocados por el exploit

- `include/exploit/checkm8_internal.h` — versión, helpers CPID
- `include/exploit/dfu_proto.h` — `dfu_get_status_timeout`
- `src/exploit/checkm8_usb_tuning.c` — timeouts env
- `src/exploit/checkm8.c` — log de inicio
- `src/exploit/checkm8_patch.c` — payload, finalize
- `src/exploit/dfu_proto.c` — GETSTATUS con timeout configurable
