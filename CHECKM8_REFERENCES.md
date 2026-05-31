# checkm8 / A10 — Referencias externas (GitHub)

Documento de investigación para CPID **0x8010** (Apple A10 / T8010, iBoot-2696) y chips vecinos (**0x8011** A10X, **0x8012** T2, **0x8015** A11). Complementa el experimento local en [`CHECKM8_A10_DEBUG_LOG.md`](CHECKM8_A10_DEBUG_LOG.md).

**Criterio de relevancia para tr4mpass:** ¿aporta offsets, secuencia USB, timing, o evidencia de `PWND:[checkm8]` en hardware similar al nuestro?

---

## Tabla rápida CPID ↔ SoC

| CPID   | SoC   | SRTG típico (gaster)     | Notas |
|--------|-------|--------------------------|-------|
| 0x8010 | A10   | iBoot-2696.0.0.1.33      | iPhone 7, iPad 6/7, iPod touch 7 — **nuestro target** |
| 0x8011 | A10X  | iBoot-3135.0.0.2.3       | iPad Pro 10.5", Apple TV 4K — fixes en forks suelen apuntar aquí |
| 0x8012 | T2    | iBoot-3401.0.0.1.16      | Mac T2; cuelgues post-PATCH en gaster |
| 0x8015 | A11   | iBoot-3332.0.0.1.23      | iPhone 8 / X |

---

## Implementaciones de referencia (código)

### [0x7ff/gaster](https://github.com/0x7ff/gaster) — base actual del proyecto

**Qué es:** Implementación en C del flujo checkm8 post-ipwndfu, con payloads `payload_notA9.S`, ROP + TTBR0 para A10-class, y detección de offsets por string `SRTG:[iBoot-…]` en el serial USB.

**Relevante para 0x8010:**
- Offsets iBoot-2696 en `gaster.c` (coinciden con `chip_db_table_rop.h`).
- Stage 4: `stall+leak` → overwrite `(bmRequestType=2, bReq=3, wValue=0, wIndex=0x80)` con blob 48 B → loop `DFU_DNLOAD` de `data_sz` → suffix 16 B + DNLOAD longitud 0 + 3× `dfu_get_status`.
- `usb_timeout` por defecto **5 ms** (`USB_TIMEOUT` env).
- `calloc(DFU_MAX_TRANSFER_SZ + …)` pero el envío recorre `data_sz` (p. ej. 2016 B), no siempre 2048.

