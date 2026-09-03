#include <errno.h>

#include <zephyr/drivers/hwinfo.h>
#include <zephyr/ztest.h>

#include <kfsw/platform/reset.h>
#include <kfsw/platform/watchdog.h>

/* This suite runs on native_sim, which has no watchdog driver at all. That is
 * deliberate: it pins the arithmetic and the reset-cause decoding, which are
 * pure and must hold everywhere, and it pins the contract a composition sees
 * on a board with no watchdog hardware. Arming a real device and observing the
 * reset it causes belongs to the hardware acceptance, not here.
 */

ZTEST(platform_watchdog, test_feed_interval_leaves_room_for_two_misses)
{
	/* A third of the timeout means two consecutive feeds can be missed
	 * before the hardware expires. Anything larger removes that margin.
	 */
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
	/* After an unattended restart the watchdog is the answer an operator
	 * needs, so it outranks whatever else was latched alongside it. */
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

	/* native_sim has no watchdog. Every operation must say so rather than
	 * appearing to succeed, or a composition would believe it is guarded
	 * when nothing is watching it. */
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
