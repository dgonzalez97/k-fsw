#include <stdint.h>

#include <zephyr/kernel.h>

#include <nsi_timer_model.h>

#include <kfsw/comms/csp.h>
/* Attributes this file's messages, so its level can be raised alone. */
#define KFSW_LOG_MODULE KFSW_LOG_MODULE_CSP
#include <kfsw/services/log.h>

/* A ground node takes the time from the machine it runs on.
 *
 * A board has to be told the date; a node running as a Linux process is sitting
 * on a machine that already knows, so making someone type it in would be asking
 * for a mistake.
 *
 * Runs from main rather than an init hook: the subsystems that come up in
 * between reset the wall clock, and a time set before them does not survive.
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
		/* The simulator was started with its clock reset, so the host
		 * time is not there to take. Left unset rather than guessed:
		 * the node can still be given the time like any other.
		 */
		kfsw_log_warning("Host clock unavailable; this node has no time");
		return 0;
	}

	result = kfsw_csp_clock_set(&clock);
	if (result != 0) {
		kfsw_log_warning("Could not take the host clock: %d", result);
	}
	return 0;
}
