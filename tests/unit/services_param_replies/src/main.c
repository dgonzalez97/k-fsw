#include <errno.h>
#include <string.h>
#include <zephyr/ztest.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/crc.h>
#include <csp/csp.h>
#include <param/param.h>
#include <param/param_list.h>
#include <param/param_queue.h>
#include <param/param_server.h>
#include <kfsw/comms/csp.h>
#include <kfsw/services/parameter.h>
#include "param_list.h"

#define PEER 42
#define ID_A 0x100
#define ID_B 0x101
#define LIST_HEADER 10

enum response_mode {
	GOOD,
	LIST_NO_END,
	LIST_BAD_CRC,
	LIST_BAD_COUNT,
	VALUE_NO_END,
	VALUE_MISSING,
	VALUE_DUPLICATE,
	VALUE_CONFLICT,
	VALUE_WIDE,
	VALUE_TRUNCATED,
	VALUE_UNEXPECTED,
	VALUE_TRICKLE
};
static enum response_mode mode;
static unsigned int reads;
static unsigned int transactions;
static uint8_t port;
static uint16_t requested_index;
static uint32_t list_crc;
static uint32_t value_a = 42, value_b = 84;
static uint16_t zero;
static uint8_t connection_storage;
static param_transfer3_t descriptors[2];

void __wrap_kfsw_csp_get_info(struct kfsw_csp_info *info)
{
	memset(info, 0, sizeof(*info));
	info->initialized = true;
	info->router_running = true;
	info->address = 7;
}

csp_conn_t *__wrap_csp_connect(uint8_t priority, uint16_t node, uint8_t destination,
			       uint32_t timeout, uint32_t opts)
{
	ARG_UNUSED(priority);
	ARG_UNUSED(timeout);
	zassert_equal(node, PEER);
	zassert_false(opts & CSP_O_RDP);
	port = destination;
	reads = 0;
	transactions++;
	return (csp_conn_t *)&connection_storage;
}

void __wrap_csp_send(csp_conn_t *connection, csp_packet_t *packet)
{
	ARG_UNUSED(connection);
	if (port == CONFIG_KFSW_PARAM_LIST_PORT) {
		zassert_equal(packet->length, 7);
		zassert_equal(packet->data[0], 4);
		requested_index = sys_get_be16(&packet->data[1]);
	} else {
		zassert_equal(packet->data[0], PARAM_PULL_REQUEST_V2);
	}
	csp_buffer_free(packet);
}

int __wrap_csp_close(csp_conn_t *connection)
{
	ARG_UNUSED(connection);
	return 0;
}

static csp_packet_t *list_reply(void)
{
	if (mode == LIST_NO_END && requested_index == 2) {
		return NULL;
	}
	csp_packet_t *packet = csp_buffer_get(CSP_BUFFER_SIZE);

	zassert_not_null(packet);
	memset(packet->data, 0, CSP_BUFFER_SIZE);
	packet->data[0] = 4;
	packet->data[1] = requested_index == 2 ? 1 : 0;
	sys_put_be16(requested_index, &packet->data[2]);
	sys_put_be16(mode == LIST_BAD_COUNT && requested_index == 1 ? 3 : 2, &packet->data[4]);
	sys_put_be32(list_crc ^ (mode == LIST_BAD_CRC ? 1U : 0U), &packet->data[6]);
	packet->length = LIST_HEADER;
	if (requested_index < 2) {
		memcpy(&packet->data[LIST_HEADER], &descriptors[requested_index],
		       offsetof(param_transfer3_t, help) + 1);
		packet->length += offsetof(param_transfer3_t, help) + 1;
	}
	return packet;
}

static void append_value(param_queue_t *queue, uint16_t id, uint32_t value, bool wide)
{
	uint64_t wide_value = UINT64_C(0x100000000);
	param_t descriptor = {.id = id,
			      .node = &zero,
			      .array_size = 1,
			      .type = wide ? PARAM_TYPE_UINT64 : PARAM_TYPE_UINT32,
			      .array_step = 4};

	zassert_ok(param_queue_add(queue, &descriptor, -1, wide ? (void *)&wide_value : &value));
}

csp_packet_t *__wrap_csp_read(csp_conn_t *connection, uint32_t timeout)
{
	ARG_UNUSED(connection);
	zassert_true(timeout > 0);
	if (port == CONFIG_KFSW_PARAM_LIST_PORT) {
		return list_reply();
	}
	if (reads++ != 0 && mode != VALUE_TRICKLE) {
		return NULL;
	}
	if (mode == VALUE_TRICKLE) {
		k_sleep(K_MSEC(MIN(timeout, 5)));
	}
	csp_packet_t *packet = csp_buffer_get(CSP_BUFFER_SIZE);
	param_queue_t queue = {0};

	zassert_not_null(packet);
	packet->data[0] = PARAM_PULL_RESPONSE_V2;
	packet->data[1] = mode == VALUE_NO_END || mode == VALUE_TRICKLE ? 0 : PARAM_FLAG_END;
	param_queue_init(&queue, &packet->data[2], CSP_BUFFER_SIZE - 2, 0, PARAM_QUEUE_TYPE_SET, 2);
	append_value(&queue, mode == VALUE_UNEXPECTED ? 0x123 : ID_A, value_a, mode == VALUE_WIDE);
	if (mode != VALUE_MISSING && mode != VALUE_TRICKLE) {
		append_value(&queue, ID_B, value_b, false);
	}
	if (mode == VALUE_DUPLICATE || mode == VALUE_CONFLICT) {
		append_value(&queue, ID_A, value_a + (mode == VALUE_CONFLICT), false);
	}
	packet->length = queue.used + 2;
	if (mode == VALUE_TRUNCATED) {
		packet->length--;
	}
	return packet;
}

