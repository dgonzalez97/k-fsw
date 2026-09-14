#include <errno.h>

#include <zephyr/drivers/hwinfo.h>
#include <zephyr/ztest.h>

#include <kfsw/platform/reset.h>
#include <kfsw/platform/watchdog.h>

/* native_sim has no watchdog driver, so this suite tests the feed interval,
 * reset-cause decoding and the -ENODEV behaviour. Real resets are tested on
 * hardware.
 */

ZTEST(platform_watchdog, test_feed_interval_leaves_room_for_two_misses)
{
	/* A third of the timeout, so two feeds can be missed. */
	zassert_equal(kfsw_platform_watchdog_feed_interval_ms(9000U), 3000U);
	zassert_equal(kfsw_platform_watchdog_feed_interval_ms(8000U), 2666U);

	for (uint32_t timeout = 100U; timeout <= 32000U; timeout += 100U) {
		uint32_t interval = kfsw_platform_watchdog_feed_interval_ms(timeout);

		zassert_true(interval * 2U < timeout,
			     "two missed feeds must still fit inside %u ms", timeout);
	}
}

ZTEST(platform_watchdog, test_feed_interval_never_zero)
{
	/* A zero interval would schedule a keep-alive that never yields. */
	zassert_equal(kfsw_platform_watchdog_feed_interval_ms(0U), 1U);
	zassert_equal(kfsw_platform_watchdog_feed_interval_ms(1U), 1U);
	zassert_equal(kfsw_platform_watchdog_feed_interval_ms(2U), 1U);
	zassert_equal(kfsw_platform_watchdog_feed_interval_ms(3U), 1U);
}

ZTEST(platform_watchdog, test_watchdog_cause_is_recognized_among_others)
{
	zassert_true(kfsw_platform_reset_cause_is_watchdog(RESET_WATCHDOG));

	/* A reset can latch several causes at once; the watchdog must still be
	 * seen when it is not the only bit set. */
	zassert_true(kfsw_platform_reset_cause_is_watchdog(RESET_WATCHDOG | RESET_PIN));
	zassert_true(kfsw_platform_reset_cause_is_watchdog(RESET_BROWNOUT | RESET_WATCHDOG));

	zassert_false(kfsw_platform_reset_cause_is_watchdog(0U));
	zassert_false(kfsw_platform_reset_cause_is_watchdog(RESET_POR));
	zassert_false(kfsw_platform_reset_cause_is_watchdog(RESET_SOFTWARE | RESET_PIN));
}

ZTEST(platform_watchdog, test_cause_name_prefers_the_watchdog)
{
	/* The watchdog is reported first when several causes are latched. */
	zassert_str_equal(kfsw_platform_reset_cause_name(RESET_WATCHDOG), "watchdog");
	zassert_str_equal(kfsw_platform_reset_cause_name(RESET_WATCHDOG | RESET_PIN), "watchdog");
	zassert_str_equal(kfsw_platform_reset_cause_name(RESET_POR | RESET_WATCHDOG), "watchdog");

	zassert_str_equal(kfsw_platform_reset_cause_name(RESET_POR), "power-on");
	zassert_str_equal(kfsw_platform_reset_cause_name(RESET_PIN), "pin");
	zassert_str_equal(kfsw_platform_reset_cause_name(RESET_SOFTWARE), "software");
	zassert_str_equal(kfsw_platform_reset_cause_name(0U), "unknown");
}

ZTEST(platform_watchdog, test_absent_hardware_is_reported_not_pretended)
{
	struct kfsw_platform_watchdog_info info;

	/* Without a watchdog every call must fail. */
	zassert_equal(kfsw_platform_watchdog_init(), -ENODEV);
	zassert_equal(kfsw_platform_watchdog_start(), -ENODEV);
	zassert_equal(kfsw_platform_watchdog_feed(), -ENODEV);
	zassert_equal(kfsw_platform_watchdog_stop_feeding(), -ENODEV);

	zassert_ok(kfsw_platform_watchdog_get_info(&info));
	zassert_false(info.device_bound);
	zassert_equal(info.state, KFSW_PLATFORM_WATCHDOG_UNCONFIGURED);
	zassert_equal(info.feeds, 0U);
}

ZTEST(platform_watchdog, test_get_info_rejects_null)
{
	zassert_equal(kfsw_platform_watchdog_get_info(NULL), -EINVAL);
}

ZTEST_SUITE(platform_watchdog, NULL, NULL, NULL, NULL, NULL);
