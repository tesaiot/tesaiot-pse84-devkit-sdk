/**
 * @file tesaiot_protected_update.h
 * @brief OPTIGA Protected Update self-test entry point.
 *
 * Protected Update is how the TESAIoT platform replaces a certificate or key
 * on the secure element: it sends a signed manifest and the new content, and
 * the chip accepts them only if the signature checks out against a trust
 * anchor it already holds. Requesting one from the platform is declared in
 * tesaiot_optiga.h; this header declares the offline self-test.
 */

#ifndef TESAIOT_PROTECTED_UPDATE_H
#define TESAIOT_PROTECTED_UPDATE_H

#ifdef __cplusplus
extern "C" {
#endif

/** Exercise Protected Update without a platform connection. Provided by
 *  libbento_hsm.a.
 *
 *  It offers a read-only configuration check and a full update run. The
 *  update run is NOT confined to a scratch object: it resets the metadata of
 *  its target object, then writes a trust-anchor certificate and a 64-byte
 *  shared secret into the chip before sending the update. Those writes
 *  persist and replace whatever those objects held. Run it only on a board
 *  whose provisioning you can afford to redo. */
void tesaiot_run_protected_update_isolated_test(void);

#ifdef __cplusplus
}
#endif

#endif /* TESAIOT_PROTECTED_UPDATE_H */
