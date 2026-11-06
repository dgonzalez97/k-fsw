#include <errno.h>
#include <string.h>
#include <csp/csp.h>
#include "csp_conn.h"
#include <kfsw/services/fwu_lite.h>
#include <zephyr/drivers/flash/flash_simulator.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/storage/stream_flash.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>

#define BLOCK KFSW_FWU_LITE_MAX_BLOCK_SIZE
#define IMAGE_SIZE (BLOCK * 2U + 17U)
#define SOURCE "/checkout/k-fsw/firmware.bin"
static uint8_t image[IMAGE_SIZE];
static csp_packet_t tx;
static csp_packet_t rx;
static uint8_t response[CSP_BUFFER_SIZE];
static size_t response_size;
static bool tx_owned;
static bool rx_owned;
static bool pending;
static unsigned int connected;
static unsigned int closed;
static unsigned int frees;
static unsigned int reads;
static unsigned int sends[7];
static unsigned int writes;
static uint16_t peer;
static uint8_t lose_opcode;
static uint32_t lose_offset;
static bool lose_forever;
static uint32_t lost_id;
static unsigned int retries_seen;
static unsigned int stale_remaining;
static uint8_t stale_kind;
static unsigned int open_count;
static unsigned int close_count;
static size_t source_offset;
static bool source_changed;
static bool source_short;
static bool source_grown;
static uint8_t target_opcode;
static uint32_t last_timeout;
static bool replace_on_start;
static bool oversized_read;
static uint32_t read_delay_ms;
static bool saw_short_timeout;
static bool real_rdp_read;
static bool delay_matched_reply;
static csp_conn_t rdp_connection;

csp_packet_t *__real_csp_read(csp_conn_t *connection, uint32_t timeout);

static size_t fail_after_bytes;
static size_t programmed_bytes;

static int programmed_byte(const struct device *device, off_t offset, uint8_t value)
{
	ARG_UNUSED(device);
	ARG_UNUSED(offset);
	if (fail_after_bytes != 0U && programmed_bytes >= fail_after_bytes) {
		return -EIO;
	}
	programmed_bytes++;
	return value;
}

static const struct flash_simulator_cb flash_callbacks = {.write_byte = programmed_byte};

int __real_stream_flash_buffered_write(struct stream_flash_ctx *ctx, const uint8_t *data,
				       size_t size, bool flush);
int __wrap_stream_flash_buffered_write(struct stream_flash_ctx *ctx, const uint8_t *data,
				       size_t size, bool flush)
{
	writes++;
	return __real_stream_flash_buffered_write(ctx, data, size, flush);
}

int __wrap_nsi_host_open(const char *path, int flags)
{
	zassert_equal(strcmp(path, SOURCE), 0);
	zassert_equal(flags, 0);
	open_count++;
	source_offset = 0U;
	return 42;
}

long __wrap_nsi_host_read(int fd, void *data, unsigned long size)
{
	zassert_equal(fd, 42);
	if (oversized_read) {
		return (long)UINT32_MAX + 1L;
	}
	size_t count = MIN((size_t)size, sizeof(image) - source_offset);
	if (source_short && open_count % 2U == 0U && count > 0U) {
		count--;
	}
	memcpy(data, image + source_offset, count);
	if (source_changed && open_count % 2U == 0U && source_offset == 0U && count > 0U) {
		((uint8_t *)data)[0] ^= 1U;
	}
	if (source_grown && open_count % 2U == 0U && count == 0U && size != 0U) {
		((uint8_t *)data)[0] = 1U;
		return 1;
	}
	source_offset += count;
	return (long)count;
}

int __wrap_nsi_host_close(int fd)
{
	zassert_equal(fd, 42);
	close_count++;
	return 0;
}

csp_conn_t *__wrap_csp_connect(uint8_t priority, uint16_t node, uint8_t port, uint32_t timeout,
			       uint32_t options)
{
	ARG_UNUSED(priority);
	ARG_UNUSED(port);
	ARG_UNUSED(timeout);
	zassert_equal(node, 2U);
	zassert_true((options & CSP_O_CRC32) != 0U);
	connected++;
	return (csp_conn_t *)&connected;
}

int __wrap_csp_close(csp_conn_t *connection)
{
	zassert_equal_ptr(connection, &connected);
	closed++;
	return 0;
}

