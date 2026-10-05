#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

#include <zephyr/kernel.h>

#include <kfsw/comms/csp.h>
#include <kfsw/services/health.h>
#define KFSW_LOG_MODULE KFSW_LOG_MODULE_HEALTH
#include <kfsw/services/log.h>

#include "csp_liveness.h"

/*
 * The application loop reporting to health only proves the scheduler runs. A
 * node whose router or connection pool has stopped serving still reports, and
 * the watchdog keeps being fed while the ground cannot reach it.
 *
 * This asks the router to carry a packet to this node's own address. It needs no
 * wire and no peer, and it fails when the machinery an operator depends on has
 * stopped, including when another thread is holding a connection it will never
 * release.
 */

/* The probe is the only reason this file is compiled, so a composition that
 * enables it without a router has nothing to probe. The unit suite replaces
 * the router instead of bringing it up.
 */
BUILD_ASSERT(IS_ENABLED(CONFIG_KFSW_CSP) || IS_ENABLED(CONFIG_ZTEST),
	     "KFSW_CSP_LIVENESS needs KFSW_CSP");

#define LIVENESS_PAYLOAD_SIZE 1U

static uint8_t liveness_handle;
static bool registered;
static uint32_t probes;
static uint32_t failures;

static void liveness_work_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(liveness_work, liveness_work_handler);

static void liveness_work_handler(struct k_work *work)
{
	struct kfsw_csp_info info;
	uint32_t round_trip_us;
	int result;

	ARG_UNUSED(work);

	kfsw_csp_get_info(&info);
	result = kfsw_csp_ping(info.address, CONFIG_KFSW_CSP_LIVENESS_TIMEOUT_MS,
			       LIVENESS_PAYLOAD_SIZE, &round_trip_us);
	if (probes < UINT32_MAX) {
		probes++;
	}

	if (result == 0) {
		(void)kfsw_health_report(liveness_handle);
	} else {
		if (failures < UINT32_MAX) {
			failures++;
		}
		/* Not reporting is the point: health stops feeding the watchdog
		 * when the deadline passes.
		 */
		kfsw_log_warning("CSP liveness probe failed (%d)", result);
	}

	(void)k_work_reschedule(&liveness_work, K_MSEC(CONFIG_KFSW_CSP_LIVENESS_PERIOD_MS));
}

int kfsw_csp_liveness_start(void)
{
	int result;

	if (registered) {
		return -EALREADY;
	}
	result =
		kfsw_health_register("csp", CONFIG_KFSW_CSP_LIVENESS_DEADLINE_MS, &liveness_handle);
	if (result != 0) {
		return result;
	}
	registered = true;

	/* Reported once up front, so the first period cannot miss the deadline. */
	(void)kfsw_health_report(liveness_handle);
	(void)k_work_reschedule(&liveness_work, K_MSEC(CONFIG_KFSW_CSP_LIVENESS_PERIOD_MS));
	return 0;
}

void kfsw_csp_liveness_get_counters(uint32_t *probe_count, uint32_t *failure_count)
{
	if (probe_count != NULL) {
		*probe_count = probes;
	}
	if (failure_count != NULL) {
		*failure_count = failures;
	}
}