**Issues útiles:** [#31](https://github.com/0x7ff/gaster/issues/31) (A10X), [#53](https://github.com/0x7ff/gaster/issues/53) (T2 post-PATCH), [#15](https://github.com/0x7ff/gaster/issues/15) (USB-C / AMPDevices en macOS).

---

### [verygenericname/gaster](https://github.com/verygenericname/gaster) — fork “Fixes A10x”

**Qué es:** Fork usado por SSHRD_Script; README: *“Fixes A10x”*.

**Diferencias reales vs upstream (verificado en `gaster.c`):**
- Tras el payload, **omite** suffix + zero-length DNLOAD solo para **`cpid == 0x8011 || 0x8012`** (comentario: *fixes A10X and T2*).
- Para **`0x8010` sigue el path completo** de finalize (igual que upstream).
- Incluye `notA9.dfu_handle_bus_reset` en el struct (upstream actual también).

**Conclusión para tr4mpass:** útil como referencia de **A10X/T2**, no como parche mágico para **A10 (8010)**. Ya lo documentamos en el debug log v1.0.29.

---

### [pgarba/King](https://github.com/pgarba/King) — checkm8 en C, T8010 en Linux

**Qué es:** Port de checkm8/ipwndfu a C/C++. README con **éxito documentado** en iPhone 7 (`CPID:8010`, iBoot-2696) en Linux (Ubuntu) y Windows.

**Por qué importa:** Demuestra que **PWND en A10 es alcanzable** fuera de macOS, con un pipeline distinto al de gaster moderno:
- Flujo resumido: *stage 1 heap grooming* → *stage 2 usb setup* → *stage 3 exploit* (3 etapas, estilo ipwndfu, no RESET/SPRAY/PATCH de gaster).
- Gadget/documentación: `t8010_nop_gadget = 0x10000CC6C`, `callback_chain = 0x1800B0800`, overwrite pad `0x5C0` — alineado con nuestros valores.

**Acción sugerida:** Comparar el **stage 3 USB** de King (fuente en repo) con `checkm8_patch.c` / `checkm8_stages.c` — posible divergencia en orden de requests o en payload (sin ROP gaster).

**Nota Windows:** requiere libusbK (Zadig) y parche de `MAX_PATH` en libusb — paralelo a problemas Linux/xHCI que vemos nosotros.

---

### [axi0mX/ipwndfu](https://github.com/axi0mX/ipwndfu) — exploit original

**Qué es:** checkm8 en Python; referencia histórica.

**Relevante:**
- [`device_platform.py`](https://github.com/axi0mX/ipwndfu/blob/master/device_platform.py): `DevicePlatform(cpid=0x8010, srtg='iBoot-2696.0.0.1.33', …)`.
- Issue [#158](https://github.com/axi0mX/ipwndfu/issues/158): fallos en Linux en stage 2 (`USBError 19`); otros usuarios reportan **éxito** en 8010 con `PWND:[checkm8]` en el mismo issue.

**Conclusión:** el chip está soportado; los fallos Linux son conocidos desde 2019 y dependen del stack USB, no de “8010 no explotable”.

---

### [joshuah345/gaster](https://github.com/joshuah345/gaster) — fork con releases

**Qué es:** Fork activo con releases precompilados (2023); mismo ámbito A7–A11 que upstream.

**Uso:** binarios de comparación rápida en el mismo host Linux que tr4mpass (`gaster pwn`, `usb_timeout: 5`).

---

### [palera1n](https://github.com/palera1n/palera1n) / [Achilles](https://github.com/alfiecg24/Achilles)

**Qué es:** Herramientas de usuario final; **no exponen** el exploit checkm8 en C legible (checkra1n embebido / gaster como dependencia).

**Relevante:** confirman que el ecosistema asume A10/A10X pwnables vía checkra1n/gaster; no sustituyen ingeniería de stage 4.

---

## Issues y parches accionables (gaster)

| Issue | CPID / tema | Hallazgo | ¿Aplica a nuestro 8010? |
|-------|-------------|----------|-------------------------|
| [#31](https://github.com/0x7ff/gaster/issues/31) | **0x8011** (A10X) | Cuelgue/loop post-PATCH; fix: reemplazar `3 * EP0_MAX_TRANSFER_SZ + 1` por `DFU_MAX_TRANSFER_SZ` en spray/final CLR | **Ya usamos** `DFU_MAX_TRANSFER_SZ` en `checkm8_spray.c` (hole final). Verificar línea exacta si algún path aún usa `3*EP0+1`. |
| [#53](https://github.com/0x7ff/gaster/issues/53) | **0x8012** (T2) | Post-PATCH hang; bisect: commit `7ffffff9…` rompe; comentario 2026: problemas en `gaster.c` ~1218 (suffix/zero DNLOAD) | Parcial: mismo bloque de finalize que probamos en v1.0.28 |
| [#15](https://github.com/0x7ff/gaster/issues/15) | General | Post-PATCH stuck; **USB-A hub** vs adaptador C; en macOS `killall -STOP` de AMP* | Linux: validar cable/hub; no es fix de código |
| [#22](https://github.com/0x7ff/gaster/issues/22) | 0x8960 Linux | Reboot tras PATCH; resuelto en commits posteriores | Patrón “device reset after stage 4” similar a nuestro bus reset |

---

## Artículos / gists (análisis, no código mantenido)

### [andersonmbwale/iCloud-Bypass-Namibia](https://github.com/andersonmbwale/iCloud-Bypass-Namibia) — walkthrough iPhone 7

**Contenido:** Explicación del UAF, `INSECURE_MEMORY`, simplificación del exploit para **CPID:8010**.

**Datos útiles (verificar vs gaster actual):**
- `t8010_nop_gadget = 0x10000CC6C`
- `callback_chain = 0x1800B0800` (nosotros: `insecure_memory_base` + ROP, jump `0x1820B0610`)
- Overwrite: `0x5C0` bytes cero + `pack('<32x2Q', nop, callback_chain)`
- Secuencia: stall → leak ×2 → control transfer con overwrite → payload en chunks `0x800` → **`usb_reset`**

**Caveat:** el artículo usa requests distintos a gaster 2023 (`bmRequestType=0` en un paso); tratarlo como intuición, no como fuente única de verdad.

---

### [a1exdandy — ipwndfu patch gist](https://gist.github.com/a1exdandy/ae3fb332efac879e97a41291f7fef727)

**Qué es:** Parches antiguos ipwndfu (A8/A9, `t8010_t8011` en nombres de binarios).

**Relevancia:** tabla `ExecConfig` con `iBoot-2696` / `aes_crypto_cmd=0x10000C8F4` para t8010 — coherente con gaster.

---

### [Opuqide gist — iBoot / SecureROM](https://gist.github.com/Opuqide/a47c5e03c6901edcf7a7d113ca1db885)

**Qué es:** Contexto educativo checkm8 / iBoot; sin offsets USB.

**Uso:** onboarding del equipo; no para debug de stage 4.

---

## SEP / jailbreak post-pwn (no arreglan DFU exploit)

| Repo | Relación con A10 |
|------|------------------|
| [guacaplushy/checkp4le](https://github.com/guacaplushy/checkp4le) (deprecated → palera1n-c) | **Después** de pwn: re-habilitar SEP en A10 con kpf custom; asume checkm8 ya funcionó |
| [palera1n COMMONISSUES](https://github.com/palera1n/palera1n/blob/main/docs/COMMONISSUES.md) | SEP panic si hay passcode en A10/A11; redirige A10 a checkp4le |

No explican por qué nuestro payload DNLOAD hace timeout sin `PWND`.

---

## Búsquedas GitHub recomendadas (manual)

Sin `gh` en el entorno; repetir en GitHub → Code search:

```
"cpid == 0x8010" checkm8
"CPID:8010" PWND
"SRTG:[iBoot-2696" gaster
"0x8011" "DFU_MAX_TRANSFER_SZ" gaster
t8010_nop_gadget 0x10000CC6C
insecure_memory_base 0x1800B0000
```

---

## Síntesis para el siguiente paso en tr4mpass

1. **verygenericname/gaster** no sustituye trabajo en **8010**; solo clarifica **8011/8012** (skip partial finalize).
2. **gaster upstream** sigue siendo la referencia de offsets y stage 4 para 8010; issues #31/#53 son sobre otros CPIDs o USB host.
3. **pgarba/King** es la referencia más fuerte de “**8010 + Linux + PWND**” con código C abierto — merece un diff de stage 3 USB vs nuestra implementación gaster-notA9.
4. Problemas post-PATCH en la comunidad apuntan a **USB (timeout 5 ms, cable, reset)** y **finalize**, no a offsets iBoot-2696 incorrectos en forks recientes.
5. Nuestro bloqueo actual encaja con issues Linux de ipwndfu/gaster; offsets iBoot-2696 en forks recientes parecen correctos.
6. **King** es el siguiente experimento de **código** (no MD): pipeline distinto con PWND documentado en Linux para el mismo 8010.

---

## Changelog de este documento

| Fecha       | Notas |
|-------------|-------|
| 2026-05-30  | Investigación inicial: gaster, verygenericname, King, ipwndfu #158, gaster #31/#53/#15, artículo iPhone 7 |
| 2026-05-30  | Síntesis con OPUS + estado rama `comp-changes`; King stage-3 diff vs gaster |
