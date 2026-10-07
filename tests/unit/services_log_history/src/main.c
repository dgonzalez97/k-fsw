#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/ztest.h>
#include <csp/csp.h>
#include <csp/csp_debug.h>
#include <kfsw/comms/csp.h>
#include <kfsw/services/log.h>
#include <kfsw/services/log_history.h>
#if CONFIG_KFSW_LOG_REMOTE
#include <kfsw/services/log_remote.h>
#endif

#include "log_history_internal.h"

static bool force_overwrite;
int __real_kfsw_log_history_get_encoded(uint64_t sequence, struct kfsw_log_encoded *record);
int __wrap_kfsw_log_history_get_encoded(uint64_t sequence, struct kfsw_log_encoded *record)
{
	return force_overwrite ? -ENOENT : __real_kfsw_log_history_get_encoded(sequence, record);
}

static struct kfsw_log_history_window window(void)
{
	struct kfsw_log_history_window result;
	zassert_ok(kfsw_log_history_window(32U, &result));
	return result;
}

static void before(void *fixture)
{
	ARG_UNUSED(fixture);
	force_overwrite = false;
	zassert_ok(kfsw_log_set_level(0U));
	for (unsigned int module = 0; module < KFSW_LOG_MODULE_COUNT; module++) {
		zassert_ok(kfsw_log_set_module_level(module, 0U));
	}
}

ZTEST(log_history, test_bounds_and_independent_reads)
{
	struct kfsw_log_record first;
	struct kfsw_log_record second;
	struct kfsw_log_history_window bounds = window();

	zassert_equal(kfsw_log_history_window(0U, &bounds), -EINVAL);
	zassert_equal(kfsw_log_history_window(33U, &bounds), -EINVAL);
	zassert_equal(kfsw_log_history_window(1U, NULL), -EINVAL);
	zassert_equal(kfsw_log_history_get(0U, &first), -ENOENT);
	zassert_equal(kfsw_log_history_get(bounds.end, NULL), -EINVAL);
	kfsw_log_warning("retained\nmessage\r");
	zassert_ok(kfsw_log_history_get(bounds.end, &first));
	zassert_ok(kfsw_log_history_get(bounds.end, &second));
	zassert_mem_equal(&first, &second, sizeof(first));
	zassert_equal(first.sequence, bounds.end);
	zassert_equal(first.severity, 2U);
	zassert_false(first.truncated);
	zassert_equal(strcmp(first.text, "retained message "), 0);
	zassert_equal(window().end, bounds.end + 1U);
}

ZTEST(log_history, test_filters_and_truncation)
{
	struct kfsw_log_history_window bounds = window();
	struct kfsw_log_record record;
	char text[240];

	zassert_ok(kfsw_log_set_level(2U));
	kfsw_log_info("globally filtered");
	zassert_ok(kfsw_log_set_module_level(KFSW_LOG_MODULE_APP, 3U));
	kfsw_log_warning("module filtered");
	zassert_equal(window().end, bounds.end);
	memset(text, 'x', sizeof(text) - 1U);
	text[sizeof(text) - 1U] = '\0';
	/* Too long to package with its string, so the text is kept, cut to fit. */
	kfsw_log_error("%s", text);
	zassert_ok(kfsw_log_history_get(bounds.end, &record));
	zassert_true(record.truncated);
	zassert_equal(strlen(record.text), KFSW_LOG_ENCODED_SIZE - 1U);
	kfsw_log_error("short");
	zassert_ok(kfsw_log_history_get(bounds.end + 1U, &record));
	zassert_false(record.truncated);
}

ZTEST(log_history, test_markers_pass_every_level)
{
	struct kfsw_log_history_window bounds = window();
	struct kfsw_log_record record;

	zassert_ok(kfsw_log_set_level(4U));
	kfsw_log_marker("@READY uptime_ms=%d", 5);
	zassert_ok(kfsw_log_history_get(bounds.end, &record));
	zassert_equal(record.module, KFSW_LOG_MODULE_APP);
	zassert_equal(record.severity, 1U);
	zassert_equal(strcmp(record.text, "@READY uptime_ms=5"), 0);
}

