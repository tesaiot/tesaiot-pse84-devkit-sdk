# Third-party notices

Firmware and documentation published from this repository incorporate software
and fonts written by other people. This file collects the notices that their
licences require to travel with a distribution, in source or binary form. It is
the notice file for the whole package: it is not a substitute for the notices
that appear inside individual source files, and where both exist, both apply.

**Compiled 2026-08-29** against the tree as it stood on that date. Every entry
names the file it was read from. Where a licence text was retrieved from
upstream rather than found in this tree, the entry says so and gives the URL
and the SHA-256 of what was retrieved, so the claim can be re-checked.

**Scope.** The components below are third-party in the sense of being neither
Infineon/Cypress nor TESAIoT work. Infineon and Cypress material in this package
is governed by the Infineon End User License Agreement at
`template/bsps/TARGET_KIT_PSE84_AI/EULA`, whose §§82-90 carve out third-party
software: *"Portions of the Software may be licensed under free and/or open
source licenses ... Third Party Software is subject to the applicable license
agreement and not this Agreement."* This file is that applicable-licence record.

**Related file.** `template/THIRD_PARTY.md` records, per component, where the
code came from and what its upstream repository LICENSE actually permits — a
provenance and permission record scoped to `template/`. This file is the
complementary one: the notices that must accompany a distribution, with their
texts. Read that one to decide whether something may be redistributed; read this
one for what must ship alongside it when it is.

**Honesty rule for this document.** Where provenance or licence is not
established, the entry says "not established" and states what is missing. No
entry asserts a licence that has not been read from either the component itself
or its upstream project. One entry is in that state today and is listed in
§3.

---

## Contents

- §1 — Components with a licence established and reproduced
  - §1.1 littlefs — BSD-3-Clause
  - §1.2 Bosch BMI270 SensorAPI configuration blob — BSD-3-Clause
  - §1.3 lwIP — BSD-3-Clause (lwIP variant)
  - §1.4 LVGL — MIT
  - §1.5 MicroPython — MIT
  - §1.6 FreeRTOS Kernel — MIT
  - §1.7 coreMQTT — MIT
  - §1.8 jsmn — MIT
  - §1.9 TinySoundFont — MIT (two copyright holders)
  - §1.10 jQuery — MIT
  - §1.11 Doxygen output JavaScript — MIT
  - §1.12 doxygen-awesome-css — MIT
  - §1.13 Noto Sans Thai — SIL Open Font License 1.1
  - §1.14 minimp3 — CC0-1.0 (public domain dedication)
  - §1.15 Mbed TLS — Apache-2.0
  - §1.16 CMSIS / Arm — Apache-2.0
  - §1.17 Twemoji emoji graphics — CC-BY 4.0
- §2 — Proprietary third-party components (no open-source grant)
  - §2.1 SEGGER emUSB-Host — proprietary, licensed to Cypress only
  - §2.2 DEEPCRAFT™ Studio generated model code (Imagimob AB) — credited; their tool, their terms
  - §2.3 VeriSilicon / Vivante VGLite — redistributed by Infineon
  - §2.4 DEEPCRAFT™ Ready Models (Imagimob AB) — credited; evaluation licence, not distributable
  - §2.5 `audio_pdm.c` / `.h` — ported from Infineon's DEEPCRAFT™ audio example
- §3 — Components whose provenance is not established
- §4 — TESAIoT's own licensing, and three things it collides with

---

# §1 — Components with a licence established and reproduced

## §1.1 littlefs — BSD-3-Clause

| | |
|---|---|
| Component | littlefs (the little filesystem) |
| Version | v2.11 — `LFS2_VERSION 0x0002000b` at `template/bento_libs/claw/common/storage_c/littlefs/lfs2.h:24`; on-disk format 2.1 (`LFS2_DISK_VERSION 0x00020001`, `:31`) |
| Where it lives | `template/bento_libs/claw/common/storage_c/littlefs/` — `lfs2.c`, `lfs2.h`, `lfs2_util.c`, `lfs2_util.h` |
| Upstream | `littlefs-project/littlefs`, reaching this tree through MicroPython's vendored copy (the `lfs2_`/`LFS2_` prefixing is MicroPython's rename) |
| Licence | BSD-3-Clause |
| Obligation | §1 and §2 require the copyright notice, conditions and disclaimer be reproduced in source **and** in the documentation accompanying a binary distribution. This entry discharges the binary-form clause. |

Governing notice as it appears in this tree, `lfs2.c:1-7` (identical at
`lfs2.h:1-7`, `lfs2_util.c:1-7`, `lfs2_util.h:1-7`):

```
/*
 * The little filesystem
 *
 * Copyright (c) 2022, The littlefs authors.
 * Copyright (c) 2017, Arm Limited. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
```

Full licence text, retrieved from
`https://raw.githubusercontent.com/littlefs-project/littlefs/v2.11.0/LICENSE.md`
on 2026-08-29:

```
Copyright (c) 2022, The littlefs authors.  
Copyright (c) 2017, Arm Limited. All rights reserved.

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

-  Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.
-  Redistributions in binary form must reproduce the above copyright notice, this
   list of conditions and the following disclaimer in the documentation and/or
   other materials provided with the distribution.
-  Neither the name of ARM nor the names of its contributors may be used to
   endorse or promote products derived from this software without specific prior
   written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

---

## §1.2 Bosch BMI270 SensorAPI configuration blob — BSD-3-Clause

| | |
|---|---|
| Component | BMI270 SensorAPI configuration file (8192-byte sensor firmware blob) |
| Version | v2.86.1 — `template/bento_libs/claw/common/mpy/bmi270_config_data.h:1` |
| Where it lives | `template/bento_libs/claw/common/mpy/bmi270_config_data.h` (`BMI270_CONFIG_FILE_SIZE (8192U)`, `:9`) |
| Upstream | `boschsensortec/BMI270_SensorAPI` |
| Licence | BSD-3-Clause |
| Obligation | Same as §1.1: full conditions and disclaimer in source and in binary-form documentation. The one-line summary in the header is not sufficient on its own, which is why the full text is below. |

Notice as it appears in this tree, `bmi270_config_data.h:2`:

```
/* Copyright (c) 2023 Bosch Sensortec GmbH. BSD-3-Clause license. */
```

Full licence text, retrieved from
`https://raw.githubusercontent.com/boschsensortec/BMI270_SensorAPI/master/LICENSE`
on 2026-08-29:

```
Copyright (c) 2023 Bosch Sensortec GmbH. All rights reserved.

BSD-3-Clause

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its
    contributors may be used to endorse or promote products derived from
    this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.
```

---

## §1.3 lwIP — BSD-3-Clause (lwIP variant)

| | |
|---|---|
| Component | lwIP TCP/IP stack |
| Version | `STABLE-2_1_2_RELEASE` — pinned in `template/proj_cm33_ns/libs/lwip.mtb` |
| Where it lives | The stack itself is fetched at build time, not vendored. What ships in this package is the configuration header `template/proj_cm33_ns/lwipopts.h`, which carries lwIP's own copyright and licence text and is therefore covered by it. (`template/proj_cm55/lwipopts.h` is TESAIoT-authored and carries no lwIP notice.) |
| Upstream | `lwip-tcpip/lwip` |
| Licence | BSD-3-Clause, lwIP's three-clause variant |
| Obligation | Reproduce copyright, conditions and disclaimer in source and in binary-form documentation. |

Governing notice as it appears in this tree, `template/proj_cm33_ns/lwipopts.h:1-31`:

```
/*
 * Copyright (c) 2001-2003 Swedish Institute of Computer Science.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT
 * SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 *
 * This file is part of the lwIP TCP/IP stack.
 *
 * Author: Simon Goldschmidt
 *
 */
```

Upstream `COPYING` at the pinned tag, retrieved from
`https://raw.githubusercontent.com/lwip-tcpip/lwip/STABLE-2_1_2_RELEASE/COPYING`
on 2026-08-29. It names a different year range and a different author from the
header above; both are genuine lwIP per-file notices and both are reproduced
rather than reconciled:

```
/*
 * Copyright (c) 2001, 2002 Swedish Institute of Computer Science.
 * All rights reserved. 
 * 
 * Redistribution and use in source and binary forms, with or without modification, 
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission. 
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED 
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF 
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT 
 * SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, 
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT 
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS 
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN 
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING 
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY 
 * OF SUCH DAMAGE.
 *
 * This file is part of the lwIP TCP/IP stack.
 * 
 * Author: Adam Dunkels <adam@sics.se>
 *
 */
```

---

## §1.4 LVGL — MIT

