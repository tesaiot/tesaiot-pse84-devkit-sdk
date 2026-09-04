/* sdk-example: core=cm33 variant=mtb-mpy group=security
 * id:      cm33/security/02_model_signature_hook
 * title:   Replace the staged-model signature check with your own
 * teaches: how to override a WEAK symbol the archive exports, and how to return
 *          a verdict that does not pretend to know more than it does
 * apis:    optiga_verify_staged_model
 * entry:   example_mpy_secure_model_signature_hook
 */

/*******************************************************************************
 * WHAT THIS HOOK IS FOR
 * ---------------------
 * A model can be uploaded to the board at run time. Before CM55 hands it to
 * TensorFlow-Lite Micro, the loader asks one question:
 *
 *     int optiga_verify_staged_model(const uint8_t *blob, uint32_t blob_len);
 *
 * The answer is reported as stage_info()['sig_rc']. This matters because
 * mtb_ml_model_init() validates nothing: a malformed blob becomes pointer
 * arithmetic inside AllocateTensors on the core that owns the display.
 *
 * libbento_mpy.a exports a WEAK default that always answers
 * AI_STAGE_SIG_NO_VERIFIER (-10) — "nobody checked", which is honest and is not
 * the same as "checked and fine". dist/mpy_secure/overridable.txt names this
 * symbol for exactly that reason. Define your own and yours wins at link time,
 * with no flag to set and nothing to register.
 *
 * ONE LINK-TIME HAZARD, AND IT IS A HARD ERROR
 * --------------------------------------------
 * Weak-vs-strong resolves silently; STRONG-vs-STRONG does not. The full BENTO
 * firmware compiles a strong definition of this symbol in
 * optiga_trust_helpers.c (the one that verifies ECDSA P-256 inside the secure
 * element). Link this file into a project that also has that one and you get a
 * multiple-definition error. That is the good outcome — it tells you two
 * policies were fighting over one decision. Pick one.
 *
 * WHAT A HONEST OVERRIDE LOOKS LIKE
 * ---------------------------------
 * The verdict codes are deliberately distinct, and collapsing them is the
 * mistake this hook exists to prevent: "no signature", "malformed", "the chip
 * said no" and "could not ask the chip" call for four different responses, and
 * a bus failure that reads as an attack sends you debugging the wrong system.
 *
 *     +1   verified. Return this ONLY after a real cryptographic check.
 *     -1   no trailer — the blob is unsigned
 *     -2   trailer present but magic, algorithm or CRC is wrong
 *     -3   could not hash
 *     -4   the signature did not verify
 *     -5   could not reach the verifier
 *    -10   AI_STAGE_SIG_NO_VERIFIER: this build cannot check signatures
 *
 * The implementation below does every check it genuinely can — length, magic,
 * algorithm, trailer CRC — and then returns -10 rather than +1, because it has
 * no key and no crypto engine. Replace that last step with your verification
 * and return +1 there, and only there. A hook that returns +1 because the CRC
 * matched has turned a signature into a checksum, which is what
 * ipc_model_stage_defs.h spends a paragraph warning against: anyone can
 * recompute a CRC over anything.
 *
 * Pair it with EDGE_AI_REQUIRE_SIGNED so a board that cannot verify refuses to
 * load rather than loading unverified.
 *
 * SIGNATURE PROVENANCE: confirmed identical in three places —
 *   BENTO-TESAIoT-libraries/claw/common/mpy/modedgeai.c:67  (extern + weak def)
 *   BENTO-TESAIoT-libraries/claw/kit-pse84-ai/modules/tesaiot/
 *       optiga_trust_helpers.c:4526                          (the strong one)
 *   dist/tesaiot_hsm/include/optiga_trust_helpers.h          (shipped decl)
 ******************************************************************************/

#include <stdio.h>
#include <string.h>

