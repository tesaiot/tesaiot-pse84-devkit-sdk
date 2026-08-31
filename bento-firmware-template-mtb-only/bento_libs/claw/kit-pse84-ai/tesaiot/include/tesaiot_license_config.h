/**
 * @file tesaiot_license_config.h
 * @brief TESAIoT license configuration — PLACEHOLDER VALUES.
 *
 * This template ships with no real credentials. The values below are not a
 * licence for any device and will not verify.
 *
 * The original of this file in the BENTO development tree carries a live
 * OPTIGA UID and the ECDSA signature issued for it. That pair is per-device
 * and is not distributable: handing it out gives away one board's identity
 * together with a signature that authorises it. It was replaced here before
 * this template was published or zipped.
 *
 * To license your own board
 * -------------------------
 * 1. Read the OPTIGA UID off the device:
 *
 *        import optiga
 *        print(optiga.uid())
 *
 * 2. Send that UID to TESAIoT and receive a signature in return. On the
 *    issuing side it is produced by:
 *
 *        ./bento-release.sh sign-device <UID>
 *
 *    which signs the UID bytes with the licence private key and emits this
 *    header filled in.
 *
 * 3. Replace both defines below and rebuild.
 *
 * What happens if you do not
 * --------------------------
 * The check in tesaiot_license.c tests for a placeholder public key. A
 * development build prints a warning and continues, so the firmware runs
 * unlicensed on your bench. A release build — anything compiled with
 * TESAIOT_RELEASE_BUILD=1, which is how the shipped archives are built —
 * refuses instead. That guard exists because the placeholder path is a licence
 * bypass: it is a substring test against a header the integrator supplies, so
 * without it anyone holding the library could authorise every device by
 * editing one line.
 */

#ifndef TESAIOT_LICENSE_CONFIG_H
#define TESAIOT_LICENSE_CONFIG_H

/*============================================================================
 * LICENSE CONFIGURATION — replace both values with the ones issued to you
 *============================================================================*/

/**
 * Your OPTIGA Trust M UID: 27 bytes as 54 hex characters.
 * The value below is 54 zeroes, which is not a real UID.
 */
#define TESAIOT_DEVICE_UID      "000000000000000000000000000000000000000000000000000000"

/**
 * Your licence key: an ECDSA P-256 signature over the UID, Base64 encoded.
 * The value below is not a signature.
 */
#define TESAIOT_LICENSE_KEY     "REPLACE_WITH_THE_LICENCE_KEY_ISSUED_FOR_YOUR_DEVICE_UID"

/*============================================================================
 * END OF CONFIGURATION
 *============================================================================*/

#endif /* TESAIOT_LICENSE_CONFIG_H */