csp_packet_t *__wrap_csp_buffer_get(size_t unused)
{
	ARG_UNUSED(unused);
	zassert_false(tx_owned);
	tx_owned = true;
	return &tx;
}

void __wrap_csp_buffer_free(void *packet)
{
	if (packet == &tx) {
		zassert_true(tx_owned);
		tx_owned = false;
	} else {
		zassert_equal_ptr(packet, &rx);
		zassert_true(rx_owned);
		rx_owned = false;
	}
	frees++;
}

void __wrap_csp_send(csp_conn_t *connection, csp_packet_t *packet)
{
	struct kfsw_fwu_lite_message request;
	struct kfsw_fwu_lite_message reply;
	uint8_t receipt[KFSW_FWU_LITE_RECEIPT_SIZE];
	zassert_equal_ptr(connection, &connected);
	zassert_ok(kfsw_fwu_lite_decode(packet->data, packet->length, &request));
	sends[request.opcode]++;
	last_timeout = 0U;
	if (lost_id != 0U && request.opcode == lose_opcode && request.offset == lose_offset) {
		zassert_equal(request.request_id, lost_id,
			      "retries must preserve request identity");
		retries_seen++;
	}
	if (replace_on_start && request.opcode == KFSW_FWU_LITE_OP_START_FLASHING) {
		struct kfsw_fwu_status current;
		struct kfsw_fwu_handle successor;
		zassert_ok(kfsw_fwu_get_status(&current));
		struct kfsw_fwu_image description = current.image;
		zassert_ok(kfsw_fwu_session_abort(&current.handle, &current));
		zassert_ok(kfsw_fwu_session_begin(&current.handle.owner, &description,
						  request.transfer_id + 1000U, &successor,
						  &current));
		for (size_t offset = 0U; offset < sizeof(image); offset += BLOCK) {
			zassert_ok(kfsw_fwu_session_write(&successor, offset, image + offset,
							  MIN(BLOCK, sizeof(image) - offset),
							  &current));
		}
		zassert_ok(kfsw_fwu_session_verify(&successor, &current));
		replace_on_start = false;
	}
	zassert_ok(kfsw_fwu_lite_handle_peer(peer, &request, &reply, receipt));
	zassert_ok(kfsw_fwu_lite_encode(&reply, response, sizeof(response), &response_size));
	pending = true;
	if (request.opcode == lose_opcode && request.offset == lose_offset &&
	    (lost_id == 0U || lose_forever)) {
		lost_id = request.request_id;
		pending = false;
	}
	if (request.opcode == target_opcode) {
		/* Each exchange can encounter the same stale flood. */
		stale_remaining = (stale_kind == 10U || stale_kind == 13U) ? 16U : 1U;
	}
	__wrap_csp_buffer_free(packet);
}

csp_packet_t *__wrap_csp_read(csp_conn_t *connection, uint32_t timeout)
{
	zassert_equal_ptr(connection, &connected);
	zassert_true(timeout <= kfsw_fwu_lite_get_timeout_ms());
	if (stale_remaining != 0U && last_timeout != 0U) {
		zassert_true(timeout <= last_timeout,
			     "stale packets must not restart the deadline");
	}
	last_timeout = timeout;
	if (real_rdp_read) {
		return __real_csp_read(&rdp_connection, timeout);
	}
	if (delay_matched_reply) {
		k_sleep(K_MSEC(read_delay_ms));
	}
	if (stale_remaining != 0U && read_delay_ms != 0U) {
		saw_short_timeout |= timeout < kfsw_fwu_lite_get_timeout_ms();
		k_sleep(K_MSEC(read_delay_ms));
	}
	reads++;
	if (!pending) {
		return NULL;
	}
	zassert_false(rx_owned);
	rx_owned = true;
	memcpy(rx.data, response, response_size);
	rx.length = response_size;
	if (stale_remaining != 0U) {
		stale_remaining--;
		switch (stale_kind) {
		case 13:
		case 1:
			rx.data[0]++;
			break;
		case 2:
			rx.data[1] = KFSW_FWU_LITE_REQUEST;
			break;
		case 3:
			rx.data[2] = KFSW_FWU_LITE_OP_BLOCK;
			break;
		case 4:
			rx.data[7]++;
			break;
		case 5:
			rx.data[11]++;
			break;
		case 6:
			rx.data[15]++;
			break;
		case 7:
			sys_put_be32(1U, rx.data + 16);
			sys_put_be32(1U, rx.data + 30);
			break;
		case 8:
			rx.length = CSP_BUFFER_SIZE + 1U;
			break;
		case 9:
			rx.data[20]++;
			break;
		case 10:
			rx.data[7]++;
			break;
		case 11:
			rx.data[14U + 28U + 10U] ^= 1U;
			break;
		case 12:
			sys_put_be32(1U, rx.data + 16);
			sys_put_be32(1U, rx.data + 30);
			sys_put_be32(1U, rx.data + 34);
			break;
		default:
			zassert_unreachable();
		}
	} else {
		pending = false;
		last_timeout = 0U;
	}
	return &rx;
}