#include "ipc_model_stage_defs.h"   /* ai_stage_header_t, ai_stage_sig_t,
                                     * ai_stage_sig_offset(), ai_stage_crc32(),
                                     * AI_STAGE_SIG_* and AI_STAGE_SIG_NO_VERIFIER */

#include "../sdk_examples_cm33.h"

/* The verdicts, spelled out. ipc_model_stage_defs.h defines only the -10; the
 * rest are the loader's own contract and are named here so the code reads. */
#define EX_SIG_OK           (1)
#define EX_SIG_ABSENT      (-1)
#define EX_SIG_MALFORMED   (-2)

/* ── THE OVERRIDE ─────────────────────────────────────────────────────────
 * No __attribute__((weak)) here: this must be strong so it wins over the
 * archive's weak default. No `static` either — a static one would not be the
 * symbol the loader calls, and the build would look fine while changing
 * nothing. That silent no-op is the most likely way to get this wrong.
 */
int optiga_verify_staged_model(const uint8_t *blob, uint32_t blob_len);

int optiga_verify_staged_model(const uint8_t *blob, uint32_t blob_len)
{
    /* Bad arguments are malformed input, not a missing signature. Saying
     * "unsigned" here would let a truncated download look like an ordinary
     * unsigned model. */
    if (blob == NULL || blob_len < sizeof(ai_stage_header_t)) {
        return EX_SIG_MALFORMED;
    }

    const ai_stage_header_t *h = (const ai_stage_header_t *)(const void *)blob;

    /* Where the trailer would be, if there is one. The helper is a static
     * inline in the shared header precisely so both cores compute it from one
     * source — do not re-derive it. */
    const uint32_t sig_off = ai_stage_sig_offset(h);

    /* 64-bit arithmetic on purpose: model_offset and model_bytes come off the
     * wire, and a 32-bit sum of two attacker-chosen values wraps. */
    if ((uint64_t)sig_off + sizeof(ai_stage_sig_t) > (uint64_t)blob_len) {
        return EX_SIG_ABSENT;               /* no room for a trailer */
    }

    const ai_stage_sig_t *t =
        (const ai_stage_sig_t *)(const void *)(blob + sig_off);

    if (t->magic != AI_STAGE_SIG_MAGIC) {
        return EX_SIG_ABSENT;               /* an ordinary unsigned blob */
    }
    if (t->alg != AI_STAGE_SIG_ALG_ECDSA_P256) {
        return EX_SIG_MALFORMED;            /* signed, but not in a way we know */
    }

    /* The trailer's own CRC, over every byte above footer_crc32. This proves
     * the trailer arrived intact. It proves NOTHING about who wrote it. */
    {
        const uint32_t n = (uint32_t)offsetof(ai_stage_sig_t, footer_crc32);
        if (ai_stage_crc32(0u, (const uint8_t *)t, n) != t->footer_crc32) {
            return EX_SIG_MALFORMED;
        }
    }

    /* ── YOUR VERIFICATION GOES HERE ──────────────────────────────────────
     *
     * You have: t->sig (raw r||s, 32 bytes each), t->key_id (8 bytes, which
     * signing key), and the signed region — blob[0 .. sig_off), manifest AND
     * model together, because signing only the model would leave the window
     * geometry, the training rate and the axis convention unauthenticated.
     *
     *     uint8_t digest[32];
     *     if (sha256(blob, sig_off, digest) != 0) return -3;
     *     switch (ecdsa_p256_verify(pubkey_for(t->key_id), digest, t->sig)) {
     *         case VERIFIED:    return EX_SIG_OK;   //  +1, and only here
     *         case REJECTED:    return -4;          //  the key said no
     *         default:          return -5;          //  could not ask
     *     }
     *
     * Until that exists, say so. Returning +1 from here would mean this hook
     * had quietly redefined "signed" as "has a valid CRC". */
    return AI_STAGE_SIG_NO_VERIFIER;
}

/* ── Exercising it ────────────────────────────────────────────────────────── */