| | |
|---|---|
| Component | LVGL (Light and Versatile Graphics Library) |
| Version | v9.5.0 — `template/proj_cm55/deps/lvgl.mtb` pins `https://github.com/lvgl/lvgl#v9.5.0` |
| Where it lives | Vendored, modified copies of eight upstream files: `template/proj_cm55/modules/lvgl_display/core/{lv_refr.c, lv_vg_lite_utils.c, lv_draw_vg_lite.c, lv_draw_vg_lite_img.c, lv_conf.h, lv_port_disp.c, lv_port_indev.c}` and `template/proj_cm55/lv_port_indev.c`. The rest of the library is fetched at build time. LVGL is also linked into the shipped archives `dist/cm55_core/…/libbento_cm55.a` and `dist/ipc_core/…/libbento_ipc.a`. |
| | LVGL-format generated assets also ship: `template/bento_libs/claw/common/thai_text/src/fonts/lv_font_noto_thai_*.c`, `template/bento_libs/claw/common/modules/ipc_lcd/emoji_assets/c/*.c`, `template/proj_cm55/modules/page-components/_core/icons/*.c`, `template/proj_cm55/modules/game_sprites_mpy/game_sprites/*.c`. |
| Upstream | `lvgl/lvgl` |
| Licence | MIT |
| Obligation | The copyright notice and permission notice must be retained in all copies and substantial portions. |
| In-tree licence file | `template/proj_cm55/modules/lvgl_display/LICENSE-LVGL.txt` |

Each of the eight vendored files carries the notice at the top of the file,
above the Infineon notice covering Infineon's modifications. Upstream text,
retrieved from `https://raw.githubusercontent.com/lvgl/lvgl/v9.5.0/LICENCE.txt`
on 2026-08-29 (SHA-256
`27a80bd36832ab42d35ad60c08b2b230a807a9bc0d58e94ec1531543dc49cbe8`):

```
MIT licence
Copyright (c) 2025 LVGL Kft

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the “Software”), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
```

Note on the copyright year: LVGL's `LICENCE.txt` at tag `v9.5.0` reads
"Copyright (c) 2025 LVGL Kft". Older LVGL releases carry 2021. The 2025 line is
what the pinned version actually ships, and it is what is reproduced here and
in the source headers.

---


**`success_checkmark.json` is LVGL's, and is covered here.** `template/proj_cm55/modules/page-components/_core/lottie_json/success_checkmark.json` (SHA-256 `c654c7cf147a5e03fec645a40d142a5c6987b103ec8609b7f64e739b0119157b`, 5138 bytes) is LVGL's own example asset `examples/widgets/lottie/lv_example_lottie_approve.json`, renamed on import. Byte-identical in LVGL v9.2.0, v9.4.0 and v9.5.0, and covered by LVGL's repository-wide MIT grant (Copyright (c) 2025 LVGL Kft). It is not among the third-party carve-outs listed in LVGL's `COPYRIGHTS.md`. Established 2026-08-29 by byte comparison; it had been listed under §3.1 as provenance-not-established until then.

## §1.5 MicroPython — MIT

| | |
|---|---|
| Component | MicroPython |
| Version | Infineon `micropython-psoc-edge` port. No tag is pinned by this package; `template/proj_cm33_ns/Makefile.micropython:68` points at a sibling checkout (`MPY_DIR = $(BENTO_WORKSPACE)/micropython-psoc-edge-psoc-edge-main`). The port tree's `LICENSE` reads "Copyright (c) 2013-2025 Damien P. George". |
| Where it lives | `template/bento_libs/claw/common/mpy/mpy_main.c` is a refactor of upstream `ports/psoc-edge/main.c`. It is the only file in that directory derived from MicroPython source; the other fifty-three are TESAIoT and Infineon extension modules that use the MicroPython API but copy no upstream code. MicroPython is linked into the shipped archive `dist/mpy_secure/…/libbento_mpy.a`. |
| Upstream | `micropython/micropython`, via Infineon's psoc-edge port |
| Licence | MIT |
| Obligation | Retain copyright and permission notice in all copies and substantial portions. |
| In-tree licence file | `template/bento_libs/claw/common/mpy/LICENSE-MICROPYTHON.txt` |

Governing notice, from the port tree's `LICENSE` (SHA-256
`419137268636eb7d29b83c68e4f5f4494bcb172c24b18a122eb46312c4966940`, read
2026-08-29):

```
The MIT License (MIT)

Copyright (c) 2013-2025 Damien P. George

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
```

The remainder of that file is a per-directory listing of third-party software
inside the MicroPython repository. It is reproduced in full in
`template/bento_libs/claw/common/mpy/LICENSE-MICROPYTHON.txt`. Note that the
listing describes the whole upstream repository; only a subset is compiled into
this firmware.

---

## §1.6 FreeRTOS Kernel — MIT

| | |
|---|---|
| Component | FreeRTOS Kernel |
| Version | Kernel V10.6.2 (`template/proj_cm33_ns/FreeRTOSConfig.h:2`). Assets pinned as `freertos release-v10.6.202` (`template/proj_cm33_ns/deps/freertos.mtb`) and `release-v10.6.201` (`template/proj_cm55/deps/freertos.mtb`). |
| Where it lives | Kernel fetched at build time. This package ships the configuration headers `template/proj_cm33_ns/FreeRTOSConfig.h` and `template/proj_cm55/FreeRTOSConfig.h`, which carry the kernel's own copyright and MIT text. |
| Upstream | `FreeRTOS/FreeRTOS-Kernel` |
| Licence | MIT |
| Obligation | Retain copyright and permission notice in all copies and substantial portions. |

Governing notice as it appears in this tree, `template/proj_cm33_ns/FreeRTOSConfig.h:1-5`:

```
/*
 * FreeRTOS Kernel V10.6.2
 * Copyright (C) 2021 Amazon.com, Inc. or its affiliates.  All Rights Reserved.
 * Copyright (C) 2019-2024 Cypress Semiconductor Corporation, or a subsidiary of
 * Cypress Semiconductor Corporation.  All Rights Reserved.
 */
```

Upstream licence text at the matching kernel tag, retrieved from
`https://raw.githubusercontent.com/FreeRTOS/FreeRTOS-Kernel/V10.6.2/LICENSE.md`
on 2026-08-29:

```
MIT License

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

---

## §1.7 coreMQTT — MIT

| | |
|---|---|
| Component | AWS coreMQTT (and the AWS IoT Device SDK embedded-C port around it) |
| Version | Infineon asset `mqtt release-v4.7.0` (`mtb://mqtt#release-v4.7.0`), with `aws-iot-device-sdk-embedded-C#202103.00` and `aws-iot-device-sdk-port release-v2.7.0` |
| Where it lives | Not vendored. The library is fetched at build time and linked into shipped firmware. The configuration headers in this package — `template/bento_libs/claw/common/modules/tesaiot_mqtt/core_mqtt_config.h`, `template/proj_cm33_ns/core_http_config.h`, `template/proj_cm55/core_http_config.h` — are TESAIoT-authored and carry no AWS copyright text. |
| Upstream | `FreeRTOS/coreMQTT` |
| Licence | MIT |
| Obligation | Attribution is owed because coreMQTT is linked into the distributed binary, not because any of its source ships here. |

Upstream licence text, retrieved from
`https://raw.githubusercontent.com/FreeRTOS/coreMQTT/v2.1.1/LICENSE` on
2026-08-29. The Infineon asset tag `release-v4.7.0` is Infineon's wrapper
version and does not correspond to an upstream coreMQTT tag; the licence text
below is upstream's and has been unchanged across coreMQTT's v2.x line:

