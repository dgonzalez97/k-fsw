/* The boot service's accessors and its start-up report.
 *
 * capture_reset() is a SYS_INIT at priority 51 and has already consumed the
 * lastwords record before the first test runs, so the branch that reports a
 * note from the previous run is driven on hardware rather than here.
 *
 * That puts a ceiling on this file: the hardware-id fallback, the unavailable
 * reset cause, the watchdog cause and the whole lastwords report read state
 * that capture_reset() fixed before the suite started, so they are reachable
 * only by making that handler injectable.
 */

#include <zephyr/ztest.h>

#include <kfsw/platform/hardware.h>
#include <kfsw/platform/reset.h>
#include <kfsw/services/boot.h>

ZTEST(boot, test_image_version_and_revisions_are_reported)
{
	const char *version = kfsw_boot_get_image_version();
	const char *revisions = kfsw_boot_get_revisions();

	zassert_not_null(version);
	zassert_not_null(revisions);
	/* Both are stamped at build time, so neither may be empty in any image. */
	zassert_true(strlen(version) > 0);
	zassert_true(strlen(revisions) > 0);
}

ZTEST(boot, test_hardware_id_is_readable_before_the_report)
{
	const char *id = kfsw_boot_get_hardware_id();

	/* Readable from the first instruction: the accessor returns the buffer,
	   which start-up fills in. An empty string is a valid answer. */
	zassert_not_null(id);
}

ZTEST(boot, test_reset_cause_has_a_name)
{
	uint32_t cause = kfsw_boot_get_reset_cause();
	const char *name = kfsw_platform_reset_cause_name(cause);

	zassert_not_null(name);
	zassert_true(strlen(name) > 0);
}

ZTEST(boot, test_the_start_report_fills_the_hardware_id)
{
	kfsw_boot_service_start();

	/* Whatever the platform answered, the accessor is a valid string: the
	   report falls back to an empty buffer when the id is unavailable, and
	   prints unknown in the marker rather than leaving it unset. */
	const char *id = kfsw_boot_get_hardware_id();

	zassert_not_null(id);
	zassert_true(strlen(id) < KFSW_HARDWARE_ID_TEXT_SIZE);
}

ZTEST(boot, test_the_start_report_is_repeatable)
{
	uint32_t first = kfsw_boot_get_reset_cause();

	kfsw_boot_service_start();
	kfsw_boot_service_start();

	/* Reporting does not consume or change what it reports, so a second
	   call says the same thing. */
	zassert_equal(kfsw_boot_get_reset_cause(), first);
}

#if CONFIG_KFSW_LASTWORDS
ZTEST(boot, test_lastwords_are_exposed_after_capture)
{
	const struct kfsw_lastwords *note = kfsw_boot_get_lastwords();

	/* capture_reset() took the record at init, so the service always has one
	   to hand out; on a power-on boot its reason is NONE. */
	zassert_not_null(note);
	zassert_true(note->reason <= KFSW_LASTWORDS_UNKNOWN);
}
#endif

ZTEST_SUITE(boot, NULL, NULL, NULL, NULL, NULL);
