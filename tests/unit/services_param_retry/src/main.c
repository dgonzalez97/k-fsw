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

#define PEER 42U
#define ID_A 0x100U
#define ID_B 0x101U
#define DESCRIPTOR_SIZE (offsetof(param_transfer3_t, help) + 1U)
#define LOOKUP_VERSION 5U

enum response_mode {
	GOOD,
	SILENT,
	PARTIAL,
	BAD_REPLY,
	WIDE,
	REFUSED,
	DENIED,
	WAIT_TIMEOUT,
	RETRY_NO_BUFFER,
	RETRY_REFUSED,
	DISCOVERY_RETRY
};
static enum response_mode mode;
static unsigned int losses;
static unsigned int lost_pull;
static unsigned int pulls;
static unsigned int pushes;
static unsigned int sends;
static unsigned int opened;
static unsigned int closed;
static unsigned int reads;
static unsigned int list_requests;
static unsigned int lookups;
static uint8_t port;
static uint8_t operation;
static uint16_t requested_index;
static uint8_t connection_storage;
static uint16_t zero;
static param_transfer3_t descriptors[2];
static uint32_t list_crc;
extern struct k_mutex kfsw_param_remote_lock;
K_SEM_DEFINE(lock_held, 0, 1);
K_SEM_DEFINE(release_lock, 0, 1);
K_THREAD_STACK_DEFINE(lock_stack, 1024);
static struct k_thread lock_thread;

void __wrap_kfsw_csp_get_info(struct kfsw_csp_info *info)
{
	memset(info, 0, sizeof(*info));
	info->initialized = true;
	info->router_running = mode != DENIED;
	info->address = 7;
}

csp_conn_t *__wrap_csp_connect(uint8_t priority, uint16_t node, uint8_t destination,
			       uint32_t timeout, uint32_t opts)
{
	ARG_UNUSED(priority);
	ARG_UNUSED(timeout);
	zassert_true(node == PEER || node == PEER + 1U);
	zassert_false(opts & CSP_O_RDP);
	if (mode == REFUSED || (mode == RETRY_REFUSED && pulls != 0U)) {
		return NULL;
	}
	port = destination;
	reads = 0;
	opened++;
	return (csp_conn_t *)&connection_storage;
}

csp_packet_t *__real_csp_buffer_get(size_t unused);

csp_packet_t *__wrap_csp_buffer_get(size_t unused)
{
	if (mode == RETRY_NO_BUFFER && pulls != 0U) {
		return NULL;
	}
	return __real_csp_buffer_get(unused);
}

void __wrap_csp_send(csp_conn_t *connection, csp_packet_t *packet)
{
	ARG_UNUSED(connection);
	operation = packet->data[0];
	sends++;
	if (port == CONFIG_KFSW_PARAM_LIST_PORT) {
		if (operation == LOOKUP_VERSION) {
			zassert_equal(mode, DISCOVERY_RETRY);
			lookups++;
		} else {
			zassert_equal(operation, 4);
			list_requests++;
			requested_index = sys_get_be16(&packet->data[1]);
		}
	} else if (operation == PARAM_PULL_REQUEST_V2) {
		pulls++;
	} else {
		zassert_equal(operation, PARAM_PUSH_REQUEST_V2);
		pushes++;
	}
	csp_buffer_free(packet);
}

int __wrap_csp_close(csp_conn_t *connection)
{
	ARG_UNUSED(connection);
	closed++;
	return 0;
}

static void append_value(param_queue_t *queue, uint16_t id, uint64_t value)
{
	uint32_t scalar = (uint32_t)value;
	param_t entry = {.id = id,
			 .node = &zero,
			 .array_size = 1,
			 .type = mode == WIDE ? PARAM_TYPE_UINT64 : PARAM_TYPE_UINT32,
			 .array_step = 4};

	zassert_ok(param_queue_add(queue, &entry, -1, mode == WIDE ? (void *)&value : &scalar));
}