```
MIT License

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

---

## §1.8 jsmn — MIT

| | |
|---|---|
| Component | jsmn (minimalistic JSON parser, single header) |
| Version | No version macro in the file. Header-only, guard `JSMN_H` at `:24`. |
| Where it lives | `template/bento_libs/claw/common/ble_nus/vendor/jsmn.h` |
| Upstream | `zserge/jsmn` |
| Licence | MIT |
| Obligation | The full MIT text is embedded in the header, which satisfies source redistribution. It does not satisfy binary distribution of firmware built from it; this entry does. |

Governing notice as it appears in this tree, `jsmn.h:1-23`:

```
/*
 * MIT License
 *
 * Copyright (c) 2010 Serge Zaitsev
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
```

Upstream `LICENSE`, retrieved from
`https://raw.githubusercontent.com/zserge/jsmn/master/LICENSE` on 2026-08-29.
It spells the author's name "Serge A. Zaitsev" where the header says "Serge
Zaitsev"; both are reproduced as found:

```
Copyright (c) 2010 Serge A. Zaitsev

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
```

---

## §1.9 TinySoundFont — MIT (two copyright holders)

| | |
|---|---|
| Component | TinySoundFont (SoundFont2 synthesiser, single header) |
| Version | v0.9 — `template/proj_cm55/modules/audio_player/tsf.h:1` |
| Where it lives | `template/proj_cm55/modules/audio_player/tsf.h`, consumed by `bento_tsf.c` in the same directory. Acknowledged at `template/proj_cm55/modules/audio_player/README.md:835`. |
| Upstream | `schellingb/TinySoundFont`, itself derived from `stevefolta/SFZero` |
| Licence | MIT, with **two** copyright holders. Both must be carried. |
| Obligation | Retain both copyright lines and the permission notice. |

Governing notice as it appears in this tree, `tsf.h:22-41`:

```
   LICENSE (MIT)

   Copyright (C) 2017-2025 Bernhard Schelling
   Based on SFZero, Copyright (C) 2012 Steve Folta (https://github.com/stevefolta/SFZero)

   Permission is hereby granted, free of charge, to any person obtaining a copy of this
   software and associated documentation files (the "Software"), to deal in the Software
   without restriction, including without limitation the rights to use, copy, modify, merge,
   publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons
   to whom the Software is furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in all
   copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
   INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
   PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
   LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
   TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
   USE OR OTHER DEALINGS IN THE SOFTWARE.
```

---

## §1.10 jQuery — MIT

| | |
|---|---|
| Component | jQuery |
| Version | 3.6.0 — banner at `template/bsps/TARGET_KIT_PSE84_AI/docs/html/jquery.js:1` |
| Where it lives | `template/bsps/TARGET_KIT_PSE84_AI/docs/html/jquery.js` (minified, ~176 KB), inside the BSP's generated Doxygen output |
| Upstream | `jquery/jquery` |
| Licence | MIT (OpenJS Foundation) |
| Obligation | Retain the copyright and permission notice. The minified banner is jQuery's own standard attribution and is generally accepted for the script itself; this entry carries the full text. |

Banner as it appears in this tree, `jquery.js:1`:

```
/*! jQuery v3.6.0 | (c) OpenJS Foundation and other contributors | jquery.org/license */
```

Upstream licence text at the matching tag, retrieved from
`https://raw.githubusercontent.com/jquery/jquery/3.6.0/LICENSE.txt` on
2026-08-29 (the upstream file continues with a section on bundled dependencies
that do not ship here; the MIT grant itself is reproduced in full):

```
Copyright OpenJS Foundation and other contributors, https://openjsf.org/

Permission is hereby granted, free of charge, to any person obtaining
a copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice shall be
included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
```

---

## §1.11 Doxygen output JavaScript — MIT

| | |
|---|---|
| Component | The JavaScript Doxygen emits into its HTML output |
| Version | Copyright range 1997-2020, matching the Doxygen release that generated the BSP documentation |
| Where it lives | `template/bsps/TARGET_KIT_PSE84_AI/docs/html/{dynsections.js, menu.js, menudata.js, navtree.js, resize.js}` and `.../docs/html/search/search.js` |
| Upstream | `doxygen/doxygen` |
| Licence | MIT, Dimitri van Heesch |
| Obligation | Retain the copyright and permission notice. Each file carries it in a `@licstart` block. |

**This entry covers the emitted JavaScript, not the Doxygen program.** Doxygen
itself is licensed GPL-2.0 and is not distributed in this package — it is a
build-time tool run on the developer's machine. The JavaScript it writes into
generated HTML carries its own MIT notice, quoted below, and that is what ships.
Conflating the two would misstate both.

Governing notice as it appears in this tree, `dynsections.js:1-24` (identical in
`menu.js`, `menudata.js`, `navtree.js`, `resize.js` and `search/search.js`):

```
/*
 @licstart  The following is the entire license notice for the JavaScript code in this file.

 The MIT License (MIT)

 Copyright (C) 1997-2020 by Dimitri van Heesch

 Permission is hereby granted, free of charge, to any person obtaining a copy of this software
 and associated documentation files (the "Software"), to deal in the Software without restriction,
 including without limitation the rights to use, copy, modify, merge, publish, distribute,
 sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:

 The above copyright notice and this permission notice shall be included in all copies or
 substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
 BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
 DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

 @licend  The above is the entire license notice for the JavaScript code in this file
 */
```

---

## §1.12 doxygen-awesome-css — MIT

| | |
|---|---|
| Component | doxygen-awesome-css |
| Version | Not tagged in-file. Copyright range in the CSS is 2021-2025, implying v2.3.x or later. |
| Where it lives | `tools/doxygen-awesome/doxygen-awesome.css`, `tools/doxygen-awesome/doxygen-awesome-sidebar-only.css` |
| Upstream | `jothepro/doxygen-awesome-css` |
| Licence | MIT — `doxygen-awesome.css:1` `/* SPDX-License-Identifier: MIT */`, `:7` `Copyright (c) 2021 - 2025 jothepro` |
| Obligation | Retain copyright and permission notice. |
| In-tree licence file | `tools/doxygen-awesome/LICENSE` — **already present**, the only third-party licence file that existed in this package before this notices file was written. |

One discrepancy, recorded rather than silently corrected: `tools/doxygen-awesome/LICENSE`
reads `Copyright (c) 2021 - 2023 jothepro` while the vendored CSS reads
`2021 - 2025`. The LICENSE copy is stale relative to the CSS it accompanies.
Refreshing it requires re-vendoring from a known upstream tag, which has not
been done.

---

## §1.13 Noto Sans Thai — SIL Open Font License 1.1

| | |
|---|---|
| Component | Noto Sans Thai |
| Version | 2.002 — read from the font's own `name` table, ID 5 (`Version 2.002`) and ID 3 (`2.002;GOOG;NotoSansThai-Regular`) |
| Where it lives | `template/bento_libs/claw/common/thai_text/data/NotoSansThai.ttf` (218,652 bytes, SHA-256 `5a1c559bb539583c8a1fd99d1c5b9491e5e14478c9cd2bd0970d5c3096cc9ef8`) and the five OFL derivatives `template/bento_libs/claw/common/thai_text/src/fonts/lv_font_noto_thai_{14,16,20,24,28}.c`, rasterised from a PUA-remapped copy by `tools/regen.sh` |
| Upstream | `notofonts/thai` |
| Licence | SIL Open Font License, Version 1.1 |
| Obligation | OFL §2 requires the licence and copyright notice to accompany any redistribution of the Font Software, stand-alone or bundled. §1 extends the licence to derivative works, so the five generated `.c` fonts are covered too. |
| In-tree licence file | `template/bento_libs/claw/common/thai_text/data/OFL.txt`, with provenance in `.../data/UPSTREAM` |

Governing notice, read from the font binary's `name` table — ID 0 (copyright)
and ID 13 (licence description):

```
Copyright 2022 The Noto Project Authors (https://github.com/notofonts/thai)

This Font Software is licensed under the SIL Open Font License, Version 1.1.
This license is available with a FAQ at: https://scripts.sil.org/OFL
```

**Licence correction.** Until 2026-08-29 the library's own
`template/bento_libs/claw/common/thai_text/README.md` described this font as
"Apache 2.0 / OFL" and claimed a licence copy existed in `data/`. Neither was
true: the font carries only the OFL notice quoted above, and no licence file was
present. Both statements have been corrected and the licence file added.

**Reserved Font Name.** OFL §3 forbids using a Reserved Font Name in a Modified
Version. The Noto project declares none for this family — the copyright header
in `OFL.txt` carries no "with Reserved Font Name" clause — so the PUA-remapped
intermediate and the rasterised fonts may keep the Noto Sans Thai name.

Full licence text is `template/bento_libs/claw/common/thai_text/data/OFL.txt`,
retrieved verbatim from `https://raw.githubusercontent.com/notofonts/thai/main/OFL.txt`
on 2026-08-29 (SHA-256 `dad6e6abc2bf3fc37cc698af7607c3f4d4235039695713b222e5a034fb5b9b1c`).
It is ~4.3 KB and is not duplicated here; it ships with the font, which is what
OFL §2 requires.

---

## §1.14 minimp3 — CC0-1.0 (public domain dedication)

| | |
|---|---|
| Component | minimp3 |
| Version | No version macro. The file contains the upstream issue-88 fix (`minimp3.h:895` in the pre-2026-08-29 numbering), so it post-dates 2019. |
| Where it lives | `template/proj_cm55/modules/audio_player/minimp3.h`, used by `bento_mp3.c`. Noted at `template/proj_cm55/modules/audio_player/README.md:836`. |
| Upstream | `lieff/minimp3` |
| Licence | CC0-1.0 |
| Obligation | **None.** CC0 waives attribution. This entry exists for completeness of the inventory, not because anything is owed. |

Dedication as it appears in this tree, `minimp3.h:3-8`:

```
/*
    https://github.com/lieff/minimp3
    To the extent possible under law, the author(s) have dedicated all copyright and related and neighboring rights to this software to the public domain worldwide.
    This software is distributed without any warranty.
    See <http://creativecommons.org/publicdomain/zero/1.0/>.
*/
```

