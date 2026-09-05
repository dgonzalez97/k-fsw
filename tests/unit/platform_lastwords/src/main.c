#include <string.h>

#include <zephyr/ztest.h>

#include <kfsw/platform/lastwords.h>

/* The record lives in memory start-up does not clear, so these run in one
 * image and each case leaves it empty for the next.
 */
static void clear_record(void)
{
	struct kfsw_lastwords discard;

	(void)kfsw_lastwords_take(&discard);
}

ZTEST(kfsw_platform_lastwords, test_nothing_written_reads_as_nothing)
{
	struct kfsw_lastwords record;

	clear_record();
	zassert_false(kfsw_lastwords_take(&record), "an empty record must not be believed");
	zassert_equal(record.reason, KFSW_LASTWORDS_NONE);
}

ZTEST(kfsw_platform_lastwords, test_a_note_survives_and_reads_back)
{
	struct kfsw_lastwords record;

	clear_record();
	kfsw_lastwords_write(KFSW_LASTWORDS_COMMANDED, 0x1234U, 6675U, 7U);

	zassert_true(kfsw_lastwords_take(&record));
	zassert_equal(record.reason, KFSW_LASTWORDS_COMMANDED);
	zassert_equal(record.detail, 0x1234U);
	zassert_equal(record.uptime_ms, 6675U);
	zassert_equal(record.boot_count, 7U);
}

ZTEST(kfsw_platform_lastwords, test_a_note_is_reported_once)
{
	struct kfsw_lastwords record;

	clear_record();
	kfsw_lastwords_write(KFSW_LASTWORDS_BROWNOUT, 0U, 10U, 1U);

	zassert_true(kfsw_lastwords_take(&record));
	/* Attributing one restart's reason to the next would be worse than
	 * saying nothing, so reading has to consume it.
	 */
	zassert_false(kfsw_lastwords_take(&record), "a note must not be reported twice");
	zassert_equal(record.reason, KFSW_LASTWORDS_NONE);
}

ZTEST(kfsw_platform_lastwords, test_the_last_note_wins)
{
	struct kfsw_lastwords record;

	clear_record();
	kfsw_lastwords_write(KFSW_LASTWORDS_COMMANDED, 1U, 100U, 1U);
	/* Whatever is closest to the restart is the more useful account: a
	 * commanded reboot that then browns out went down for the second
	 * reason.
	 */
	kfsw_lastwords_write(KFSW_LASTWORDS_BROWNOUT, 2U, 200U, 1U);

	zassert_true(kfsw_lastwords_take(&record));
	zassert_equal(record.reason, KFSW_LASTWORDS_BROWNOUT);
	zassert_equal(record.detail, 2U);
}

ZTEST(kfsw_platform_lastwords, test_every_reason_has_a_name)
{
	/* Ground and the log both print these, so a missing one would surface
	 * as a blank field rather than an error.
	 */
	zassert_str_equal(kfsw_lastwords_reason_name(KFSW_LASTWORDS_NONE), "none");
	zassert_str_equal(kfsw_lastwords_reason_name(KFSW_LASTWORDS_COMMANDED), "commanded");
	zassert_str_equal(kfsw_lastwords_reason_name(KFSW_LASTWORDS_BROWNOUT), "brownout");
	zassert_str_equal(kfsw_lastwords_reason_name(KFSW_LASTWORDS_FATAL), "fatal");
	zassert_str_equal(kfsw_lastwords_reason_name(KFSW_LASTWORDS_STARVED), "starved");
	zassert_str_equal(kfsw_lastwords_reason_name(KFSW_LASTWORDS_UNKNOWN), "unknown");
}

ZTEST(kfsw_platform_lastwords, test_no_detector_is_reported_not_pretended)
{
	/* A composition that believes it is watching the rail when nothing is
	 * would read a missing brown-out record as good news.
	 */
	zassert_equal(kfsw_lastwords_watch_supply(), -ENOTSUP);
}

ZTEST_SUITE(kfsw_platform_lastwords, NULL, NULL, NULL, NULL, NULL);