csp_packet_t *__wrap_csp_read(csp_conn_t *connection, uint32_t timeout)
{
	ARG_UNUSED(connection);
	zassert_true(timeout > 0);
	if (port == CONFIG_KFSW_PARAM_LIST_PORT &&
	    (operation == LOOKUP_VERSION || (mode == DISCOVERY_RETRY && list_requests == 1U))) {
		return NULL;
	}
	if (port != CONFIG_KFSW_PARAM_LIST_PORT &&
	    (operation == PARAM_PUSH_REQUEST_V2 || mode == SILENT || pulls <= losses ||
	     pulls == lost_pull)) {
		return NULL;
	}
	if (mode == WAIT_TIMEOUT) {
		/* Model a blocking receive consuming the supplied budget. */
		k_sleep(K_MSEC(timeout));
		return NULL;
	}
	if (mode == PARTIAL && reads++ != 0U) {
		return NULL;
	}
	csp_packet_t *packet = csp_buffer_get(CSP_BUFFER_SIZE);

	zassert_not_null(packet);
	memset(packet->data, 0, CSP_BUFFER_SIZE);
	if (port == CONFIG_KFSW_PARAM_LIST_PORT) {
		packet->data[0] = 4;
		packet->data[1] = requested_index == 2U ? 1U : 0U;
		sys_put_be16(requested_index, &packet->data[2]);
		sys_put_be16(2, &packet->data[4]);
		sys_put_be32(list_crc, &packet->data[6]);
		packet->length = 10;
		if (requested_index < 2U) {
			memcpy(&packet->data[10], &descriptors[requested_index], DESCRIPTOR_SIZE);
			packet->length += DESCRIPTOR_SIZE;
		}
		return packet;
	}
	param_queue_t queue = {0};

	packet->data[0] = mode == BAD_REPLY ? PARAM_PUSH_RESPONSE : PARAM_PULL_RESPONSE_V2;
	packet->data[1] = mode == PARTIAL && pulls == 1U ? 0 : PARAM_FLAG_END;
	param_queue_init(&queue, &packet->data[2], CSP_BUFFER_SIZE - 2, 0, PARAM_QUEUE_TYPE_SET, 2);
	if (mode != PARTIAL || pulls == 1U) {
		append_value(&queue, ID_A, mode == WIDE ? UINT64_MAX : mode == PARTIAL ? 999 : 42);
	}
	if (mode != PARTIAL || pulls != 1U) {
		append_value(&queue, ID_B, 84);
	}
	packet->length = queue.used + 2;
	return packet;
}

static void *setup(void)
{
	const struct kfsw_param_definition_set *sets[] = {&kfsw_param_param_definitions};

	zassert_ok(kfsw_param_init(sets, ARRAY_SIZE(sets)));
	zassert_ok(kfsw_csp_init());
	for (size_t i = 0; i < ARRAY_SIZE(descriptors); i++) {
		descriptors[i].id = sys_cpu_to_be16(ID_A + i);
		descriptors[i].type = PARAM_TYPE_UINT32;
		descriptors[i].size = 1;
		descriptors[i].name[0] = 'a' + i;
		list_crc = crc32_ieee_update(list_crc, (uint8_t *)&descriptors[i], DESCRIPTOR_SIZE);
	}
	return NULL;
}

static void before(void *unused)
{
	ARG_UNUSED(unused);
	mode = GOOD;
	losses = 0;
	lost_pull = 0;
	zassert_ok(kfsw_param_remote_refresh(PEER));
	pulls = pushes = sends = opened = closed = list_requests = lookups = 0;
}

static uint32_t resend_count(void)
{
	struct kfsw_param_stats stats;
	struct kfsw_param_value value;
	struct kfsw_param_info info;

	zassert_ok(kfsw_param_get_stats(&stats));
	zassert_ok(kfsw_param_get("param_read_resends", &value));
	zassert_equal(value.type, KFSW_PARAM_U32);
	zassert_equal(value.scalar.u32, stats.read_resends);
	zassert_ok(kfsw_param_get_info("param_read_resends", &info));
	zassert_equal(info.id, 0x1a20);
	zassert_true(info.flags & KFSW_PARAM_FLAG_READ_ONLY);
	return stats.read_resends;
}