---

## §1.15 Mbed TLS — Apache-2.0

| | |
|---|---|
| Component | Mbed TLS, as repackaged by Infineon (`ifx-mbedtls`) |
| Version | `ifx-mbedtls release-v3.6.400`, i.e. the Mbed TLS 3.6 line |
| Where it lives | The library is fetched at build time. This package ships three configuration headers carrying Arm/Mbed TLS copyright: `template/proj_cm55/mbedtls_user_config.h`, `template/proj_cm33_ns/configs/mbedtls_user_config.h`, `template/proj_cm33_ns/configs/ifx_psa_crypto_config.h`. Mbed TLS is linked into shipped firmware. |
| Upstream | `Mbed-TLS/mbedtls` |
| Licence | Apache-2.0 |
| Obligation | Apache-2.0 §4(a) requires a copy of the licence with any distribution; §4(b) requires modified files to carry prominent modification notices; §4(d) requires propagation of any NOTICE file. |

Governing notice as it appears in this tree, `template/proj_cm55/mbedtls_user_config.h:11-26`:

```
*  Copyright (C) 2006-2018, ARM Limited, All Rights Reserved
*  SPDX-License-Identifier: Apache-2.0
*
*  Licensed under the Apache License, Version 2.0 (the "License"); you may
*  not use this file except in compliance with the License.
*  You may obtain a copy of the License at
*
*  http://www.apache.org/licenses/LICENSE-2.0
*
*  Unless required by applicable law or agreed to in writing, software
*  distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
*  WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
*  See the License for the specific language governing permissions and
*  limitations under the License.
*
*  This file is part of mbed TLS (https://tls.mbed.org)
```

and `template/proj_cm33_ns/configs/ifx_psa_crypto_config.h:8-11`:

```
 *  Copyright The Mbed TLS Contributors
 *  Copyright (C) 2022 Cypress Semiconductor Corporation
 *  SPDX-License-Identifier: Apache-2.0
```

**Where the Apache-2.0 text is.** The full Apache License 2.0 text ships in this
package at `template/bsps/TARGET_KIT_PSE84_AI/LICENSE`. That copy is scoped in
its own context to the Infineon BSP, but the text is the licence itself and is
identical for every Apache-2.0 component; it is the copy that discharges §4(a)
for this entry and for §1.16. A separately-scoped copy has not been added, and
that is a deliberate choice recorded here rather than an omission.

**Modification notices.** Four patches in `third_party_patches/ifx-mbedtls/`
modify Mbed TLS source. Apache-2.0 §4(b) requires modified files to carry
prominent notices; the patch headers serve that purpose only once the patches
are applied. The patches themselves document what changed
(`third_party_patches/README.md`, `docs/THIRD_PARTY_PATCHES.md`).

---

## §1.16 CMSIS / Arm — Apache-2.0

| | |
|---|---|
| Component | CMSIS-Core and Arm startup code |
| Version | CMSIS-Core `release-v6.1.0` per `template/proj_cm33_ns/libs/cmsis.mtb` and `deps/assetlocks.json`. The vendored `partition_ARMCM33.h` is CMSIS v5.0.1 vintage (`:4` `@version V5.0.1`). |
| Where it lives | `template/bsps/TARGET_KIT_PSE84_AI/COMPONENT_CM33/COMPONENT_SECURE_DEVICE/partition_ARMCM33.h`, `.../COMPONENT_SECURE_DEVICE/s_start_pse84.c`, `.../COMPONENT_CM33/COMPONENT_NON_SECURE_DEVICE/ns_start_pse84.c`, `.../COMPONENT_CM55/COMPONENT_NON_SECURE_DEVICE/ns_start_pse84.c` |
| Upstream | `ARM-software/CMSIS_6`, via Infineon's `cmsis` asset |
| Licence | Apache-2.0 |
| Obligation | §4(a) licence copy, §4(b) modification notices, §4(d) NOTICE propagation. |

Governing notice as it appears in this tree, `partition_ARMCM33.h:8-10`:

```
 * Copyright (c) 2009-2016 ARM Limited. All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
```

A second Arm notice appears at `ns_start_pse84.c:52`
(`** Copyright (c) 2018 Arm Limited. All rights reserved.`), and
`ns_start_pse84.c:9-11` carries `Copyright (c) 2009-2021 Arm Limited` under the
same Apache-2.0 identifier.

Full Apache-2.0 text: `template/bsps/TARGET_KIT_PSE84_AI/LICENSE` — see the note
under §1.15.

**Not present in this package**, and therefore not listed: CMSIS-DSP, CMSIS-NN
and the Ethos-U driver. No `arm_math.h`, no CMSIS-NN source and no Ethos-U
source exists anywhere in the publishable set; the neural work goes through
`ml-middleware release-v3.1.0` and `ml-tflite-micro release-v3.1.0`, fetched at
build time.

---

## §1.17 Twemoji emoji graphics — CC-BY 4.0

