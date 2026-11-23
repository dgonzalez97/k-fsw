#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include <kfsw/comms/can.h>
#include <kfsw/comms/csp.h>
#include <kfsw/comms/uart.h>
#include <kfsw/services/health.h>

#include "csp_liveness.h"

#define SAMPLE_WAIT_MS (3U * CONFIG_KFSW_CSP_LIVENESS_PERIOD_MS)
#define OVERDUE_WAIT_MS (CONFIG_KFSW_CSP_LIVENESS_DEADLINE_MS + SAMPLE_WAIT_MS)

static bool freeze_kiss[2];
static bool missing_kiss;
static bool freeze_can;
static uint32_t kiss_passes[2] = {UINT32_MAX - 1U, 0U};
static uint32_t can_passes;

int __wrap_kfsw_platform_watchdog_release(void)
{
	return 0;
}

int __wrap_kfsw_platform_watchdog_feed(void)
{
	return 0;
}

void kfsw_uart_visit(kfsw_uart_visitor_t visitor, void *context)
{
	for (size_t i = 0U; i < ARRAY_SIZE(kiss_passes); i++) {
		struct kfsw_uart_info info = {
			.interface_name = i == 0U ? "KISS_1" : "KISS_2",
			.interface_started = true,
			.receiver_opened = !IS_ENABLED(CONFIG_KFSW_TEST_RECEIVERS_UNOPENED),
		};

		if (missing_kiss && i == 1U) {
			continue;
		}
		if (!freeze_kiss[i]) {
			kiss_passes[i]++;
		}
		info.rx_passes = kiss_passes[i];
		if (!visitor(&info, context)) {
			break;
		}
	}
}

void kfsw_can_get_info(struct kfsw_can_info *info)
{
	if (!freeze_can) {
		can_passes++;
	}
	*info = (struct kfsw_can_info){
		.interface_name = "CAN",
		.ready = !IS_ENABLED(CONFIG_KFSW_TEST_RECEIVERS_UNOPENED),
		.rx_passes = can_passes,
	};
}

void kfsw_csp_get_info(struct kfsw_csp_info *info)
{
	*info = (struct kfsw_csp_info){.address = 7U};
}

int kfsw_csp_ping(uint16_t node, uint32_t timeout, size_t size, uint32_t *rtt)
{
	ARG_UNUSED(node);
	ARG_UNUSED(timeout);
	ARG_UNUSED(size);
	*rtt = 1U;
	return 0;
}

static struct kfsw_health_component component(const char *name)
{
	struct kfsw_health_status status;
	struct kfsw_health_component entry;

	zassert_ok(kfsw_health_get_status(&status));
	for (uint8_t i = 0U; i < status.count; i++) {
		zassert_ok(kfsw_health_get_component(i, &entry));
		if (strcmp(entry.name, name) == 0) {
			return entry;
		}
	}
	zassert_unreachable("missing component %s", name);
	return (struct kfsw_health_component){0};
}

ZTEST(app_csp_receivers, test_progress_obligations_and_startup_rollback)
{
	struct kfsw_health_status status;
	uint8_t blocker;

	if (IS_ENABLED(CONFIG_KFSW_TEST_RECEIVERS_UNOPENED)) {
		zassert_ok(kfsw_csp_liveness_start());
		zassert_ok(kfsw_health_start());
		k_sleep(K_MSEC(OVERDUE_WAIT_MS));
		zassert_ok(kfsw_health_get_status(&status));
		zassert_equal(status.count, 1U);
		zassert_true(status.feeding);
		zassert_false(component("csp").overdue);
		return;
	}

	/* Fail the last registration, then the middle one. Neither may leave
	 * the earlier registrations behind or schedule the work item.
	 */
	zassert_ok(kfsw_health_register("can", 100U, &blocker));
	zassert_equal(kfsw_csp_liveness_start(), -EEXIST);
	zassert_ok(kfsw_health_get_status(&status));
	zassert_equal(status.count, 1U);
	zassert_ok(kfsw_health_unregister(blocker));
	zassert_ok(kfsw_health_register("kiss", 100U, &blocker));
	zassert_equal(kfsw_csp_liveness_start(), -EEXIST);
	zassert_ok(kfsw_health_get_status(&status));
	zassert_equal(status.count, 1U);
	zassert_ok(kfsw_health_unregister(blocker));

	kiss_passes[0] = UINT32_MAX - 1U;
	zassert_ok(kfsw_csp_liveness_start());
	zassert_ok(kfsw_health_start());
	zassert_equal(kfsw_csp_liveness_start(), -EALREADY);
	zassert_false(component("kiss").overdue);
	zassert_false(component("can").overdue);
	k_sleep(K_MSEC(SAMPLE_WAIT_MS));
	zassert_true(component("kiss").reports > 0U);
	zassert_true(component("can").reports > 0U);
	zassert_false(component("kiss").overdue, "counter wrap must count as progress");

	freeze_kiss[1] = true;
	/* F2: the deadline starts at the last report, with one health evaluation
	 * interval of allowance, even when suspension occurs between samples.
	 */
	uint32_t since = component("kiss").since_report_ms;

	k_sleep(K_MSEC(CONFIG_KFSW_CSP_LIVENESS_DEADLINE_MS - since +
		       CONFIG_KFSW_HEALTH_INTERVAL_MS + 1U));
	zassert_true(component("kiss").overdue, "every watched KISS receiver must advance");
	zassert_ok(kfsw_health_get_status(&status));
	zassert_false(status.feeding);
	zassert_str_equal(status.faulted_by, "kiss");
	uint32_t stopped_feeds = status.feeds;

	k_sleep(K_MSEC(CONFIG_KFSW_HEALTH_INTERVAL_MS + 1U));
	zassert_ok(kfsw_health_get_status(&status));
	zassert_equal(status.feeds, stopped_feeds);
	zassert_false(component("can").overdue);
	zassert_false(component("csp").overdue);
	freeze_kiss[1] = false;
	freeze_can = true;
	k_sleep(K_MSEC(OVERDUE_WAIT_MS));
	zassert_false(component("kiss").overdue);
	zassert_true(component("can").overdue);
	freeze_can = false;
	k_sleep(K_MSEC(SAMPLE_WAIT_MS));
	zassert_false(component("can").overdue);

	missing_kiss = true;
	k_sleep(K_MSEC(OVERDUE_WAIT_MS));
	zassert_true(component("kiss").overdue, "a missing receiver remains watched");
	missing_kiss = false;
	k_sleep(K_MSEC(SAMPLE_WAIT_MS));
	zassert_false(component("kiss").overdue);
}

ZTEST_SUITE(app_csp_receivers, NULL, NULL, NULL, NULL, NULL);