ZTEST(log_history, test_packet_trace_is_logged_without_colours)
{
	struct kfsw_log_history_window bounds = window();
	struct kfsw_log_record record;

	csp_print_func("\033[32mOUT: S %u, D %u\033[0m\n", 7U, 9U);
	zassert_ok(kfsw_log_history_get(bounds.end, &record));
	zassert_equal(record.module, KFSW_LOG_MODULE_CSP);
	zassert_equal(strcmp(record.text, "OUT: S 7, D 9"), 0);
	csp_print_func("\033[0m\n");
	zassert_ok(kfsw_log_set_module_level(KFSW_LOG_MODULE_CSP, 2U));
	csp_print_func("filtered\n");
	zassert_equal(window().end, bounds.end + 1U);
}

ZTEST(log_history, test_records_are_packages_rebuilt_as_text)
{
	struct kfsw_log_history_window bounds = window();
	struct kfsw_log_encoded encoded;
	struct kfsw_log_record record;
	char name[] = "from RAM";

	kfsw_log_warning("%s and %u, %s", name, 42U, "from flash");
	/* The RAM string is copied in; the buffer changing after does not matter. */
	name[0] = 'X';
	zassert_ok(kfsw_log_history_get_encoded(bounds.end, &encoded));
	zassert_true(encoded.package);
	zassert_true(encoded.size < KFSW_LOG_ENCODED_SIZE);
	zassert_ok(kfsw_log_history_get(bounds.end, &record));
	zassert_str_equal(record.text, "from RAM and 42, from flash");
	zassert_false(record.truncated);
}

ZTEST(log_history, test_ring_wrap_and_stale_sequence)
{
	struct kfsw_log_history_window before = window();
	struct kfsw_log_history_window after;
	struct kfsw_log_record record;

	for (unsigned int i = 0; i < 20; i++) {
		kfsw_log_info("message %u", i);
	}
	after = window();
	zassert_equal(after.end, before.end + 20U);
	zassert_equal(after.end - after.first, 8U);
	zassert_equal(after.overwritten, after.end - 9U);
	zassert_equal(kfsw_log_history_get(before.end, &record), -ENOENT);
	zassert_ok(kfsw_log_history_window(2U, &after));
	zassert_equal(after.end - after.first, 2U);
	zassert_ok(kfsw_log_history_get(after.first, &record));
	zassert_equal(strcmp(record.text, "message 18"), 0);
}

static K_THREAD_STACK_DEFINE(writer_stack, 2048);
static struct k_thread writer_thread;

static void writer(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);
	for (unsigned int i = 0; i < 80U; i++) {
		kfsw_log_warning("concurrent %u", i);
		k_msleep(1);
	}
}

ZTEST(log_history, test_concurrent_writer_keeps_records_consistent)
{
	struct kfsw_log_record record;

	k_thread_create(&writer_thread, writer_stack, K_THREAD_STACK_SIZEOF(writer_stack), writer,
			NULL, NULL, NULL, 5, 0, K_NO_WAIT);
	for (unsigned int i = 0; i < 100U; i++) {
		struct kfsw_log_history_window bounds = window();
		for (uint64_t sequence = bounds.first; sequence < bounds.end; sequence++) {
			int result = kfsw_log_history_get(sequence, &record);
			zassert_true(result == 0 || result == -ENOENT);
			if (result == 0) {
				zassert_equal(record.sequence, sequence);
				zassert_not_null(memchr(record.text, '\0', sizeof(record.text)));
			}
		}
		k_msleep(1);
	}
	zassert_ok(k_thread_join(&writer_thread, K_SECONDS(1)));
}

#if CONFIG_KFSW_LOG_REMOTE
#define NONCE 0x123456789abcULL

static csp_conn_t *query(uint8_t stream, uint8_t minimum, uint16_t count, size_t size,
			 uint8_t version)
{
	csp_packet_t *packet = csp_buffer_get(size);
	csp_conn_t *connection =
		csp_connect(CSP_PRIO_NORM, 7U, CONFIG_KFSW_LOG_REMOTE_PORT, 100U, CSP_O_CRC32);
	zassert_not_null(packet);
	zassert_not_null(connection);
	memset(packet->data, 0, size);
	packet->data[0] = version;
	packet->data[1] = stream;
	packet->data[2] = minimum;
	sys_put_be16(count, &packet->data[3]);
	sys_put_be64(NONCE, &packet->data[5]);
	packet->length = size;
	csp_send(connection, packet);
	return connection;
}