| | |
|---|---|
| Component | Twemoji (Twitter Emoji) graphics |
| Version | `jdecked/twemoji` default branch as of 2026-08-29; the artwork is unchanged from `twitter/twemoji` |
| Where it lives | `template/bento_libs/claw/common/modules/ipc_lcd/emoji_assets/src_png/*.png` — 18 files, 72x72, unmodified. Derivatives: `.../png20/*.png` (the same 18, downscaled to 20x20 and renamed to the `emoji_uXXXX.png` form) and `.../c/*.c` plus `c/disabled/` (LVGL C byte arrays) |
| Upstream | https://github.com/jdecked/twemoji — the maintained fork of https://github.com/twitter/twemoji |
| Licence | **CC-BY 4.0** for the graphics. (Twemoji's *code* is MIT, but no Twemoji code is redistributed here — only the bitmaps.) |
| Obligation | CC-BY 4.0 §3(a): retain creator identification, a copyright notice, a notice referring to the licence, a notice referring to the disclaimer of warranties, and a URI to the licence; and indicate that the material was modified. §3(a)(2) allows this "in any reasonable manner based on the medium, means, and context". This entry discharges it. |

**How the provenance was established.** Not from the filename — the
`emoji_uXXXX.png` spelling in `png20/` and `c/` is Google's Noto Color Emoji
convention, and it is what first suggested Noto, but it is a rename applied by
our own conversion pipeline. The originals in `src_png/` use Twemoji's
bare-codepoint convention (`1f534.png`). Settled by byte comparison instead,
2026-08-29: each of the 18 files in `src_png/` was compared with `cmp` against
`https://raw.githubusercontent.com/jdecked/twemoji/main/assets/72x72/<codepoint>.png`
— **18 identical, 0 mismatched, 0 absent upstream**. The same files are *not*
byte-identical to Noto Color Emoji `png/72` (for example `1f680`: 1,064 bytes
here, 3,757 bytes in Noto). Noto is excluded; Twemoji is confirmed.

Governing statement, `README.md` §License, retrieved from
`https://raw.githubusercontent.com/jdecked/twemoji/main/README.md` on 2026-08-29:

```
Code licensed under the MIT License: <http://opensource.org/licenses/MIT>

Graphics licensed under CC-BY 4.0: <https://creativecommons.org/licenses/by/4.0/>
```

Copyright holders, from `LICENSE` at the same commit:

```
Copyright (c) 2022-present Jason Sofonia & Justine De Caires
Copyright (c) 2014-2021 Twitter
```

The repository also carries the full CC-BY 4.0 text as `LICENSE-GRAPHICS`
(retrieved 2026-08-29; it opens `Attribution 4.0 International`).

**The notice that must ship**, satisfying each limb of §3(a):

> Emoji graphics from Twemoji.
> Copyright (c) 2014-2021 Twitter, Inc and other contributors.
> Copyright (c) 2022-present Jason Sofonia & Justine De Caires.
> Licensed under CC-BY 4.0 — https://creativecommons.org/licenses/by/4.0/
> Source: https://github.com/jdecked/twemoji
> Modified: the 72x72 source bitmaps were downscaled to 20x20 and converted to
> LVGL C byte arrays for embedded rendering. No artwork was redrawn.
> The material is provided without warranties or conditions of any kind, to the
> extent permitted by the licence.

Twemoji accepts this form. `README.md` §"Attribution Requirements": *"we consider
the guide a bit onerous and as a project, will accept a mention in a project
README or an 'About' section or footer on a website."* A notices file shipped
with the firmware and the repository meets that.

**The obligation is real.** Under the Noto hypothesis (Apache-2.0) attribution
would have been discharged simply by shipping a copy of the licence. Under
CC-BY 4.0 it is a *condition of the grant*: ship the images without the
attribution above and the licence does not cover the distribution.

---

# §2 — Proprietary third-party components (no open-source grant)

These are third-party but not open source. No attribution notice discharges
anything here; what each needs is a licence, a removal, or counsel. They are
listed because a notices file that silently omitted them would misrepresent
what the package contains.

## §2.1 SEGGER emUSB-Host — proprietary, licensed to Cypress only

| | |
|---|---|
| Version | V2.48.1 — `template/proj_cm55/imports/emusb-host/Config/usbh_config_io.c:20` |
| Where it lives | `template/proj_cm55/imports/emusb-host/Config/usbh_config_io.c` and `.../Config/COMPONENT_PSE84/usbh_config.c`. The library itself is fetched via `template/proj_cm55/deps/emusb-host.mtb`. |
| Status | **Redistribution risk, not an attribution gap.** `usbh_config_io.c:24-31` states that the source was licensed to Cypress Semiconductor Corporation, including the right to distribute the **object code** version, under licence number `USBH-00303`. A third party redistributing the source is not covered by that grant. `:14-16` further states the file may not be used to write a similar product. |
| Action | Removal from the publishable set and replacement with a fetch step, the way `emusb-host.mtb` already handles the library. Work in progress at the time this file was written; nothing here is a substitute for it. |

## §2.2 DEEPCRAFT™ Studio generated model code (Imagimob AB) — credited; their tool, their terms

### Who made these, and what they are

The three models this package **does** ship — motion, audio and radar — are not
TESAIoT's work. Nobody here trained them, authored them or owns them. They are
**DEEPCRAFT™ Studio exports**: C source emitted by Imagimob's ImagiNet compiler
from a model built in Infineon's Edge AI tool, and each of the six files says so
in its own first lines.

> `Copyright © 2023- Imagimob AB, All Rights Reserved.`
>
> `Generated at <date>. Any changes will be lost.`

**Imagimob AB is an Infineon Technologies company**, and **DEEPCRAFT™ Studio**
is Infineon's Edge AI development tool. The network, the feature pipeline, the
quantised weights and the generated C are all products of that tool. What is
TESAIoT's in `template/proj_cm55/modules/ai_models/` is the plumbing around
them — the slot ABI, the weak stubs and the engine that calls the four entry
points. Nothing inside the generated files is ours, and the PDM front end beside
them is not ours either: it is ported from an Infineon example, and §2.5 says
so.

Please go to the source rather than to our copy:

- **DEEPCRAFT™ Studio**, which generates this code, and where you can build and
  train a model of your own —
  https://www.infineon.com/design-resources/embedded-software/deepcraft-edge-ai-solutions/deepcraft-studio
  (download: https://softwaretools.infineon.com/assets/com.ifx.tb.tool.deepcraftstudio)
- **DEEPCRAFT™ Edge AI solutions**, the product family —
  https://www.infineon.com/design-resources/embedded-software/deepcraft-edge-ai-solutions
- **Infineon's PSoC™ Edge DEEPCRAFT™ code example**, the deployment pattern this
  firmware follows and the repository where the Ready Models and their licence
  live —
  https://github.com/Infineon/mtb-example-psoc-edge-ml-deepcraft-deploy-ready-model
  (`release-v1.4.1`; the licence file is `LICENSE_Imagimob.txt`, also published
  at https://developer.imagimob.com/legal/ai-model-evaluation-license-agreement)

| | |
|---|---|
| Component | DEEPCRAFT™ Studio generated model code — motion, audio, radar |
| Author / copyright | **Imagimob AB.** That is the whole of what the files state — `model_{motion,audio,radar}.{c,h}:3` reads `Copyright © 2023- Imagimob AB, All Rights Reserved.` and nothing further. Imagimob AB is an Infineon Technologies company; that relationship is taken from Infineon's own public DEEPCRAFT™ pages linked above, **not** from the file headers, and it is recorded here because the credit belongs to both. |
| Generated by | DEEPCRAFT™ Studio / ImagiNet Compiler. Per file, from lines 2 and 5: motion `5.6.3587.65534`, generated 2025-09-25; audio `5.5.3417.65534`, generated 2025-08-20; radar `5.8.4292`, generated 2026-02-20 |
| Where it lives | `template/proj_cm55/modules/ai_models/model_{motion,audio,radar}.{c,h}` — and these **do** ship, unlike the three Ready Model archives in §2.4 |
| Licence text in the files | `All Rights Reserved` — a reservation, with no grant written into the source |
| TESAIoT's rights in them | **None.** Not authored here, not trained here, not owned here. |

### TESAIoT's position, stated plainly

TESAIoT holds **no rights** in these three models and passes **none** on. They
came out of Infineon's tool and they carry Imagimob's copyright. Anything
generated by DEEPCRAFT™ Studio, or derived from a DEEPCRAFT™ model, is
Imagimob's and Infineon's. Any terms for it are theirs to set, and none are set
here.

The Apache-2.0 grant in this repository's `LICENSE` covers the code this project
wrote. **It does not reach inside these files**, and nothing here may be read as
TESAIoT licensing them to anyone.

Our use is **research and teaching**, and **not commercial**. Note what this
entry does *not* say: no licence grant accompanies these files. The header
reserves all rights and states nothing further, so there are no terms here to
use them under, and this entry **credits** Imagimob and Infineon rather than
passing anything on. A reader who needs a grant — for a product, or for anything
else — must obtain it from Infineon and Imagimob directly. §2.4's two routes are
where to start.

### Where a reader might get the wrong idea

Two places in this tree can be read as a TESAIoT authorship claim. The first is
corrected; the second is a wording pattern that is still in the shipped headers,
and it is recorded here rather than described as fixed:

- `template/proj_cm55/modules/ai_models/` sits inside a repository whose own
  code is Apache-2.0. **The generated model files are the exception**, and §4.3
  records the boundary. *Corrected.*
- Documentation that calls motion, audio and radar the *built-in* or *supplied*
  models means only that the files are present in the tree. It has never meant,
  and must not be written to suggest, that this project produced them. **The
  word still ships**: `ai_engine.h:168` ("like any built-in one") is in the
  template copy, in `template/lib/edge_ai/include/` and in `dist/edge_ai/include/`,
  and `template/proj_cm55/modules/ai_models/README.md:260` uses it of a run-time
  registered row. Those are headers and are outside this file's scope to change;
  read them with this paragraph in hand. *Open.*

### Status

The source states a reservation and no grant, so this entry credits rather than
reproduces a licence. That credit is what this package can state as fact. The
licensing position for a commercial reader is Infineon's and Imagimob's to
state, not ours, and the two links above are where to ask.

## §2.3 VeriSilicon / Vivante VGLite — redistributed by Infineon

| | |
|---|---|
| Version | Inside `mtb-dsl-pse8xxgp release-v1.2.0`, at `pdl/drivers/third_party/COMPONENT_GFXSS/vsi/gcnano/VGLite/` |
| Where it lives | Not vendored. Two patches in `third_party_patches/mtb-dsl-pse8xxgp/` carry VGLite code as diff context. |
| Status | Governed by the Infineon/Cypress terms under which Infineon redistributes it. `third_party_patches/README.md` records the open question of whether the diffs may be redistributed at all, and says plainly that it is a question for counsel. |

---

## §2.4 DEEPCRAFT™ Ready Models — Siren, Cough and Factory Alarm, by Imagimob AB

### Who made these, and what they do

Three of the audio models this kit can run are not TESAIoT's work. **Siren
Detection**, **Cough Detection** and **Factory Alarm Detection** are
**DEEPCRAFT™ Ready Models**, authored by **Imagimob AB, an Infineon
Technologies company**, and published by Infineon for PSoC™ Edge. The
copyright line in the companion generated sources reads *"Copyright © 2023-
Imagimob AB, All Rights Reserved."*

They are good, and they are the reason a kit owner can hear real audio Edge AI
work on the first day. Each arrives already trained on a labelled acoustic
corpus none of us has, already quantised, and already Vela-compiled for the
Ethos-U55 NPU (`Vela 3.11.0 Optimised`), against
`ml-middleware release-v3.1.0` and `core-lib release-v1.6.0`. Without them this
package would demonstrate the Edge AI pipeline only on the motion and radar
models of this file's §2.2 — which are DEEPCRAFT™ Studio output and Imagimob's
too, not ours — and would have nothing to say about audio event detection at all.

Where to find them at the source — please start here rather than with our copy:

- **DEEPCRAFT™ Ready Models**, product page —
  https://www.infineon.com/design-resources/embedded-software/deepcraft-edge-ai-solutions/deepcraft-ready-models
- **The upstream code example** —
  https://github.com/Infineon/mtb-example-psoc-edge-ml-deepcraft-deploy-ready-model
  (`release-v1.4.1`), the ModusToolbox™ example our own
  `template/proj_cm55/Makefile:596` names as the origin. Its README lists seven
  Ready Models, including the three here by name.
- **DEEPCRAFT™ Studio**, where Ready Models are delivered and where you can
  train your own — https://www.infineon.com/design-resources/embedded-software/deepcraft-edge-ai-solutions/deepcraft-studio
  (download: https://softwaretools.infineon.com/assets/com.ifx.tb.tool.deepcraftstudio)
- **The licence itself**, `LICENSE_Imagimob.txt` in that repository, also
  published at
  https://developer.imagimob.com/legal/ai-model-evaluation-license-agreement

### TESAIoT's position, stated plainly

TESAIoT holds **no rights** in these models and passes **none** on. We
reference them under the **Imagimob AI Model Evaluation License Agreement**
and abide by its terms. Our use is research and teaching. It is not commercial
deployment — no TESAIoT product is sold on the strength of
them — and nothing in this package is offered as a licence to anyone else. The
route to a shippable audio model runs through Infineon's own tool: a model
trained in DEEPCRAFT™ Studio by whoever ships the product carries no evaluation
limit. That is route 2 below, and it is not a claim that TESAIoT has trained
one.

Where the licence and our packaging disagree, the licence wins. That is why
these three archives are named as a blocker on this package's own publication
checklist rather than quietly shipped, and why the two production routes below
lead to Infineon and Imagimob rather than to us.

| | |
|---|---|
| Component | DEEPCRAFT™ Ready Models — Siren Detection, Cough Detection, Factory Alarm Detection |
| Author / copyright | Imagimob AB, an Infineon Technologies company |
| Where it lives | `template/proj_cm55/modules/ai_models/{siren,cough,alarm}_lib_eval.a` — **in the development tree only.** They are excluded from both release packages, and their absence from each finished zip is verified on every cut. |
| Linked by | `template/proj_cm55/Makefile:651`, `LDLIBS += $(AI_MODEL_LIBS)`. That variable is built at `:633-636` from `$(wildcard ./modules/ai_models/$(m)_lib*.a)`, so a model is linked only when it is in `AI_MODELS` **and** its archive is actually present on disk. The default preset is `EDGE_AI_MODEL ?= combo` (`:449`), and `combo` is `AI_MODELS := motion audio radar cough alarm siren` (`:474`) — **all six**, not only these three. In a package built from the release zip the three archives are absent, so the wildcard matches nothing for them, those slots fall through to the weak stubs in `ai_model_slots.c`, and they report that they did not load. (`:623-631` is a comment block describing the objcopy recipe, not a link rule.) |
| Upstream | https://github.com/Infineon/mtb-example-psoc-edge-ml-deepcraft-deploy-ready-model @ `release-v1.4.1`, `proj_cm55/ready_models/CONFIG_Debug/TOOLCHAIN_GCC_ARM/` |
| **Licence** | **Imagimob AI Model Evaluation License Agreement** — that repository's `LICENSE_Imagimob.txt` (20,230 bytes, SHA-256 `516ef9bf6ace807a4e4cdec08e44f94433a35591d3a3717b683d9f15bbc86135`, retrieved 2026-08-29). The example's own code is separately under the Infineon EULA in its `LICENSE`, whose §3 defers to this agreement for these files. |
| **May a reader redistribute them?** | **No** — Evaluation Licence §2.2(c). See "What you may and may not do" below. |

### How the provenance was established

Four independent lines of evidence agree, and the last of them is exact.

1. **The archives name their own origin.** `strings` over each `.a` (2026-08-29)
   shows the objects were compiled on a Windows machine with an Infineon tools
   install — `C:/Users/Sudarsanamsa/Infineon/Tools/mtb-gcc-arm-eabi/14.2.1` —
   from `C:/W/RC1_staticLib/PSOC_Edge_Hello_World/proj_cm55/components/COMPONENT_{COUGH,ALARM}_MODEL/`
   and `C:/W/FML_lib/.../COMPONENT_SIREN_MODEL/`. They link
   `mtb_shared/ml-middleware/release-v3.1.0` and `core-lib/release-v1.6.0`, are
   marked `Vela 3.11.0 Optimised` (Ethos-U55), and export the Imagimob `IMAI_*`
   API. These are Infineon-built artefacts, not anything of ours.
2. **The upstream example matches by name.** Its README lists seven Ready
   Models, including *Cough Detection*, *Factory Alarm Detection* and *Siren
   Detection* — our three.
3. **The example ships the governing licence** as a separate top-level file,
   `LICENSE_Imagimob.txt`, distinct from the Infineon EULA that covers the
   example's own source.
4. **Byte comparison against the upstream tag settles it.** The files here are
   `release-v1.4.1`'s
   `proj_cm55/ready_models/CONFIG_Debug/TOOLCHAIN_GCC_ARM/` archives, with the
   global symbols renamed per model. Upstream `siren` hashes
   `c38d9ab6…`; ours hashes `a8a95656…`, and the difference is **exactly the
   longer symbol strings** — no code, no weights, no data changed. See "Our one
   alteration" below.

Note what is **not** the source: `Infineon/deepcraft-studio-accelerators`
(CC BY-NC 4.0 — retrieved 2026-08-29, `LICENSE.txt` opens
`Attribution-NonCommercial 4.0 International`) holds *trainable Studio projects*,
and `Infineon/deepcraft-model-zoo-for-psoc` (CC BY 4.0, plus an `EULA.txt`) holds
*vision pipelines*. Neither publishes siren, cough or alarm. Ready Models are a
third, separate product, delivered through DEEPCRAFT™ Studio under the Imagimob
evaluation agreement — which is why no open-source licence anywhere covers these
files.

### What you may and may not do — with the clause numbers, so you can check us

Everything below is quoted from `LICENSE_Imagimob.txt`. Read it yourself; it is
short, and the link is above.

**You may** evaluate these models on your own kit, for 60 days, to decide
whether to license them. Grant — §2.1:

> **Limited license for evaluation purposes only.** For a **non-renewable,
> non-extendable period of 60 days** ("Evaluation Period"), beginning with the
> installation date of a copy of the AI Model by Customer, Imagimob … grants to
> Customer a limited, non-exclusive, royalty-free, non-assignable,
> non-transferable, non-sublicensable license to use the AI Model **solely for
> the purpose to enable Customer to evaluate the AI Model's suitability for
> Customer's internal business needs** … to determine whether to purchase the
> non-evaluation version of the Licensed AI Model from Imagimob.

**You may not** ship a product that links them, republish the archives, or put
them to commercial use. Restrictions — §2.2. Customer may not, or facilitate or
allow others to:

> a) use the AI Model for any other purposes than for the Evaluation;
> b) … make more than 2 copies of the AI Model;
> c) **distribute, make commercial use of, publicly perform, or publicly display
> the AI Model without separate written permission from Imagimob.** The AI Model
> must be used and reviewed in a **secure evaluation environment** and properly
> managed at all times so as to prohibit and prevent access to the AI Model in
> violation of this Evaluation License Agreement;
> d) **port, modify, adapt, translate, make alterations to the AI Model or
> create derivative works** based upon the AI Model;
> e) decompile, reverse engineer, disassemble or otherwise attempt to derive
> source code, algorithms or the underlying structure of the AI Model …;
> f) remove any product identification, copyright or proprietary notices …; or
> g) **disclose to third parties the results of any testing, technical results
> or other performance data** relating to the Evaluation Materials without
> Imagimob's prior written consent.

