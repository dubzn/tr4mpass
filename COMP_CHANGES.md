# Branch `comp-changes` — checkm8 A10 (CPID 0x8010)

Rama de trabajo para iterar el exploit checkm8 en **iPhone 7-class / iBoot-2696** sin mezclar con `main` hasta validar en hardware.

**Versión actual del exploit:** `1.0.40` (`CHECKM8_EXPLOIT_VERSION` en logs).

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
| **1.0.33** | En **Linux + CPID 0x8010**, payload default **1000 ms** si el env no está puesto | Probado: payload sigue timeouteando en offset 0 |
| **1.0.34** | Bus reset entre overwrite STALL y payload | Payload/finalize llegan, pero sin `PWND`; probable destrucción de heap/overwrite |
| **1.0.35** | `libusb_clear_halt(EP0)` | Falla con `LIBUSB_ERROR_NOT_FOUND` |
| **1.0.36** | Reopen del handle sin bus reset | No limpia el estado; payload timeout |
| **1.0.37** | "King path" parcial: overwrite 48 B vía `(0,0,0,0)` | También STALL; no entrega payload |
| **1.0.38** | `USBDEVFS_RESETEP` directo sobre EP0 | Probado: EP0 OUT/IN devuelven `ENOENT`; fallback reopen; payload timeout |
| **1.0.39** | Payload DNLOAD diagnóstico (`status`/`actual_length`) + `CHECKM8_EP0_RECOVERY` explícito | Probado: en 5/6 intentos el DATA stage reportó `actual=2016`; no hubo `PWND` |
| **1.0.40** | `CHECKM8_FINALIZE_MODE=skip` y `CHECKM8_POST_PAYLOAD_DELAY_MS`; log `cancel_requested` | Próxima prueba: aislar finalize/timing después de payload completo |

**Estado conocido actual:** stages 1–3 OK; stage 4 concentra el problema. Sin recovery EP0, el payload `DFU_DNLOAD` sigue timeouteando a nivel libusb, pero `v1.0.39` mostró `actual=2016` en 5/6 intentos, o sea que el DATA stage generalmente sí se transfiere. `USBDEVFS_RESETEP(EP0)` falla con `ENOENT`. Con bus reset pre-payload, payload/finalize llegan pero el serial vuelve limpio. El "King path" parcial también STALLa y deja el dispositivo wedged.

**Últimas corridas en logs:** `src/log_white.txt` y `src/log_black.txt` muestran `v1.0.39`: gaster baseline (`CHECKM8_EP0_RECOVERY=none`), payload timeout con `actual=2016` en `white` 3/3 y `black` 2/3, primer intento `black` parcial `actual=128`. Finalize falla y el serial vuelve limpio. Sin `PWND`.

---

## Síntesis OPUS vs Composer vs rama `comp-changes`

### OPUS (`OPUS_ANALYSIS.txt`) — análisis sin implementación

Opus identificó 5 puntos leyendo gaster; el TXT termina en *"Now let me fix all three bugs"* **sin commit de código**.

| # | Hallazgo Opus | ¿En código hoy? | Evidencia en logs |
|---|---------------|-----------------|-------------------|
| 1 | Overwrite `(2,3,0,0x80)` 48 B, no `(0,0,0,0)` 64 B | **Sí** — desde v1.0.29 | `send_overwrite: 48 bytes (bmReqType=0x02…)` |
| 2 | `checkm8_overwrite_t` solo `dfu_callback_t`, sin `heap_pad` | **Sí** — struct 48 B | `sizeof(ow)` en log = 48 |
| 3 | Sin pre-payload DNLOAD antes del payload | **Sí** — desde v1.0.29 | no aparece en logs |
| 4 | Layout `data_sz=2016` vs calloc 2048+ | **Ya alineado** — Opus exageró el “bloque separado” | `assemble_payload: data=2016 transfer=2016` |
| 5 | EP0 colgado tras overwrite STALL → PIPE/timeout en lo siguiente | **Observado** — no resuelto | payload + finalize timeout; serial limpio |

**Conclusión Opus:** v1.0.28 iba en dirección **incorrecta**; la corrección gaster (1–3) **ya está en la rama** vía revisión Composer + v1.0.29. Opus no aportó código nuevo; confirmó lo que Composer ya había cruzado en el debug log (sección *Opus analysis review*).