static void before(void *fixture)
{
	struct kfsw_fwu_status status;
	ARG_UNUSED(fixture);
	fail_after_bytes = 0U;
	programmed_bytes = 0U;
	flash_simulator_set_callbacks(
		DEVICE_DT_GET(DT_MTD_FROM_FIXED_PARTITION(DT_CHOSEN(kfsw_fwu_partition))),
		&flash_callbacks);
	zassert_ok(kfsw_fwu_get_status(&status));
	if (status.handle.generation != 0U) {
		zassert_ok(kfsw_fwu_session_abort(&status.handle, &status));
	}
	for (size_t i = 0U; i < sizeof(image); i++) {
		image[i] = (uint8_t)(i * 23U + 5U);
	}
	memset(sends, 0, sizeof(sends));
	connected = closed = frees = reads = writes = 0U;
	open_count = close_count = 0U;
	peer = 16U;
	lose_opcode = target_opcode = stale_kind = 0U;
	lose_offset = lost_id = retries_seen = stale_remaining = last_timeout = 0U;
	lose_forever = source_changed = source_short = source_grown = pending = false;
	replace_on_start = oversized_read = saw_short_timeout = false;
	real_rdp_read = delay_matched_reply = false;
	read_delay_ms = 0U;
	zassert_false(tx_owned);
	zassert_false(rx_owned);
	zassert_ok(kfsw_fwu_lite_set_retries(1U));
	zassert_ok(kfsw_fwu_lite_set_timeout_ms(100U));
}

static void after(void *fixture)
{
	ARG_UNUSED(fixture);
	zassert_equal(open_count, close_count);
	zassert_equal(connected, closed);
	zassert_false(tx_owned);
	zassert_false(rx_owned);
}

static void send_image(void)
{
	zassert_ok(kfsw_fwu_lite_send_file(2U, SOURCE, NULL, NULL));
	struct kfsw_fwu_status status;
	zassert_ok(kfsw_fwu_get_status(&status));
	zassert_equal(status.received, sizeof(image));
	zassert_equal(status.state, KFSW_FWU_RECEIVING);
	zassert_equal(sends[KFSW_FWU_LITE_OP_VERIFY], 0U);
	zassert_equal(sends[KFSW_FWU_LITE_OP_START_FLASHING], 0U);
}

ZTEST(fwu_lite_client, test_receipt_only)
{
	send_image();
	zassert_equal(sends[KFSW_FWU_LITE_OP_STATUS], 1U);
	zassert_equal(writes, 3U);
}

ZTEST(fwu_lite_client, test_lost_begin)
{
	lose_opcode = KFSW_FWU_LITE_OP_BEGIN;
	send_image();
	zassert_equal(retries_seen, 1U);
	zassert_equal(sends[KFSW_FWU_LITE_OP_BEGIN], 2U);
	zassert_equal(writes, 3U);
}

ZTEST(fwu_lite_client, test_lost_full_block)
{
	lose_opcode = KFSW_FWU_LITE_OP_BLOCK;
	send_image();
	zassert_equal(retries_seen, 1U);
	zassert_equal(writes, 3U);
}

ZTEST(fwu_lite_client, test_lost_short_tail)
{
	lose_opcode = KFSW_FWU_LITE_OP_BLOCK;
	lose_offset = 2U * BLOCK;
	send_image();
	zassert_equal(retries_seen, 1U);
	zassert_equal(writes, 3U);
}