static int read_pair(int64_t deadline)
{
	const char *names[] = {"a", "b"};
	struct kfsw_param_value values[2];
	int buffers = csp_buffer_remaining();
	int result = kfsw_param_remote_get_many_until(PEER, names, 2, values, deadline);

	zassert_equal(opened, closed);
	zassert_equal(csp_buffer_remaining(), buffers);
	if (result == 0) {
		zassert_equal(values[0].scalar.u32, 42);
		zassert_equal(values[1].scalar.u32, 84);
	}
	return result;
}

ZTEST(param_retry, test_clean_and_lost_replies)
{
	for (losses = 0; losses < 3U; losses++) {
		uint32_t previous = resend_count();

		pulls = 0;
		zassert_ok(read_pair(k_uptime_get() + 1000));
		zassert_equal(pulls, losses + 1U);
		zassert_equal(resend_count() - previous, losses);
	}
}

ZTEST(param_retry, test_attempt_ceiling)
{
	mode = SILENT;
	uint32_t previous = resend_count();

	zassert_equal(read_pair(k_uptime_get() + 1000), -ETIMEDOUT);
	zassert_equal(pulls, 3);
	zassert_equal(resend_count() - previous, 2);
}

static void hold_remote_lock(void *first, void *second, void *third)
{
	ARG_UNUSED(first);
	ARG_UNUSED(second);
	ARG_UNUSED(third);
	zassert_ok(k_mutex_lock(&kfsw_param_remote_lock, K_MSEC(100)));
	k_sem_give(&lock_held);
	zassert_ok(k_sem_take(&release_lock, K_MSEC(1000)));
	k_mutex_unlock(&kfsw_param_remote_lock);
}

ZTEST(param_retry, test_lock_admission_uses_the_deadline)
{
	uint32_t previous = resend_count();

	k_thread_create(&lock_thread, lock_stack, K_THREAD_STACK_SIZEOF(lock_stack),
			hold_remote_lock, NULL, NULL, NULL, K_PRIO_PREEMPT(1), 0, K_NO_WAIT);
	zassert_ok(k_sem_take(&lock_held, K_MSEC(100)));
	int64_t start = k_uptime_get();
	int result = read_pair(start + 20);

	k_sem_give(&release_lock);
	zassert_ok(k_thread_join(&lock_thread, K_MSEC(100)));
	zassert_equal(result, -ETIMEDOUT);
	zassert_between_inclusive(k_uptime_get() - start, 20, 50);
	zassert_equal(sends, 0);
	zassert_equal(resend_count(), previous);
}

ZTEST(param_retry, test_descriptor_requests_on_retry_are_counted)
{
	const char *names[] = {"a", "b"};
	struct kfsw_param_value values[2];
	uint32_t previous = resend_count();
	int buffers = csp_buffer_remaining();

	mode = DISCOVERY_RETRY;
	zassert_ok(kfsw_param_remote_get_many_until(PEER + 1U, names, ARRAY_SIZE(names), values,
						    k_uptime_get() + 1000));
	zassert_equal(lookups, 2);
	zassert_equal(list_requests, 4);
	zassert_equal(pulls, 1);
	zassert_equal(sends, 7);
	zassert_equal(resend_count() - previous, 5);
	zassert_equal(values[0].scalar.u32, 42);
	zassert_equal(values[1].scalar.u32, 84);
	zassert_equal(opened, closed);
	zassert_equal(csp_buffer_remaining(), buffers);
}

ZTEST(param_retry, test_deadline_limits_attempts)
{
	int64_t start = k_uptime_get();
	uint32_t previous = resend_count();

	zassert_equal(read_pair(start), -ETIMEDOUT);
	zassert_equal(sends, 0);
	mode = WAIT_TIMEOUT;
	zassert_equal(read_pair(start + 20), -ETIMEDOUT);
	zassert_equal(pulls, 1);
	zassert_between_inclusive(k_uptime_get() - start, 20, 50);
	zassert_equal(resend_count(), previous);
}

