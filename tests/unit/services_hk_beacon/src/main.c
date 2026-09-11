#include <string.h>

#include <csp/csp.h>

#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>

#include <kfsw/services/hk.h>
#include <kfsw/services/parameter.h>

#define TEST_TABLE 30U

/*
 * A beacon is the one thing housekeeping does without being asked, so the
 * questions here are not about values: they are about whether an unprompted
 * transmitter can be trusted with a link. What does it address, when does it
 * stay quiet, and what does it refuse to be configured as.
 *
 * The tick is internal to the service because nothing outside it should decide
 * when a beacon is due. A test is the exception, and declaring it here is
 * honest about that: driving it through the collector thread instead would
 * make every case below a sleep.
 */
void kfsw_hk_beacon_tick(uint8_t report, int64_t now);

static uint16_t counter_u16 = 0x1234U;
static uint32_t counter_u32 = 0xAABBCCDDU;

static const struct kfsw_param_definition test_definitions[] = {
	{
		.offset = 0x00,
		.type = KFSW_PARAM_U16,
		.flags = KFSW_PARAM_FLAG_READ_ONLY,
		.name = "beacon_test_u16",
		.value = &counter_u16,
	},
	{
		.offset = 0x02,
		.type = KFSW_PARAM_U32,
		.flags = KFSW_PARAM_FLAG_READ_ONLY,
		.name = "beacon_test_u32",
		.value = &counter_u32,
	},
};

static const struct kfsw_param_definition_set test_set = {
	.table = TEST_TABLE,
	.name = "beacontest",
	.definitions = test_definitions,
	.count = ARRAY_SIZE(test_definitions),
};

/* What the wrapped link saw. */
static struct {
	unsigned int sends;
	uint8_t priority;
	uint16_t destination;
	uint8_t destination_port;
	uint8_t source_port;
	uint32_t options;
	uint8_t payload[CONFIG_KFSW_HK_SAMPLE_BYTES];
	uint16_t length;
} sent;

static int buffers_free = 64;
static csp_packet_t test_packet;

csp_packet_t *__wrap_csp_buffer_get(size_t unused)
{
	ARG_UNUSED(unused);
	if (buffers_free <= 0) {
		return NULL;
	}
	memset(&test_packet, 0, sizeof(test_packet));
	return &test_packet;
}

int __wrap_csp_buffer_remaining(void)
{
	return buffers_free;
}

void __wrap_csp_sendto(uint8_t prio, uint16_t dst, uint8_t dst_port, uint8_t src_port,
		       uint32_t opts, csp_packet_t *packet)
{
	sent.sends++;
	sent.priority = prio;
	sent.destination = dst;
	sent.destination_port = dst_port;
	sent.source_port = src_port;
	sent.options = opts;
	sent.length = packet->length;
	if (packet->length <= sizeof(sent.payload)) {
		memcpy(sent.payload, packet->data, packet->length);
	}
}

static void define_and_collect(uint8_t report)
{
	const struct kfsw_hk_entry entries[] = {
		{.node = KFSW_HK_NODE_LOCAL, .param_id = KFSW_PARAM_ID(TEST_TABLE, 0x00)},
		{.node = KFSW_HK_NODE_LOCAL, .param_id = KFSW_PARAM_ID(TEST_TABLE, 0x02)},
	};

	zassert_ok(kfsw_hk_define(report, entries, ARRAY_SIZE(entries)), "the report was refused");
	zassert_ok(kfsw_hk_collect(report), "the report collected nothing");
}

static void *beacon_setup(void)
{
	static const struct kfsw_param_definition_set *const sets[] = {&test_set};

	zassert_ok(kfsw_param_init(sets, ARRAY_SIZE(sets)), "the parameter table did not start");
	zassert_ok(kfsw_hk_init(), "housekeeping did not start");
	return NULL;
}

