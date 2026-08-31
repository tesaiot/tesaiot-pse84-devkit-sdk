/*
 * Compile-time guard for the prebuilt IPC archive's page ordinal.
 *
 * lib/ipc_core/libbento_ipc.a was compiled against one specific value of
 * PAGE_ID_PLAYGROUND and stores it as an immediate — `cmp r0, #7`, with no
 * relocation. The linker therefore has no representation of the constant and
 * cannot warn when it is wrong: the build succeeds, the board boots, and the
 * archive compares the wrong page. PROVENANCE.txt records the value it was
 * built with; this file makes the mismatch a compile error instead.
 *
 * The page_id_t values in page_manager.h are explicit and never guarded by
 * ENABLE_PAGE_* — flipping a menu flag, or `bento.sh remove <menu>` letting
 * proj_cm55/Makefile force a flag to 0, no longer renumbers anything. That
 * was not always so: the enum used to be #if-wrapped, so PAGE_ID_PLAYGROUND
 * floated with the flag set, and this assert is what caught the drift when
 * the Motor menu was turned off. It stays as the backstop for the one edit
 * that can still move the value: someone renumbering the enum by hand.
 *
 * If this fires: restore PAGE_ID_PLAYGROUND = 7 in page_manager.h, or
 * rebuild the archives and update lib/ipc_core/PROVENANCE.txt to the new
 * ordinal.
 *
 * //! [doc-drift-fix] — docs/template_local_deltas.list
 */
#include "page_manager.h"

_Static_assert(PAGE_ID_PLAYGROUND == 7,
    "PAGE_ID_PLAYGROUND has moved. lib/ipc_core/libbento_ipc.a was built for "
    "ordinal 7 and stores it as an immediate; a mismatch links cleanly and "
    "compares the wrong page at runtime. See lib/ipc_core/PROVENANCE.txt.");
