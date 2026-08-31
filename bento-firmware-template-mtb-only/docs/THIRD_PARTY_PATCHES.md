# Patched ModusToolbox assets

This firmware does not build against a stock `mtb_shared`. Six assets carry
local changes — twelve files — and `make getlibs` fetches the pristine upstream
of every one of them.

Found 2026-08-28 by running `getlibs` into an empty workspace and diffing the
result against this bench. Until then every build, including the one that
produced the released package, had used a workspace where the patches were
already applied, so nothing ever failed and nothing ever said why.

`template/common.mk` now stops the build before the first object is compiled if
the patches are absent. The diffs are in `third_party_patches/`.

## Why this matters more than it looks

Only one of the twelve stops a build. The rest change behaviour, and their
absence is silent:

| If missing | What a customer sees |
|---|---|
| `cy_tls_optiga_key.c` | build stops in ninja on a missing file — the loud one |
| `cy_tls.c` | build succeeds; the OPTIGA key never binds to the TLS session, mTLS falls back to a software key, and the broker rejects the device |
| `pk_internal.h` | build stops on `priv_id` / `grp` — mbedtls 3.6 made those private |
| `vg_lite.c` | a missed GPU interrupt deadlocks the display for ~49.7 days instead of timing out in 5 s |
| `vg_lite_hal.c` | GPU events are lost to a read/clear race against the ISR |
| `cy_lwip_dhcp_server.c` | SoftAP advertises a DNS server it does not have; clients flood UDP 53, ICMP replies exhaust SDPCM TX credits, and all outgoing TCP stalls |

## The twelve files

### secure-sockets 3.12.1 — the OPTIGA mTLS binding

| File | Change |
|---|---|
| `source/COMPONENT_MBEDTLS/cy_tls.c` | 154 lines. Holds the client certificate and the opaque PK context for the session and binds the OPTIGA-backed PSA key into the handshake. Carries diagnostics, off by default. |
| `source/COMPONENT_MBEDTLS/cy_tls_optiga_key.c` | new, 13 lines. Stores the PSA key id the OPTIGA driver produced, for `cy_tls.c` to read. |
| `source/COMPONENT_MBEDTLS/include/cy_tls_optiga_key.h` | new, 7 lines. Its two declarations. |

`proj_cm33_ns/Makefile` compiles the second of these directly out of
`mtb_shared`, which is why its absence is the failure a customer meets first.

### ifx-mbedtls 3.6.400 — PSA opaque keys

| File | Change |
|---|---|
| `library/pk_internal.h` | 2 lines. `pk->priv_id` and `grp.id` wrapped in `MBEDTLS_PRIVATE()`. Required to compile. |
| `library/pk_wrap.c` | Tracing for which ECDSA variant and hash TLS asks the secure element for, and the deterministic-to-randomised fallback. `PK_DIAG 0` by default, `printf` defined away. Diagnostic only. |
| `include/psa/internal_trusted_storage.h` | new, 27 lines. `MBEDTLS_PSA_CRYPTO_SE_C` needs the ITS interface to exist; the RAM implementation is ours, in `psa_its_ram_stubs.c`. |
| `include/psa/error.h` | new. Companion to the above. |

### mtb-dsl-pse8xxgp 1.2.0 — VGLite

| File | Change |
|---|---|
| `.../VGLite/vg_lite.c` | Default wait becomes 5000 ms instead of `VG_LITE_INFINITE`, and a timeout is handled rather than waited on forever. |
| `.../VGLiteKernel/rtos/vg_lite_hal.c` | Read-and-clear of `int_flags` inside a critical section, so the ISR cannot modify it between the read and the clear. |

### The remaining two

| File | Change |
|---|---|
| `lwip-network-interface-integration 1.7.1 .../cy_lwip_dhcp_server.c` | Stops advertising DHCP option 6 on SoftAP. See the table above. |
| `aws-iot-device-sdk-port 2.7.0 .../cy_aws_tcpip_port_secure_sockets.c` | Root certificates are no longer freed while the connection still needs them, plus commented-out diagnostics. |

`wifi-resources 3.0.5` also differs, in the CYW55513 SoM NVRAM text file
(2 lines). It is board calibration data, not code, and is listed here for
completeness rather than as a patch to apply.

## Applying them

```bash
cd <workspace>/mtb_shared
for p in $(cat <patches>/series); do patch -p1 -F0 --forward < "<patches>/$p" || exit 1; done
shasum -a 256 -c <patches>/PATCHED.sha256
```

`-p1`, not `-p0`: the diffs carry the usual `a/` and `b/` prefixes, and their
paths are otherwise relative to `mtb_shared/`. `--forward` makes a second run a
no-op instead of offering to reverse them.

**`-F0` matters more than it looks.** GNU patch's default fuzz factor is 2, so
it will ignore the first two and last two lines of a hunk's context to find
somewhere to apply it — and then exit 0. Its own CAVEATS say the result is
"guaranteed to be correct only when the patch is applied to exactly the same
version of the file that the patch was generated from", and that compiling
cleanly "is a pretty good indication that the patch worked, but not always".
Yocto's `patch-fuzz` QA check exists for exactly this and says outright that
"it is entirely possible for an incorrectly patched file to still compile
without errors". Buildroot's own apply script uses `-F0` for the same reason.

Ten of these eleven patches fail *silently* if they are wrong, so a hunk landing
in the wrong place is the failure mode that would cost the most to find.
`-F0` refuses rather than guesses.

`PATCHED.sha256` is the second half of that: it is the SHA-256 of all eleven
files as they should end up. Checking it proves the patches landed where they
were meant to, which the patch exit status alone does not.

```bash
cd <workspace>/mtb_shared
shasum -a 256 -c <patches>/PATCHED.sha256
```

Verified 2026-08-28 by running `getlibs` into an empty workspace, applying all
eleven, and diffing every one of the eleven files against this bench: identical.

The diffs are generated from each asset's own git history
(`git show HEAD:<path>`) rather than from a second checkout. A second checkout
is easy to patch by accident, and then the diff comes out empty — which is what
happened on the first attempt here, and seven of the eleven were written as
0-byte files that applied silently and did nothing.

## The decision

**The professor ruled 2026-08-28: the patches ship with both packages.** The
basis: Buildroot ships patches even for packages it marks non-redistributable
and states they carry the patched work's licence; Infineon's own meta-freescale
layer publishes patches against EULA'd NXP sources. The customer fetches the
pristine sources from Infineon via `getlibs`; these diffs are the delta that
makes the firmware correct.

The cleaner end state — forking the five assets and repointing the `.mtb`
files so no patch step exists at all — remains open, blocked only on where the
forks would be hosted.