static void beacon_before(void *fixture)
{
	ARG_UNUSED(fixture);
	for (uint8_t report = 0U; report < CONFIG_KFSW_HK_REPORTS; report++) {
		(void)kfsw_hk_set_beacon(report, 1U, 0U);
		(void)kfsw_hk_clear(report);
	}
	memset(&sent, 0, sizeof(sent));
	buffers_free = 64;
}

ZTEST_SUITE(kfsw_hk_beacon, NULL, beacon_setup, beacon_before, NULL, NULL);

/* The floor is what stops an operator turning a node into a transmitter that
 * swamps its own link, so it is refused rather than clamped.
 */
ZTEST(kfsw_hk_beacon, test_an_interval_under_the_floor_is_refused)
{
	uint16_t node = 0U;
	uint32_t interval = 0U;

	zassert_equal(kfsw_hk_set_beacon(0U, 1U, CONFIG_KFSW_HK_BEACON_FLOOR_MS - 1U), -ERANGE,
		      "an interval under the floor was accepted");
	zassert_ok(kfsw_hk_get_beacon(0U, &node, &interval), "the beacon could not be read back");
	zassert_equal(interval, 0U, "the refused interval was kept anyway");
}

ZTEST(kfsw_hk_beacon, test_the_floor_itself_is_allowed)
{
	zassert_ok(kfsw_hk_set_beacon(0U, 1U, CONFIG_KFSW_HK_BEACON_FLOOR_MS),
		   "the floor was refused by its own limit");
}

/* Address 0 is the broadcast-ish unset case and 16383 is the top of the CSP
 * address space; beyond it the field simply cannot carry the number.
 */
ZTEST(kfsw_hk_beacon, test_an_address_that_cannot_exist_is_refused)
{
	zassert_equal(kfsw_hk_set_beacon(0U, 0U, CONFIG_KFSW_HK_BEACON_FLOOR_MS), -EINVAL,
		      "node 0 was accepted");
	zassert_equal(kfsw_hk_set_beacon(0U, 16384U, CONFIG_KFSW_HK_BEACON_FLOOR_MS), -EINVAL,
		      "an address past the CSP range was accepted");
}

ZTEST(kfsw_hk_beacon, test_an_unknown_report_is_refused)
{
	uint16_t node = 0U;
	uint32_t interval = 0U;

	zassert_equal(
		kfsw_hk_set_beacon(CONFIG_KFSW_HK_REPORTS, 1U, CONFIG_KFSW_HK_BEACON_FLOOR_MS),
		-EINVAL, "a report that does not exist was given a beacon");
	zassert_equal(kfsw_hk_get_beacon(CONFIG_KFSW_HK_REPORTS, &node, &interval), -EINVAL,
		      "a report that does not exist reported a beacon");
}

ZTEST(kfsw_hk_beacon, test_what_was_set_reads_back)
{
	uint16_t node = 0U;
	uint32_t interval = 0U;

	zassert_ok(kfsw_hk_set_beacon(1U, 42U, 7000U), "the beacon was refused");
	zassert_ok(kfsw_hk_get_beacon(1U, &node, &interval), "the beacon could not be read back");
	zassert_equal(node, 42U, "the destination changed");
	zassert_equal(interval, 7000U, "the interval changed");
}

ZTEST(kfsw_hk_beacon, test_zero_stops_it)
{
	uint32_t interval = 7000U;
	uint16_t node = 0U;

	zassert_ok(kfsw_hk_set_beacon(0U, 42U, 7000U), "the beacon was refused");
	zassert_ok(kfsw_hk_set_beacon(0U, 42U, 0U), "the beacon could not be stopped");
	zassert_ok(kfsw_hk_get_beacon(0U, &node, &interval), "the beacon could not be read back");
	zassert_equal(interval, 0U, "a stopped beacon still reports an interval");

	define_and_collect(0U);
	kfsw_hk_beacon_tick(0U, 100000);
	zassert_equal(sent.sends, 0U, "a stopped beacon transmitted");
}

