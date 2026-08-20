#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include <kfsw/platform/time.h>

ZTEST(platform_time, test_monotonic_reads_do_not_go_backwards)
{
	uint64_t previous_ms = kfsw_time_monotonic_ms();
	uint64_t previous_us = kfsw_time_monotonic_us();

	for (size_t i = 0; i < 64U; i++) {
		const uint64_t current_ms = kfsw_time_monotonic_ms();
		const uint64_t current_us = kfsw_time_monotonic_us();

		zassert_true(current_ms >= previous_ms, "millisecond clock moved backwards");
		zassert_true(current_us >= previous_us, "microsecond clock moved backwards");
		previous_ms = current_ms;
		previous_us = current_us;
	}
}

ZTEST(platform_time, test_sleep_advances_both_time_units_consistently)
{
	const uint64_t start_ms = kfsw_time_monotonic_ms();
	const uint64_t start_us = kfsw_time_monotonic_us();

	k_sleep(K_MSEC(5));

	const uint64_t end_us = kfsw_time_monotonic_us();
	const uint64_t end_ms = kfsw_time_monotonic_ms();
	const uint64_t elapsed_ms = end_ms - start_ms;
	const uint64_t elapsed_us = end_us - start_us;

	zassert_true(elapsed_ms >= 5U, "millisecond clock did not advance");
	zassert_true(elapsed_us >= 5000U, "microsecond clock did not advance");
	zassert_within(elapsed_us, elapsed_ms * 1000U, 1000U,
		       "time units disagree after the same sleep");
}

ZTEST_SUITE(platform_time, NULL, NULL, NULL, NULL, NULL);
