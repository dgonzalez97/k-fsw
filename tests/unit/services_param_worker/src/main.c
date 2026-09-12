#include <string.h>
#include <csp/csp.h>
#include <csp/csp_crc32.h>
#include <csp/csp_iflist.h>
#include <csp/csp_interface.h>
#include <csp/interfaces/csp_if_lo.h>
#include <param/param_server.h>
#include <param/param_queue.h>
#include <zephyr/ztest.h>
#include <zephyr/sys/byteorder.h>
#include <kfsw/comms/csp.h>
#include <kfsw/services/parameter.h>
#include "parameter_internal.h"

static K_SEM_DEFINE(sample_entered, 0, 1);
static K_SEM_DEFINE(sample_release, 0, 1);
static K_SEM_DEFINE(ping_reply, 0, 1);
static K_SEM_DEFINE(value_reply, 0, 8);
static atomic_t block_sample;
static uint32_t backing;

extern const k_tid_t kfsw_param_list_thread;
static atomic_t fail_list_allocation;
static K_SEM_DEFINE(list_reply, 0, 1);
static uint16_t listed_id, listed_total;
static uint8_t listed_status;
static uint32_t listed_crc;

csp_packet_t *__real_csp_buffer_get(size_t size);

csp_packet_t *__wrap_csp_buffer_get(size_t size)
{
	if (k_current_get() == kfsw_param_list_thread && atomic_cas(&fail_list_allocation, 1, 0)) {
		return NULL;
	}
	return __real_csp_buffer_get(size);
}

static void sample(void *value)
{
	if (atomic_cas(&block_sample, 1, 0)) {
		k_sem_give(&sample_entered);
		zassert_ok(k_sem_take(&sample_release, K_SECONDS(2)));
	}
	*(uint32_t *)value = 42;
}

static int receive_reply(csp_iface_t *iface, uint16_t via, csp_packet_t *packet, int from_me)
{
	ARG_UNUSED(iface);
	ARG_UNUSED(via);
	ARG_UNUSED(from_me);
	if (packet->id.sport == CSP_PING) {
		k_sem_give(&ping_reply);
	} else if (packet->id.sport == CONFIG_KFSW_PARAM_LIST_PORT) {
		if (packet->data[0] == 4) {
			listed_status = packet->data[1];
			listed_total = sys_get_be16(&packet->data[4]);
			listed_crc = sys_get_be32(&packet->data[6]);
			if (listed_status == 0) {
				listed_id = sys_get_be16(&packet->data[10]);
			}
		} else {
			listed_id = sys_get_be16(packet->data);
		}
		k_sem_give(&list_reply);
	} else if (packet->id.sport == CONFIG_KFSW_PARAM_PORT) {
		k_sem_give(&value_reply);
	}
	csp_buffer_free(packet);
	return CSP_ERR_NONE;
}

static csp_iface_t link = {.name = "PEER", .addr = 7, .netmask = 14, .nexthop = receive_reply};

static void inject(uint8_t port)
{
	csp_packet_t *packet = csp_buffer_get(8);

	zassert_not_null(packet);
	packet->id = (csp_id_t){.src = 42,
				.dst = 7,
				.sport = 20,
				.dport = port,
				.pri = CSP_PRIO_NORM,
				.flags = CSP_FCRC32};
	packet->length = 2;
	/* libparam v2 PULL with an empty identifier queue still returns END. */
	packet->data[0] = PARAM_PULL_REQUEST_V2;
	packet->data[1] = 0;
	zassert_ok(csp_crc32_append(packet));
	zassert_ok(csp_if_lo.nexthop(&csp_if_lo, CSP_NO_VIA_ADDRESS, packet, 0));
}

static void inject_list(bool indexed, uint16_t index, uint32_t crc)
{
	static uint8_t next_port = 21;
	csp_packet_t *packet = csp_buffer_get(16);

	zassert_not_null(packet);
	packet->id = (csp_id_t){.src = 42,
				.dst = 7,
				.sport = next_port++,
				.dport = CONFIG_KFSW_PARAM_LIST_PORT,
				.pri = CSP_PRIO_NORM,
				.flags = CSP_FCRC32};
	packet->data[0] = indexed ? 4 : 3;
	sys_put_be16(index, &packet->data[1]);
	sys_put_be32(crc, &packet->data[3]);
	packet->length = indexed ? 7 : 1;
	zassert_ok(csp_crc32_append(packet));
	zassert_ok(csp_if_lo.nexthop(&csp_if_lo, CSP_NO_VIA_ADDRESS, packet, 0));
}

