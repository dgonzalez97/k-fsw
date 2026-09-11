#include <string.h>

#include <csp/csp.h>

#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>

#include <kfsw/services/hk.h>
#include <kfsw/services/parameter.h>

#define TEST_TABLE 30U
#define REQUEST_SIZE 5U
#define MAX_REPLIES 8

/*
 * What a housekeeping request means.
 *
 * The accept loop around this only accepts, reads and closes; everything that
 * can be got wrong lives in the answer — refusing a frame too short to hold a
 * request, refusing a version this build cannot decode, honouring a count and
 * a starting age, and stopping cleanly when the buffer pool runs dry rather
 * than holding a connection open while the router needs it.
 *
 * The request is served directly, so none of this needs a router.
 */
void kfsw_hk_serve_request(struct csp_conn_s *connection, struct csp_packet_s *request);

static uint16_t counter_u16 = 0x1234U;
static uint32_t counter_u32 = 0xAABBCCDDU;

static const struct kfsw_param_definition test_definitions[] = {
	{
		.offset = 0x00,
		.type = KFSW_PARAM_U16,
		.flags = KFSW_PARAM_FLAG_READ_ONLY,
		.name = "server_test_u16",
		.value = &counter_u16,
	},
	{
		.offset = 0x02,
		.type = KFSW_PARAM_U32,
		.flags = KFSW_PARAM_FLAG_READ_ONLY,
		.name = "server_test_u32",
		.value = &counter_u32,
	},
};

static const struct kfsw_param_definition_set test_set = {
	.table = TEST_TABLE,
	.name = "servertest",
	.definitions = test_definitions,
	.count = ARRAY_SIZE(test_definitions),
};

/* What the wrapped link saw, and what it will allow. */
static struct {
	unsigned int replies;
	unsigned int frees;
	uint16_t length[MAX_REPLIES];
	uint8_t payload[MAX_REPLIES][CONFIG_KFSW_HK_SAMPLE_BYTES];
} link;

static int buffers_available = MAX_REPLIES;
static csp_packet_t packets[MAX_REPLIES];

csp_packet_t *__wrap_csp_buffer_get(size_t unused)
{
	ARG_UNUSED(unused);
	if (buffers_available <= 0) {
		return NULL;
	}
	buffers_available--;
	return &packets[MAX_REPLIES - 1 - buffers_available];
}

void __wrap_csp_buffer_free(void *packet)
{
	ARG_UNUSED(packet);
	link.frees++;
}

int __wrap_csp_send(csp_conn_t *connection, csp_packet_t *packet)
{
	ARG_UNUSED(connection);
	if (link.replies < MAX_REPLIES) {
		link.length[link.replies] = packet->length;
		if (packet->length <= CONFIG_KFSW_HK_SAMPLE_BYTES) {
			memcpy(link.payload[link.replies], packet->data, packet->length);
		}
	}
	link.replies++;
	return CSP_ERR_NONE;
}

/* A request as it arrives off the wire. */
static csp_packet_t request_storage;

static csp_packet_t *make_request(uint8_t version, uint8_t report, uint8_t count, uint16_t age,
				  uint16_t length)
{
	memset(&request_storage, 0, sizeof(request_storage));
	request_storage.data[0] = version;
	request_storage.data[1] = report;
	request_storage.data[2] = count;
	sys_put_be16(age, &request_storage.data[3]);
	request_storage.length = length;
	return &request_storage;
}

static void define_report(uint8_t report)
{
	const struct kfsw_hk_entry entries[] = {
		{.node = KFSW_HK_NODE_LOCAL, .param_id = KFSW_PARAM_ID(TEST_TABLE, 0x00)},
		{.node = KFSW_HK_NODE_LOCAL, .param_id = KFSW_PARAM_ID(TEST_TABLE, 0x02)},
	};

	zassert_ok(kfsw_hk_define(report, entries, ARRAY_SIZE(entries)), "the report was refused");
}

