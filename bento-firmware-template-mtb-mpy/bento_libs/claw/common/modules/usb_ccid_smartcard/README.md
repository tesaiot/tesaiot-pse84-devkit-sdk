# USB CCID Smart Card Reader — Thai National ID Card

Reads a Thai national ID smart card (text fields + JPEG photo) through a USB
CCID reader on the **CM55** core via SEGGER emUSB-Host, and exposes the result
to the UI through `smartcard_state_t`.

- `usb_ccid_smartcard.c` — driver: enumeration, ATR/power-on, APDU transport,
  Thai-ID field reads, photo read, and the polling task.
- `usb_ccid_smartcard.h` — `smartcard_state_t`, `thai_id_card_t`,
  `thai_id_photo_t`, and the public API.
- `usb_ccid_smartcard.mk` — sources/includes for the build.

This module is **shared**: it lives in `BENTO-TESAIoT-Claw-libraries` and is
linked by every firmware project that has a Smart Card page (AI Kit, Eva Kit,
…). A change here means **rebuild every project that uses it** (workspace
rule §4).

---

## 1. Architecture & execution context

| Concern | Where it runs |
|---|---|
| USB Host stack (emUSB-Host) + CCID class | **CM55** |
| Card enumeration / APDU / photo read | **CM55**, in the `SC_Poll` task |
| Smart Card UI page + JPEG decode + display | **CM55**, in the LVGL GFX task |

There is **no cross-core IPC** for the card data — the poll task fills
`smartcard_state_t` and the page reads it directly (volatile fields).

