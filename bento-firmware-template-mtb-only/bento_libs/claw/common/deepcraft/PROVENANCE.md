# Provenance — common/deepcraft/

| File | Origin | License | Vendored |
|---|---|---|---|
| `deepcraft_interface.h` | github.com/Infineon/micropython-deepcraft-model-interface @ v0.1.0 (`deepcraft/deepcraft_interface.h`) | MIT (c) 2026 Infineon Technologies AG | 2026-07-19, byte-identical |
| `deepcraft_engine.c` | same repo (`deepcraft/deepcraft_engine.c`) | MIT (c) 2026 Infineon Technologies AG | 2026-07-19, byte-identical |

Policy: these two files are vendored **unmodified** so upstream fixes can be
re-synced by copy. Known upstream quirks (documented, deliberately NOT patched
in v1): state set to RUNNING before the target confirms READY; `send()` return
values ignored by the engine; single-instance `s_model` static.

BENTO integration lives OUTSIDE these files:
- `common/bento_link/` — transport layer implementing the vtable seam
  (`deepcraft_interface_t`, first-member cast pattern per the header docs).
- `common/mpy/moddeepcraft.c` — MicroPython module (replaces the upstream
  `deepcraft_interface.c` MPY adapter, whose `machine.IPC` duck-type does not
  exist in our port fork).

The upstream repo ships NO CM55-side model runtime (stripped by Infineon);
our per-project `deepcraft_task` provides it (stub first, DEEPCRAFT Studio
models later).
