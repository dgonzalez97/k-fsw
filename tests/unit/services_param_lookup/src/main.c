/* Reading one remote parameter used to cost one CSP exchange per parameter the
 * node owned: the whole descriptor list, downloaded to answer a question about
 * a single entry. These cases pin the exchange count, which is the property
 * that regressed, rather than only the value that came back.
 */
#include <errno.h>
#include <string.h>

#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/crc.h>
#include <zephyr/ztest.h>

#include <csp/csp.h>
#include <param/param.h>
#include <param/param_list.h>
#include <param/param_queue.h>
#include <param/param_server.h>

#include <kfsw/comms/csp.h>
#include <kfsw/services/parameter.h>

#include "param_list.h"

/* One node per case. The descriptor cache holds a single node at a time, so
 * switching nodes is what gives each test a cold cache to measure against. */
#define PEER_NAMED 42
#define PEER_REPEAT 43
#define PEER_SILENT 44
#define PEER_UNKNOWN 45
#define PEER_MISNAMED 46
#define ID_A 0x100
#define LOOKUP_VERSION 5U
#define LOOKUP_HEADER 2U
#define LIST_HEADER 10U
#define DESCRIPTOR_SIZE (offsetof(param_transfer3_t, help) + 1U)

enum lookup_mode {
	LOOKUP_SERVED,   /* the node answers lookups */
	LOOKUP_SILENT,   /* an older node: no reply at all */
	LOOKUP_UNKNOWN,  /* the node has no such parameter */
	LOOKUP_MISNAMED, /* the node answers with a different descriptor */
};

static enum lookup_mode mode;
static unsigned int transactions;
static unsigned int list_requests;
static unsigned int lookup_requests;
static uint8_t port;
static uint16_t requested_index;
static bool lookup_pending;
static char requested_name[KFSW_PARAM_NAME_MAX + 1U];
static uint8_t connection_storage;
static param_transfer3_t descriptor;
static uint16_t zero;
static uint32_t value_a = 42;

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
	ARG_UNUSED(opts);
	zassert_true(node >= PEER_NAMED && node <= PEER_MISNAMED);
	port = destination;
	transactions++;
	return (csp_conn_t *)&connection_storage;
}

void __wrap_csp_send(csp_conn_t *connection, csp_packet_t *packet)
{
	ARG_UNUSED(connection);
	if (port == CONFIG_KFSW_PARAM_LIST_PORT) {
		if (packet->data[0] == LOOKUP_VERSION) {
			size_t length = packet->length - 1U;

			zassert_true(length > 0U && length <= KFSW_PARAM_NAME_MAX);
			memcpy(requested_name, &packet->data[1], length);
			requested_name[length] = '\0';
			lookup_pending = true;
			lookup_requests++;
		} else {
			zassert_equal(packet->length, 7);
			requested_index = sys_get_be16(&packet->data[1]);
			lookup_pending = false;
			list_requests++;
		}
	}
	csp_buffer_free(packet);
}

int __wrap_csp_close(csp_conn_t *connection)
{
	ARG_UNUSED(connection);
	return 0;
}

static csp_packet_t *lookup_reply(void)
{
	csp_packet_t *packet;

	if (mode == LOOKUP_SILENT) {
		return NULL;
	}
	packet = csp_buffer_get(CSP_BUFFER_SIZE);
	zassert_not_null(packet);
	memset(packet->data, 0, CSP_BUFFER_SIZE);
	packet->data[0] = LOOKUP_VERSION;
	packet->data[1] = (mode == LOOKUP_UNKNOWN) ? 1U : 0U;
	packet->length = LOOKUP_HEADER;
	if (mode != LOOKUP_UNKNOWN) {
		param_transfer3_t answer = descriptor;

		if (mode == LOOKUP_MISNAMED) {
			answer.name[0] = 'z';
		}
		memcpy(&packet->data[LOOKUP_HEADER], &answer, DESCRIPTOR_SIZE);
		packet->length += DESCRIPTOR_SIZE;
	}
	return packet;
}

/* The walk the client falls back to when a node does not serve lookups. */
static csp_packet_t *list_reply(void)
{
	csp_packet_t *packet = csp_buffer_get(CSP_BUFFER_SIZE);

	zassert_not_null(packet);
	memset(packet->data, 0, CSP_BUFFER_SIZE);
	packet->data[0] = 4;
	packet->data[1] = (requested_index == 1U) ? 1U : 0U;
	sys_put_be16(requested_index, &packet->data[2]);
	sys_put_be16(1, &packet->data[4]);
	sys_put_be32(crc32_ieee_update(0, (const uint8_t *)&descriptor, DESCRIPTOR_SIZE),
		     &packet->data[6]);
	packet->length = LIST_HEADER;
	if (requested_index == 0U) {
		memcpy(&packet->data[LIST_HEADER], &descriptor, DESCRIPTOR_SIZE);
		packet->length += DESCRIPTOR_SIZE;
	}
	return packet;
}