**When the period ends, delete them.** §10.3:

> Upon the expiration of the Evaluation Period or upon termination … Customer
> shall **delete or destroy all copies of the AI Model** in Customer's control.

Ownership — §5.1: *"The AI Model is licensed not sold."* Term — §10.1: the
agreement *"will remain in effect for the Evaluation Period only."* Infineon's
own EULA does not widen any of this: its §3 carves out software under a separate
agreement, and this is that agreement.

### They are metered, by design — and Infineon says so publicly

Two limits run in parallel, and neither is a surprise.

**Contractual** — 60 days from installation, evaluation use only, no
distribution (§2.1, §2.2(a), §2.2(c)).

**Technical** — the licence reserves it, in the paragraph headed *Technical
restrictions*:

> Imagimob reserves the right to in the AI Model **put in place mechanisms that
> limit functionality of the AI Model in whole or part** during and after the
> Evaluation Period.

Infineon documents the effect publicly, in the code example's `README.md:16`:

> Pre-trained models that are ready for production, referred to as "Ready
> Models" … These models, when deployed on a device, are **intended specifically
> for testing purposes and come with a limited number of inferences**.

That mechanism is present and active in the three archives we hold: each
`library.o` carries a call counter and a fixed ceiling, and once the ceiling is
reached the model's dequeue entry point returns an error instead of a result for
the rest of that boot. The ceiling is a compiled-in constant and differs per
model. **The per-model constants are deliberately not printed here** —
§2.2(e) and §2.2(g) restrict disassembly and the disclosure of technical
results; they were read for compliance assessment only and are recorded outside
this published file.

