#include <string.h>

#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>

#include <kfsw/services/hk.h>
#include <kfsw/services/parameter.h>

#define TEST_TABLE 30U

static uint16_t counter_u16 = 0x1234U;
static uint32_t counter_u32 = 0xAABBCCDDU;
static uint8_t counter_u8 = 0x5AU;
static char label[16] = "hello";

static void sample_counter(void *value)
{
	*(uint16_t *)value = counter_u16;
}

static const struct kfsw_param_definition test_definitions[] = {
	{
		.offset = 0x00,
		.type = KFSW_PARAM_U16,
		.flags = KFSW_PARAM_FLAG_READ_ONLY,
		.name = "hk_test_u16",
		.value = &counter_u16,
		.sample = sample_counter,
	},
	{
		.offset = 0x02,
		.type = KFSW_PARAM_U32,
		.flags = KFSW_PARAM_FLAG_READ_ONLY,
		.name = "hk_test_u32",
		.value = &counter_u32,
	},
	{
		.offset = 0x06,
		.type = KFSW_PARAM_U8,
		.flags = KFSW_PARAM_FLAG_READ_ONLY,
		.name = "hk_test_u8",
		.value = &counter_u8,
	},
	{
		.offset = 0x08,
		.type = KFSW_PARAM_STRING,
		.capacity = sizeof(label),
		.flags = KFSW_PARAM_FLAG_READ_ONLY,
		.name = "hk_test_label",
		.value = label,
		.default_text = "hello",
	},
};

static const struct kfsw_param_definition_set test_set = {
	.table = TEST_TABLE,
	.name = "hktest",
	.definitions = test_definitions,
	.count = ARRAY_SIZE(test_definitions),
};

static void *hk_setup(void)
{
	static const struct kfsw_param_definition_set *const sets[] = {&test_set};

	zassert_ok(kfsw_param_init(sets, ARRAY_SIZE(sets)), "the parameter table did not start");
	zassert_ok(kfsw_hk_init(), "housekeeping did not start");
	return NULL;
}

static void hk_before(void *fixture)
{
	ARG_UNUSED(fixture);
	for (uint8_t report = 0U; report < CONFIG_KFSW_HK_REPORTS; report++) {
		(void)kfsw_hk_clear(report);
	}
	counter_u16 = 0x1234U;
	counter_u32 = 0xAABBCCDDU;
	counter_u8 = 0x5AU;
}

ZTEST_SUITE(kfsw_hk, NULL, hk_setup, hk_before, NULL, NULL);

/* A report has to be refused when it is defined, not discovered mid-pass. */
ZTEST(kfsw_hk, test_definition_naming_nothing_is_refused)
{
	const struct kfsw_hk_entry entries[] = {
		{.node = KFSW_HK_NODE_LOCAL, .param_id = KFSW_PARAM_ID(TEST_TABLE, 0x00)},
		{.node = KFSW_HK_NODE_LOCAL, .param_id = KFSW_PARAM_ID(TEST_TABLE, 0x7F)},
	};

	zassert_equal(kfsw_hk_define(0U, entries, ARRAY_SIZE(entries)), -ENOENT,
		      "a report naming a parameter that does not exist was accepted");
	zassert_equal(kfsw_hk_collect(0U), -ENOENT, "the refused report was left defined");
}

/* And the report that was working must survive the attempt. */
ZTEST(kfsw_hk, test_a_refused_definition_leaves_the_previous_one)
{
	const struct kfsw_hk_entry good[] = {
		{.node = KFSW_HK_NODE_LOCAL, .param_id = KFSW_PARAM_ID(TEST_TABLE, 0x00)},
	};
	const struct kfsw_hk_entry bad[] = {
		{.node = KFSW_HK_NODE_LOCAL, .param_id = KFSW_PARAM_ID(TEST_TABLE, 0x7F)},
	};
	struct kfsw_hk_entry read_back[CONFIG_KFSW_HK_ENTRIES];
	size_t count = ARRAY_SIZE(read_back);

	zassert_ok(kfsw_hk_define(0U, good, ARRAY_SIZE(good)), "the good definition was refused");
	zassert_not_equal(kfsw_hk_define(0U, bad, ARRAY_SIZE(bad)), 0,
			  "the bad definition was accepted");

	zassert_ok(kfsw_hk_get_definition(0U, read_back, &count), "the report was lost");
	zassert_equal(count, 1U, "the report changed shape");
	zassert_equal(read_back[0].param_id, KFSW_PARAM_ID(TEST_TABLE, 0x00),
		      "the report holds the refused entry");
}