> **CM55 has no serial console.** All debugging is done **on-screen** by writing
> to `s_sc_state.error_msg`, which the Smart Card page renders. `printf` from
> CM55 (especially from USB notification/ISR context) can deadlock — never add it
> here. See [§7 Diagnostics](#7-diagnostics-no-serial-on-cm55).

### Init context rule (do not move)

`usb_ccid_smartcard_request_init()` / the USB Host init **must** be kicked off
from `main.c` boot (`init_joystick_usb_host()`), **not** from
`page_smart_card_create()`. Calling USB Host init from the LVGL GFX task context
deadlocks the USB Host stack and hangs the page on entry.
(The Eva Kit page may still call it in `create` because an `s_init_requested`
flag makes the second call a no-op.)

---

## 2. Thai ID card read flow (per insert)

In `_smartcard_poll_task`, on first card detection:

1. `read_thai_id()` — SELECT applet, then READ BINARY each text field
   (CID, name TH/EN, DOB, address, …). **Text shows immediately** on success.
2. `read_thai_id_photo()` — read the JPEG photo (non-fatal: text already shown).
3. `USBH_CCID_PowerOff()`.

APDU reference: <https://github.com/chakphanu/ThaiNationalIDCard/blob/master/APDU.md>

---

## 3. The T=0 two-step (the #1 gotcha)

The card is **T=0**. A READ BINARY that returns data does **not** hand the data
back in the same response — it returns a status word telling you how many bytes
are ready, and you must fetch them with a **GET RESPONSE**:

```
1. READ BINARY    80 B0 <Phi> <Plo> 02 00 FF     -> card replies SW = 61 xx
                                                     (xx = bytes available, no body)
2. GET RESPONSE   00 C0 00 00 <xx>                -> body (xx bytes) + SW 90 00
```

Verified on hardware (ACR39U-class reader): a photo READ BINARY returns
**`61 FF`** with **no body**; only after GET RESPONSE do you get the 255 bytes
+ `90 00`.

### Why text "just worked" but the photo did not

Thai-ID **text** fields are each ≤ 255 bytes, so for some readers the field
fits in a single response and the `61 xx` path is never exercised — text read
fine while the photo (which always needs GET RESPONSE) came back empty.

### Transport: use `USBH_CCID_APDU` directly

We issue **both** APDUs inline with `USBH_CCID_APDU()` and keep the GET RESPONSE
body. Do **not** rely on a wrapper to hide the `61 xx` handling for the photo:

- `send_apdu()`'s built-in `61 xx` handler did **not** deliver the photo body here.
- `send_apdu_raw()` / `send_apdu_bulk()` (raw-bulk via `USBH_CCID_Cmd`) returned
  failure on the photo endpoint.

`USBH_CCID_APDU()` returns the raw card reply **including the trailing 2-byte
status word** — always strip the last 2 bytes (`resp_len -= 2`) to get the data.

---

## 4. Photo read (the JPEG) — read until EOI, not a fixed length

The photo is a JPEG stored in an EF that begins at **byte offset `0x017B`**.
`P1:P2` in READ BINARY is the **big-endian byte offset**, read in 255-byte
chunks (`offset += bytes_read`). Chunk 0 = `0x017B`, chunk 1 = `0x027A`, …

### ⚠️ Do NOT use a fixed segment table

A previous implementation hard-coded **20 segments × 255 = 5100 bytes**.
A real card's photo was **5101 bytes** — the fixed read stopped **1 byte short**,
cutting off the final `D9` of the JPEG `FF D9` EOI marker. `jd_prepare()` still
parsed the header (so dimensions looked fine), but `jd_decomp()` ran out of
input at the very end and returned **`JDR_INP` (=2)** → the photo never rendered,
**with no error shown** (see [§6](#6-page-error-display-bug-latent)).

The current code **loops on the offset until** one of:

- the JPEG **EOI** (`FF D9`) appears in the accumulated data — *the normal stop*
  (scan must span the **chunk boundary**: start at `old_total - 1`);
- a **short read** (`< 255` bytes) — end of EF;
- a non-`90 00` / non-`61 xx` status (e.g. offset past EF) on a later chunk;
- the **16 KB** safety cap (`THAI_ID_PHOTO_MAX_SIZE`).

This adapts to any photo size. JPEG header (`FF D8 FF`) is validated on chunk 0.

---

## 5. Photo decode (TJPGD) & the JD_USE_SCALE regression

Decode lives in the page (`page_smart_card.c::decode_thai_id_photo`), using the
**direct TJPGD API** (`jd_prepare` / `jd_decomp`), **not** the LVGL image decoder
chain. Decoded **RGB888** is capped at a **128 KB** budget; if the full-res image
would exceed it, the code asks TJPGD to **descale** (scale 1–3, i.e. ÷2…÷8).

> **`JD_USE_SCALE` must be `1`** in `lvgl/.../libs/tjpgd/tjpgdcnf.h`.
> With `JD_USE_SCALE = 0`, any `scale > 0` makes `jd_decomp()` return
> `JDR_PAR` → the photo fails to decode.

This setting **regressed once already**: it was `1` in lvgl `release-v9.4.0`, but
an lvgl bump to `release-v9.5.0` shipped `tjpgdcnf.h` with `JD_USE_SCALE = 0`.
`getlibs`/library re-fetch resets this file, so it must be **re-applied** (ideally
via `apply_patches.sh`) after any lvgl update.

> Note: a standard Thai-ID photo (≈148×178 → ~79 KB RGB888) decodes at
> `scale = 0`, so `JD_USE_SCALE` does **not** affect it. It is kept enabled
> **defensively** for larger photos.

---

## 6. Page error-display bug (latent)

In `page_smart_card.c`, the "decode fail" message is written to `error_label`
inside the photo block, **but** the subsequent generic error block hides
`error_label` whenever `sc->error_msg` is empty — so a real decode failure shows
as a **blank photo with no error**. This made the photo bug hard to see. The
photo read now works, so it is not user-visible, but if you ever need decode
failures to surface, fix the ordering/precedence in that page (out of scope for
this driver).

---

## 7. Diagnostics (no serial on CM55)

Because CM55 has no UART and `printf` is unsafe here, the only debug channel is
the **on-screen `error_msg`**. Pattern used while bringing this up:

```c
snprintf(s_sc_state.error_msg, sizeof(s_sc_state.error_msg),
         "P0 st=%d n=%lu %02X%02X|SW%02X%02X", ...);
```

Useful values to surface: `USBH_CCID_APDU` status, response length, the first
bytes, and the **status word** (last 2 bytes, pre-strip). This is exactly how the
`61 FF` → GET RESPONSE → `FF D8 FF … 90 00` chain and the 5101-byte off-by-one
were diagnosed. Remember the page bug in §6: a non-empty `error_msg` from the
poll task takes display precedence over anything the page itself sets.

---

## 8. Known gotchas (from real incidents)

| Symptom | Root cause | Fix |
|---|---|---|
| Page hangs on entry | USB CCID init called from LVGL GFX task | Init from `main.c` boot only |
| Text reads OK, **photo blank, no error** | photo READ BINARY needs GET RESPONSE (`61 xx`); wrapper didn't deliver body | Explicit `USBH_CCID_APDU` READ + GET RESPONSE, strip SW |
| Photo blank, decode `JDR_INP` (2), dims look valid | fixed 20×255 read truncated a 5101-byte JPEG (lost EOI `D9`) | Read by offset **until `FF D9`** / short read / cap |
| Photo `JDR_PAR` (5) on larger photos | `JD_USE_SCALE = 0` in `tjpgdcnf.h` (lvgl re-fetch reset it) | Set `JD_USE_SCALE = 1`; re-apply after `getlibs` |
| Decode fails but **no on-screen error** | page error block hides label when `error_msg` empty | §6 (page-side, latent) |
| `printf`/deadlock on CM55 | newlib stdio takes UART mutex in USB callback/ISR | Never `printf` here; use `error_msg` |

---

## 9. Build / rebuild scope

This is a **shared library**. After editing this module, rebuild **every**
project that links it (workspace rule §4):

```bash
./clean_build.sh all        # or per target: ai / eva / game
```

The Smart Card driver runs on CM55, so a clean CM55 build of each affected
project is required (the build system caches object files by timestamp).