ZTEST(fwu_lite_client, test_exhaustion_then_later_invocation_resume)
{
	lose_opcode = KFSW_FWU_LITE_OP_BLOCK;
	lose_forever = true;
	zassert_equal(kfsw_fwu_lite_send_file(2U, SOURCE, NULL, NULL), -ETIMEDOUT);
	struct kfsw_fwu_status status;
	zassert_ok(kfsw_fwu_get_status(&status));
	zassert_equal(status.received, BLOCK);
	uint32_t generation = status.handle.generation;
	lose_opcode = 0U;
	lose_forever = false;
	send_image();
	zassert_ok(kfsw_fwu_get_status(&status));
	zassert_equal(status.handle.generation, generation);
	zassert_equal(sends[KFSW_FWU_LITE_OP_BEGIN], 1U);
	zassert_equal(writes, 3U);
}

ZTEST(fwu_lite_client, test_begin_exhaustion_then_status_reconciles)
{
	lose_opcode = KFSW_FWU_LITE_OP_BEGIN;
	lose_forever = true;
	zassert_equal(kfsw_fwu_lite_send_file(2U, SOURCE, NULL, NULL), -ETIMEDOUT);
	lose_opcode = 0U;
	send_image();
	zassert_equal(sends[KFSW_FWU_LITE_OP_BEGIN], 2U);
}

ZTEST(fwu_lite_client, test_complete_reconnect_no_writes)
{
	send_image();
	send_image();
	zassert_equal(writes, 3U);
}

ZTEST(fwu_lite_client, test_different_image_rejected)
{
	send_image();
	image[0] ^= 1U;
	zassert_equal(kfsw_fwu_lite_send_file(2U, SOURCE, NULL, NULL), -ESTALE);
	zassert_equal(writes, 3U);
}

ZTEST(fwu_lite_client, test_different_peer_rejected)
{
	send_image();
	peer = 17U;
	zassert_equal(kfsw_fwu_lite_send_file(2U, SOURCE, NULL, NULL), -EACCES);
	zassert_equal(writes, 3U);
}

ZTEST(fwu_lite_client, test_changed_source)
{
	source_changed = true;
	zassert_equal(kfsw_fwu_lite_send_file(2U, SOURCE, NULL, NULL), -ESTALE);
	zassert_equal(sends[KFSW_FWU_LITE_OP_ABORT], 0U);
	zassert_equal(sends[KFSW_FWU_LITE_OP_VERIFY], 0U);
}

ZTEST(fwu_lite_client, test_short_read)
{
	source_short = true;
	zassert_equal(kfsw_fwu_lite_send_file(2U, SOURCE, NULL, NULL), -ESTALE);
	zassert_equal(writes, 0U);
}

ZTEST(fwu_lite_client, test_source_grows)
{
	source_grown = true;
	zassert_equal(kfsw_fwu_lite_send_file(2U, SOURCE, NULL, NULL), -ESTALE);
	zassert_equal(sends[KFSW_FWU_LITE_OP_VERIFY], 0U);
}

ZTEST(fwu_lite_client, test_resume_prefix_crc_checked)
{
	lose_opcode = KFSW_FWU_LITE_OP_BLOCK;
	lose_forever = true;
	zassert_equal(kfsw_fwu_lite_send_file(2U, SOURCE, NULL, NULL), -ETIMEDOUT);
	lose_opcode = 0U;
	source_changed = true;
	zassert_equal(kfsw_fwu_lite_send_file(2U, SOURCE, NULL, NULL), -EILSEQ);
	zassert_equal(writes, 1U);
}

static void stale(uint8_t kind)
{
	stale_kind = kind;
	target_opcode = KFSW_FWU_LITE_OP_BEGIN;
	send_image();
	zassert_true(frees > connected);
}
ZTEST(fwu_lite_client, test_stale_version)
{
	stale(1U);
}
ZTEST(fwu_lite_client, test_wrong_direction)
{
	stale(2U);
}
ZTEST(fwu_lite_client, test_wrong_opcode)
{
	stale(3U);
}
ZTEST(fwu_lite_client, test_wrong_request)
{
	stale(4U);
}
ZTEST(fwu_lite_client, test_wrong_transfer)
{
	stale(5U);
}
ZTEST(fwu_lite_client, test_wrong_generation)
{
	stale_kind = 6U;
	target_opcode = KFSW_FWU_LITE_OP_BLOCK;
	send_image();
}
ZTEST(fwu_lite_client, test_wrong_progress)
{
	stale_kind = 7U;
	target_opcode = KFSW_FWU_LITE_OP_BLOCK;
	send_image();
}
ZTEST(fwu_lite_client, test_oversized_response)
{
	stale(8U);
}
ZTEST(fwu_lite_client, test_wrong_crc)
{
	stale(9U);
}
ZTEST(fwu_lite_client, test_wrong_descriptor)
{
	stale(11U);
}

