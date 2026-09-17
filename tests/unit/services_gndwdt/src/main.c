#include <errno.h>
#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include <kfsw/services/gndwdt.h>

/* The compiled timeout, in milliseconds, plus a margin for the check period. */
#define TIMEOUT_MS (CONFIG_KFSW_GNDWDT_TIMEOUT_S * 1000U)
#define PAST_TIMEOUT_MS (TIMEOUT_MS + 300U)

static void stop_service(void *fixture)
{
	ARG_UNUSED(fixture);

	(void)kfsw_gndwdt_stop();
}

ZTEST(services_gndwdt, test_stopped_service_never_expires)
{
	struct kfsw_gndwdt_status before;
	struct kfsw_gndwdt_status after;

	kfsw_gndwdt_get_status(&before);
	zassert_false(before.running);
	zassert_equal(kfsw_gndwdt_evaluate(), 0);

	/* Contact before the service starts is ignored. Counters run for the
	 * life of the process, so compare rather than expect zero.
	 */
	kfsw_gndwdt_contact(9U);
	kfsw_gndwdt_get_status(&after);
	zassert_equal(after.contacts, before.contacts);

	k_sleep(K_MSEC(PAST_TIMEOUT_MS));
	zassert_equal(kfsw_gndwdt_evaluate(), 0);
}

ZTEST(services_gndwdt, test_contact_holds_the_countdown_open)
{
	struct kfsw_gndwdt_status status;

	zassert_equal(kfsw_gndwdt_start(), 0);
	zassert_equal(kfsw_gndwdt_start(), -EALREADY);

	for (int repeat = 0; repeat < 3; repeat++) {
		k_sleep(K_MSEC(TIMEOUT_MS / 2U));
		kfsw_gndwdt_contact(7U);
		zassert_equal(kfsw_gndwdt_evaluate(), 0);
	}

	kfsw_gndwdt_get_status(&status);
	zassert_true(status.running);
	zassert_true(status.enabled);
	zassert_equal(status.contacts, 3U);
	zassert_equal(status.last_node, 7U);
	zassert_equal(status.expiries, 0U);
}

ZTEST(services_gndwdt, test_silence_expires_once_per_timeout)
{
	struct kfsw_gndwdt_status before;
	struct kfsw_gndwdt_status after;

	zassert_equal(kfsw_gndwdt_start(), 0);
	kfsw_gndwdt_contact(7U);
	kfsw_gndwdt_get_status(&before);

	k_sleep(K_MSEC(PAST_TIMEOUT_MS));
	zassert_equal(kfsw_gndwdt_evaluate(), -ETIMEDOUT);

	/* The countdown restarts, so the next check is not overdue again. */
	zassert_equal(kfsw_gndwdt_evaluate(), 0);

	kfsw_gndwdt_get_status(&after);
	zassert_true(after.expiries > before.expiries);
}

ZTEST(services_gndwdt, test_disarmed_service_never_expires)
{
	struct kfsw_gndwdt_status status;

	zassert_equal(kfsw_gndwdt_start(), 0);
	kfsw_gndwdt_set_enabled(false);

	k_sleep(K_MSEC(PAST_TIMEOUT_MS));
	zassert_equal(kfsw_gndwdt_evaluate(), 0);

	kfsw_gndwdt_get_status(&status);
	zassert_false(status.enabled);
	zassert_equal(status.expiries, 0U);

	/* Arming again restarts the countdown rather than expiring at once. */
	kfsw_gndwdt_set_enabled(true);
	zassert_equal(kfsw_gndwdt_evaluate(), 0);
}

ZTEST(services_gndwdt, test_timeout_bounds_are_enforced)
{
	struct kfsw_gndwdt_status status;

	zassert_equal(kfsw_gndwdt_start(), 0);

	zassert_equal(kfsw_gndwdt_set_timeout_s(CONFIG_KFSW_GNDWDT_TIMEOUT_MIN_S - 1U), -ERANGE);
	zassert_equal(kfsw_gndwdt_set_timeout_s(CONFIG_KFSW_GNDWDT_TIMEOUT_MAX_S + 1U), -ERANGE);

	kfsw_gndwdt_get_status(&status);
	zassert_equal(status.timeout_s, CONFIG_KFSW_GNDWDT_TIMEOUT_S);

	zassert_equal(kfsw_gndwdt_set_timeout_s(CONFIG_KFSW_GNDWDT_TIMEOUT_MAX_S), 0);
	kfsw_gndwdt_get_status(&status);
	zassert_equal(status.timeout_s, CONFIG_KFSW_GNDWDT_TIMEOUT_MAX_S);

	/* A longer timeout means silence that was overdue no longer is. */
	zassert_equal(kfsw_gndwdt_evaluate(), 0);
}

ZTEST(services_gndwdt, test_stop_and_status_are_safe)
{
	zassert_equal(kfsw_gndwdt_stop(), -EALREADY);
	zassert_equal(kfsw_gndwdt_start(), 0);
	zassert_equal(kfsw_gndwdt_stop(), 0);
	zassert_equal(kfsw_gndwdt_stop(), -EALREADY);

	/* A null destination is ignored. */
	kfsw_gndwdt_get_status(NULL);
}

ZTEST_SUITE(services_gndwdt, NULL, NULL, NULL, stop_service, NULL);
