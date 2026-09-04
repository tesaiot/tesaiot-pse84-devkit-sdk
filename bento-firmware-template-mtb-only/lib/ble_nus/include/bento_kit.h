/*******************************************************************************
 * File: bento_kit.h
 *
 * The board's own name for itself, in one place.
 *
 * Two files used to derive this independently - nus_commands.c for
 * bento.info.board and bento_fw.c for bento.fw.query - and both wrote the same
 * two-way test: Eva Kit, or else "ai". A third board then arrived. The HMI Kit
 * defines USE_KIT_PSE84_HMI, matched neither arm, fell through the else, and
 * told every desktop it was an AI Kit. Confirmed on hardware 2026-09-02: both
 * verbs answered kit:"ai" from an HMI Kit, and the desktop uses that field to
 * decide which sensor tiles to offer - tiles for parts this board does not have.
 *
 * Adding a board now means adding one arm here. A board that forgets gets
 * "unknown", which is wrong in a way somebody will notice, rather than wrong
 * in a way that looks like a different product.
 ******************************************************************************/
#ifndef BENTO_KIT_H
#define BENTO_KIT_H

#if   defined(USE_KIT_PSE84_EVAL_EPC2)
#define BENTO_KIT_TAG "eva"
#elif defined(USE_KIT_PSE84_HMI)
#define BENTO_KIT_TAG "hmi"
#elif defined(USE_KIT_PSE84_AI)
#define BENTO_KIT_TAG "ai"
#else
/* Deliberately not a real board. A build that lands here has not declared
 * itself, and saying so beats impersonating whichever board is listed first. */
#define BENTO_KIT_TAG "unknown"
#endif

#endif /* BENTO_KIT_H */
