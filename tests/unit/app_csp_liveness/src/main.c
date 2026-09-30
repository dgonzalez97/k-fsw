#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include <kfsw/comms/csp.h>
#include <kfsw/services/health.h>

#include "csp_liveness.h"

#define NODE_ADDRESS 7U
#define PROBE_WAIT_MS (3U * CONFIG_KFSW_CSP_LIVENESS_PERIOD_MS)
#define OVERDUE_WAIT_MS                                                                            \
	(CONFIG_KFSW_CSP_LIVENESS_DEADLINE_MS + (2U * CONFIG_KFSW_CSP_LIVENESS_PERIOD_MS))

/* What the router answers with, and what it was asked. */
static int ping_result;
static uint16_t ping_node;
static uint32_t ping_timeout_ms;

void kfsw_csp_get_info(struct kfsw_csp_info *info)
{
	*info = (struct kfsw_csp_info){
		.address = NODE_ADDRESS,
		.hostname = "test",
		.initialized = true,
		.router_running = true,
	};
}

int kfsw_csp_ping(uint16_t node, uint32_t timeout_ms, size_t payload_size, uint32_t *round_trip_ms)
{
	ARG_UNUSED(payload_size);

	ping_node = node;
	ping_timeout_ms = timeout_ms;
	if (ping_result == 0 && round_trip_ms != NULL) {
		*round_trip_ms = 1U;
	}
	return ping_result;
}

/* The one component this suite watches, whatever index it landed on. */
static void read_probe_component(struct kfsw_health_component *component)
{
	struct kfsw_health_status status;

	zassert_ok(kfsw_health_get_status(&status));
	for (uint8_t index = 0U; index < status.count; index++) {
		zassert_ok(kfsw_health_get_component(index, component));
		if (strcmp(component->name, "csp") == 0) {
			return;
		}
	}
	zassert_unreachable("the probe did not register with health");
}

/*
 * The module keeps its registration and its counters for the lifetime of the
 * image, so the states are walked in one ordered test rather than in separate
 * ones that would each inherit the last one's work item.
 */
ZTEST(app_csp_liveness, test_the_probe_reports_only_while_the_router_answers)
{
	struct kfsw_health_component component;
	uint32_t probes;
	uint32_t failures;
	uint32_t reports;

	ping_result = 0;
	zassert_ok(kfsw_csp_liveness_start());
	zassert_equal(kfsw_csp_liveness_start(), -EALREADY,
		      "a second start registered the component twice");

	/* Registered and reported before the first period elapses, so a slow
	 * first probe cannot be read as a missed deadline.
	 */
	read_probe_component(&component);
	zassert_equal(component.deadline_ms, CONFIG_KFSW_CSP_LIVENESS_DEADLINE_MS);
	zassert_true(component.reports > 0U, "the probe did not report at start");
	zassert_false(component.overdue);

	k_sleep(K_MSEC(PROBE_WAIT_MS));

	kfsw_csp_liveness_get_counters(&probes, &failures);
	zassert_true(probes > 0U, "the probe never ran");
	zassert_equal(failures, 0U, "a probe the router answered was counted as a failure");
	zassert_equal(ping_node, NODE_ADDRESS, "the probe was not aimed at this node");
	zassert_equal(ping_timeout_ms, CONFIG_KFSW_CSP_LIVENESS_TIMEOUT_MS);

	read_probe_component(&component);
	zassert_true(component.reports > 1U, "a successful probe did not report");
	zassert_false(component.overdue);
	reports = component.reports;

	/* A router that no longer carries a packet to this node. Health must see
	 * a component that stopped reporting, which is what withholds the feed.
	 */
	ping_result = -ETIMEDOUT;
	k_sleep(K_MSEC(OVERDUE_WAIT_MS));

	kfsw_csp_liveness_get_counters(&probes, &failures);
	zassert_true(failures > 0U, "a failed probe was not counted");

	read_probe_component(&component);
	zassert_equal(component.reports, reports, "a failed probe still reported to health");
	zassert_true(component.overdue, "the deadline passed without the component going overdue");

	/* And it recovers, rather than latching a fault of its own. */
	ping_result = 0;
	k_sleep(K_MSEC(PROBE_WAIT_MS));

	read_probe_component(&component);
	zassert_true(component.reports > reports, "the probe did not resume reporting");
	zassert_false(component.overdue);
}

ZTEST_SUITE(app_csp_liveness, NULL, NULL, NULL, NULL, NULL);
