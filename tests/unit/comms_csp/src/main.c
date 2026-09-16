#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/ztest.h>

#include <csp/csp.h>

#include <kfsw/comms/csp.h>

static bool count_interface(const struct kfsw_csp_interface_info *info, void *context)
{
	size_t *visits = context;

	zassert_not_null(info);
	(*visits)++;
	return true;
}

static bool count_route(const struct kfsw_csp_route_info *info, void *context)
{
	size_t *visits = context;

	zassert_not_null(info);
	(*visits)++;
	return true;
}

ZTEST(comms_csp_state, test_reports_configured_identity_before_init)
{
	struct kfsw_csp_info info = {0};

	kfsw_csp_get_info(&info);

	zassert_equal(info.address, 7U);
	zassert_equal(strcmp(info.hostname, "kfsw-test"), 0);
	zassert_false(info.initialized);
	zassert_false(info.router_running);
	zassert_equal(info.free_buffers, 0U);
}

ZTEST(comms_csp_state, test_rejects_operations_before_init)
{
	uint32_t round_trip_ms = UINT32_MAX;

	zassert_equal(kfsw_csp_start(), CSP_ERR_INVAL);
	zassert_equal(kfsw_csp_ping(1U, 10U, 1U, &round_trip_ms), CSP_ERR_INVAL);
	zassert_equal(round_trip_ms, UINT32_MAX);
}

ZTEST(comms_csp_state, test_hides_interfaces_and_routes_before_init)
{
	size_t interface_visits = 0U;
	size_t route_visits = 0U;

	kfsw_csp_visit_interfaces(count_interface, &interface_visits);
	kfsw_csp_visit_routes(count_route, &route_visits);

	zassert_equal(interface_visits, 0U);
	zassert_equal(route_visits, 0U);
}

ZTEST(comms_csp_state, test_reports_error_counters)
{
	struct kfsw_csp_counters counters = {.buffer_out = 9U, .last_error = 9U};

	/* Nothing has run, so every counter reads zero. */
	kfsw_csp_get_counters(&counters);
	zassert_equal(counters.buffer_out, 0U);
	zassert_equal(counters.conn_out, 0U);
	zassert_equal(counters.conn_overflow, 0U);
	zassert_equal(counters.conn_noroute, 0U);
	zassert_equal(counters.invalid_reply, 0U);
	zassert_equal(counters.last_error, 0U);
	zassert_equal(counters.last_can_error, 0U);

	kfsw_csp_clear_counters();
	counters.buffer_out = 9U;
	kfsw_csp_get_counters(&counters);
	zassert_equal(counters.buffer_out, 0U);

	/* A null destination is ignored. */
	kfsw_csp_get_counters(NULL);
}

ZTEST(comms_csp_state, test_names_error_codes)
{
	zassert_equal(strcmp(kfsw_csp_error_name(0U), "none"), 0);
	zassert_equal(strcmp(kfsw_csp_error_name(2U), "MTU exceeded"), 0);
	zassert_equal(strcmp(kfsw_csp_error_name(5U), "unknown"), 0);
	zassert_equal(strcmp(kfsw_csp_error_name(200U), "unknown"), 0);
	zassert_equal(strcmp(kfsw_csp_can_error_name(0U), "none"), 0);
	zassert_equal(strcmp(kfsw_csp_can_error_name(1U), "frame lost"), 0);
	zassert_equal(strcmp(kfsw_csp_can_error_name(200U), "unknown"), 0);
}

ZTEST_SUITE(comms_csp_state, NULL, NULL, NULL, NULL, NULL);