ZTEST(param_retry, test_answers_are_not_retried)
{
	const enum response_mode modes[] = {BAD_REPLY, WIDE, REFUSED};
	uint32_t previous = resend_count();

	for (size_t i = 0; i < ARRAY_SIZE(modes); i++) {
		mode = modes[i];
		pulls = 0;
		zassert_equal(read_pair(k_uptime_get() + 1000),
			      mode == REFUSED ? -ECONNREFUSED : -EBADMSG);
		zassert_equal(pulls, mode == REFUSED ? 0 : 1);
	}
	const char *name = "unknown";
	struct kfsw_param_value value;
	unsigned int previous_sends = sends;

	zassert_equal(kfsw_param_remote_get(PEER, name, &value), -ENOENT);
	mode = DENIED;
	zassert_equal(kfsw_param_remote_get(PEER + 1U, "a", &value), -EACCES);
	zassert_equal(sends, previous_sends);
	zassert_equal(resend_count(), previous);
}

ZTEST(param_retry, test_partial_attempts_do_not_combine)
{
	const char *names[] = {"a", "b"};
	struct kfsw_param_value values[2], unchanged[2];
	uint32_t cached_a = param_get_uint32(param_list_find_id(PEER, ID_A));
	uint32_t cached_b = param_get_uint32(param_list_find_id(PEER, ID_B));

	mode = PARTIAL;
	memset(values, 0xa5, sizeof(values));
	memcpy(unchanged, values, sizeof(values));
	zassert_equal(kfsw_param_remote_get_many(PEER, names, 2, values), -EBADMSG);
	zassert_equal(pulls, 2);
	zassert_mem_equal(values, unchanged, sizeof(values));
	zassert_equal(param_get_uint32(param_list_find_id(PEER, ID_A)), cached_a);
	zassert_equal(param_get_uint32(param_list_find_id(PEER, ID_B)), cached_b);
}

ZTEST(param_retry, test_set_acknowledgement_loss_stays_single_shot)
{
	const struct kfsw_param_value value = {
		.type = KFSW_PARAM_U32, .size = sizeof(uint32_t), .scalar.u32 = 10};
	uint32_t previous = resend_count();

	zassert_equal(kfsw_param_remote_set(PEER, "a", &value), -ETIMEDOUT);
	zassert_equal(pushes, 1);
	zassert_equal(pulls, 0);
	zassert_equal(opened, closed);
	zassert_equal(resend_count(), previous);
}

ZTEST(param_retry, test_unsent_attempt_does_not_count)
{
	const enum response_mode modes[] = {RETRY_NO_BUFFER, RETRY_REFUSED};

	for (size_t i = 0; i < ARRAY_SIZE(modes); i++) {
		uint32_t previous = resend_count();

		mode = modes[i];
		pulls = 0;
		losses = 1;
		zassert_equal(read_pair(k_uptime_get() + 1000),
			      mode == RETRY_NO_BUFFER ? -ENOMEM : -ECONNREFUSED);
		zassert_equal(pulls, 1);
		zassert_equal(resend_count(), previous);
	}
}

ZTEST(param_retry, test_later_window_retries_the_logical_read)
{
	const char *names[18];
	/* Two windows do not fit the ztest thread's stack. */
	static struct kfsw_param_value values[18];
	uint32_t previous = resend_count();

	for (size_t i = 0; i < ARRAY_SIZE(names); i++) {
		names[i] = i % 2U == 0U ? "a" : "b";
	}
	lost_pull = 2;
	zassert_ok(kfsw_param_remote_get_many(PEER, names, ARRAY_SIZE(names), values));
	zassert_equal(pulls, 4);
	zassert_equal(resend_count() - previous, 2);
	for (size_t i = 0; i < ARRAY_SIZE(values); i++) {
		zassert_equal(values[i].scalar.u32, i % 2U == 0U ? 42 : 84);
	}
	zassert_equal(opened, closed);
}

ZTEST_SUITE(param_retry, NULL, setup, before, NULL, NULL);