static csp_packet_t *reply(csp_conn_t *connection, uint8_t type)
{
	csp_packet_t *packet = csp_read(connection, 500U);
	zassert_not_null(packet);
	zassert_equal(packet->data[0], 2U);
	zassert_equal(packet->data[1], type);
	zassert_equal(sys_get_be64(&packet->data[2]), NONCE);
	return packet;
}

ZTEST(log_history, test_remote_filtered_read_and_completion)
{
	csp_conn_t *connection;
	csp_packet_t *packet;
	uint64_t sequence = window().end;

	kfsw_log_info("filtered on read");
	kfsw_log_error("wire record");
	connection = query(0U, 3U, 2U, 13U, 2U);
	packet = reply(connection, 0U);
	zassert_equal(packet->length, 35U);
	zassert_equal(packet->data[10], 0U, "text unless the parameter says otherwise");
	zassert_equal(sys_get_be64(&packet->data[11]), sequence);
	csp_buffer_free(packet);
	packet = reply(connection, 1U);
	zassert_equal(sys_get_be64(&packet->data[10]), sequence + 1U);
	zassert_equal(packet->data[27], 3U);
	zassert_equal(packet->data[28], 0U);
	zassert_mem_equal(&packet->data[30], "wire record", 11U);
	csp_buffer_free(packet);
	packet = reply(connection, 2U);
	zassert_equal(packet->length, 13U);
	zassert_equal(packet->data[10], 0U);
	zassert_equal(sys_get_be16(&packet->data[11]), 1U);
	csp_buffer_free(packet);
	(void)csp_close(connection);
}

ZTEST(log_history, test_remote_long_records_fit_encrypted_link)
{
	char text[221];

	memset(text, 'x', sizeof(text) - 1U);
	text[sizeof(text) - 1U] = '\0';
	for (int length = 100; length <= 220; length += 60) {
		csp_conn_t *connection;
		csp_packet_t *packet;
		size_t expected;

		kfsw_log_error("%.*s", length, text);
		connection = query(0U, 3U, 1U, 13U, 2U);
		packet = reply(connection, 0U);
		csp_buffer_free(packet);
		packet = reply(connection, 1U);
		/* Text that fits a package keeps all of it, else what the fallback kept. */
		expected = MIN((size_t)length, KFSW_LOG_ENCODED_SIZE - 1U);
		if (length <= 100) {
			expected = (size_t)length;
		}
		zassert_equal(packet->data[29], expected, "length %d", length);
		zassert_equal(packet->length, 30U + expected);
		zassert_equal(packet->data[28], ((size_t)length > expected) ? 1U : 0U);
		zassert_mem_equal(&packet->data[30], text, expected);
		csp_buffer_free(packet);
		packet = reply(connection, 2U);
		zassert_equal(packet->data[10], 0U);
		csp_buffer_free(packet);
		(void)csp_close(connection);
	}
}

struct collected {
	struct kfsw_log_remote_start start;
	struct kfsw_log_remote_message last;
	unsigned int messages;
};

static void collect_start(const struct kfsw_log_remote_start *start, void *context)
{
	((struct collected *)context)->start = *start;
}

static bool collect_message(const struct kfsw_log_remote_message *message, void *context)
{
	struct collected *collected = context;

	collected->last = *message;
	collected->messages++;
	return true;
}

static const struct kfsw_log_remote_visitor collector = {
	.start = collect_start,
	.message = collect_message,
};

ZTEST(log_history, test_remote_client_reads_both_formats)
{
	struct collected collected = {0};
	struct kfsw_log_record rebuilt;
	struct kfsw_log_encoded encoded = {0};

	kfsw_log_warning("value %u", 7U);
	zassert_ok(kfsw_log_remote_read(7U, KFSW_LOG_REMOTE_LOG, 1U, 0U, &collector, &collected));
	zassert_equal(collected.start.format, KFSW_LOG_REMOTE_TEXT);
	zassert_equal(collected.messages, 1U);
	zassert_false(collected.last.package);
	zassert_str_equal((const char *)collected.last.data, "value 7");

	zassert_ok(kfsw_log_remote_set_format(KFSW_LOG_REMOTE_DICTIONARY));
	collected = (struct collected){0};
	zassert_ok(kfsw_log_remote_read(7U, KFSW_LOG_REMOTE_LOG, 1U, 0U, &collector, &collected));
	zassert_ok(kfsw_log_remote_set_format(KFSW_LOG_REMOTE_TEXT));
	zassert_equal(kfsw_log_remote_set_format(2U), -ERANGE);
	zassert_equal(collected.start.format, KFSW_LOG_REMOTE_DICTIONARY);
	zassert_true(collected.last.package);

	/* The package is the record as held, so this image can rebuild its text. */
	encoded.package = true;
	encoded.size = collected.last.size;
	memcpy(encoded.data, collected.last.data, collected.last.size);
	kfsw_log_history_format(&encoded, &rebuilt);
	zassert_str_equal(rebuilt.text, "value 7");
}