The practical consequence for a kit owner: **a default build eventually stops
detecting on these three models.** That is the evaluation meter working as
Infineon documents it, not a fault in the model and not a bug in this firmware.
A build that must keep detecting needs one of the two routes below.

### The two lawful routes to production, and how to take them

Both lead to Infineon's own tools, and both are better than what this package
can give you.

1. **Buy the non-evaluation model.** §2.1 names this route in its own text: the
   Evaluation Period exists so that you can decide *"whether to purchase the
   non-evaluation version of the Licensed AI Model from Imagimob"*. Start at the
   Ready Models product page above, or through your Infineon sales contact.
   A purchased model carries no evaluation meter and no 60-day clock.
2. **Train your own in DEEPCRAFT™ Studio.**
   https://www.infineon.com/design-resources/embedded-software/deepcraft-edge-ai-solutions/deepcraft-studio
   — record and label your own audio, train, and deploy to PSoC™ Edge. A model you
   train yourself is yours: no evaluation limit, and Imagimob's published
   licensing metrics make production use on Infineon MCUs free.
   **That "free" row covers models you train yourself. It does not cover Ready
   Models** — do not read one onto the other.

This SDK is built for route 2, and the route runs through Infineon's tool, not
through us. `template/proj_cm55/modules/ai_models/model_{audio,motion,radar}.c`
are DEEPCRAFT™ Studio exports with their weights in-tree — **also Imagimob's
work, not TESAIoT's**, as this file's §2.2 records — and what they demonstrate is the *shape* a Studio
export takes once it lands in this tree, not any training done here.
`template/proj_cm55/modules/ai_models/README.md` §"Adding a model" is the
procedure for putting your own Studio export beside them. On the documentation
site, the Edge AI chapters E1–E4 and the `libbento_edge_ai.a` reference cover
select → confirm → start, parallel sets, stop/unload and diagnostics for any
model, whether it is a Ready Model, one of these three, or one you trained
yourself.

### Our one alteration, disclosed

`template/proj_cm55/Makefile:596-620` documents an `objcopy` pass over each
pristine `.a`: global symbols are renamed into a per-model namespace and
sections renamed to `.ml_weights`. The reason is mundane — all three Ready
Models export the same Imagimob `IMAI_*` entry points, so without renaming, no
two of them can be linked into one image, and the kit's Edge AI page offers all
three side by side.

We state it here because §2.2(d) prohibits altering the AI Model, and a term of
the agreement should be disclosed by us rather than discovered by Infineon. No
code, no weights and no behaviour are touched: the byte difference from upstream
is exactly the longer symbol strings (see provenance item 4). Infineon and
Imagimob are welcome to tell us they would rather we did this differently, and
we will follow whatever they prefer.

### How this package stays compliant

1. **These files are not published, and that is verified rather than assumed.**
   Distribution is prohibited by §2.2(c). It applies to the repository, to the
   release zips, and to the `sdk/html` documentation. A kit owner who wants to
   hear them running fetches them from Infineon directly, under Infineon's
   licence — the same arrangement this project already uses for SEGGER
   emUSB-Host (§2.1), and the right way round.
2. **The release gate names them, in three places.** `bento-release.sh` excludes
   `*_lib_eval.a` when the package tree is assembled, scans the assembled tree
   for the same suffix, and then scans the finished zip's own file listing —
   because the archives are binaries, and the copyright-text sweep that catches
   SEGGER source cannot see them. Checked 2026-08-29: `unzip -l` over both
   `bento-firmware-template-mtb-mpy.zip` and
   `bento-firmware-template-mtb-only.zip` finds no `_lib_eval.a`.
3. **The build still links stubs, not models.** `EDGE_AI_MODEL ?= combo` names
   all six models, but with the three archives absent the weak "slot not filled"
   definitions in `ai_model_slots.c` satisfy the link: the image builds, siren,
   cough and alarm report that they did not load, and every other model runs.
   `template/proj_cm55/modules/ai_models/README.md` is the procedure for a kit
   owner who has fetched the archives from Infineon and wants to fill those
   slots.
4. **Shipping a product with these models is a commercial conversation**, not a
   packaging change — route 1 above.


## §2.5 `audio_pdm.c` / `.h` — ported from Infineon's DEEPCRAFT™ audio example

This entry exists because the file was, briefly, listed in this document among
the parts of `modules/ai_models/` that are TESAIoT's. It is not. It is a port of
Infineon's code, and the file has said so from the day it was written.

| | |
|---|---|
| Where it lives | `template/proj_cm55/modules/ai_models/audio_pdm.{c,h}` |
| Origin | Infineon's ModusToolbox™ code example **mtb-example-psoc-edge-ml-deepcraft-deploy-audio**, its file proj_cm55/audio.c. Stated by the file itself — `audio_pdm.h:5-7`: *"Ported from Infineon's mtb-example-psoc-edge-ml-deepcraft-deploy-audio (proj_cm55/audio.c) and adapted to the BENTO AI Kit BSP."* |
| What is derived | **The data path.** `audio_pdm.c:10-14`: *"Data path is faithful to Infineon's reference (proj_cm55/audio.c): raw int16 PDM samples, ping-pong buffers filled RX_FIFO_TRIG_LEVEL at a time, no scaling here — normalization to [-1,1] happens at enqueue in ai_engine.c exactly as the reference's pdm_data_process()."* The decimation it configures (CIC/32, FIR1/3) is the reference's as well, because that is what the model was trained against — `audio_pdm.h:15-18`. |
| What is TESAIoT's | The adaptation to the AI Kit BSP and the build guards. That is an adaptation of Infineon's code, not an independent work. **This file must not appear in any list of what this project authored.** |
| Upstream | https://github.com/Infineon/mtb-example-psoc-edge-ml-deepcraft-deploy-audio |
| Licence | **Not established.** The file carries no licence header of its own, and nothing in this tree records the terms under which the port was made. Infineon's DEEPCRAFT™ code examples carry the Infineon EULA in their own repository. |
| Action | Establish the terms against the upstream repository's `LICENSE` before redistributing this file, and credit Infineon for the data path either way. This is the same class of question as §2.3, and it is not an OSS attribution matter. |

The same correction applies wherever else this project describes the contents of
that directory: `template/THIRD_PARTY.md` carries the matching row, and
`template/proj_cm55/modules/ai_models/README.md` no longer counts this file among
the plumbing that is ours.


---

# §3 — Components whose provenance is not established

The entry below asserts no licence, because none has been established. It says
what is known, what is missing, and what would settle it.

*(The emoji bitmaps sat in this section until 2026-08-29. They are Twemoji under
CC-BY 4.0, proven by byte comparison against upstream, and have moved to §1.17.)*

## §3.1 Lottie animation JSON — `Welcome.json`, provenance NOT ESTABLISHED

| | |
|---|---|
| Affected files | `template/proj_cm55/modules/page-components/_core/lottie_json/Welcome.json` (Lottie 4.8.0, 428x123). **`success_checkmark.json` was listed here until 2026-08-29 and is not affected** — it is LVGL's own example asset, see §1.4. |
| **Not** affected | `heartbeat_pulse.json`, `loading_ring.json`, `loading_spinner.json`, `notification_bell.json`, `wifi_radar.json` — all Lottie 5.7.0, 200x200, `"meta":{}` empty, descriptive English layer names (`Heart`/`PulseRing1`, `Ring1`, `Dots`/`Track`, `BellBody`, `Dot`/`Arc1`). Hand-authored in-house; no third-party claim attaches. Verified 2026-08-29. |
| **Not** affected | `template/bento_libs/claw/common/ble_nus/character_lottie.c` and `character_lottie_default.c`. First-party: the header records them as generated by `characters/tesaiot-bento/gen_lotties.py` and embedded via `xxd`. Verified 2026-08-29 — the byte arrays contain zero occurrences of `LottieFiles` and zero `"g"` generator keys, and decode to Lottie 5.7.3, 200x200. |
| What is known | `Welcome.json` carries `"meta":{"g":"LottieFiles AE 3.1.1","a":"","k":"","d":"","tc":""}` and a single layer named `"katman 2 Outlines"` — *katman* is Turkish for *layer*. It was exported from After Effects with the LottieFiles plugin, by someone other than this project. |
| What this does **not** establish | `meta.g` records the *authoring and export tool*, not the distribution source. A file exported with the LottieFiles After Effects plugin carries this string whether or not it was ever published on lottiefiles.com. It is therefore **not** evidence that these came from the LottieFiles catalogue, and **not** evidence that the Lottie Simple License applies to them. |
| The author field | The Lottie format *does* define an author key, `meta.a`, and defines **no licence key at all**. In both files `meta.a` is the empty string. A blank author is equally consistent with a catalogue download and with private artwork, so it neither confirms nor refutes the origin — and because no Lottie file anywhere can self-declare a licence, the absence of licensing metadata proves nothing. |
| Searches performed (2026-08-29) | The literal layer name `"katman 2 Outlines"` returns no match. Candidate LottieFiles pages (`/41924-welcome`, `/12546-welcome`, the `/free-animations/welcome` collections) could not be read: lottiefiles.com returns HTTP 403 to automated fetches, so no candidate's dimensions, author or licence label could be compared. No match is claimed. |
| **Licence** | **Not established.** None is asserted. |
| Why more analysis will not help | The evidence inside the files is exhausted. The format carries no licence field, and the generator string does not identify a source. Nothing further can be learned from the bytes. |
| Until then | These two files should not be published. |

