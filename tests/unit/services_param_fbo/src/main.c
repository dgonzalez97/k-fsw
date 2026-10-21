#include <errno.h>

#include <csp/csp.h>
#include <csp/csp_crc32.h>
#include <csp/csp_iflist.h>
#include <csp/csp_interface.h>
#include <csp/interfaces/csp_if_lo.h>
#include <param/param_queue.h>
#include <param/param_server.h>
#include <zephyr/ztest.h>

#include <kfsw/comms/csp.h>
#include <kfsw/services/fbo.h>
#include <kfsw/services/command.h>
#include <kfsw/services/parameter.h>

#include "parameter_internal.h"

static K_SEM_DEFINE(reply, 0, 1);
static atomic_t latency_calls;
static int latency_error;
static atomic_t guard_calls;
static uint32_t guard;

int __real_kfsw_fbo_schedule_set_latency_s(uint32_t seconds);

int __wrap_kfsw_fbo_schedule_set_latency_s(uint32_t seconds)
{
	atomic_inc(&latency_calls);
	return latency_error != 0 ? latency_error : __real_kfsw_fbo_schedule_set_latency_s(seconds);
}

static int validate_guard(const union kfsw_param_scalar *value)
{
	return value->u32 > 1U ? -ERANGE : 0;
}

static void guard_changed(const union kfsw_param_scalar *value)
{
	ARG_UNUSED(value);
	atomic_inc(&guard_calls);
}

static int receive_reply(csp_iface_t *iface, uint16_t via, csp_packet_t *packet, int from_me)
{
	ARG_UNUSED(iface);
	ARG_UNUSED(via);
	ARG_UNUSED(from_me);
	if (packet->id.sport == CONFIG_KFSW_PARAM_PORT && packet->length >= 2U &&
	    packet->data[0] == PARAM_PUSH_RESPONSE && packet->data[1] == PARAM_FLAG_END) {
		k_sem_give(&reply);
	}
	csp_buffer_free(packet);
	return CSP_ERR_NONE;
}

static csp_iface_t link = {.name = "PEER", .addr = 7, .netmask = 14, .nexthop = receive_reply};

static void push_latency(uint32_t seconds, int tail)
{
	static uint8_t next_port = 20;
	csp_packet_t *packet = csp_buffer_get(CSP_BUFFER_SIZE);
	param_queue_t queue = {0};
	uint16_t node = CONFIG_KFSW_CSP_ADDRESS;
	const struct kfsw_param_entry *entry = kfsw_param_find_name("fbo_sched_latency_s");
	param_t parameter = {.id = entry->info.id,
			     .node = &node,
			     .type = PARAM_TYPE_UINT32,
			     .array_size = 1,
			     .array_step = sizeof(uint32_t)};

	zassert_not_null(packet);
	packet->id = (csp_id_t){.src = 42,
				.dst = CONFIG_KFSW_CSP_ADDRESS,
				.sport = next_port++,
				.dport = CONFIG_KFSW_PARAM_PORT,
				.pri = CSP_PRIO_NORM,
				.flags = CSP_FCRC32};
	packet->data[0] = PARAM_PUSH_REQUEST_V2;
	packet->data[1] = 0;
	param_queue_init(&queue, &packet->data[2], CSP_BUFFER_SIZE - 2, 0, PARAM_QUEUE_TYPE_SET, 2);
	if (tail == 2) {
		param_t timeout = parameter;
		uint32_t milliseconds = (seconds + 1U) * 1000U;

		timeout.id = KFSW_PARAM_ID(KFSW_COMMAND_PARAM_TABLE_ID, 0x14);
		zassert_ok(param_queue_add(&queue, &timeout, -1, &milliseconds));
	}
	zassert_ok(param_queue_add(&queue, &parameter, -1, &seconds));
	if (tail == 1) {
		uint32_t invalid = 2U;

		parameter.id = KFSW_PARAM_ID(20, 0);
		zassert_ok(param_queue_add(&queue, &parameter, -1, &invalid));
	}
	packet->length = queue.used + 2;
	zassert_ok(csp_crc32_append(packet));
	zassert_ok(csp_if_lo.nexthop(&csp_if_lo, CSP_NO_VIA_ADDRESS, packet, 0));
}

