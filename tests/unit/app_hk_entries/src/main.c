#include <errno.h>
#include <stdint.h>

#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>

#include <kfsw/services/hk.h>
#include <kfsw/services/parameter.h>

#include "hk_entries.h"

ZTEST(app_hk_entries, test_local_entry)
{
	struct kfsw_hk_entry entry;

	zassert_ok(kfsw_app_hk_parse_entry("2:0x10", &entry));
	zassert_equal(entry.node, KFSW_HK_NODE_LOCAL);
	zassert_equal(entry.param_id, KFSW_PARAM_ID(2U, 0x10U));
}

ZTEST(app_hk_entries, test_remote_entry_at_the_limits)
{
	struct kfsw_hk_entry entry;

	zassert_ok(kfsw_app_hk_parse_entry("65535:255:255", &entry));
	zassert_equal(entry.node, UINT16_MAX);
	zassert_equal(entry.param_id, KFSW_PARAM_ID(255U, 255U));
}

ZTEST(app_hk_entries, test_field_too_large_is_refused)
{
	struct kfsw_hk_entry entry = {.node = 7U, .param_id = 7U};

	/* Each of these used to wrap into a different, possibly existing, ID. */
	zassert_equal(kfsw_app_hk_parse_entry("1:300", &entry), -EINVAL);
	zassert_equal(kfsw_app_hk_parse_entry("256:0", &entry), -EINVAL);
	zassert_equal(kfsw_app_hk_parse_entry("70000:1:4", &entry), -EINVAL);
	zassert_equal(kfsw_app_hk_parse_entry("99999999999999999999:1", &entry), -EINVAL);
	zassert_equal(entry.node, 7U);
	zassert_equal(entry.param_id, 7U);
}

ZTEST(app_hk_entries, test_sign_and_shape_are_refused)
{
	struct kfsw_hk_entry entry;

	zassert_equal(kfsw_app_hk_parse_entry("-1:4", &entry), -EINVAL);
	zassert_equal(kfsw_app_hk_parse_entry("1:+4", &entry), -EINVAL);
	zassert_equal(kfsw_app_hk_parse_entry(" 1:4", &entry), -EINVAL);
	zassert_equal(kfsw_app_hk_parse_entry("1", &entry), -EINVAL);
	zassert_equal(kfsw_app_hk_parse_entry("1:", &entry), -EINVAL);
	zassert_equal(kfsw_app_hk_parse_entry("1:2:3:", &entry), -EINVAL);
	zassert_equal(kfsw_app_hk_parse_entry("1:2:3:4", &entry), -EINVAL);
	zassert_equal(kfsw_app_hk_parse_entry("1:2x", &entry), -EINVAL);
	zassert_equal(kfsw_app_hk_parse_entry("", &entry), -EINVAL);
	zassert_equal(kfsw_app_hk_parse_entry(NULL, &entry), -EINVAL);
}

ZTEST(app_hk_entries, test_list_stops_at_the_first_bad_entry)
{
	struct kfsw_hk_entry entries[4];
	size_t count = 0U;

	zassert_ok(
		kfsw_app_hk_parse_entries("1:0 2:4  3:9:8", entries, ARRAY_SIZE(entries), &count));
	zassert_equal(count, 3U);
	zassert_equal(entries[2].node, 3U);
	zassert_equal(entries[2].param_id, KFSW_PARAM_ID(9U, 8U));

	count = 0U;
	zassert_equal(kfsw_app_hk_parse_entries("1:0 1:300", entries, ARRAY_SIZE(entries), &count),
		      -EINVAL);
	zassert_equal(count, 0U);
	zassert_equal(kfsw_app_hk_parse_entries("1:0 1:1", entries, 1U, &count), -E2BIG);
}

ZTEST_SUITE(app_hk_entries, NULL, NULL, NULL, NULL, NULL);
