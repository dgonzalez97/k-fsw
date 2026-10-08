#include <errno.h>
#include <string.h>
#include <zephyr/ztest.h>
#include <csp/csp_iflist.h>
#include <kfsw/comms/csp.h>

static csp_iface_t test_interface = {.name = "TEST"};

static void *setup(void)
{
	struct kfsw_csp_route_info info;

	zassert_equal(kfsw_csp_route_lookup(2U, &info), -ENETDOWN);
	zassert_ok(kfsw_csp_init());
	csp_iflist_add(&test_interface);
	return NULL;
}

ZTEST(comms_csp_routes, test_longest_prefix_and_default)
{
	struct kfsw_csp_route_info info;

	/* Same addresses work in both CSP versions; express masks by host width. */
	char table[KFSW_CSP_ROUTE_TABLE_MAX_LENGTH + 1U];
	snprintk(table, sizeof(table), "20/%u TEST 3,0/0 LOOP 1,16/%u LOOP 2",
		 KFSW_CSP_HOST_BITS - 2U, KFSW_CSP_HOST_BITS - 4U);
	zassert_ok(kfsw_csp_route_table_apply(table));
	zassert_ok(kfsw_csp_route_lookup(2U, &info));
	zassert_equal(info.prefix_length, 0U);
	zassert_true(info.has_via);
	zassert_equal(info.via, 1U);
	zassert_equal(strcmp(info.interface_name, "LOOP"), 0);
	zassert_ok(kfsw_csp_route_lookup(17U, &info));
	zassert_equal(info.prefix_length, KFSW_CSP_HOST_BITS - 4U);
	zassert_equal(info.via, 2U);
	zassert_ok(kfsw_csp_route_lookup(21U, &info));
	zassert_equal(info.address, 20U);
	zassert_equal(info.prefix_length, KFSW_CSP_HOST_BITS - 2U);
	zassert_equal(info.via, 3U);
	zassert_equal(strcmp(info.interface_name, "TEST"), 0);
}

ZTEST(comms_csp_routes, test_direct_loopback_and_no_match)
{
	struct kfsw_csp_route_info info = {.address = 99U};

	zassert_ok(kfsw_csp_route_table_apply("2 LOOP"));
	zassert_equal(kfsw_csp_route_lookup(3U, &info), -ENOENT);
	zassert_equal(info.address, 99U);
	zassert_equal(kfsw_csp_route_lookup(2U, NULL), -EINVAL);
	zassert_equal(kfsw_csp_route_lookup(KFSW_CSP_BROADCAST_ADDRESS + 1U, &info), -EINVAL);
	zassert_ok(kfsw_csp_route_lookup(2U, &info));
	zassert_false(info.has_via);
	zassert_ok(kfsw_csp_route_lookup(7U, &info));
	zassert_equal(info.address, 7U);
	zassert_equal(info.prefix_length, KFSW_CSP_HOST_BITS);
	zassert_false(info.has_via);
	zassert_equal(strcmp(info.interface_name, "LOOP"), 0);
}

ZTEST(comms_csp_routes, test_connected_subnet_and_default_interface)
{
	struct kfsw_csp_route_info info;

	zassert_ok(kfsw_csp_route_table_apply("2 LOOP 3"));
	test_interface.addr = 2U;
	test_interface.netmask = KFSW_CSP_HOST_BITS;
	zassert_ok(kfsw_csp_route_lookup(2U, &info));
	zassert_equal(strcmp(info.interface_name, "TEST"), 0);
	zassert_false(info.has_via);
	test_interface.netmask = 0U;
	test_interface.is_default = 1U;
	zassert_ok(kfsw_csp_route_lookup(4U, &info));
	zassert_equal(strcmp(info.interface_name, "TEST"), 0);
	zassert_false(info.has_via);
	test_interface.is_default = 0U;
}

ZTEST_SUITE(comms_csp_routes, NULL, setup, NULL, NULL, NULL);
