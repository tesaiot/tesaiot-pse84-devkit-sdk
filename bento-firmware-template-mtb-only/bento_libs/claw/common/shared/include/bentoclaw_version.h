#ifndef BENTOCLAW_VERSION_H
#define BENTOCLAW_VERSION_H

/* Per-project version override: each firmware provides bentoclaw_version_project.h
 * on its include path with its own BENTOCLAW_VERSION so the on-screen version, the
 * .fw_identity record, and the GitHub release tag all match per project (same
 * __has_include pattern as fw_id_config.h). Projects that don't provide one fall
 * back to the shared default below. */
#if defined(__has_include)
#  if __has_include("bentoclaw_version_project.h")
#    include "bentoclaw_version_project.h"
#  endif
#endif

#ifndef BENTOCLAW_VERSION
#define BENTOCLAW_VERSION     "1.4.0"
#endif
#define BENTOCLAW_VERSION_STR "BentoClaw v" BENTOCLAW_VERSION

#endif /* BENTOCLAW_VERSION_H */
