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

#include "log_history_internal.h"

static bool force_overwrite;
int __real_kfsw_log_history_get(uint64_t sequence, struct kfsw_log_record *record);
int __wrap_kfsw_log_history_get(uint64_t sequence, struct kfsw_log_record *record)
{
	return force_overwrite ? -ENOENT : __real_kfsw_log_history_get(sequence, record);
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
	kfsw_log_error("%s", text);
	zassert_ok(kfsw_log_history_get(bounds.end, &record));
	zassert_true(record.truncated);
	zassert_equal(strlen(record.text), KFSW_LOG_TEXT_SIZE - 1U);
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

#if CONFIG_KFSW_LOG_HISTORY_CSP
static csp_conn_t *query(uint8_t minimum, uint16_t count, size_t size)
{
	csp_packet_t *packet = csp_buffer_get(size);
	csp_conn_t *connection =
		csp_connect(CSP_PRIO_NORM, 7U, CONFIG_KFSW_LOG_HISTORY_PORT, 100U, CSP_O_CRC32);
	zassert_not_null(packet);
	zassert_not_null(connection);
	memset(packet->data, 0, size);
	packet->data[0] = 1U;
	packet->data[1] = minimum;
	sys_put_be16(count, &packet->data[2]);
	sys_put_be64(0x123456789abcULL, &packet->data[4]);
	packet->length = size;
	csp_send(connection, packet);
	return connection;
}

static csp_packet_t *reply(csp_conn_t *connection, uint8_t type)
{
	csp_packet_t *packet = csp_read(connection, 500U);
	zassert_not_null(packet);
	zassert_equal(packet->data[0], 1U);
	zassert_equal(packet->data[1], type);
	zassert_equal(sys_get_be64(&packet->data[2]), 0x123456789abcULL);
	return packet;
}

ZTEST(log_history, test_csp_filtered_read_and_completion)
{
	csp_conn_t *connection;
	csp_packet_t *packet;
	uint64_t sequence = window().end;

	kfsw_log_info("filtered on read");
	kfsw_log_error("wire record");
	connection = query(3U, 2U, 12U);
	packet = reply(connection, 0U);
	zassert_equal(packet->length, 34U);
	zassert_equal(sys_get_be64(&packet->data[10]), sequence);
	csp_buffer_free(packet);
	packet = reply(connection, 1U);
	zassert_equal(sys_get_be64(&packet->data[10]), sequence + 1U);
	zassert_equal(packet->data[27], 3U);
	zassert_mem_equal(&packet->data[30], "wire record", 11U);
	csp_buffer_free(packet);
	packet = reply(connection, 2U);
	zassert_equal(packet->length, 13U);
	zassert_equal(packet->data[10], 0U);
	zassert_equal(sys_get_be16(&packet->data[11]), 1U);
	csp_buffer_free(packet);
	(void)csp_close(connection);
}

ZTEST(log_history, test_csp_long_records_fit_encrypted_link)
{
	const size_t lengths[] = {190U, 191U, 220U};
	char text[221];
	struct kfsw_log_record retained;

	memset(text, 'x', sizeof(text));
	for (size_t i = 0; i < ARRAY_SIZE(lengths); i++) {
		uint64_t sequence = window().end;
		csp_conn_t *connection;
		csp_packet_t *packet;

		kfsw_log_error("%.*s", (int)lengths[i], text);
		connection = query(3U, 1U, 12U);
		packet = reply(connection, 0U);
		csp_buffer_free(packet);
		packet = reply(connection, 1U);
		zassert_equal(packet->length, 220U);
		zassert_equal(packet->data[28], lengths[i] > 190U ? 1U : 0U);
		zassert_equal(packet->data[29], 190U);
		zassert_mem_equal(&packet->data[30], text, 190U);
		csp_buffer_free(packet);
		packet = reply(connection, 2U);
		zassert_equal(packet->data[10], 0U);
		zassert_equal(sys_get_be16(&packet->data[11]), 1U);
		csp_buffer_free(packet);
		(void)csp_close(connection);
		zassert_ok(kfsw_log_history_get(sequence, &retained));
		zassert_equal(strlen(retained.text), MIN(lengths[i], KFSW_LOG_TEXT_SIZE - 1U));
	}
}

ZTEST(log_history, test_csp_overwrite_reported)
{
	csp_conn_t *connection;
	csp_packet_t *packet;

	kfsw_log_error("overwritten during retrieval");
	force_overwrite = true;
	connection = query(0U, 1U, 12U);
	packet = reply(connection, 0U);
	csp_buffer_free(packet);
	packet = reply(connection, 2U);
	zassert_equal(packet->data[10], 1U);
	zassert_equal(sys_get_be16(&packet->data[11]), 0U);
	csp_buffer_free(packet);
	(void)csp_close(connection);
	force_overwrite = false;
}

ZTEST(log_history, test_csp_rejects_invalid_requests)
{
	const uint16_t counts[] = {0U, 33U, 1U, 1U};
	const uint8_t levels[] = {0U, 0U, 4U, 0U};
	const size_t sizes[] = {12U, 12U, 12U, 13U};

	for (size_t i = 0; i < ARRAY_SIZE(counts); i++) {
		csp_conn_t *connection = query(levels[i], counts[i], sizes[i]);
		zassert_is_null(csp_read(connection, 50U));
		(void)csp_close(connection);
	}
}
#endif

static void *setup(void)
{
#if CONFIG_KFSW_LOG_HISTORY_CSP
	zassert_equal(kfsw_log_history_server_start(), -ENETDOWN);
	zassert_ok(kfsw_csp_init());
	zassert_ok(kfsw_csp_start());
	zassert_ok(kfsw_log_history_server_start());
	zassert_ok(kfsw_log_history_server_start());
#endif
	return NULL;
}

static struct kfsw_log_retained_header retained(uint64_t next_sequence)
{
	struct kfsw_log_retained_header header = {
		.magic = KFSW_LOG_RETAINED_MAGIC,
		.version = KFSW_LOG_RETAINED_VERSION,
		.depth = CONFIG_KFSW_LOG_HISTORY_DEPTH,
		.record_size = sizeof(struct kfsw_log_record),
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
	struct kfsw_log_retained_header rejected[6];
	struct kfsw_log_record record;

	kfsw_log_warning("before the reset");
	rejected[0] = retained(window().end);
	rejected[0].magic = 0U;
	rejected[1] = retained(window().end);
	rejected[1].version = KFSW_LOG_RETAINED_VERSION + 1U;
	rejected[2] = retained(window().end);
	rejected[2].depth = CONFIG_KFSW_LOG_HISTORY_DEPTH + 1U;
	rejected[3] = retained(window().end);
	rejected[3].record_size = sizeof(struct kfsw_log_record) - 1U;
	rejected[4] = retained(0U);
	/* Valid in every field, but one bit of the header did not survive. */
	rejected[5] = retained(window().end);
	rejected[5].crc ^= 1U;

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
