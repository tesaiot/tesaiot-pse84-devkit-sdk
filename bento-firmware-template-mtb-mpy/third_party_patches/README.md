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
```
