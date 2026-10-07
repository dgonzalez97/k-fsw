#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <stdio.h>

#include <zephyr/ztest.h>

#include <csp/csp.h>
#include <csp/csp_id.h>
#include <csp/csp_iflist.h>
#include <csp/csp_rtable.h>

#include <kfsw/comms/csp.h>

static csp_iface_t kiss_1 = {.name = "KISS_1", .addr = 8U, .netmask = KFSW_CSP_HOST_BITS};
static csp_iface_t kiss_2 = {.name = "KISS_2", .addr = 9U, .netmask = KFSW_CSP_HOST_BITS};

static bool count_route(const struct kfsw_csp_route_info *info, void *context)
{
	size_t *count = context;

	zassert_not_null(info);
	(*count)++;
	return true;
}

static void *routing_setup(void)
{
	zassert_equal(kfsw_csp_init(), CSP_ERR_NONE);
	csp_iflist_add(&kiss_1);
	csp_iflist_add(&kiss_2);
	return NULL;
}

ZTEST(comms_routing, test_native_table_check_rejects_bad_input_without_mutation)
{
	size_t before = 0U;
	size_t after = 0U;
	size_t entries = 0U;

	zassert_equal(csp_rtable_load("0/0 KISS_1,11 KISS_2 11"), 2);
	kfsw_csp_visit_routes(count_route, &before);
	zassert_equal(kfsw_csp_route_table_check("not-a-route", NULL), CSP_ERR_INVAL);
	zassert_equal(kfsw_csp_route_table_check("10 UNKNOWN", NULL), CSP_ERR_INVAL);
	zassert_equal(kfsw_csp_route_table_check(
			      "0 LOOP,1 LOOP,2 LOOP,3 LOOP,4 LOOP,5 LOOP,6 LOOP,7 LOOP,"
			      "8 LOOP,9 LOOP",
			      NULL),
		      CSP_ERR_NOMEM);
	zassert_equal(kfsw_csp_route_table_check("10 KISS_1,11 KISS_2 11", &entries), CSP_ERR_NONE);
	zassert_equal(entries, 2U);
	kfsw_csp_visit_routes(count_route, &after);
	zassert_equal(after, before);
}

ZTEST(comms_routing, test_longest_prefix_and_via_follow_pinned_libcsp_semantics)
{
	csp_route_t *route;

	csp_rtable_clear();
	zassert_equal(csp_rtable_load("0/0 KISS_1,11 KISS_2 11"), 2);

	route = csp_rtable_find_route(10U);
	zassert_not_null(route);
	zassert_equal(strcmp(route->iface->name, "KISS_1"), 0);
	zassert_equal(route->netmask, 0U);
	zassert_equal(route->via, CSP_NO_VIA_ADDRESS);

	route = csp_rtable_find_route(11U);
	zassert_not_null(route);
	zassert_equal(strcmp(route->iface->name, "KISS_2"), 0);
	zassert_equal(route->netmask, KFSW_CSP_HOST_BITS);
	zassert_equal(route->via, 11U);
}

ZTEST(comms_routing, test_limits_follow_the_protocol_version)
{
	char route[24];

	/* libcsp and K-FSW agree on the version this image speaks. */
	zassert_equal(csp_id_get_host_bits(), KFSW_CSP_HOST_BITS);
	zassert_equal(csp_id_get_max_nodeid(), KFSW_CSP_BROADCAST_ADDRESS);
	zassert_equal(csp_id_get_header_size(), IS_ENABLED(CONFIG_KFSW_CSP_VERSION_1) ? 4U : 6U);

	/* A prefix longer than the address, or an address past broadcast, is refused. */
	(void)snprintf(route, sizeof(route), "11/%u KISS_2", KFSW_CSP_HOST_BITS + 1U);
	zassert_equal(kfsw_csp_route_table_check(route, NULL), CSP_ERR_INVAL);
	(void)snprintf(route, sizeof(route), "%u KISS_1", KFSW_CSP_BROADCAST_ADDRESS + 1U);
	zassert_equal(kfsw_csp_route_table_check(route, NULL), CSP_ERR_INVAL);
}

ZTEST_SUITE(comms_routing, NULL, routing_setup, NULL, NULL, NULL);