csp_packet_t *__wrap_csp_read(csp_conn_t *connection, uint32_t timeout)
{
	ARG_UNUSED(connection);
	zassert_true(timeout > 0);
	if (port == CONFIG_KFSW_PARAM_LIST_PORT) {
		return lookup_pending ? lookup_reply() : list_reply();
	}

	csp_packet_t *packet = csp_buffer_get(CSP_BUFFER_SIZE);
	param_queue_t queue = {0};
	param_t entry = {.id = ID_A,
			 .node = &zero,
			 .array_size = 1,
			 .type = PARAM_TYPE_UINT32,
			 .array_step = 4};

	zassert_not_null(packet);
	packet->data[0] = PARAM_PULL_RESPONSE_V2;
	packet->data[1] = PARAM_FLAG_END;
	param_queue_init(&queue, &packet->data[2], CSP_BUFFER_SIZE - 2, 0, PARAM_QUEUE_TYPE_SET, 2);
	zassert_ok(param_queue_add(&queue, &entry, -1, &value_a));
	packet->length = queue.used + 2;
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

	descriptor.id = sys_cpu_to_be16(ID_A);
	descriptor.type = PARAM_TYPE_UINT32;
	descriptor.size = 1;
	strcpy(descriptor.name, "a");
	return NULL;
}

static void before(void *unused)
{
	ARG_UNUSED(unused);
	mode = LOOKUP_SERVED;
	transactions = 0;
	list_requests = 0;
	lookup_requests = 0;
	lookup_pending = false;
}

/* The claim that matters: one descriptor exchange and one value exchange, no
 * matter how many parameters the node owns.
 */
ZTEST(param_lookup, test_a_named_read_asks_only_for_that_name)
{
	struct kfsw_param_value value;

	zassert_ok(kfsw_param_remote_get(PEER_NAMED, "a", &value));
	zassert_equal(value.scalar.u32, 42);
	zassert_equal(lookup_requests, 1, "the name was not looked up directly");
	zassert_equal(list_requests, 0, "the whole list was walked to read one value");
	zassert_equal(transactions, 2, "a named read cost more than a lookup and a value");
}

/* A descriptor already held answers with no packet at all. */
ZTEST(param_lookup, test_a_second_read_reuses_the_descriptor)
{
	struct kfsw_param_value value;

	zassert_ok(kfsw_param_remote_get(PEER_REPEAT, "a", &value));
	transactions = 0;
	lookup_requests = 0;

	zassert_ok(kfsw_param_remote_get(PEER_REPEAT, "a", &value));
	zassert_equal(lookup_requests, 0, "a cached descriptor was fetched again");
	zassert_equal(transactions, 1, "a repeat read cost more than its value");
}

/* A node that predates the lookup answers nothing, and must not be reported as
 * missing the parameter: the walk still has to happen.
 */
ZTEST(param_lookup, test_silence_falls_back_to_walking_the_list)
{
	struct kfsw_param_value value;

	mode = LOOKUP_SILENT;
	zassert_ok(kfsw_param_remote_get(PEER_SILENT, "a", &value));
	zassert_equal(value.scalar.u32, 42);
	zassert_true(lookup_requests >= 1, "the lookup was never attempted");
	zassert_true(list_requests >= 1, "the fallback walk never happened");
}

ZTEST(param_lookup, test_an_unknown_name_is_reported_without_a_walk)
{
	struct kfsw_param_value value;

	mode = LOOKUP_UNKNOWN;
	zassert_equal(kfsw_param_remote_get(PEER_UNKNOWN, "missing", &value), -ENOENT);
	zassert_equal(list_requests, 0, "an absent name triggered a whole-list download");
}

/* A reply naming something else is a different parameter's descriptor, and
 * accepting it would publish one parameter's value under another's name.
 */
ZTEST(param_lookup, test_a_misnamed_answer_is_refused)
{
	struct kfsw_param_value value;

	mode = LOOKUP_MISNAMED;
	zassert_not_equal(kfsw_param_remote_get(PEER_MISNAMED, "a", &value), 0);
}

ZTEST_SUITE(param_lookup, NULL, setup, before, NULL, NULL);
