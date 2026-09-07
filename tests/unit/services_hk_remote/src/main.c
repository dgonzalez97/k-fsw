/*
 * Housekeeping collection from a node that is not this one.
 *
 * The two remote reads are wrapped and answered by fakes, so the cases worth
 * asserting -- a node that never replies, one that lists fewer parameters than
 * were asked of it, one that lists them and then fails to hand them over --
 * are reached here rather than by standing up a second node and provoking it.
 */

#include <string.h>

#include <zephyr/fff.h>
#include <zephyr/ztest.h>

#include <kfsw/services/hk.h>
#include <kfsw/services/parameter.h>

DEFINE_FFF_GLOBALS;

#define REMOTE_NODE 42U
#define REPORT_ID 0U
#define FIRST_ID KFSW_PARAM_ID(20U, 0U)
#define SECOND_ID KFSW_PARAM_ID(20U, 4U)

FAKE_VALUE_FUNC(int, __wrap_kfsw_param_remote_visit, uint16_t, kfsw_param_visitor_t, void *);
FAKE_VALUE_FUNC(int, __wrap_kfsw_param_remote_get_many, uint16_t, const char *const *, size_t,
		struct kfsw_param_value *);

/* What the next visit fake offers, and what the next read hands back. */
static uint16_t offered_ids[4];
static size_t offered_count;
static uint32_t handed_values[4];

static int offer_parameters(uint16_t node, kfsw_param_visitor_t visitor, void *context)
{
	for (size_t index = 0U; index < offered_count; index++) {
		struct kfsw_param_info info = {
			.node = node,
			.id = offered_ids[index],
			.table = (uint8_t)(offered_ids[index] >> 8),
			.offset = (uint8_t)offered_ids[index],
			.array_size = 1U,
			.type = KFSW_PARAM_U32,
			.name = "remote",
		};

		if (!visitor(&info, context)) {
			break;
		}
	}
	return 0;
}

static int hand_values(uint16_t node, const char *const *names, size_t count,
		       struct kfsw_param_value *values)
{
	ARG_UNUSED(node);
	ARG_UNUSED(names);

	for (size_t index = 0U; index < count; index++) {
		values[index].type = KFSW_PARAM_U32;
		values[index].size = sizeof(uint32_t);
		values[index].scalar.u32 = handed_values[index];
	}
	return 0;
}

static void define_report(size_t entry_count)
{
	const struct kfsw_hk_entry entries[] = {
		{.node = REMOTE_NODE, .param_id = FIRST_ID},
		{.node = REMOTE_NODE, .param_id = SECOND_ID},
	};

	/* Defining a remote entry asks the node how wide its value is, so the
	 * node has to describe itself before the report can exist. The fakes
	 * are cleared afterwards, leaving each test to say what the node does
	 * when the report is collected rather than when it was defined.
	 */
	offered_ids[0] = FIRST_ID;
	offered_ids[1] = SECOND_ID;
	offered_count = 2U;
	__wrap_kfsw_param_remote_visit_fake.custom_fake = offer_parameters;

	zassert_ok(kfsw_hk_define(REPORT_ID, entries, entry_count));

	RESET_FAKE(__wrap_kfsw_param_remote_visit);
	FFF_RESET_HISTORY();
	memset(offered_ids, 0, sizeof(offered_ids));
	offered_count = 0U;
}

static void reset_fakes(void *fixture)
{
	ARG_UNUSED(fixture);

	RESET_FAKE(__wrap_kfsw_param_remote_visit);
	RESET_FAKE(__wrap_kfsw_param_remote_get_many);
	FFF_RESET_HISTORY();

	memset(offered_ids, 0, sizeof(offered_ids));
	memset(handed_values, 0, sizeof(handed_values));
	offered_count = 0U;

	(void)kfsw_hk_clear(REPORT_ID);
	zassert_ok(kfsw_hk_init());
}

ZTEST_SUITE(services_hk_remote, NULL, NULL, reset_fakes, NULL, NULL);

