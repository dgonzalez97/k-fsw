#include <stdint.h>

#include <zephyr/kernel.h>

#include <nsi_timer_model.h>

#include <kfsw/comms/csp.h>
#define KFSW_LOG_MODULE KFSW_LOG_MODULE_CSP
#include <kfsw/services/log.h>

/* A native node takes the time from the host.
 *
 * Runs from main instead of an init hook, because subsystems that start in
 * between reset the wall clock.
 */
int kfsw_clock_from_host(void)
{
	struct kfsw_csp_clock clock;
	uint64_t seconds = 0U;
	uint32_t nanoseconds = 0U;
	int result;

	hwtimer_get_pseudohost_rtc_time(&nanoseconds, &seconds);

	clock.seconds = (int32_t)seconds;
	clock.nanoseconds = nanoseconds;

	if (!kfsw_csp_clock_is_set(&clock)) {
		/* The simulator was started with its clock reset, so leave the time unset. */
		kfsw_log_warning("Host clock unavailable; this node has no time");
		return 0;
	}

	result = kfsw_csp_clock_set(&clock);
	if (result != 0) {
		kfsw_log_warning("Could not take the host clock: %d", result);
	}
	return 0;
}
