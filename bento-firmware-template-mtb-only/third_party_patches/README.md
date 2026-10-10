# Patched ModusToolbox assets

Eleven files across five assets need local changes that `make getlibs` does
not provide. The diffs are in this directory: one directory per asset, `NNNN-`
ordering inside it, a `series` file giving the order they apply in, and a
header on every patch saying what it changes and why.

## Applying

Run this from the workspace that holds `mtb_shared/`, after `make getlibs` in
each project. `<patches>` is this directory. The parentheses keep a failure
from closing your shell; on a second run `patch` reports each patch as
previously applied, which is expected.

```bash
( cd <workspace>/mtb_shared &&
  for p in $(cat <patches>/series); do
      patch -p1 -F0 --forward < "<patches>/$p" || exit 1
  done &&
  shasum -a 256 -c <patches>/PATCHED.sha256 )
```

`-F0` is not optional. GNU patch's default fuzz factor is 2: it will ignore two
lines of context at each end of a hunk to find somewhere to apply it, and still
exit 0. With `-F0` a hunk applies exactly where it was made or not at all.

`PATCHED.sha256` is the other half. The exit status tells you a patch went in
somewhere; the digest tells you it went in correctly. The build checks the same
digests before it compiles anything (`verify_asset_patches.sh`, at the root of
the package) and names any file that is absent or different.

## What is here

Ten of the eleven fail **silently** when absent or misapplied. Each patch's
header says what breaks without it. The three that matter most:

| Patch | Without it |
|---|---|
| `secure-sockets/0003-bind-optiga-key-to-tls-session` | builds and runs; mTLS falls back to a software key and the broker rejects the device |
| `mtb-dsl-pse8xxgp/0001-bound-the-gpu-wait` | a missed GPU interrupt deadlocks the display for ~49.7 days instead of timing out in 5 s |
| `lwip-network-interface-integration/0001-stop-advertising-a-dns-server-on-softap` | clients flood UDP 53; the ICMP replies exhaust SDPCM TX credits and all outgoing TCP stalls |

Only `secure-sockets/0002` stops a build.

Each patch carries the licence of the file it modifies.