ZTEST(services_hk_remote, test_silent_node_fails_every_entry_it_owed)
{
	struct kfsw_hk_stats before;
	struct kfsw_hk_stats after;

	define_report(2U);
	kfsw_hk_get_stats(&before);

	__wrap_kfsw_param_remote_visit_fake.return_val = -ETIMEDOUT;

	(void)kfsw_hk_collect(REPORT_ID);

	kfsw_hk_get_stats(&after);
	zassert_equal(__wrap_kfsw_param_remote_visit_fake.call_count, 1U,
		      "the node should be asked once");
	zassert_equal(__wrap_kfsw_param_remote_get_many_fake.call_count, 0U,
		      "nothing should be read from a node that did not answer");
	zassert_equal(after.entries_failed - before.entries_failed, 2U,
		      "both entries the report owed should count as failed");
}

ZTEST(services_hk_remote, test_node_that_answers_carries_its_values_into_the_sample)
{
	struct kfsw_hk_sample sample;

	define_report(2U);

	offered_ids[0] = FIRST_ID;
	offered_ids[1] = SECOND_ID;
	offered_count = 2U;
	handed_values[0] = 0xA1B2C3D4U;
	handed_values[1] = 0x0000BEEFU;

	__wrap_kfsw_param_remote_visit_fake.custom_fake = offer_parameters;
	__wrap_kfsw_param_remote_get_many_fake.custom_fake = hand_values;

	zassert_ok(kfsw_hk_collect(REPORT_ID));
	zassert_ok(kfsw_hk_get(REPORT_ID, 0U, &sample));

	zassert_equal(__wrap_kfsw_param_remote_get_many_fake.call_count, 1U,
		      "two values within one window should cost one exchange");
	zassert_equal(__wrap_kfsw_param_remote_get_many_fake.arg0_val, REMOTE_NODE,
		      "the read should be addressed to the node the entry named");
	zassert_equal(sample.flags & KFSW_HK_FLAG_INCOMPLETE, 0U,
		      "a sample with every value present is complete");
}

ZTEST(services_hk_remote, test_node_short_of_values_leaves_the_sample_incomplete)
{
	struct kfsw_hk_sample sample;
	struct kfsw_hk_stats before;
	struct kfsw_hk_stats after;

	define_report(2U);
	kfsw_hk_get_stats(&before);

	/* The node lists one of the two parameters the report asked of it. */
	offered_ids[0] = FIRST_ID;
	offered_count = 1U;
	handed_values[0] = 0x11223344U;

	__wrap_kfsw_param_remote_visit_fake.custom_fake = offer_parameters;
	__wrap_kfsw_param_remote_get_many_fake.custom_fake = hand_values;

	/* A partial sample is kept and flagged rather than thrown away, so the
	 * collection succeeds and the sample carries the bad news itself.
	 */
	zassert_ok(kfsw_hk_collect(REPORT_ID));
	zassert_ok(kfsw_hk_get(REPORT_ID, 0U, &sample));

	kfsw_hk_get_stats(&after);
	zassert_equal(sample.flags & KFSW_HK_FLAG_INCOMPLETE, KFSW_HK_FLAG_INCOMPLETE,
		      "the missing value should mark the sample incomplete");

	/* A value the node never listed is not counted as a failed entry: the
	 * counter follows reads that were attempted and refused, while the
	 * sample flag is what reports a value that never arrived at all.
	 */
	zassert_equal(after.entries_failed - before.entries_failed, 0U,
		      "a value that was never listed is reported by the flag, not the counter");
}

ZTEST(services_hk_remote, test_read_failure_after_listing_still_counts_the_entries)
{
	struct kfsw_hk_stats before;
	struct kfsw_hk_stats after;

	define_report(2U);
	kfsw_hk_get_stats(&before);

	offered_ids[0] = FIRST_ID;
	offered_ids[1] = SECOND_ID;
	offered_count = 2U;

	/* The node lists both, then refuses the read. */
	__wrap_kfsw_param_remote_visit_fake.custom_fake = offer_parameters;
	__wrap_kfsw_param_remote_get_many_fake.return_val = -EIO;

	(void)kfsw_hk_collect(REPORT_ID);

	kfsw_hk_get_stats(&after);
	zassert_equal(__wrap_kfsw_param_remote_get_many_fake.call_count, 1U,
		      "the window should be attempted once");
	zassert_equal(after.entries_failed - before.entries_failed, 2U,
		      "a window that failed should fail the entries it carried");
}