**Recommended fix — regenerate them in-house.** This is the cheapest option and
the only one that ends the question permanently. The five clean animations in the
same directory were already hand-authored here, and `gen_lotties.py` already
generates the Buddy character set, so both the capability and the precedent exist
in the tree. Regenerated files are first-party, need no notice, and moot the
point below.

**Why "just cite the Lottie Simple License" is not available to us.** Even if
these files were later shown to be LottieFiles catalogue assets, the LottieFiles
terms of service, licence page and upload guidelines could not be retrieved — all
three return HTTP 403 to automated fetching. The Lottie Simple License text in
circulation appears to permit commercial redistribution without attribution, but
that reading rests on a third-party reproduction of the text plus a LottieFiles
help-centre article, not on the licence page itself. **Treat the LottieFiles
terms as unread, not as cleared.**

---

# §4 — TESAIoT's own licensing, and three things it collides with

This section is not a third-party notice. It records the state of *our* licence
metadata, because three facts in the tree contradict the licensing this package
is about to publish, and all three were found while compiling this file.

## §4.1 The rights holder is styled five different ways

`LICENSE` (Apache-2.0) and `LICENSE-BINARY.md` both need one name for the
copyright holder. The tree does not supply one. Across the 513 source files in
the publishable areas, 28 copyright lines name us, in five spellings:

| Spelling as it appears | Lines |
|---|---|
| `TESAIoT Foundation Platform` | 16 |
| `TESAIoT AIoT Foundation Platform` | 5 |
| `Assoc. Prof. Wiroon Sriborrirux (TESAIoT Platform Creator)` | 4 |
| `TESAIoT` | 2 |
| `TESAIoT Platform` | 1 |

**Decided, and no longer a placeholder.** `LICENSE:190` and
`LICENSE-BINARY.md:10` both now read **`Thai Embedded Systems Association
(TESA)`** — a legal person who can hold and enforce the right, which is what the
five spellings above could not supply: four of them name a *platform*, which is
a product, not an entity. This paragraph previously recorded the holder as the
placeholder `TESAIoT` and said a decision was outstanding; that was written at
11:20 on 2026-08-29 and the two licence files were settled at 14:23 the same
day, so the text was three hours stale rather than wrong when written.

**What remains open is the source tree, not the licence files.** The 28
copyright lines in the publishable areas still carry the five spellings
tabulated above, none of which is the name now in `LICENSE`. Aligning them is
outstanding work; until it is done, a reader comparing a source header against
`LICENSE` will find two different rights holders named.

## §4.2 Two of our own files are marked MIT and attributed to Infineon

| | |
|---|---|
| Where it lives | `template/bento_libs/claw/common/deepcraft/deepcraft_interface.h`, `template/bento_libs/claw/common/deepcraft/deepcraft_engine.c` |
| What they say | Both carry `Copyright (c) 2026 Infineon Technologies AG` and `SPDX-License-Identifier: MIT`. |
| Why it matters | They sit inside `bento_libs/claw/`, which the new `LICENSE` places under Apache-2.0. A blanket statement that "our source is Apache-2.0" is false while these two files declare otherwise, and the declaration also assigns the copyright to Infineon rather than to us. |
| What is **not** established | Whether these files genuinely derive from MIT-licensed Infineon code, or whether the header was copied in from a template and is simply wrong. Nothing in the tree records their origin. |
| Action | Settle it before publication, and do not resolve it by deleting the header. If they are derived, the notice must stay and `LICENSE` must carve them out. If the header is wrong, correct it deliberately and say so. Overwriting an upstream notice with our own SPDX tag would be a false licence claim, which is the one error in this area that cannot be walked back quietly. |


## §4.3 The Apache-2.0 grant stops at the generated model files

| | |
|---|---|
| Where it lives | `template/proj_cm55/modules/ai_models/model_{motion,audio,radar}.{c,h}` |
| What they say | `Copyright © 2023- Imagimob AB, All Rights Reserved.` — see §2.2. |
| Why it matters | These six files ship inside a repository whose `LICENSE` is Apache-2.0. They are **DEEPCRAFT™ Studio output, authored by Imagimob AB, an Infineon Technologies company**, and TESAIoT neither trained nor owns them. A blanket reading of "everything in this repository is Apache-2.0" would extend a grant over code this project has no right to license. |
| What is settled | The credit. §2.2 names the author, the tool that generated the files, the per-file compiler version and generation date, and Infineon's own public pages and repository for the tool, the models and the licence. |
| Action | Keep the carve-out visible wherever the Apache-2.0 grant is described — `README.md` §Licensing and `template/THIRD_PARTY.md` both state it. Never add an SPDX header or a TESAIoT copyright line to these files: the rule in `README.md` §"SPDX header convention" applies to them exactly, and a tag added over Imagimob's notice would be a false licence claim. Commercial use of these models, or of anything derived from them, is a conversation with Infineon and Imagimob, not a packaging change. |

---

# Appendix — how each entry was verified

| Component | Licence text source | Verified against upstream? |
|---|---|---|
| littlefs | fetched `littlefs v2.11.0/LICENSE.md` | yes |
| Bosch BMI270 | fetched `BMI270_SensorAPI/LICENSE` (default branch) | yes, but the branch is not pinned to the v2.86.1 tag |
| lwIP | fetched `lwip STABLE-2_1_2_RELEASE/COPYING` + in-tree header | yes |
| LVGL | fetched `lvgl v9.5.0/LICENCE.txt`, SHA-256 recorded | yes, byte-for-byte |
| MicroPython | in-tree port `LICENSE`, SHA-256 recorded | yes, against the tree this package builds against; no upstream tag is pinned |
| FreeRTOS | fetched `FreeRTOS-Kernel V10.6.2/LICENSE.md` | yes |
| coreMQTT | fetched `coreMQTT v2.1.1/LICENSE` | text yes; the **version** could not be matched, because the Infineon asset tag `release-v4.7.0` is not an upstream coreMQTT tag |
| jsmn | fetched `zserge/jsmn` default-branch `LICENSE` + in-tree header | yes; the two spell the author's name differently and both are reproduced |
| TinySoundFont | in-tree header, `tsf.h:22-40` | no — quoted from the vendored file only |
| jQuery | fetched `jquery 3.6.0/LICENSE.txt` | yes |
| Doxygen output JS | in-tree `@licstart` block | no — quoted from the vendored file only |
| doxygen-awesome | in-tree `tools/doxygen-awesome/LICENSE` | no; and the in-tree copy's year range is stale against the CSS |
| Noto Sans Thai | fetched `notofonts/thai/OFL.txt`, SHA-256 recorded; version and copyright read from the font's own `name` table | yes |
| minimp3 | in-tree header | no — quoted from the vendored file only; CC0 owes nothing regardless |
| Mbed TLS | in-tree headers; full Apache-2.0 text at `template/bsps/TARGET_KIT_PSE84_AI/LICENSE` | notice yes, upstream NOTICE file not retrieved |
| CMSIS / Arm | in-tree headers; full Apache-2.0 text as above | notice yes, upstream NOTICE file not retrieved |
| DEEPCRAFT Ready Models (`*_lib_eval.a`) | fetched `LICENSE_Imagimob.txt` + `LICENSE` + `README.md` from `Infineon/mtb-example-psoc-edge-ml-deepcraft-deploy-ready-model`, SHA-256 recorded | yes — and corroborated against the archives' own build strings and against Infineon's public README statement of the inference limit |
| Twemoji emoji graphics | fetched `jdecked/twemoji` `README.md` + `LICENSE` + `LICENSE-GRAPHICS` | yes — and the 18 bitmaps were `cmp`-compared byte-for-byte against upstream `assets/72x72/` (18/18 identical) and shown NOT to match Noto |
| Lottie JSON (`Welcome`) | — | **no licence established**; lottiefiles.com returns HTTP 403 to automated fetching, so its terms were never read. Excluded from both packages. (`success_checkmark.json` is LVGL's, MIT — §1.4.) |