static void *setup(void)
{
	static uint32_t local;
	static const struct kfsw_param_definition definition = {
		.offset = 0, .type = KFSW_PARAM_U32, .name = "local", .value = &local};
	static const struct kfsw_param_definition_set set = {
		.table = 20, .name = "test", .definitions = &definition, .count = 1};
	const struct kfsw_param_definition_set *sets[] = {&set};

	zassert_ok(kfsw_param_init(sets, 1));
	zassert_ok(kfsw_csp_init());
	for (size_t i = 0; i < 2; i++) {
		descriptors[i].id = sys_cpu_to_be16(ID_A + i);
		descriptors[i].type = PARAM_TYPE_UINT32;
		descriptors[i].size = 1;
		descriptors[i].name[0] = 'a' + i;
		list_crc = crc32_ieee_update(list_crc, (uint8_t *)&descriptors[i],
					     offsetof(param_transfer3_t, help) + 1);
	}
	return NULL;
}

static void before(void *unused)
{
	ARG_UNUSED(unused);
	mode = GOOD;
	value_a = 42;
	value_b = 84;
	transactions = 0;
	zassert_ok(kfsw_param_remote_refresh(PEER));
}

ZTEST(param_replies, test_complete_values_and_identical_duplicates)
{
	const char *names[] = {"a", "b"};
	struct kfsw_param_value values[2];

	mode = VALUE_DUPLICATE;
	zassert_ok(kfsw_param_remote_get_many(PEER, names, 2, values));
	zassert_equal(values[0].scalar.u32, 42);
	zassert_equal(values[1].scalar.u32, 84);
}

ZTEST(param_replies, test_failed_value_reply_publishes_nothing)
{
	const char *names[] = {"a", "b"};
	struct kfsw_param_value values[2], unchanged[2];
	const enum response_mode failures[] = {VALUE_NO_END, VALUE_MISSING,   VALUE_CONFLICT,
					       VALUE_WIDE,   VALUE_TRUNCATED, VALUE_UNEXPECTED};

	zassert_ok(kfsw_param_remote_get_many(PEER, names, 2, values));
	for (size_t i = 0; i < ARRAY_SIZE(failures); i++) {
		memset(values, 0xa5, sizeof(values));
		memcpy(unchanged, values, sizeof(values));
		value_a = 999;
		mode = failures[i];
		zassert_equal(kfsw_param_remote_get_many(PEER, names, 2, values),
			      mode == VALUE_NO_END ? -ETIMEDOUT : -EBADMSG);
		zassert_mem_equal(values, unchanged, sizeof(values));
		zassert_equal(param_get_uint32(param_list_find_id(PEER, ID_A)), 42);
	}
}

ZTEST(param_replies, test_incomplete_list_never_enters_shared_cache)
{
	const enum response_mode failures[] = {LIST_NO_END, LIST_BAD_CRC, LIST_BAD_COUNT};

	for (size_t i = 0; i < ARRAY_SIZE(failures); i++) {
		mode = failures[i];
		zassert_not_equal(kfsw_param_remote_refresh(PEER), 0);
		zassert_is_null(param_list_find_id(PEER, ID_A));
		zassert_is_null(param_list_find_id(PEER, ID_B));
		mode = GOOD;
		zassert_ok(kfsw_param_remote_refresh(PEER));
	}
}

ZTEST(param_replies, test_trickle_does_not_extend_deadline)
{
	const char *names[] = {"a", "b"};
	struct kfsw_param_value values[2];
	int64_t start = k_uptime_get();
	int buffers = csp_buffer_remaining();

	mode = VALUE_TRICKLE;
	zassert_equal(kfsw_param_remote_get_many_until(PEER, names, 2, values, start + 30),
		      -ETIMEDOUT);
	zassert_between_inclusive(k_uptime_get() - start, 30, 80);
	zassert_equal(csp_buffer_remaining(), buffers);
}

extern struct k_mutex kfsw_param_remote_lock;
static K_THREAD_STACK_DEFINE(waiter_stack, 3072);
static struct k_thread waiter;
static int waiter_result;

static void wait_for_remote(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);
	const char *names[] = {"a"};
	struct kfsw_param_value value;

	waiter_result =
		kfsw_param_remote_get_many_until(PEER, names, 1, &value, k_uptime_get() + 20);
}

ZTEST(param_replies, test_mutex_admission_uses_the_same_budget)
{
	k_mutex_lock(&kfsw_param_remote_lock, K_FOREVER);
	k_thread_create(&waiter, waiter_stack, K_THREAD_STACK_SIZEOF(waiter_stack), wait_for_remote,
			NULL, NULL, NULL, 5, 0, K_NO_WAIT);
	int result = k_thread_join(&waiter, K_MSEC(100));

	k_mutex_unlock(&kfsw_param_remote_lock);
	zassert_ok(result);
	zassert_equal(waiter_result, -ETIMEDOUT);
}

ZTEST_SUITE(param_replies, NULL, setup, before, NULL, NULL);