static void assert_state(uint32_t seconds, atomic_val_t calls)
{
	const struct kfsw_param_entry *entry = kfsw_param_find_name("fbo_sched_latency_s");
	struct kfsw_param_value stored;

	/* Inspect backing storage before a sample can copy scheduler state over it. */
	kfsw_param_table_lock();
	zassert_ok(kfsw_param_read_stored_entry(entry, &stored));
	kfsw_param_table_unlock();
	zassert_equal(stored.scalar.u32, seconds, "latency parameter changed");
	zassert_equal(kfsw_fbo_schedule_get_latency_s(), seconds,
		      "rejected PUSH changed scheduler latency");
	zassert_equal(atomic_get(&latency_calls), calls, "unexpected latency application count");
	zassert_equal(guard, 0U);
	zassert_equal(atomic_get(&guard_calls), 0);
}

ZTEST(services_param_fbo, test_latency_acceptance)
{
	static const struct kfsw_param_definition definition = {
		.offset = 0,
		.type = KFSW_PARAM_U32,
		.name = "guard",
		.value = &guard,
		.validate = validate_guard,
		.changed = guard_changed,
	};
	static const struct kfsw_param_definition_set set = {
		.table = 20,
		.name = "test",
		.definitions = &definition,
		.count = 1,
	};
	const struct kfsw_param_definition_set *sets[] = {&kfsw_fbo_param_definitions,
							  &kfsw_command_param_definitions, &set};
	const uint32_t initial = CONFIG_KFSW_FBO_SCHEDULE_LATENCY_S;
	struct kfsw_param_value local = {
		.type = KFSW_PARAM_U32,
		.size = sizeof(uint32_t),
		.scalar.u32 = initial + 2U,
	};

	zassert_ok(kfsw_param_init(sets, ARRAY_SIZE(sets)));
	zassert_ok(kfsw_csp_init());
	csp_iflist_add(&link);
	zassert_ok(kfsw_csp_route_table_apply("0/0 PEER"));
	zassert_ok(kfsw_param_server_start());
	zassert_ok(kfsw_csp_start());
	atomic_clear(&latency_calls);
	atomic_clear(&guard_calls);

	push_latency(initial + 1U, true);
	zassert_not_equal(k_sem_take(&reply, K_MSEC(100)), 0, "rejected PUSH was ACKed");
	assert_state(initial, 0);

	push_latency(initial + 1U, false);
	zassert_ok(k_sem_take(&reply, K_SECONDS(1)), "valid PUSH was not ACKed");
	assert_state(initial + 1U, 1);

	push_latency(0U, false);
	zassert_not_equal(k_sem_take(&reply, K_MSEC(100)), 0, "invalid latency was ACKed");
	assert_state(initial + 1U, 1);

	zassert_ok(kfsw_param_set("fbo_sched_latency_s", &local));
	assert_state(initial + 2U, 2);

	uint32_t timeout_before = kfsw_command_get_timeout_ms();

	push_latency(initial + 2U, 2);
	zassert_not_equal(k_sem_take(&reply, K_MSEC(100)), 0, "coupled batch was ACKed");
	assert_state(initial + 2U, 2);
	zassert_equal(kfsw_command_get_timeout_ms(), timeout_before);

	latency_error = -EBUSY;
	push_latency(initial + 3U, 0);
	zassert_not_equal(k_sem_take(&reply, K_MSEC(100)), 0, "failed application was ACKed");
	assert_state(initial + 2U, 3);
	local.scalar.u32 = initial + 3U;
	zassert_equal(kfsw_param_set("fbo_sched_latency_s", &local), -EBUSY);
	assert_state(initial + 2U, 4);
	latency_error = 0;
	struct kfsw_param_value timeout = {.type = KFSW_PARAM_U32,
					   .size = sizeof(uint32_t),
					   .scalar.u32 = (initial + 4U) * 1000U};

	zassert_ok(kfsw_param_set("cmd_timeout_ms", &timeout));
	zassert_equal(kfsw_param_set("fbo_sched_latency_s", &local), -ERANGE);
	assert_state(initial + 2U, 4);
	timeout.scalar.u32 = timeout_before;
	zassert_ok(kfsw_param_set("cmd_timeout_ms", &timeout));

	atomic_set(&latency_calls, 2);

	kfsw_param_table_lock();
	zassert_ok(kfsw_param_write_default(kfsw_param_find_name("fbo_sched_latency_s")));
	kfsw_param_table_unlock();
	assert_state(initial, 3);
}

ZTEST_SUITE(services_param_fbo, NULL, NULL, NULL, NULL, NULL);