static void *server_setup(void)
{
	static const struct kfsw_param_definition_set *const sets[] = {&test_set};

	zassert_ok(kfsw_param_init(sets, ARRAY_SIZE(sets)), "the parameter table did not start");
	zassert_ok(kfsw_hk_init(), "housekeeping did not start");
	return NULL;
}

static void server_before(void *fixture)
{
	ARG_UNUSED(fixture);
	for (uint8_t report = 0U; report < CONFIG_KFSW_HK_REPORTS; report++) {
		(void)kfsw_hk_clear(report);
	}
	memset(&link, 0, sizeof(link));
	buffers_available = MAX_REPLIES;
}

ZTEST_SUITE(kfsw_hk_server, NULL, server_setup, server_before, NULL, NULL);

/*
 * A request the server cannot trust is dropped, and its buffer is always given
 * back. A leaked buffer per bad frame is a node that stops answering after a
 * few minutes of a noisy link, which is the hardest kind of fault to find from
 * the ground.
 */
ZTEST(kfsw_hk_server, test_a_request_too_short_to_read_is_refused)
{
	kfsw_hk_serve_request(
		NULL, make_request(KFSW_HK_PROTOCOL_VERSION, 0U, 1U, 0U, REQUEST_SIZE - 1U));

	zassert_equal(link.replies, 0U, "a truncated request was answered");
	zassert_equal(link.frees, 1U, "a truncated request leaked its buffer");
}

ZTEST(kfsw_hk_server, test_a_version_this_build_cannot_decode_is_refused)
{
	kfsw_hk_serve_request(
		NULL, make_request(KFSW_HK_PROTOCOL_VERSION + 1U, 0U, 1U, 0U, REQUEST_SIZE));

	zassert_equal(link.replies, 0U,
		      "a request from a protocol this build does not know "
		      "was answered");
	zassert_equal(link.frees, 1U, "the refused request leaked its buffer");
}

ZTEST(kfsw_hk_server, test_an_unknown_report_is_answered_with_silence)
{
	kfsw_hk_serve_request(NULL, make_request(KFSW_HK_PROTOCOL_VERSION, CONFIG_KFSW_HK_REPORTS,
						 1U, 0U, REQUEST_SIZE));

	zassert_equal(link.replies, 0U, "a report that does not exist produced a reply");
	zassert_equal(link.frees, 1U, "the request leaked its buffer");
}

ZTEST(kfsw_hk_server, test_a_report_with_no_samples_says_nothing)
{
	define_report(0U);

	kfsw_hk_serve_request(NULL,
			      make_request(KFSW_HK_PROTOCOL_VERSION, 0U, 4U, 0U, REQUEST_SIZE));

	zassert_equal(link.replies, 0U, "a report holding nothing produced a reply");
}

/* One packet per sample, so a lost packet costs one sample rather than the
 * whole answer.
 */
ZTEST(kfsw_hk_server, test_each_sample_is_its_own_packet)
{
	struct kfsw_hk_sample newest;

	define_report(0U);
	for (int i = 0; i < 3; i++) {
		zassert_ok(kfsw_hk_collect(0U), "the report collected nothing");
	}

	kfsw_hk_serve_request(NULL,
			      make_request(KFSW_HK_PROTOCOL_VERSION, 0U, 3U, 0U, REQUEST_SIZE));

	zassert_equal(link.replies, 3U, "three samples did not arrive as three packets");
	zassert_ok(kfsw_hk_get(0U, 0U, &newest), "the report held no sample");
	zassert_equal(link.length[0], newest.length, "the first reply is not a whole frame");
	zassert_mem_equal(link.payload[0], newest.data, newest.length,
			  "the first reply is not the newest sample");
}

ZTEST(kfsw_hk_server, test_a_count_is_honoured)
{
	define_report(0U);
	for (int i = 0; i < 4; i++) {
		zassert_ok(kfsw_hk_collect(0U), "the report collected nothing");
	}

	kfsw_hk_serve_request(NULL,
			      make_request(KFSW_HK_PROTOCOL_VERSION, 0U, 2U, 0U, REQUEST_SIZE));

	zassert_equal(link.replies, 2U, "the server sent more than it was asked for");
}

