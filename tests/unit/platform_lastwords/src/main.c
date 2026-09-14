#include <string.h>

#include <zephyr/ztest.h>

#include <kfsw/platform/lastwords.h>

/* The note is in RAM start-up doesn't clear, so each case clears it for the next. */
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
	/* Reading clears the note. */
	zassert_false(kfsw_lastwords_take(&record), "a note must not be reported twice");
	zassert_equal(record.reason, KFSW_LASTWORDS_NONE);
}

ZTEST(kfsw_platform_lastwords, test_the_last_note_wins)
{
	struct kfsw_lastwords record;

	clear_record();
	kfsw_lastwords_write(KFSW_LASTWORDS_COMMANDED, 1U, 100U, 1U);
	/* A second write replaces the first. */
	kfsw_lastwords_write(KFSW_LASTWORDS_BROWNOUT, 2U, 200U, 1U);

	zassert_true(kfsw_lastwords_take(&record));
	zassert_equal(record.reason, KFSW_LASTWORDS_BROWNOUT);
	zassert_equal(record.detail, 2U);
}

ZTEST(kfsw_platform_lastwords, test_a_writer_can_take_its_own_note_back)
{
	struct kfsw_lastwords record;

	clear_record();
	kfsw_lastwords_write(KFSW_LASTWORDS_STARVED, 0U, 10U, 1U);

	/* A predicted restart can be withdrawn. */
	zassert_true(kfsw_lastwords_withdraw(KFSW_LASTWORDS_STARVED));
	zassert_false(kfsw_lastwords_take(&record), "a withdrawn note must be gone");
}

ZTEST(kfsw_platform_lastwords, test_a_writer_cannot_take_back_someone_elses)
{
	struct kfsw_lastwords record;

	clear_record();
	kfsw_lastwords_write(KFSW_LASTWORDS_FATAL, 0x0800abcdU, 10U, 1U);

	/* Withdrawing with a different reason must not clear the note. */
	zassert_false(kfsw_lastwords_withdraw(KFSW_LASTWORDS_STARVED));
	zassert_true(kfsw_lastwords_take(&record));
	zassert_equal(record.reason, KFSW_LASTWORDS_FATAL);
	zassert_equal(record.detail, 0x0800abcdU);
}

ZTEST(kfsw_platform_lastwords, test_withdrawing_nothing_is_not_an_error)
{
	clear_record();
	zassert_false(kfsw_lastwords_withdraw(KFSW_LASTWORDS_STARVED));
}

ZTEST(kfsw_platform_lastwords, test_every_reason_has_a_name)
{
	/* Every reason has a name. */
	zassert_str_equal(kfsw_lastwords_reason_name(KFSW_LASTWORDS_NONE), "none");
	zassert_str_equal(kfsw_lastwords_reason_name(KFSW_LASTWORDS_COMMANDED), "commanded");
	zassert_str_equal(kfsw_lastwords_reason_name(KFSW_LASTWORDS_BROWNOUT), "brownout");
	zassert_str_equal(kfsw_lastwords_reason_name(KFSW_LASTWORDS_FATAL), "fatal");
	zassert_str_equal(kfsw_lastwords_reason_name(KFSW_LASTWORDS_STARVED), "starved");
	zassert_str_equal(kfsw_lastwords_reason_name(KFSW_LASTWORDS_UNKNOWN), "unknown");
}

ZTEST(kfsw_platform_lastwords, test_no_detector_is_reported_not_pretended)
{
	/* No detector on this host. */
	zassert_equal(kfsw_lastwords_watch_supply(), -ENOTSUP);
}

ZTEST_SUITE(kfsw_platform_lastwords, NULL, NULL, NULL, NULL, NULL);
