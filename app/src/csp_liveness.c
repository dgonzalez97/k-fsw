#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/kernel.h>

#include <kfsw/comms/csp.h>
#include <kfsw/services/health.h>
#define KFSW_LOG_MODULE KFSW_LOG_MODULE_HEALTH
#include <kfsw/services/log.h>

#include "csp_liveness.h"

#if CONFIG_KFSW_CSP_KISS_UART && !CONFIG_KFSW_CSP_UART_INTERRUPT_DRIVEN
#include <kfsw/comms/uart.h>

static struct {
	const char *name;
	uint32_t passes;
	uint32_t last_progress_ms;
	bool advanced;
	bool stalled;
} kiss_receivers[CONFIG_CSP_UART_RX_THREAD_NUM];
static size_t kiss_count;
static uint8_t kiss_handle;

static bool watch_kiss(const struct kfsw_uart_info *info, void *context)
{
	int *result = context;

	if (!info->receiver_opened) {
		return true;
	}
	if (kiss_count == ARRAY_SIZE(kiss_receivers)) {
		*result = -ENOSPC;
		return false;
	}
	kiss_receivers[kiss_count].name = info->interface_name;
	kiss_receivers[kiss_count].passes = info->rx_passes;
	kiss_receivers[kiss_count].last_progress_ms = k_uptime_get_32();
	kiss_count++;
	return true;
}

static bool sample_kiss(const struct kfsw_uart_info *info, void *context)
{
	ARG_UNUSED(context);

	for (size_t i = 0U; i < kiss_count; i++) {
		if (strcmp(kiss_receivers[i].name, info->interface_name) != 0) {
			continue;
		}
		kiss_receivers[i].advanced =
			info->receiver_opened && info->rx_passes != kiss_receivers[i].passes;
		kiss_receivers[i].passes = info->rx_passes;
	}
	return true;
}

static void report_kiss(void)
{
	bool advanced = true;

	if (kiss_count == 0U) {
		return;
	}
	for (size_t i = 0U; i < kiss_count; i++) {
		kiss_receivers[i].advanced = false;
	}
	kfsw_uart_visit(sample_kiss, NULL);
	for (size_t i = 0U; i < kiss_count; i++) {
		if (kiss_receivers[i].advanced) {
			kiss_receivers[i].last_progress_ms = k_uptime_get_32();
			kiss_receivers[i].stalled = false;
		} else {
			advanced = false;
			if (!kiss_receivers[i].stalled &&
			    (uint32_t)(k_uptime_get_32() - kiss_receivers[i].last_progress_ms) >
				    CONFIG_KFSW_CSP_LIVENESS_DEADLINE_MS) {
				kfsw_log_warning("CSP receiver %s stopped advancing",
						 kiss_receivers[i].name);
				kiss_receivers[i].stalled = true;
			}
		}
	}
	if (advanced) {
		(void)kfsw_health_report(kiss_handle);
	}
}
#endif

#if CONFIG_KFSW_CSP_CAN
#include <kfsw/comms/can.h>
#if !CONFIG_ZTEST
#include <csp/drivers/can_zephyr.h>
BUILD_ASSERT(CONFIG_KFSW_CSP_LIVENESS_DEADLINE_MS >
		     CSP_CAN_RX_IDLE_WAKE_MS + CONFIG_KFSW_CSP_LIVENESS_PERIOD_MS,
	     "CAN health deadline must cover an idle wake and a liveness period");
#endif

static uint8_t can_handle;
static uint32_t can_passes;
static uint32_t can_last_progress_ms;
static bool can_watched;
static bool can_stalled;
static char can_name[KFSW_CAN_INTERFACE_NAME_SIZE];

static void report_can(void)
{
	struct kfsw_can_info info;
	bool advanced;

	if (!can_watched) {
		return;
	}
	kfsw_can_get_info(&info);
	advanced = info.ready && strcmp(info.interface_name, can_name) == 0 &&
		   info.rx_passes != can_passes;
	can_passes = info.rx_passes;
	if (advanced) {
		can_last_progress_ms = k_uptime_get_32();
		can_stalled = false;
		(void)kfsw_health_report(can_handle);
	} else if (!can_stalled && (uint32_t)(k_uptime_get_32() - can_last_progress_ms) >
					   CONFIG_KFSW_CSP_LIVENESS_DEADLINE_MS) {
		/* Samples can be faster than CAN's idle wake. */
		kfsw_log_warning("CSP receiver %s stopped advancing", can_name);
		can_stalled = true;
	}
}
#endif

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

#if CONFIG_KFSW_CSP_KISS_UART && !CONFIG_KFSW_CSP_UART_INTERRUPT_DRIVEN
	report_kiss();
#endif
#if CONFIG_KFSW_CSP_CAN
	report_can();
#endif

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

#if CONFIG_KFSW_CSP_KISS_UART && !CONFIG_KFSW_CSP_UART_INTERRUPT_DRIVEN
	kiss_count = 0U;
	memset(kiss_receivers, 0, sizeof(kiss_receivers));
	kfsw_uart_visit(watch_kiss, &result);
	if (result != 0) {
		(void)kfsw_health_unregister(liveness_handle);
		return result;
	}
	if (kiss_count > 0U) {
		result = kfsw_health_register("kiss", CONFIG_KFSW_CSP_LIVENESS_DEADLINE_MS,
					      &kiss_handle);
		if (result != 0) {
			(void)kfsw_health_unregister(liveness_handle);
			return result;
		}
	}
#endif
#if CONFIG_KFSW_CSP_CAN
	struct kfsw_can_info can_info;

	kfsw_can_get_info(&can_info);
	can_watched = can_info.ready;
	can_stalled = false;
	can_passes = can_info.rx_passes;
	can_last_progress_ms = k_uptime_get_32();
	memcpy(can_name, can_info.interface_name, sizeof(can_name));
	if (can_watched) {
		result = kfsw_health_register("can", CONFIG_KFSW_CSP_LIVENESS_DEADLINE_MS,
					      &can_handle);
		if (result != 0) {
#if CONFIG_KFSW_CSP_KISS_UART && !CONFIG_KFSW_CSP_UART_INTERRUPT_DRIVEN
			if (kiss_count > 0U) {
				(void)kfsw_health_unregister(kiss_handle);
			}
#endif
			(void)kfsw_health_unregister(liveness_handle);
			return result;
		}
	}
#endif
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