/* Asking from an age is how ground picks up where a dropped pass left off. */
ZTEST(kfsw_hk_server, test_a_starting_age_is_honoured)
{
	struct kfsw_hk_sample second_oldest;

	define_report(0U);
	for (int i = 0; i < 3; i++) {
		zassert_ok(kfsw_hk_collect(0U), "the report collected nothing");
	}

	kfsw_hk_serve_request(NULL,
			      make_request(KFSW_HK_PROTOCOL_VERSION, 0U, 1U, 1U, REQUEST_SIZE));

	zassert_equal(link.replies, 1U, "the request from an age produced nothing");
	zassert_ok(kfsw_hk_get(0U, 1U, &second_oldest), "the report held no sample at that age");
	zassert_mem_equal(link.payload[0], second_oldest.data, second_oldest.length,
			  "the server answered from the wrong age");
}

/* Asking past the end is a normal thing for ground to do when it does not know
 * how much the node kept.
 */
ZTEST(kfsw_hk_server, test_asking_past_the_end_stops_rather_than_wrapping)
{
	define_report(0U);
	zassert_ok(kfsw_hk_collect(0U), "the report collected nothing");

	kfsw_hk_serve_request(NULL,
			      make_request(KFSW_HK_PROTOCOL_VERSION, 0U, 8U, 0U, REQUEST_SIZE));

	zassert_equal(link.replies, 1U, "the server sent more samples than it held");
}

ZTEST(kfsw_hk_server, test_an_age_beyond_what_is_held_says_nothing)
{
	define_report(0U);
	zassert_ok(kfsw_hk_collect(0U), "the report collected nothing");

	kfsw_hk_serve_request(NULL,
			      make_request(KFSW_HK_PROTOCOL_VERSION, 0U, 4U, 9U, REQUEST_SIZE));

	zassert_equal(link.replies, 0U, "an age past the ring produced a reply");
}

/*
 * An empty pool is normal under load, not a fault. The server sends what it
 * managed and stops, rather than waiting on a buffer while holding a
 * connection the router wants back.
 */
ZTEST(kfsw_hk_server, test_an_empty_pool_ends_the_answer_rather_than_waiting)
{
	define_report(0U);
	for (int i = 0; i < 3; i++) {
		zassert_ok(kfsw_hk_collect(0U), "the report collected nothing");
	}

	buffers_available = 1;
	kfsw_hk_serve_request(NULL,
			      make_request(KFSW_HK_PROTOCOL_VERSION, 0U, 3U, 0U, REQUEST_SIZE));

	zassert_equal(link.replies, 1U, "the server did not send what it could");
	zassert_equal(buffers_available, 0, "the server took a buffer it did not have");
}

ZTEST(kfsw_hk_server, test_no_pool_at_all_is_survivable)
{
	define_report(0U);
	zassert_ok(kfsw_hk_collect(0U), "the report collected nothing");

	buffers_available = 0;
	kfsw_hk_serve_request(NULL,
			      make_request(KFSW_HK_PROTOCOL_VERSION, 0U, 2U, 0U, REQUEST_SIZE));

	zassert_equal(link.replies, 0U, "the server replied without a buffer");
	zassert_equal(link.frees, 1U, "the request leaked its buffer when the pool was empty");
}

/* Whatever else happens, the request itself is given back exactly once. */
ZTEST(kfsw_hk_server, test_the_request_is_always_given_back)
{
	define_report(0U);
	zassert_ok(kfsw_hk_collect(0U), "the report collected nothing");

	kfsw_hk_serve_request(NULL,
			      make_request(KFSW_HK_PROTOCOL_VERSION, 0U, 1U, 0U, REQUEST_SIZE));

	zassert_equal(link.frees, 1U, "a served request was freed %u times", link.frees);
}