ZTEST(log_history, test_remote_client_arguments_and_streams)
{
	struct collected collected = {0};

	zassert_equal(kfsw_log_remote_read(7U, KFSW_LOG_REMOTE_LOG, 0U, 0U, &collector, &collected),
		      -EINVAL);
	zassert_equal(
		kfsw_log_remote_read(7U, KFSW_LOG_REMOTE_LOG, 33U, 0U, &collector, &collected),
		-EINVAL);
	zassert_equal(kfsw_log_remote_read(7U, KFSW_LOG_REMOTE_LOG, 1U, 4U, &collector, &collected),
		      -EINVAL);
	zassert_equal(kfsw_log_remote_read(7U, KFSW_LOG_REMOTE_LOG, 1U, 0U, NULL, &collected),
		      -EINVAL);
	/* This image keeps no journal. */
	zassert_equal(
		kfsw_log_remote_read(7U, KFSW_LOG_REMOTE_JOURNAL, 4U, 0U, &collector, &collected),
		-ENOTSUP);
}

ZTEST(log_history, test_remote_overwrite_reported)
{
	struct collected collected = {0};
	csp_conn_t *connection;
	csp_packet_t *packet;

	kfsw_log_error("overwritten during retrieval");
	force_overwrite = true;
	connection = query(0U, 0U, 1U, 13U, 2U);
	packet = reply(connection, 0U);
	csp_buffer_free(packet);
	packet = reply(connection, 2U);
	zassert_equal(packet->data[10], 1U);
	zassert_equal(sys_get_be16(&packet->data[11]), 0U);
	csp_buffer_free(packet);
	(void)csp_close(connection);
	zassert_equal(kfsw_log_remote_read(7U, KFSW_LOG_REMOTE_LOG, 1U, 0U, &collector, &collected),
		      -EIO);
	force_overwrite = false;
}

ZTEST(log_history, test_remote_rejects_invalid_requests)
{
	/* count 0, count 33, level 4, a stream that does not exist, version 1, size 12 */
	const uint16_t counts[] = {0U, 33U, 1U, 1U, 1U, 1U};
	const uint8_t levels[] = {0U, 0U, 4U, 0U, 0U, 0U};
	const uint8_t streams[] = {0U, 0U, 0U, 2U, 0U, 0U};
	const uint8_t versions[] = {2U, 2U, 2U, 2U, 1U, 2U};
	const size_t sizes[] = {13U, 13U, 13U, 13U, 13U, 12U};

	for (size_t i = 0; i < ARRAY_SIZE(counts); i++) {
		csp_conn_t *connection =
			query(streams[i], levels[i], counts[i], sizes[i], versions[i]);
		zassert_is_null(csp_read(connection, 50U), "request %zu was answered", i);
		(void)csp_close(connection);
	}
}
#endif

static void *setup(void)
{
#if CONFIG_KFSW_LOG_REMOTE
	zassert_equal(kfsw_log_remote_server_start(), -ENETDOWN);
	zassert_ok(kfsw_csp_init());
	zassert_ok(kfsw_csp_start());
	zassert_ok(kfsw_log_remote_server_start());
	zassert_ok(kfsw_log_remote_server_start());
#endif
	return NULL;
}

static struct kfsw_log_retained_header retained(uint64_t next_sequence)
{
	struct kfsw_log_retained_header header = {
		.magic = KFSW_LOG_RETAINED_MAGIC,
		.version = KFSW_LOG_RETAINED_VERSION,
		.depth = CONFIG_KFSW_LOG_HISTORY_DEPTH,
		.record_size = sizeof(struct kfsw_log_encoded),
		.image = kfsw_log_history_image(),
		.next_sequence = next_sequence,
	};