ZTEST(fwu_lite_client, test_stale_flood_bounded)
{
	stale_kind = 10U;
	target_opcode = KFSW_FWU_LITE_OP_BEGIN;
	zassert_equal(kfsw_fwu_lite_send_file(2U, SOURCE, NULL, NULL), -ETIMEDOUT);
	zassert_equal(reads, 1U + 2U * 16U);
	zassert_equal(sends[KFSW_FWU_LITE_OP_BEGIN], 2U);
}

ZTEST(fwu_lite_client, test_explicit_verify_activate_and_repeated_start)
{
	send_image();
	struct kfsw_fwu_status status;
	zassert_ok(kfsw_fwu_lite_remote_status(2U, NULL, &status));
	struct kfsw_fwu_handle handle = status.handle;
	stale_kind = 3U;
	target_opcode = KFSW_FWU_LITE_OP_VERIFY;
	zassert_ok(kfsw_fwu_lite_remote_verify(2U, &handle, &status));
	zassert_equal(status.state, KFSW_FWU_VERIFIED);
	zassert_ok(kfsw_fwu_lite_remote_activate(2U, &handle, &status));
	zassert_equal(status.state, KFSW_FWU_READY);
	zassert_ok(kfsw_fwu_lite_start_flashing(2U));
	zassert_ok(kfsw_fwu_lite_remote_abort(2U, &handle, &status));
	zassert_true(status.aborted);
}

ZTEST(fwu_lite_client, test_start_cannot_activate_unverified)
{
	send_image();
	zassert_equal(kfsw_fwu_lite_start_flashing(2U), -EINVAL);
}

ZTEST(fwu_lite_client, test_invalid_path_and_node)
{
	zassert_equal(kfsw_fwu_lite_send_file(2U, "firmware.bin", NULL, NULL), -EINVAL);
	zassert_equal(kfsw_fwu_lite_send_file(0U, SOURCE, NULL, NULL), -EINVAL);
	zassert_equal(open_count, 0U);
}

ZTEST(fwu_lite_client, test_receipt_and_retry_count)
{
	struct kfsw_fwu_status receipt;
	uint32_t resent;
	lose_opcode = KFSW_FWU_LITE_OP_BLOCK;
	zassert_ok(kfsw_fwu_lite_send_file(2U, SOURCE, &receipt, &resent));
	zassert_equal(receipt.received, sizeof(image));
	zassert_not_equal(receipt.handle.generation, 0U);
	zassert_equal(receipt.state, KFSW_FWU_RECEIVING);
	zassert_equal(resent, 1U);
}

ZTEST(fwu_lite_client, test_incompatible_version_reported)
{
	stale_kind = 13U;
	target_opcode = KFSW_FWU_LITE_OP_BEGIN;
	zassert_equal(kfsw_fwu_lite_send_file(2U, SOURCE, NULL, NULL), -EPROTONOSUPPORT);
	zassert_equal(reads, 17U);
}

ZTEST(fwu_lite_client, test_validate_rejects_incomplete_success_response)
{
	send_image();
	struct kfsw_fwu_status status;
	zassert_ok(kfsw_fwu_lite_remote_status(2U, NULL, &status));
	struct kfsw_fwu_handle handle = status.handle;
	stale_kind = 12U;
	target_opcode = KFSW_FWU_LITE_OP_VERIFY;
	zassert_ok(kfsw_fwu_lite_remote_verify(2U, &handle, &status));
	zassert_equal(status.received, sizeof(image));
	zassert_equal(status.committed, sizeof(image));
}

ZTEST(fwu_lite_client, test_start_bound_to_resolved_generation)
{
	send_image();
	replace_on_start = true;
	zassert_equal(kfsw_fwu_lite_start_flashing(2U), -ESTALE);
	struct kfsw_fwu_status status;
	zassert_ok(kfsw_fwu_get_status(&status));
	zassert_equal(status.state, KFSW_FWU_VERIFIED);
}