/*
 * The regression this suite exists for.
 *
 * A ground listener recognises housekeeping by the port a frame came *from*,
 * so a beacon must leave from the serving port or it is invisible. It must
 * arrive somewhere nothing binds, or it lands on another node's request
 * handler, where a frame whose first byte is also a version number can be read
 * as a request. The two are different ports and neither may drift.
 */
ZTEST(kfsw_hk_beacon, test_the_ports_are_the_ones_the_ground_expects)
{
	define_and_collect(0U);
	zassert_ok(kfsw_hk_set_beacon(0U, 5U, CONFIG_KFSW_HK_BEACON_FLOOR_MS),
		   "the beacon was refused");

	kfsw_hk_beacon_tick(0U, 100000);

	zassert_equal(sent.sends, 1U, "the beacon did not transmit");
	zassert_equal(sent.source_port, CONFIG_KFSW_HK_CSP_PORT,
		      "a beacon that does not come from the serving port is invisible to ground");
	zassert_equal(sent.destination_port, CONFIG_KFSW_HK_BEACON_PORT,
		      "a beacon addressed to the serving port can be read as a request");
	zassert_not_equal(CONFIG_KFSW_HK_BEACON_PORT, CONFIG_KFSW_HK_CSP_PORT,
			  "the beacon port and the serving port must differ");
	zassert_equal(sent.destination, 5U, "the beacon went to the wrong node");
}

/* Low priority and a CRC, because a beacon must never outrank the traffic
 * somebody is waiting for, and a corrupt sample should be dropped not decoded.
 */
ZTEST(kfsw_hk_beacon, test_a_beacon_yields_to_real_traffic)
{
	define_and_collect(0U);
	zassert_ok(kfsw_hk_set_beacon(0U, 5U, CONFIG_KFSW_HK_BEACON_FLOOR_MS),
		   "the beacon was refused");

	kfsw_hk_beacon_tick(0U, 100000);

	zassert_equal(sent.priority, CSP_PRIO_LOW, "a beacon claimed priority over real traffic");
	zassert_true((sent.options & CSP_O_CRC32) != 0U, "a beacon went out without a checksum");
}

/* A beacon is the request path's frame, not a second format. */
ZTEST(kfsw_hk_beacon, test_a_beacon_carries_the_newest_sample)
{
	struct kfsw_hk_sample sample;

	define_and_collect(0U);
	zassert_ok(kfsw_hk_get(0U, 0U, &sample), "the report held no sample");
	zassert_ok(kfsw_hk_set_beacon(0U, 5U, CONFIG_KFSW_HK_BEACON_FLOOR_MS),
		   "the beacon was refused");

	kfsw_hk_beacon_tick(0U, 100000);

	zassert_equal(sent.length, sample.length, "the beacon is not the frame a request gets");
	zassert_mem_equal(sent.payload, sample.data, sample.length,
			  "the beacon carries different bytes than the shell reports");
}

/*
 * The rule that makes an unprompted transmitter safe to have at all: a reply
 * somebody is waiting for outranks a broadcast nobody asked for. Counted
 * rather than logged, so a quiet beacon can still be explained after the pass.
 */
ZTEST(kfsw_hk_beacon, test_a_beacon_never_takes_the_last_buffers)
{
	struct kfsw_hk_stats before;
	struct kfsw_hk_stats after;

	define_and_collect(0U);
	zassert_ok(kfsw_hk_set_beacon(0U, 5U, CONFIG_KFSW_HK_BEACON_FLOOR_MS),
		   "the beacon was refused");

	kfsw_hk_get_stats(&before);
	buffers_free = CONFIG_KFSW_HK_BEACON_BUFFER_RESERVE;
	kfsw_hk_beacon_tick(0U, 100000);
	kfsw_hk_get_stats(&after);

	zassert_equal(sent.sends, 0U, "a beacon spent the buffers a reply needed");
	zassert_equal(after.beacons_skipped, before.beacons_skipped + 1U,
		      "the skip was not counted, so a quiet beacon cannot be explained");
	zassert_equal(after.beacons_sent, before.beacons_sent, "a skipped beacon was counted sent");
}

