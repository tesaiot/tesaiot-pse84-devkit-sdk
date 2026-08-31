# Build variants
<!-- //! [doc-drift-fix] — see docs/template_local_deltas.list; a sync that reverts this file must be refused -->

One checkout, two firmware personalities.

```
make build                                          # MTB + MicroPython (default)
make build BENTO_VARIANT=mtb-only                      # MTB only, plain C on FreeRTOS
```

`BENTO_VARIANT` is read in `common.mk`, which then includes `variants/<name>.mk`
**after** the board's `bsps/TARGET_$(TARGET)/bsp_features.mk`. Board layer
first, variant layer second, so a variant file states only its differences.

## Which variant to work in

| | `mtb-mpy` | `mtb-only` |
|---|---|---|
| REPL over UART | yes | no |
| BENTO IDE can flash it | yes, over TACP | no |
| `/boot.py`, `/main.py`, the Python modules | yes | no |
| LVGL UI, sensors, radar, Edge AI inference | **identical** | **identical** |
| WiFi association and saved networks | yes | yes — `/.wifi_creds` read at boot, written from the WiFi worker task |
| MQTT + mTLS | yes | yes — `tesaiot_mqtt.c` compiles as an ordinary source |
| OPTIGA CSR / Protected Update | yes | yes |
| Config and credential storage | LittleFS via the VM | LittleFS via `bento_libs/claw/common/storage_c` — same volume, byte-compatible both directions |
| A LittleFS volume that fails to mount | formatted (wipes `/main.py` and the config) | reported and left untouched; `bento_storage_format()` when erasing is meant |

## What is shared, and what is not

**CM55 is variant-agnostic.** Nothing in the CM55 image references a
MicroPython symbol. Anything you change under `proj_cm55/` affects both
variants equally, and no variant guard belongs there.

**CM33_NS is where the split lives**, and it is narrow. `libbento_mpy.a` and
the MicroPython port are linked only under `BENTO_HAS_MPY=1`
(`proj_cm33_ns/Makefile`); the rest of the CM33_NS sources compile in both
variants. The one place the two paths fork in application code is `main.c`,
which either starts the MicroPython task or the plain-C boot path.

**No header in the template exposes a MicroPython type.** Nothing under
`bento_libs/`, `proj_cm33_ns/`, `proj_cm55/` or `lib/` includes `py/*.h` or
mentions `mp_obj_t` / `MP_QSTR`. The header layer needs no variant awareness.

**The archives split cleanly.** `libbento_hsm.a`, `libbento_edge_ai.a`,
`libbento_cm55.a` and `libbento_ipc.a` need no MicroPython symbol and are
linked in both variants. `libbento_mpy.a` is MicroPython glue and follows the
VM.

## How `mtb-only` replaces the VM

`variants/mtb-only.mk` documents what it verified by linking the CM33_NS
objects without the MicroPython archives. The three pieces the VM used to own
are plain C there:

1. **LittleFS mount** — `storage_c` mounts the volume over the serial-memory
   block device (`bento_storage.c`); littlefs is vendored pristine at
   `bento_libs/claw/common/storage_c/littlefs`, built `LFS2_NO_MALLOC` so a
   path that would need the heap fails to compile instead.
2. **`/.tesaiot_config`** — the same parser and serialiser as the mpy variant
   (`tesaiot_config_store.c`), with the two I/O blocks compiled against
   `bento_storage`.
3. **`/.wifi_creds`** — `lfs_wifi_creds_c.c`, the same on-disk format, including
   the legacy XOR-32 checksum on read. The mpy variant defers the write to a
   REPL-idle flusher; this variant writes immediately from the WiFi worker task.

`BENTO_ALLOW_INCOMPLETE_MTB_ONLY` is still accepted on the command line for
scripts written while this variant refused to build; it no longer changes
anything.

## The guard rail

`./bento.sh doctor` checks the toolchain and the trees a build needs. The
variant assertion — that a `mtb-only` image carries no MicroPython runtime
symbol and a `mtb-mpy` image does — runs in the release pipeline
(`bento-release.sh`, at the repository root, not in this template). It exists
because the exclusion mechanism is a blacklist: one forgotten `CY_IGNORE` links
MicroPython objects into a firmware that is supposed to have none, and it links
cleanly, because `libmicropython.a` is complete. Do not rely on the list being
right — rely on the check.

## Adding a third variant

Add `variants/<name>.mk`, add the name to the `$(filter ...)` list in
`common.mk`, and give it a `BENTO_HAS_MPY` value. Nothing else in the tree
enumerates variants.
