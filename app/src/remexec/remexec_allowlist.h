#ifndef KFSW_APP_REMEXEC_ALLOWLIST_H
#define KFSW_APP_REMEXEC_ALLOWLIST_H

#include <kfsw/services/remexec.h>

/**
 * Shell commands this composition offers for remote execution. The list is
 * the security boundary: anything absent from it is refused.
 */
extern const struct kfsw_remexec_allowlist kfsw_app_remexec_allowlist;

#endif