static const char *verdict_str(int rc)
{
    switch (rc) {
        case EX_SIG_OK:                  return "verified";
        case EX_SIG_ABSENT:              return "unsigned (no trailer)";
        case EX_SIG_MALFORMED:           return "malformed";
        case AI_STAGE_SIG_NO_VERIFIER:   return "no verifier on this board";
        default:                         return "other";
    }
}

/* A manifest with no model and no trailer — the smallest well-formed blob. */
static uint8_t s_blob[sizeof(ai_stage_header_t) + sizeof(ai_stage_sig_t) + 8u];

int example_mpy_secure_model_signature_hook(void);

int example_mpy_secure_model_signature_hook(void)
{
    printf("\r\n--- mpy_secure/10_model_signature_hook ---\r\n");
    printf("  manifest=%u B  trailer=%u B\r\n",
           (unsigned)sizeof(ai_stage_header_t), (unsigned)sizeof(ai_stage_sig_t));

    /* 1. Too short to be a manifest at all. */
    int rc = optiga_verify_staged_model(s_blob, 16u);
    printf("  truncated blob        -> %3d  (%s)\r\n", rc, verdict_str(rc));

    /* 2. NULL. A verifier that faults on NULL is a verifier an attacker can
     *    turn into a crash. */
    rc = optiga_verify_staged_model(NULL, 0u);
    printf("  NULL blob             -> %3d  (%s)\r\n", rc, verdict_str(rc));

    /* 3. A manifest with a zero-length model and no trailer. */
    memset(s_blob, 0, sizeof(s_blob));
    {
        ai_stage_header_t *h = (ai_stage_header_t *)(void *)s_blob;
        h->model_offset = (uint32_t)sizeof(ai_stage_header_t);
        h->model_bytes  = 0u;
    }
    rc = optiga_verify_staged_model(s_blob, (uint32_t)sizeof(ai_stage_header_t));
    printf("  manifest, no trailer  -> %3d  (%s)\r\n", rc, verdict_str(rc));

    /* 4. A well-formed trailer: right magic, right algorithm, correct CRC, and
     *    a signature of zeroes. Everything this hook can check passes, and it
     *    still does not claim the model is verified — which is the behaviour
     *    the whole file exists to demonstrate. */
    {
        ai_stage_header_t *h = (ai_stage_header_t *)(void *)s_blob;
        const uint32_t off = ai_stage_sig_offset(h);
        ai_stage_sig_t *t = (ai_stage_sig_t *)(void *)(s_blob + off);
        memset(t, 0, sizeof(*t));
        t->magic = AI_STAGE_SIG_MAGIC;
        t->alg   = AI_STAGE_SIG_ALG_ECDSA_P256;
        t->footer_crc32 = ai_stage_crc32(0u, (const uint8_t *)t,
                                         (uint32_t)offsetof(ai_stage_sig_t,
                                                            footer_crc32));
        rc = optiga_verify_staged_model(s_blob, off + (uint32_t)sizeof(*t));
        printf("  valid trailer, no key -> %3d  (%s)\r\n", rc, verdict_str(rc));

        if (rc == EX_SIG_OK) {
            printf("  this hook returned 'verified' without verifying "
                   "anything — do not ship it\r\n");
            return SDK_EX_REFUSED;
        }

        /* 5. Corrupt one byte of the trailer and watch the verdict change from
         *    "unsigned" to "malformed". Distinguishing those two is the point. */
        t->alg = 0xFFFFFFFFu;
        rc = optiga_verify_staged_model(s_blob, off + (uint32_t)sizeof(*t));
        printf("  unknown algorithm     -> %3d  (%s)\r\n", rc, verdict_str(rc));
        if (rc != EX_SIG_MALFORMED) {
            printf("  expected malformed (%d)\r\n", EX_SIG_MALFORMED);
            return SDK_EX_REFUSED;
        }
    }

    return SDK_EX_OK;
}