ZTEST(fwu_lite_client, test_old_generation_remote_operations)
{
	send_image();
	struct kfsw_fwu_status status;
	zassert_ok(kfsw_fwu_lite_remote_status(2U, NULL, &status));
	struct kfsw_fwu_handle handle = status.handle;
	handle.generation++;
	zassert_equal(kfsw_fwu_lite_remote_verify(2U, &handle, &status), -ESTALE);
	zassert_equal(kfsw_fwu_lite_remote_abort(2U, &handle, &status), -ESTALE);
	zassert_ok(kfsw_fwu_get_status(&status));
	zassert_equal(status.received, sizeof(image));
}

ZTEST(fwu_lite_client, test_source_length_checked_before_narrowing)
{
	oversized_read = true;
	zassert_equal(kfsw_fwu_lite_send_file(2U, SOURCE, NULL, NULL), -EFBIG);
	zassert_equal(connected, 0U);
}

ZTEST(fwu_lite_client, test_stale_replies_share_absolute_deadline)
{
	stale_kind = 10U;
	target_opcode = KFSW_FWU_LITE_OP_BEGIN;
	read_delay_ms = 20U;
	zassert_equal(kfsw_fwu_lite_send_file(2U, SOURCE, NULL, NULL), -ETIMEDOUT);
	zassert_true(saw_short_timeout);
	zassert_true(reads < 33U, "deadline must expire before all sixteen stale replies");
}

ZTEST(fwu_lite_client, test_partial_flash_failure_status_and_owned_abort)
{
	struct kfsw_fwu_status status;

	if (CONFIG_KFSW_FWU_STREAM_BUFFER_SIZE != 64U) {
		ztest_test_skip();
		return;
	}
	fail_after_bytes = 64U;
	zassert_equal(kfsw_fwu_lite_send_file(2U, SOURCE, NULL, NULL), -EIO);
	fail_after_bytes = 0U;
	zassert_ok(kfsw_fwu_lite_remote_status(2U, NULL, &status));
	zassert_equal(status.state, KFSW_FWU_FAILED);
	zassert_equal(status.received, 0U);
	zassert_equal(status.committed, 64U);
	const struct kfsw_fwu_handle handle = status.handle;
	zassert_ok(kfsw_fwu_lite_remote_abort(2U, &handle, &status));
	zassert_true(status.aborted);
	zassert_equal(status.state, KFSW_FWU_IDLE);
	zassert_equal(status.committed, 0U);
}

ZTEST_SUITE(fwu_lite_client, NULL, NULL, before, after, NULL);

ZTEST(fwu_lite_client, test_real_rdp_read_respects_exchange_deadline)
{
	struct kfsw_fwu_status status;
	int64_t started;

	if (!IS_ENABLED(CONFIG_KFSW_FWU_LITE_RDP)) {
		ztest_test_skip();
		return;
	}
	memset(&rdp_connection, 0, sizeof(rdp_connection));
	rdp_connection.state = CONN_OPEN;
	rdp_connection.idin.flags = CSP_FRDP;
	rdp_connection.rdp.conn_timeout = 1000U;
	rdp_connection.rx_queue = csp_queue_create_static(
		CSP_CONN_RXQUEUE_LEN, sizeof(csp_packet_t *), rdp_connection.rx_queue_static_data,
		&rdp_connection.rx_queue_static);
	zassert_not_null(rdp_connection.rx_queue);
	real_rdp_read = true;
	started = k_uptime_get();
	zassert_equal(kfsw_fwu_lite_remote_status(2U, NULL, &status), -ETIMEDOUT);
	zassert_true(k_uptime_get() - started < 400,
		     "two 100 ms exchanges must not use the 1000 ms RDP timeout");
	zassert_equal(csp_queue_size(rdp_connection.rx_queue), 0);
}

ZTEST(fwu_lite_client, test_correlated_reply_after_deadline_rejected)
{
	struct kfsw_fwu_status status;

	send_image();
	delay_matched_reply = true;
	read_delay_ms = 110U;
	zassert_equal(kfsw_fwu_lite_remote_status(2U, NULL, &status), -ETIMEDOUT);
}
