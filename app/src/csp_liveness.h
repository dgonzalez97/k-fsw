#ifndef KFSW_APP_CSP_LIVENESS_H
#define KFSW_APP_CSP_LIVENESS_H

#include <stdint.h>

/** Watch the CSP router with health. Call after the router and health start. */
int kfsw_csp_liveness_start(void);

/** Probes attempted and probes that failed since start. */
void kfsw_csp_liveness_get_counters(uint32_t *probe_count, uint32_t *failure_count);

#endif