	header.crc = kfsw_log_history_header_crc(&header);
	return header;
}

/* Leave a ring every slot of which agrees with its index, for what runs next. */
static void restart_clean(void)
{
	struct kfsw_log_retained_header header = {0};

	kfsw_log_history_install_retained(&header);
	for (unsigned int i = 0; i < CONFIG_KFSW_LOG_HISTORY_DEPTH; i++) {
		kfsw_log_warning("clean %u", i);
	}
}

ZTEST(log_history, test_retained_ring_continues_where_it_left_off)
{
	struct kfsw_log_history_window bounds;
	struct kfsw_log_record record;
	struct kfsw_log_retained_header header;

	kfsw_log_warning("before the reset");
	bounds = window();
	header = retained(bounds.end);

	/* The records are still in RAM; the reset only cleared .bss. */
	kfsw_log_history_install_retained(&header);
	zassert_ok(kfsw_log_history_get(bounds.end - 1U, &record));
	zassert_equal(strcmp(record.text, "before the reset"), 0);
	zassert_equal(window().end, bounds.end);

	kfsw_log_warning("after the reset");
	zassert_ok(kfsw_log_history_get(bounds.end, &record));
	zassert_equal(record.sequence, bounds.end);
	zassert_equal(strcmp(record.text, "after the reset"), 0);
	restart_clean();
}

ZTEST(log_history, test_retained_ring_that_does_not_belong_starts_clean)
{
	struct kfsw_log_retained_header rejected[7];
	struct kfsw_log_record record;

	kfsw_log_warning("before the reset");
	rejected[0] = retained(window().end);
	rejected[0].magic = 0U;
	rejected[1] = retained(window().end);
	rejected[1].version = KFSW_LOG_RETAINED_VERSION + 1U;
	rejected[2] = retained(window().end);
	rejected[2].depth = CONFIG_KFSW_LOG_HISTORY_DEPTH + 1U;
	rejected[3] = retained(window().end);
	rejected[3].record_size = sizeof(struct kfsw_log_encoded) - 1U;
	rejected[4] = retained(0U);
	/* Valid in every field, but one bit of the header did not survive. */
	rejected[5] = retained(window().end);
	rejected[5].crc ^= 1U;
	/* Left by another image, whose format strings are elsewhere. */
	rejected[6] = retained(window().end);
	rejected[6].image ^= 1U;
	rejected[6].crc = kfsw_log_history_header_crc(&rejected[6]);

	for (size_t i = 0; i < ARRAY_SIZE(rejected); i++) {
		struct kfsw_log_history_window bounds;

		kfsw_log_history_install_retained(&rejected[i]);
		bounds = window();
		zassert_equal(bounds.end, 1U, "entry %zu was accepted", i);
		zassert_equal(bounds.first, 1U);
		zassert_equal(bounds.overwritten, 0U);
		zassert_equal(kfsw_log_history_get(1U, &record), -ENOENT);
	}
	restart_clean();
}

ZTEST(log_history, test_retained_slot_that_disagrees_is_not_served)
{
	struct kfsw_log_history_window bounds;
	struct kfsw_log_record record;
	struct kfsw_log_retained_header header;

	kfsw_log_warning("before the reset");
	bounds = window();

	/* The header survived a further run whose records did not. */
	header = retained(bounds.end + CONFIG_KFSW_LOG_HISTORY_DEPTH);
	kfsw_log_history_install_retained(&header);
	bounds = window();
	zassert_equal(bounds.end - bounds.first, CONFIG_KFSW_LOG_HISTORY_DEPTH);
	for (uint64_t sequence = bounds.first; sequence < bounds.end; sequence++) {
		zassert_equal(kfsw_log_history_get(sequence, &record), -ENOENT);
	}

	/* A fresh record is served from the same ring. */
	kfsw_log_warning("after the reset");
	zassert_ok(kfsw_log_history_get(bounds.end, &record));
	zassert_equal(strcmp(record.text, "after the reset"), 0);
	restart_clean();
}

ZTEST_SUITE(log_history, NULL, setup, before, NULL, NULL);
