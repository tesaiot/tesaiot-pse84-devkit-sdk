# Patched ModusToolbox assets

Eleven files across five assets, in a layout that follows Buildroot and
Debian: one directory per asset, `NNNN-` ordering inside it, a `series` file
for the declarative order, and a header on every patch.

**Generated, not hand-maintained.** `tools/gen_asset_patches.sh` rebuilds the
whole set from `tools/asset_patches.tsv` and this bench's `mtb_shared`. The
pristine side of every diff comes from the asset's own git history
(`git show HEAD:<path>`), never from a second checkout — a second checkout is
easy to patch by accident, and then the diff comes out empty. That is not
hypothetical: the first attempt here shipped seven 0-byte patches, and an empty
patch applies silently and does nothing.

## Applying

```bash
cd <workspace>/mtb_shared
for p in $(cat <patches>/series); do
    patch -p1 -F0 --forward < "<patches>/$p" || exit 1
done
shasum -a 256 -c <patches>/PATCHED.sha256
```

`-F0` is not optional. GNU patch's default fuzz factor is 2: it will ignore two
lines of context at each end of a hunk to find somewhere to apply it, and exit
0. Its own CAVEATS say the result is correct "only when the patch is applied to
exactly the same version of the file that the patch was generated from", and
that compiling cleanly "is a pretty good indication that the patch worked, but
not always". Yocto's `patch-fuzz` QA check exists for this and says outright
that "it is entirely possible for an incorrectly patched file to still compile
without errors". Buildroot's apply script uses `-F0`. So do we.

`PATCHED.sha256` is the other half. Exit status tells you the patch went in
somewhere; the digest tells you it went in *correctly*. The build gate checks
it — see `template/verify_asset_patches.sh`.

## What is here

Ten of the eleven fail **silently** when absent or misapplied. Each patch's
header says what breaks without it. The three that matter most:

| Patch | Without it |
|---|---|
| `secure-sockets/0003-bind-optiga-key-to-tls-session` | builds and runs; mTLS falls back to a software key and the broker rejects the device |
| `mtb-dsl-pse8xxgp/0001-bound-the-gpu-wait` | a missed GPU interrupt deadlocks the display for ~49.7 days instead of timing out in 5 s |
| `lwip-network-interface-integration/0001-stop-advertising-a-dns-server-on-softap` | clients flood UDP 53; the ICMP replies exhaust SDPCM TX credits and all outgoing TCP stalls |

Only `secure-sockets/0002` stops a build.

## The better end state, when a decision is made

Patching in place fights ModusToolbox rather than using it. The tools guide
says `make getlibs` "requires the repos to be clean (that is, all changes must
be committed)" — and patching makes six of them dirty. On this bench right now
`ifx-mbedtls` has 5 modified files and `secure-sockets` 3.

A `.mtb` file is just `<URL>#<tag>#<location>`, and the Library Manager guide
puts no restriction on the host: "A URL to a git repository somewhere that is
accessible by your computer". Forking the five assets and repointing the `.mtb`
files removes the patch step entirely — the modification simply *is* the
checked-out code, the repos stay clean, and absence becomes impossible rather
than silent.

All five have a direct `.mtb` in this template, so the change is five one-line
edits. A `.mtb` for a direct dependency belongs in the application's `deps/`
directory, not `libs/`.

What blocks it is not technical: a fork means **we** host Infineon- and
Cypress-licensed source. A patch series does not — the customer fetches the
pristine source from Infineon themselves and we ship only the delta. That is a
licensing decision, and it is recorded as open in `docs/KNOWN_ISSUES.md`.

## Licensing, honestly

Buildroot's manual states the position we are relying on: "Patches are under
the same license as the files that they modify… Those patches are not covered
by the license of Buildroot." Buildroot ships patches for packages it marks
non-redistributable, and Infineon's own `meta-freescale` layer publishes
patches against NXP sources under an EULA that forbids redistributing the
source. So the practice is established, including by the vendor.

That is practice, not permission. No court has ruled on whether a diff is a
derivative work of the file it patches — the Software Freedom Conservancy says
so in as many words — and the governing document here is the Infineon/Cypress
EULA rather than copyright doctrine. **This is a question for counsel, and
nothing in this file is legal advice.**