/* And it recovers once the pool does, rather than staying off. */
ZTEST(kfsw_hk_beacon, test_it_speaks_again_once_the_pool_recovers)
{
	define_and_collect(0U);
	zassert_ok(kfsw_hk_set_beacon(0U, 5U, CONFIG_KFSW_HK_BEACON_FLOOR_MS),
		   "the beacon was refused");

	buffers_free = CONFIG_KFSW_HK_BEACON_BUFFER_RESERVE;
	kfsw_hk_beacon_tick(0U, 100000);
	zassert_equal(sent.sends, 0U, "the beacon ignored the reserve");

	buffers_free = 64;
	kfsw_hk_beacon_tick(0U, 100000 + CONFIG_KFSW_HK_BEACON_FLOOR_MS);
	zassert_equal(sent.sends, 1U, "the beacon stayed quiet after the pool recovered");
}

/* A report nobody has collected has nothing to announce. */
ZTEST(kfsw_hk_beacon, test_a_report_with_no_sample_says_nothing)
{
	zassert_ok(kfsw_hk_set_beacon(0U, 5U, CONFIG_KFSW_HK_BEACON_FLOOR_MS),
		   "the beacon was refused");

	kfsw_hk_beacon_tick(0U, 100000);

	zassert_equal(sent.sends, 0U, "a report with no sample transmitted anyway");
}

/* The interval is an interval, not a lower bound checked once. */
ZTEST(kfsw_hk_beacon, test_the_interval_is_kept_between_beacons)
{
	define_and_collect(0U);
	zassert_ok(kfsw_hk_set_beacon(0U, 5U, CONFIG_KFSW_HK_BEACON_FLOOR_MS),
		   "the beacon was refused");

	kfsw_hk_beacon_tick(0U, 100000);
	zassert_equal(sent.sends, 1U, "the first beacon did not transmit");

	/* One millisecond short of due. */
	kfsw_hk_beacon_tick(0U, 100000 + CONFIG_KFSW_HK_BEACON_FLOOR_MS - 1);
	zassert_equal(sent.sends, 1U, "the beacon transmitted before it was due");

	kfsw_hk_beacon_tick(0U, 100000 + CONFIG_KFSW_HK_BEACON_FLOOR_MS);
	zassert_equal(sent.sends, 2U, "the beacon missed its own interval");
}

/* Reports beacon independently: one silent report must not silence another. */
ZTEST(kfsw_hk_beacon, test_reports_beacon_independently)
{
	define_and_collect(1U);
	zassert_ok(kfsw_hk_set_beacon(1U, 9U, CONFIG_KFSW_HK_BEACON_FLOOR_MS),
		   "the beacon was refused");

	kfsw_hk_beacon_tick(0U, 100000);
	zassert_equal(sent.sends, 0U, "a report with no beacon transmitted");

	kfsw_hk_beacon_tick(1U, 100000);
	zassert_equal(sent.sends, 1U, "the report that was told to beacon stayed quiet");
	zassert_equal(sent.destination, 9U, "the beacon went to the wrong node");
}

/* The counter an operator reads when the ground goes quiet. */
ZTEST(kfsw_hk_beacon, test_what_was_sent_is_counted)
{
	struct kfsw_hk_stats before;
	struct kfsw_hk_stats after;

	define_and_collect(0U);
	zassert_ok(kfsw_hk_set_beacon(0U, 5U, CONFIG_KFSW_HK_BEACON_FLOOR_MS),
		   "the beacon was refused");

	kfsw_hk_get_stats(&before);
	kfsw_hk_beacon_tick(0U, 100000);
	kfsw_hk_beacon_tick(0U, 100000 + CONFIG_KFSW_HK_BEACON_FLOOR_MS);
	kfsw_hk_get_stats(&after);

	zassert_equal(after.beacons_sent, before.beacons_sent + 2U,
		      "the node did not count what it put on the link");
}
