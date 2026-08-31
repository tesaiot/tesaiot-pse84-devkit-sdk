# The three Infineon slides — what each one is good for

All three came from Infineon and all three are correct about something. They are
not interchangeable, and they disagree in one place. The filenames say what each
is authoritative *for*, because that is the question that keeps coming up.

| file | authoritative for | evidence behind it |
|---|---|---|
| `infineon-01-slot-map-cert-key-pairing.png` | which slot is which, and which certificate belongs with which key | the project's own pre-provisioning plan — **and confirmed on hardware**: enrolling into 0xE0F1 / 0xE0E1 gives `PAIR: True` |
| `infineon-02-oid-metadata-settings.png` | metadata values — Change, MUD, Read — as ready-to-use hex | the only source that gives these at all |
| `infineon-03-protected-update-verified-run.png` | that Protected Update works, and with which OIDs | **the only slide with a real on-chip run log**: `Manifest accepted` → `Fragment accepted` → `SUCCESS` |

## Where they disagree: the trust anchor

- Slide 01 — `E0E8` is the Trust Anchor, `E0E9` is the TESA CA
- Slides 02 and 03 — `0xE0E9` is the Trust Anchor

Slide 03 carries more weight because it is empirical: that workflow ran on a
chip and finished. Slide 01 is a plan, which may have been written before the
configuration settled.

**Do not resolve this from the slides.** Read the MUD off the chip — it is the
only statement of what *this* device actually enforces:

```python
optiga.init()
print(optiga.read_metadata(0xE0F1))   # target: MUD names its trust anchor
print(optiga.read_metadata(0xE0E8))
print(optiga.read_metadata(0xE0E9))
optiga.deinit()
```

Documents state intent; the chip states fact. A wrong trust anchor fails manifest
verification with no diagnostic, which is the hardest class of failure to chase —
this project lost most of 2026-08-05 to two bugs of exactly that shape.

## Settled on hardware, 2026-08-06 — the chip has no MUD at all

The advice above was to read the MUD rather than pick a slide. Done, and the
answer is neither `E0E8` nor `E0E9`:

```
0xE0F1  2011 c00101 d003e1fc07 d30100 e00103 e10111     target key — no D8 tag
0xE0F3  200e c00101 d003e1fc07 d30100 e00103            target key — no D8 tag
0xE0E8  2019 c00101 c40204b0 c50204b0 d003e1fc07 d10100 d30100 e80111
0xE0E9  2019 c00101 c40204b0 c50204b0 d003e1fc07 d10100 d30100 e80111
0xF1D4  2011 c00101 c4018c c50164 d003e1fc07 d10100
0xE0E1  2019 c00101 c40206c0 c50201f3 d003e1fc07 d10100 d30100 e80112
```

**No object carries a `D8` (MUD) tag.** The MUD that names a trust anchor lives
on the *target*, and `0xE0F1` does not have one. So the chip is not holding an
opinion the slides disagree with — nobody has told it yet.

That turns the question inside out. Setting the MUD is a step *we* perform
(slide 03, Step 3), and whichever anchor we write into it becomes the answer.
`E0E8` versus `E0E9` is a **decision to record**, not a fact to discover. Both
slots are already `E8=0x11` (Trust Anchor) holding 1200 bytes, so either works
mechanically — what settles it is which key signs the platform's manifests.

Two other things the read confirmed:

- **Protected Update cannot work as the chip stands.** No MUD on the target means
  the chip has nothing to verify a manifest against — the failure the reference
  code notes as error `0x8007`. Slide 03's Steps 0–3 are the missing prerequisite,
  not optional setup.
- **Everything is still writable.** Every object is `LcsO = 0x01` with
  `Change: LcsO < 0x07`, so metadata can be set directly and does not itself need
  a Protected Update. Nothing is locked yet.

And an independent confirmation that enrolment landed: `0xE0E1` reports
`C5 02 01F3` — 499 bytes used, exactly the DER written during `bento-enrol-3`,
with `E8 01 12` marking it a device certificate.

---

## Resolved 2026-08-06 — the slides and the contract describe different operations

They never disagreed. They answer different questions, and reading one as an
answer to the other is what cost this project two days.

| | slides 02 and 03 | PROTECTED_UPDATE_CONTRACT.md |
|---|---|---|
| what is updated | an **ECC key** | a **certificate** |
| target | `E0F1`-`E0F3` (key slots) | `E0E1`-`E0E3` (certificate slots) |
| trust anchor | `E0E9` | `E0E8` |
| confidentiality | required, secret in `F1D4` | not used |
| manifest payload type | `ePAYLOAD_KEY` | `ePAYLOAD_DATA` |

Slide 02 says so in as many words — `Int-0xE0E9 && Conf-0xF1D4: For ECC key
Protected update` — and slide 03's verified run targets `0xE0F3`, a key slot, and
writes a secret to `F1D4`. Both describe key rotation. Certificate delivery is
the other configuration of the same mechanism.

**Slide 01 agrees with the contract throughout**: `E0E1` device certificate,
`E0F1` private key, `E0E8` trust anchor for Protected Update. Nothing to
reconcile there.

### So E0E8 or E0E9?

Neither, as a fact. The trust anchor is **whatever the target object's metadata
names**, and the device writes that metadata — so it is a choice to record, and
the choice has to match on four things at once: the certificate in the anchor
slot, the anchor OID, the target's Change access condition, and the OID the
platform puts in the manifest `kid`.

This project uses `E0E8`, the platform's default. Confirmed on hardware: the
580-byte signing certificate written there and read back with the public key
matching, and the manifest `kid` decoding to `E0E8`.

### What the slides remain the only source for

- **The metadata values as usable hex** — Change, MUD and Read per OID (slide 02).
- **How to Protected Update a key slot**, which certificate delivery does not
  exercise: anchor `E0E9`, confidentiality secret in `F1D4`, target `E0F1`-`E0F3`.
  Rotating a key in the chip means coming back to these two slides.

> An earlier revision of this note leaned toward `E0E9` on the strength of slide
> 03 carrying a real on-chip log. The log is real; it is a log of key rotation.

---

## The distinction that caused the most confusion

These slides answer two different questions, and mixing them up cost real time:

- **Certificates** live in `0xE0E0`–`0xE0E3` and are written as plain data. The
  platform signs a CSR and publishes the PEM to `commands/certificate`; the
  firmware chooses the slot. Slide 01 governs this. No trust anchor involved.
- **Protected Update** targets **key** slots — `0xE0F1`–`0xE0F3` — and needs a
  manifest signed against a trust anchor, plus a confidentiality secret in
  `0xF1D4`. Slides 02 and 03 govern this.

So the `target_oid` default of `E0F1` in `PROTECTED_UPDATE_CONTRACT.md` is
correct: Protected Update updates a key, not a certificate. An earlier reading of
it here as a mistake was wrong.

One more thing the chip does not do: it enforces **no** binding between a
certificate slot and a key slot. Both are ordinary objects, and the only metadata
that binds anything is the MUD linking a Protected Update target to its trust
anchor. `Key N ↔ Cert N` is provisioning convention. That is why writing a
certificate to the wrong slot succeeds silently and only surfaces later as a
CertificateVerify failure that reads like a server rejection.