ZTEST(services_param_worker, test_blocked_sampling_keeps_router_running_and_queue_bounded)
{
	static const struct kfsw_param_definition definition = {
		.offset = 0,
		.type = KFSW_PARAM_U32,
		.flags = IS_ENABLED(KFSW_TEST_LOCAL_ONLY) ? KFSW_PARAM_FLAG_LOCAL_ONLY
							  : KFSW_PARAM_FLAG_READ_ONLY,
		.name = "slow",
		.value = &backing,
		.sample = sample,
	};
	static const struct kfsw_param_definition_set set = {
		.table = 20,
		.name = "test",
		.definitions = &definition,
		.count = 1,
	};
	const struct kfsw_param_definition_set *sets[] = {&set};
	struct kfsw_param_stats stats;
	int free_before;

	zassert_ok(kfsw_param_init(sets, ARRAY_SIZE(sets)));
	zassert_ok(kfsw_csp_init());
	csp_iflist_add(&link);
	zassert_ok(kfsw_csp_route_table_apply("0/0 PEER"));
	static csp_socket_t occupied;

	zassert_ok(csp_listen(&occupied, 1));
	zassert_ok(csp_bind(&occupied, CONFIG_KFSW_PARAM_LIST_PORT));
	zassert_equal(kfsw_param_server_start(), -EADDRINUSE);
	zassert_ok(csp_socket_close(&occupied));
	zassert_ok(kfsw_param_server_start());
	zassert_ok(kfsw_csp_start());
	free_before = csp_buffer_remaining();
	atomic_set(&block_sample, 1);
	inject(CONFIG_KFSW_PARAM_PORT);
	zassert_ok(k_sem_take(&sample_entered, K_SECONDS(1)));
	for (size_t i = 0; i < CONFIG_KFSW_PARAM_VALUE_QUEUE_DEPTH + 3U; i++) {
		inject(CONFIG_KFSW_PARAM_PORT);
	}
	inject(CSP_PING);
	zassert_ok(k_sem_take(&ping_reply, K_MSEC(100)));
	zassert_equal(kfsw_param_csp_dropped_requests(), 3);
	k_sem_give(&sample_release);
	for (size_t i = 0; i < CONFIG_KFSW_PARAM_VALUE_QUEUE_DEPTH + 1U; i++) {
		zassert_ok(k_sem_take(&value_reply, K_SECONDS(1)));
	}
	k_sleep(K_MSEC(2));
	zassert_equal(csp_buffer_remaining(), free_before);
	zassert_ok(kfsw_param_get_stats(&stats));
	zassert_equal(stats.requests_dropped, 3);

	if (IS_ENABLED(KFSW_TEST_LOCAL_ONLY)) {
		struct kfsw_param_value local_value = {
			.type = KFSW_PARAM_U32,
			.size = sizeof(uint32_t),
			.scalar.u32 = 17,
		};
		zassert_ok(kfsw_param_set("slow", &local_value));
		zassert_equal(backing, 17);
		local_value.scalar.u32 = 42;
		zassert_ok(kfsw_param_set("slow", &local_value));
	}
	/* Remote PUSH must respect read-only and local-only values. */
	csp_packet_t *push = csp_buffer_get(CSP_BUFFER_SIZE);
	param_queue_t queue = {0};
	uint16_t local = 0;
	uint32_t replacement = 99;
	param_t parameter = {.id = KFSW_PARAM_ID(20, 0),
			     .node = &local,
			     .type = PARAM_TYPE_UINT32,
			     .array_size = 1,
			     .array_step = 4};

	zassert_not_null(push);
	push->id = (csp_id_t){.src = 42,
			      .dst = 7,
			      .sport = 20,
			      .dport = CONFIG_KFSW_PARAM_PORT,
			      .pri = CSP_PRIO_NORM,
			      .flags = CSP_FCRC32};
	push->data[0] = PARAM_PUSH_REQUEST_V2;
	push->data[1] = 0;
	param_queue_init(&queue, &push->data[2], CSP_BUFFER_SIZE - 2, 0, PARAM_QUEUE_TYPE_SET, 2);
	zassert_ok(param_queue_add(&queue, &parameter, -1, &replacement));
	push->length = queue.used + 2;
	zassert_ok(csp_crc32_append(push));
	zassert_ok(csp_if_lo.nexthop(&csp_if_lo, CSP_NO_VIA_ADDRESS, push, 0));
	zassert_not_equal(k_sem_take(&value_reply, K_MSEC(50)), 0);
	zassert_equal(backing, 42);
	zassert_equal(csp_buffer_remaining(), free_before);
	atomic_set(&fail_list_allocation, 1);
	inject_list(false, 0, 0);
	zassert_ok(k_sem_take(&list_reply, K_SECONDS(1)));
	zassert_equal(atomic_get(&fail_list_allocation), 0);
	zassert_equal(listed_id, KFSW_PARAM_ID(20, 0));
	inject_list(true, 0, 0);
	zassert_ok(k_sem_take(&list_reply, K_SECONDS(1)));
	zassert_equal(listed_status, 0);
	zassert_equal(listed_total, 1);
	zassert_equal(listed_id, KFSW_PARAM_ID(20, 0));
	uint32_t fingerprint = listed_crc;

	inject_list(true, 1, fingerprint);
	zassert_ok(k_sem_take(&list_reply, K_SECONDS(1)));
	zassert_equal(listed_status, 1);
	zassert_equal(listed_total, 1);
	zassert_equal(listed_crc, fingerprint);
	k_sleep(K_MSEC(5));
	zassert_equal(csp_buffer_remaining(), free_before);
}

ZTEST_SUITE(services_param_worker, NULL, NULL, NULL, NULL, NULL);
