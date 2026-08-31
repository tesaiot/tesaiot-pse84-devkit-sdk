/**
 * @file tesaiot_optiga_core.h
 * @brief Shared, serialised access to the one OPTIGA Trust M on the board.
 *
 * TLS, provisioning and the MicroPython optiga module all talk to the same
 * chip over the same I2C bus. Everything that needs the chip goes through
 * the calls below, which keep those users from interleaving. They are
 * provided by libbento_hsm.a.
 */

#ifndef TESAIOT_OPTIGA_CORE_H
#define TESAIOT_OPTIGA_CORE_H

#ifdef __cplusplus
extern "C" {
#endif

/* Types from the OPTIGA Trust M library and FreeRTOS used below. */
#include "include/optiga_util.h"
#include "include/optiga_crypt.h"
#include "include/common/optiga_lib_common.h"
#include "FreeRTOS.h"
#include "semphr.h"

/** Start the OPTIGA stack; call once during start-up. `on_done` is the
 *  completion handler for asynchronous chip operations and `on_done_ctx` is
 *  handed back to it. Returns false when the chip could not be brought up. */
bool optiga_manager_init(callback_handler_t on_done, void *on_done_ctx);

/** Borrow the shared optiga_util instance for exclusive use (NULL before
 *  optiga_manager_init()). Always hand it back with optiga_manager_release(). */
optiga_util_t *optiga_manager_acquire(void);

/** Hand back the instance borrowed with optiga_manager_acquire(). Forgetting
 *  to do so blocks every other user of the chip. */
void optiga_manager_release(void);

/** Exclusive access without borrowing the shared instance, for a caller that
 *  creates and destroys its own optiga_crypt instance while it holds the
 *  chip. Returns false if access could not be obtained in time. End it with
 *  optiga_manager_unlock(). */
bool optiga_manager_lock(void);
void optiga_manager_unlock(void);

/** Open and close a chip session. A task may nest these; the four calls
 *  above are built on them, so use those unless a bare session is what you
 *  want. */
bool optiga_chip_enter(void);
void optiga_chip_exit(void);

#ifdef __cplusplus
}
#endif

#endif /* TESAIOT_OPTIGA_CORE_H */
