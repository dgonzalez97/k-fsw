#include <string.h>
#include <csp/csp.h>
#include <csp/csp_iflist.h>
#include <csp/csp_interface.h>
#include <zephyr/ztest.h>
#include <kfsw/comms/csp.h>

static unsigned int packets;

static int discard(csp_iface_t *iface, uint16_t via, csp_packet_t *packet, int from_me)
{
	ARG_UNUSED(iface);
	ARG_UNUSED(via);
	ARG_UNUSED(from_me);
	zassert_equal(packet->id.dst, 42);
	packets++;
	csp_buffer_free(packet);
	return CSP_ERR_NONE;
}

static csp_iface_t link = {.name = "TEST", .addr = 7, .netmask = 14, .nexthop = discard};

static K_SEM_DEFINE(start_race, 0, 1);
static K_SEM_DEFINE(start_done, 0, 1);
static K_THREAD_STACK_DEFINE(starter_stack, 1024);
static struct k_thread starter;
static int start_result;

static void start_router(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);
	k_sem_take(&start_race, K_FOREVER);
	start_result = kfsw_csp_start();
	k_sem_give(&start_done);
}

static bool check_route(const struct kfsw_csp_route_info *info, void *context)
{
	size_t *count = context;

	zassert_equal(info->address, 0);
	zassert_equal(info->prefix_length, 0);
	zassert_str_equal(info->interface_name, "TEST");
	(*count)++;
	return true;
}

ZTEST(comms_routes, test_start_and_apply_share_admission)
{
	int result;
	size_t count = 0;

	zassert_equal(kfsw_csp_route_table_apply("0/0 TEST"), CSP_ERR_INVAL);
	zassert_ok(kfsw_csp_init());
	csp_iflist_add(&link);
	zassert_ok(kfsw_csp_route_table_apply("0/0 TEST"));
	k_thread_create(&starter, starter_stack, K_THREAD_STACK_SIZEOF(starter_stack), start_router,
			NULL, NULL, NULL, 1, 0, K_NO_WAIT);
	k_sem_give(&start_race);
	result = kfsw_csp_route_table_apply("0/0 TEST");
	zassert_true((result == CSP_ERR_NONE) || (result == CSP_ERR_NOTSUP));
	zassert_ok(k_sem_take(&start_done, K_SECONDS(1)));
	zassert_ok(start_result);
	zassert_ok(k_thread_join(&starter, K_SECONDS(1)));

	for (unsigned int i = 0; i < 10; i++) {
		zassert_equal(kfsw_csp_route_table_apply("41/14 TEST"), CSP_ERR_NOTSUP);
		csp_packet_t *packet = csp_buffer_get(1);

		zassert_not_null(packet);
		packet->length = 1;
		packet->data[0] = i;
		csp_sendto(CSP_PRIO_NORM, 42, CSP_PING, 20, CSP_O_CRC32, packet);
	}
	kfsw_csp_visit_routes(check_route, &count);
	zassert_equal(count, 1);
	zassert_equal(packets, 10);
}

ZTEST_SUITE(comms_routes, NULL, NULL, NULL, NULL, NULL);
