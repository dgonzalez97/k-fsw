#include <errno.h>
#include <string.h>

#include <zephyr/ztest.h>

#include <kfsw/platform/hardware.h>

/* Reading twice is what the callers do: the shell formats one line and the
 * board table samples the same value into a parameter. Both have to agree, so
 * most cases here start from one known-good read.
 */
static void read_identifier(char *text, size_t size)
{
	int result = kfsw_platform_get_hardware_id(text, size);

	zassert_equal(result, 0, "reading the hardware identifier failed (%d)", result);
}

ZTEST(platform_hardware, test_identifier_is_lowercase_hex_byte_pairs)
{
	char text[KFSW_HARDWARE_ID_TEXT_SIZE];
	size_t length;

	read_identifier(text, sizeof(text));

	length = strlen(text);
	zassert_true(length > 0U, "identifier is empty");
	zassert_true(length <= (KFSW_HARDWARE_ID_MAX_BYTES * 2U), "identifier is too long");
	zassert_equal(length % 2U, 0U, "identifier is not a whole number of bytes");

	for (size_t i = 0U; i < length; i++) {
		bool is_digit = (text[i] >= '0') && (text[i] <= '9');
		bool is_lower_hex = (text[i] >= 'a') && (text[i] <= 'f');

		zassert_true(is_digit || is_lower_hex, "character %zu is not lowercase hex: %c", i,
			     text[i]);
	}
}

ZTEST(platform_hardware, test_identifier_does_not_change_between_reads)
{
	char first[KFSW_HARDWARE_ID_TEXT_SIZE];
	char second[KFSW_HARDWARE_ID_TEXT_SIZE];

	read_identifier(first, sizeof(first));
	read_identifier(second, sizeof(second));

	zassert_str_equal(first, second, "the same unit reported two identities");
}

ZTEST(platform_hardware, test_null_buffer_is_refused)
{
	zassert_equal(kfsw_platform_get_hardware_id(NULL, KFSW_HARDWARE_ID_TEXT_SIZE), -EINVAL,
		      "a NULL buffer was accepted");
}

/* A truncated identifier names a different unit, which is the one failure this
 * function exists to prevent. Refusing has to leave the caller's buffer alone
 * as well, or a caller that ignores the code prints half an identity.
 */
ZTEST(platform_hardware, test_short_buffer_is_refused_without_writing)
{
	char text[KFSW_HARDWARE_ID_TEXT_SIZE];
	char truncated[KFSW_HARDWARE_ID_TEXT_SIZE];
	size_t length;

	read_identifier(text, sizeof(text));
	length = strlen(text);

	memset(truncated, 'x', sizeof(truncated));
	zassert_equal(kfsw_platform_get_hardware_id(truncated, length), -ENOSPC,
		      "a buffer with no room for the terminator was accepted");

	for (size_t i = 0U; i < sizeof(truncated); i++) {
		zassert_equal(truncated[i], 'x', "byte %zu was written on a refused read", i);
	}

	zassert_equal(kfsw_platform_get_hardware_id(truncated, 0U), -ENOSPC,
		      "an empty buffer was accepted");
}

ZTEST_SUITE(platform_hardware, NULL, NULL, NULL, NULL, NULL);