/* Values that will not fit one sample are refused up front, with the number. */
ZTEST(kfsw_hk, test_a_report_too_large_is_refused_when_defined)
{
	struct kfsw_hk_entry entries[CONFIG_KFSW_HK_ENTRIES];

	/* Four strings of sixteen bytes is 64, well past the 32-byte sample
	 * this suite is built with.
	 */
	for (size_t index = 0U; index < 4U; index++) {
		entries[index].node = KFSW_HK_NODE_LOCAL;
		entries[index].param_id = KFSW_PARAM_ID(TEST_TABLE, 0x08);
	}
	zassert_equal(kfsw_hk_define(0U, entries, 4U), -EMSGSIZE,
		      "a report larger than one sample was accepted");
}

/* The frame must say what the parameters say, at the widths declared. */
ZTEST(kfsw_hk, test_a_sample_carries_the_values_at_their_declared_widths)
{
	const struct kfsw_hk_entry entries[] = {
		{.node = KFSW_HK_NODE_LOCAL, .param_id = KFSW_PARAM_ID(TEST_TABLE, 0x00)},
		{.node = KFSW_HK_NODE_LOCAL, .param_id = KFSW_PARAM_ID(TEST_TABLE, 0x02)},
		{.node = KFSW_HK_NODE_LOCAL, .param_id = KFSW_PARAM_ID(TEST_TABLE, 0x06)},
	};
	struct kfsw_hk_sample sample;

	zassert_ok(kfsw_hk_define(0U, entries, ARRAY_SIZE(entries)), "the report was refused");
	zassert_ok(kfsw_hk_collect(0U), "the report did not collect");
	zassert_ok(kfsw_hk_get(0U, 0U, &sample), "the sample was not kept");

	zassert_equal(sample.length, KFSW_HK_HEADER_SIZE + 2U + 4U + 1U,
		      "the frame is not the declared width");
	zassert_equal(sample.entry_count, 3U, "the frame counts the wrong number of values");
	zassert_equal(sample.flags & KFSW_HK_FLAG_INCOMPLETE, 0U,
		      "a complete collection was marked incomplete");

	zassert_equal(sys_get_be16(&sample.data[KFSW_HK_HEADER_SIZE]), 0x1234U,
		      "the first value is wrong");
	zassert_equal(sys_get_be32(&sample.data[KFSW_HK_HEADER_SIZE + 2U]), 0xAABBCCDDU,
		      "the second value is wrong");
	zassert_equal(sample.data[KFSW_HK_HEADER_SIZE + 6U], 0x5AU, "the third value is wrong");

	/* And the header, which ground parses before anything else. */
	zassert_equal(sample.data[0], KFSW_HK_PROTOCOL_VERSION, "the version byte is wrong");
	zassert_equal(sample.data[8], 3U, "the header disagrees with the value count");
}

/* A sampled parameter must be as fresh here as it is for an operator. */
ZTEST(kfsw_hk, test_collection_samples_rather_than_reading_stale_storage)
{
	const struct kfsw_hk_entry entries[] = {
		{.node = KFSW_HK_NODE_LOCAL, .param_id = KFSW_PARAM_ID(TEST_TABLE, 0x00)},
	};
	struct kfsw_hk_sample sample;

	zassert_ok(kfsw_hk_define(0U, entries, ARRAY_SIZE(entries)), "the report was refused");

	counter_u16 = 0x4321U;
	zassert_ok(kfsw_hk_collect(0U), "the report did not collect");
	zassert_ok(kfsw_hk_get(0U, 0U, &sample), "the sample was not kept");
	zassert_equal(sys_get_be16(&sample.data[KFSW_HK_HEADER_SIZE]), 0x4321U,
		      "the collection did not sample the value");
}

/* The ring wraps, counts what it dropped, and keeps its sequence monotonic. */
ZTEST(kfsw_hk, test_the_ring_wraps_and_says_so)
{
	const struct kfsw_hk_entry entries[] = {
		{.node = KFSW_HK_NODE_LOCAL, .param_id = KFSW_PARAM_ID(TEST_TABLE, 0x00)},
	};
	struct kfsw_hk_stats before;
	struct kfsw_hk_stats after;
	struct kfsw_hk_sample newest;
	struct kfsw_hk_sample oldest;
	uint16_t depth = 0U;

	zassert_ok(kfsw_hk_define(0U, entries, ARRAY_SIZE(entries)), "the report was refused");
	kfsw_hk_get_stats(&before);

	for (size_t index = 0U; index < (CONFIG_KFSW_HK_HISTORY + 2U); index++) {
		zassert_ok(kfsw_hk_collect(0U), "a collection failed");
	}

	kfsw_hk_get_stats(&after);
	zassert_equal(after.overwritten - before.overwritten, 2U,
		      "the ring did not count what it dropped");

	zassert_ok(kfsw_hk_depth(0U, &depth), "the depth was not readable");
	zassert_equal(depth, CONFIG_KFSW_HK_HISTORY, "the ring grew past its size");

	zassert_ok(kfsw_hk_get(0U, 0U, &newest), "the newest sample was lost");
	zassert_ok(kfsw_hk_get(0U, (uint16_t)(CONFIG_KFSW_HK_HISTORY - 1U), &oldest),
		   "the oldest sample was lost");
	zassert_equal(newest.sequence - oldest.sequence, CONFIG_KFSW_HK_HISTORY - 1U,
		      "the sequence numbers do not span the ring");
	zassert_equal(kfsw_hk_get(0U, CONFIG_KFSW_HK_HISTORY, &oldest), -ENOENT,
		      "reading past the ring returned something");
}