**Pendiente de Opus (sigue abierto):** el EP0 “wedged” tras STALL — gaster igual manda payload con timeout corto e ignora errores; nosotros vemos timeout sin PWND → el problema probablemente **no** es solo el tipo de request del overwrite.

### Composer — research + implementación

| Entregable | Tipo | Estado |
|------------|------|--------|
| [`CHECKM8_REFERENCES.md`](CHECKM8_REFERENCES.md) | Research GitHub (gaster, King, ipwndfu #158, #31/#53) | Documentado |
| Cruzado Opus vs gaster main | Review en debug log | Hecho |
| v1.0.30 — `usb_timeout=5 ms` Linux | Código | Hecho |
| v1.0.31 — finalize sin bloqueo 15 s | Código | **Validado** en logs 1.0.32 (~1 s/intento) |
| v1.0.32 — env `CHECKM8_PAYLOAD_TIMEOUT_MS` | Código | Hecho; corrida sin override |
| v1.0.33 — Linux 8010 payload **1000 ms** auto | Código | Probado; payload timeout en offset 0 |
| Path **King** (3 stages, overwrite grande `(0,0,0,0)`) | Research only | **No implementado** — mayor divergencia vs gaster |

### Research Composer: King vs tr4mpass (8010)

Referencia con PWND documentado en Linux ([pgarba/King](https://github.com/pgarba/King)):

| Paso | King (ipwndfu-style) | tr4mpass (gaster 4-stage) |
|------|----------------------|---------------------------|
| Pipeline | 3 stages + `usb_reset` | RESET → SETUP → SPRAY → PATCH + bus reset |
| Overwrite USB | `(0, 0, 0, 0)` blob **~1.5 KB** (`t8010_overwrite`) | `(2, 3, 0, 0x80)` **48 B** `dfu_callback_t` |
| Jump target | `0x1800B0800` (callback chain en overwrite) | `nop_gadget` → `insecure_memory_base` |
| Payload | Shellcode King ~0x610+ en chunks **0x800**, timeout **100 ms** | ROP+notA9 **2016 B** en 1 chunk, timeout 5 ms (→ 1000 ms en v1.0.33) |
| Post-payload | `sleep 500 ms` + **`usb_reset`** — **sin** suffix/GETSTATUS DFU | suffix + zlen + GETSTATUS (gaster) → skip si EP0 colgado |
| Stage 2 UAF | async 0x800 'A' + CLR (0x21,4) | async 0x800 zeros + abort timing Linux |

**Hipótesis:** en Linux/xHCI, el path gaster-notA9 puede estar “casi bien” en stages 1–3 pero fallar en la transición overwrite→payload→pwn; King prueba otra forma de colocar el callback y **no usa finalize DFU**.

### Progreso real hasta ahora

| Área | Progreso |
|------|----------|
| Alineación gaster (Opus 1–3) | **Completo** |
| Velocidad de iteración (finalize) | **Completo** (v1.0.31) |
| PWND | **Ninguno** |
| Experimento payload 1000 ms | **Probado**; timeout offset 0 |
| Bus reset pre-payload | **Probado**; entrega payload, sin `PWND` |
| Reopen handle | **Probado**; no limpia el problema |
| King parcial | **Probado**; también STALL |
| `USBDEVFS_RESETEP` EP0 | **Probado**; `ENOENT`, no limpia el problema |
| Payload `actual_length` | **Probado con v1.0.39**: 5/6 intentos `actual=2016`; 1/6 parcial `actual=128` |
| King completo | **Pendiente** |

---

## Variables de entorno

| Variable | Default | Uso |
|----------|---------|-----|
| `USB_TIMEOUT` | `5` | Timeout global gaster (UAF abort, overwrite, finalize DNLOAD, payload si no hay override) |
| `USB_ABORT_TIMEOUT_MIN` | `0` | Piso del barrido de abort en stage 2 |
| `CHECKM8_PAYLOAD_TIMEOUT_MS` | Linux 8010: **1000**; resto: igual que `USB_TIMEOUT` | Solo stage 4 payload. `=0` → forzar gaster (5 ms con default usb) |
| `CHECKM8_EP0_RECOVERY` | `none` | Stage 4 post-overwrite strategy: `none`, `resetep`, `reopen`, `resetep-reopen`, `bus-reset` |
| `CHECKM8_FINALIZE_MODE` | `gaster` | `gaster` mantiene suffix/zlen/status; `skip` omite finalize tras payload |
| `CHECKM8_POST_PAYLOAD_DELAY_MS` | `0` | Delay después del payload y antes de finalize/return a reset; usar para aislar timing |

Ejemplos:

```bash
# Baseline gaster estricto en Linux A10 (rápido)
export CHECKM8_PAYLOAD_TIMEOUT_MS=0
sudo ./tr4mpass …   # usb_timeout=5, payload_timeout=5

# Default actual de la rama en Linux A10: Stage 2 gaster, payload largo
unset CHECKM8_PAYLOAD_TIMEOUT_MS
sudo ./tr4mpass …

# UAF más lento en Linux (solo si stage 2 falla con 5 ms)
export USB_TIMEOUT=50
sudo ./tr4mpass …

# v1.0.39: medir actual_length del payload sin repetir recovery fallido
unset CHECKM8_EP0_RECOVERY
sudo ./tr4mpass …

# v1.0.40: aislar finalize después de comprobar DATA stage completo
unset CHECKM8_EP0_RECOVERY
unset CHECKM8_PAYLOAD_TIMEOUT_MS
export CHECKM8_FINALIZE_MODE=skip
unset CHECKM8_POST_PAYLOAD_DELAY_MS
sudo ./tr4mpass …

# Si sigue serial limpio pero actual=2016, dar tiempo antes del reset
unset CHECKM8_PAYLOAD_TIMEOUT_MS
export CHECKM8_FINALIZE_MODE=skip
export CHECKM8_POST_PAYLOAD_DELAY_MS=500
sudo ./tr4mpass …

# Reproducir explícitamente experimentos previos si hace falta comparar
CHECKM8_EP0_RECOVERY=reopen sudo ./tr4mpass …
CHECKM8_EP0_RECOVERY=resetep-reopen sudo ./tr4mpass …
CHECKM8_EP0_RECOVERY=bus-reset sudo ./tr4mpass …
```

---

## Cómo probar y registrar

1. Compilar en la máquina Linux con el iPhone en DFU.
2. Correr exploit; copiar salida a `src/log_white.txt` / `src/log_black.txt`.
3. Buscar en log:
   - `checkm8_exploit: version 1.0.40` (o la versión actual)
   - `payload_timeout=1000 ms` en Linux 8010
   - `send_payload_chunks: ... actual=2016` vs parcial/0
   - `v1.0.40: finalize mode 'skip'` en la próxima prueba
   - `checkm8_verify_pwned` → `PWND` o serial limpio/corrupto
4. Anotar resultado en [`CHECKM8_A10_DEBUG_LOG.md`](CHECKM8_A10_DEBUG_LOG.md).

---

## Próximos experimentos (orden sugerido)

1. **v1.0.40 finalize skip:** `CHECKM8_FINALIZE_MODE=skip`, sin delay extra. Mantener `CHECKM8_EP0_RECOVERY=none`.
2. **v1.0.40 delay:** si sigue `actual=2016` + serial limpio, repetir con `CHECKM8_POST_PAYLOAD_DELAY_MS=500`.
3. **Control gaster:** binario [gaster](https://github.com/0x7ff/gaster) en el **mismo** host USB → ¿PWND? Si gaster sí y tr4mpass no → bug nuestro; si ambos no → stack USB/host.
4. **Control King:** binario [King](https://github.com/pgarba/King) en el mismo host/cable/puerto. King es el control fuerte para `8010 + Linux + PWND`.
5. **usbmon:** si `actual=2016` continúa sin ejecución, capturar desde overwrite→payload→reset para ver STATUS/finalize/reset timing.
6. **Port King completo:** solo si King upstream funciona o produce mejor señal que gaster/tr4mpass.

---

## Archivos tocados por el exploit

- `include/exploit/checkm8_internal.h` — versión, helpers CPID
- `include/exploit/dfu_proto.h` — `dfu_get_status_timeout`
- `src/exploit/checkm8_usb_tuning.c` — timeouts env
- `src/exploit/checkm8.c` — log de inicio
- `src/exploit/checkm8_patch.c` — payload, finalize
- `src/exploit/dfu_proto.c` — GETSTATUS con timeout configurable