/* Redefining changes the layout, so what was collected no longer decodes. */
ZTEST(kfsw_hk, test_redefining_discards_what_was_collected)
{
	const struct kfsw_hk_entry first[] = {
		{.node = KFSW_HK_NODE_LOCAL, .param_id = KFSW_PARAM_ID(TEST_TABLE, 0x00)},
	};
	const struct kfsw_hk_entry second[] = {
		{.node = KFSW_HK_NODE_LOCAL, .param_id = KFSW_PARAM_ID(TEST_TABLE, 0x02)},
	};
	uint16_t depth = 1U;

	zassert_ok(kfsw_hk_define(0U, first, ARRAY_SIZE(first)), "the report was refused");
	zassert_ok(kfsw_hk_collect(0U), "the report did not collect");
	zassert_ok(kfsw_hk_define(0U, second, ARRAY_SIZE(second)), "the report was not redefined");

	zassert_ok(kfsw_hk_depth(0U, &depth), "the depth was not readable");
	zassert_equal(depth, 0U, "samples from the old layout survived the redefinition");
}

/* A period below the floor is refused rather than quietly clamped. */
ZTEST(kfsw_hk, test_a_period_below_the_floor_is_refused)
{
	const struct kfsw_hk_entry entries[] = {
		{.node = KFSW_HK_NODE_LOCAL, .param_id = KFSW_PARAM_ID(TEST_TABLE, 0x00)},
	};
	uint32_t period = 0U;

	zassert_ok(kfsw_hk_define(0U, entries, ARRAY_SIZE(entries)), "the report was refused");
	zassert_equal(kfsw_hk_set_period(0U, 1U), -ERANGE, "a period below the floor was accepted");
	zassert_ok(kfsw_hk_get_period(0U, &period), "the period was not readable");
	zassert_equal(period, 0U, "the refused period was stored anyway");

	zassert_ok(kfsw_hk_set_period(0U, CONFIG_KFSW_HK_PERIOD_FLOOR_MS),
		   "the floor itself was refused");
}

/* A report that was never defined answers nothing rather than an empty frame. */
ZTEST(kfsw_hk, test_an_undefined_report_collects_nothing)
{
	struct kfsw_hk_sample sample;

	zassert_equal(kfsw_hk_collect(0U), -ENOENT, "an undefined report collected");
	zassert_equal(kfsw_hk_get(0U, 0U, &sample), -ENOENT, "an undefined report had a sample");
	zassert_equal(kfsw_hk_define(CONFIG_KFSW_HK_REPORTS, NULL, 0U), -EINVAL,
		      "a report past the last one was addressable");
}

ZTEST(kfsw_hk, test_a_sample_without_a_clock_says_so)
{
	struct kfsw_hk_sample sample;
	const struct kfsw_hk_entry entries[] = {
		{.node = KFSW_HK_NODE_LOCAL, .param_id = KFSW_PARAM_ID(TEST_TABLE, 0x00U)},
	};

	zassert_ok(kfsw_hk_define(0U, entries, ARRAY_SIZE(entries)));
	zassert_ok(kfsw_hk_collect(0U));
	zassert_ok(kfsw_hk_get(0U, 0U, &sample));

	/* This composition has no CSP, so there is nowhere for a wall clock to
	 * come from and every sample is collected without one. The timestamp
	 * being zero is not enough on its own: zero is a real instant, and a
	 * reader that took it at face value would place the sample in 1970.
	 */
	zassert_equal(sample.seconds, 0U, "a node with no clock cannot time a sample");
	zassert_not_equal(sample.flags & KFSW_HK_FLAG_CLOCK_UNSET, 0U,
			  "the sample must say the clock was unset");
	zassert_equal(sample.data[9] & KFSW_HK_FLAG_CLOCK_UNSET, KFSW_HK_FLAG_CLOCK_UNSET,
		      "and the flag must reach the wire, not just the struct");
}
