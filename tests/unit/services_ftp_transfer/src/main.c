/*
 * The FTP send and receive loops, driven through a faked transport.
 *
 * Both loops are reached over CSP in the integration suites, which proves the
 * happy path and little else: a peer will not replay an offset or drop a link
 * halfway on request. ftp_link.h is the only transport the transfer engine
 * speaks, so faking it reaches those cases from the host.
 */

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/fff.h>
#include <zephyr/fs/fs.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/crc.h>
#include <zephyr/ztest.h>

#include <kfsw/platform/storage.h>
#include <kfsw/services/ftp.h>

#include "ftp_internal.h"
#include "ftp_link.h"

DEFINE_FFF_GLOBALS;

#define STORAGE_PARTITION_NODE DT_CHOSEN(kfsw_storage_partition)
#define TEST_SINK_PATH KFSW_FTP_STORAGE_ROOT "/transfer.part"
#define TEST_SOURCE_PATH KFSW_FTP_STORAGE_ROOT "/transfer.src"
#define TEST_REQUEST_ID 0x51EDU

FAKE_VALUE_FUNC(int, __wrap_kfsw_ftp_link_send, struct kfsw_ftp_link *,
		const struct kfsw_ftp_message *);
FAKE_VALUE_FUNC(int, __wrap_kfsw_ftp_link_receive, struct kfsw_ftp_link *,
		struct kfsw_ftp_link_frame *);
FAKE_VOID_FUNC(__wrap_kfsw_ftp_link_release, struct kfsw_ftp_link_frame *);

/* One scripted inbound message per call, so a test says what the peer sends
 * rather than what it would have to be persuaded to send.
 */
#define SCRIPT_MAX 4
static struct kfsw_ftp_message scripted[SCRIPT_MAX];
static int scripted_result[SCRIPT_MAX];
static size_t scripted_count;
static size_t scripted_index;

static uint8_t payload[3][8];

static struct kfsw_ftp_link link_stub;
static struct kfsw_ftp_workspace workspace;
static struct kfsw_ftp_transfer transfer;

static int deliver_scripted(struct kfsw_ftp_link *link, struct kfsw_ftp_link_frame *frame)
{
	ARG_UNUSED(link);

	if (scripted_index >= scripted_count) {
		return -ENODATA;
	}
	if (scripted_result[scripted_index] != 0) {
		return scripted_result[scripted_index++];
	}
	memset(frame, 0, sizeof(*frame));
	frame->message = scripted[scripted_index++];
	return 0;
}

/* A message that would be accepted, so a test only has to spoil the one field
 * it is about.
 */
static struct kfsw_ftp_message good_message(uint32_t offset, size_t which, uint32_t total,
					    uint32_t crc)
{
	struct kfsw_ftp_message message = {
		.opcode = KFSW_FTP_OP_PUT_DATA,
		.request_id = TEST_REQUEST_ID,
		.offset = offset,
		.total_size = total,
		.crc32 = crc,
		.data = payload[which],
		.data_size = (uint16_t)sizeof(payload[which]),
	};

	return message;
}

static void erase_storage_partition(void)
{
	const struct flash_area *area;

	zassert_ok(flash_area_open(DT_FIXED_PARTITION_ID(STORAGE_PARTITION_NODE), &area));
	zassert_ok(flash_area_flatten(area, 0, area->fa_size));
	flash_area_close(area);
}

static void *transfer_setup(void)
{
	erase_storage_partition();
	zassert_ok(kfsw_storage_init());
	zassert_ok(kfsw_storage_mount());
	zassert_ok(kfsw_ftp_init());

	for (size_t index = 0U; index < ARRAY_SIZE(payload); index++) {
		memset(payload[index], (int)(0xA0U + index), sizeof(payload[index]));
	}
	return NULL;
}

static void reset_transfer(void *fixture)
{
	ARG_UNUSED(fixture);

	RESET_FAKE(__wrap_kfsw_ftp_link_send);
	RESET_FAKE(__wrap_kfsw_ftp_link_receive);
	RESET_FAKE(__wrap_kfsw_ftp_link_release);
	FFF_RESET_HISTORY();

	memset(scripted, 0, sizeof(scripted));
	memset(scripted_result, 0, sizeof(scripted_result));
	scripted_count = 0U;
	scripted_index = 0U;

	memset(&transfer, 0, sizeof(transfer));
	transfer.link = &link_stub;
	transfer.workspace = &workspace;
	transfer.request_id = TEST_REQUEST_ID;
	transfer.data_opcode = KFSW_FTP_OP_PUT_DATA;

	__wrap_kfsw_ftp_link_receive_fake.custom_fake = deliver_scripted;
	(void)fs_unlink(TEST_SINK_PATH);
	(void)fs_unlink(TEST_SOURCE_PATH);
}

ZTEST_SUITE(services_ftp_transfer, NULL, transfer_setup, reset_transfer, NULL, NULL);

ZTEST(services_ftp_transfer, test_receive_writes_every_chunk_and_checksums_them)
{
	const uint32_t total = (uint32_t)(2U * sizeof(payload[0]));
	uint32_t expected_crc = 0U;
	struct fs_dirent info;

	expected_crc = crc32_ieee_update(expected_crc, payload[0], sizeof(payload[0]));
	expected_crc = crc32_ieee_update(expected_crc, payload[1], sizeof(payload[1]));

	transfer.total_size = total;
	transfer.crc32 = 0U;
	scripted[0] = good_message(0U, 0U, total, 0U);
	scripted[1] = good_message((uint32_t)sizeof(payload[0]), 1U, total, 0U);
	scripted_count = 2U;

	zassert_ok(kfsw_ftp_transfer_open_sink(&transfer, TEST_SINK_PATH));
	zassert_ok(kfsw_ftp_transfer_receive(&transfer));
	zassert_ok(fs_close(&transfer.file));

	zassert_equal(transfer.offset, total, "every byte received should advance the offset");
	zassert_equal(transfer.actual_crc32, expected_crc,
		      "the running checksum should cover the bytes that arrived");
	zassert_ok(fs_stat(TEST_SINK_PATH, &info));
	zassert_equal((uint32_t)info.size, total, "the file should hold what was received");
}

ZTEST(services_ftp_transfer, test_every_received_frame_is_released_exactly_once)
{
	const uint32_t total = (uint32_t)(2U * sizeof(payload[0]));

	transfer.total_size = total;
	scripted[0] = good_message(0U, 0U, total, 0U);
	scripted[1] = good_message((uint32_t)sizeof(payload[0]), 1U, total, 0U);
	scripted_count = 2U;

	zassert_ok(kfsw_ftp_transfer_open_sink(&transfer, TEST_SINK_PATH));
	zassert_ok(kfsw_ftp_transfer_receive(&transfer));
	zassert_ok(fs_close(&transfer.file));

	zassert_equal(__wrap_kfsw_ftp_link_release_fake.call_count,
		      __wrap_kfsw_ftp_link_receive_fake.call_count,
		      "a frame that was taken should be given back");
}

ZTEST(services_ftp_transfer, test_a_link_that_stops_answering_ends_the_receive)
{
	const uint32_t total = (uint32_t)(2U * sizeof(payload[0]));

	transfer.total_size = total;
	scripted[0] = good_message(0U, 0U, total, 0U);
	scripted_result[1] = -ETIMEDOUT;
	scripted_count = 2U;

	zassert_ok(kfsw_ftp_transfer_open_sink(&transfer, TEST_SINK_PATH));
	zassert_equal(kfsw_ftp_transfer_receive(&transfer), -ETIMEDOUT,
		      "the transport error should be the one reported");
	zassert_ok(fs_close(&transfer.file));

	zassert_equal(transfer.offset, (uint32_t)sizeof(payload[0]),
		      "only the chunk that arrived should count");
	zassert_equal(__wrap_kfsw_ftp_link_release_fake.call_count, 1U,
		      "a receive that failed owns no frame to release");
}

ZTEST(services_ftp_transfer, test_a_replayed_chunk_is_refused)
{
	const uint32_t total = (uint32_t)(2U * sizeof(payload[0]));

	transfer.total_size = total;
	scripted[0] = good_message(0U, 0U, total, 0U);
	/* The same offset a second time, which would rewrite bytes already
	 * counted into the checksum. */
	scripted[1] = good_message(0U, 1U, total, 0U);
	scripted_count = 2U;

	zassert_ok(kfsw_ftp_transfer_open_sink(&transfer, TEST_SINK_PATH));
	zassert_equal(kfsw_ftp_transfer_receive(&transfer), -EBADMSG,
		      "a chunk that repeats an offset should be refused");
	zassert_ok(fs_close(&transfer.file));

	zassert_equal(transfer.offset, (uint32_t)sizeof(payload[0]),
		      "the refused chunk should not advance the transfer");
}

ZTEST(services_ftp_transfer, test_a_chunk_from_another_request_is_refused)
{
	const uint32_t total = (uint32_t)sizeof(payload[0]);

	transfer.total_size = total;
	scripted[0] = good_message(0U, 0U, total, 0U);
	scripted[0].request_id = TEST_REQUEST_ID + 1U;
	scripted_count = 1U;

	zassert_ok(kfsw_ftp_transfer_open_sink(&transfer, TEST_SINK_PATH));
	zassert_equal(kfsw_ftp_transfer_receive(&transfer), -EBADMSG,
		      "a chunk carrying another request id should be refused");
	zassert_ok(fs_close(&transfer.file));
	zassert_equal(transfer.offset, 0U, "nothing should be written");
}

ZTEST(services_ftp_transfer, test_a_chunk_carrying_the_wrong_opcode_is_refused)
{
	const uint32_t total = (uint32_t)sizeof(payload[0]);

	transfer.total_size = total;
	scripted[0] = good_message(0U, 0U, total, 0U);
	scripted[0].opcode = KFSW_FTP_OP_GET_DATA;
	scripted_count = 1U;

	zassert_ok(kfsw_ftp_transfer_open_sink(&transfer, TEST_SINK_PATH));
	zassert_equal(kfsw_ftp_transfer_receive(&transfer), -EBADMSG,
		      "the receive direction should not accept the other direction's data");
	zassert_ok(fs_close(&transfer.file));
}

ZTEST(services_ftp_transfer, test_a_chunk_running_past_the_agreed_size_is_refused)
{
	const uint32_t total = (uint32_t)sizeof(payload[0]);

	transfer.total_size = total;
	/* Agreed on one chunk, offered two chunks' worth in one message. */
	scripted[0] = good_message(0U, 0U, total, 0U);
	scripted[0].data_size = (uint16_t)(2U * sizeof(payload[0]));
	scripted_count = 1U;

	zassert_ok(kfsw_ftp_transfer_open_sink(&transfer, TEST_SINK_PATH));
	zassert_equal(kfsw_ftp_transfer_receive(&transfer), -EBADMSG,
		      "a chunk cannot be longer than what is left to receive");
	zassert_ok(fs_close(&transfer.file));
	zassert_equal(transfer.offset, 0U, "nothing should be written");
}

ZTEST(services_ftp_transfer, test_a_send_that_fails_does_not_advance_the_offset)
{
	static const uint8_t contents[16] = {0};
	struct fs_file_t file;

	fs_file_t_init(&file);
	zassert_ok(fs_open(&file, TEST_SOURCE_PATH, FS_O_CREATE | FS_O_WRITE));
	zassert_equal(fs_write(&file, contents, sizeof(contents)), (ssize_t)sizeof(contents));
	zassert_ok(fs_close(&file));

	transfer.total_size = (uint32_t)sizeof(contents);
	__wrap_kfsw_ftp_link_send_fake.return_val = -ENOTCONN;

	zassert_ok(kfsw_ftp_transfer_open_source(&transfer, TEST_SOURCE_PATH));
	zassert_equal(kfsw_ftp_transfer_send(&transfer), -ENOTCONN,
		      "the transport error should be the one reported");

	zassert_equal(__wrap_kfsw_ftp_link_send_fake.call_count, 1U,
		      "a link that refused once should not be asked again");
	zassert_equal(transfer.offset, 0U, "a chunk that was not sent should not be counted");
}

ZTEST(services_ftp_transfer, test_the_reserved_firmware_path_is_matched_by_length)
{
	static const char reserved[] = CONFIG_KFSW_FTP_FIRMWARE_PATH;
	const uint16_t size = (uint16_t)(sizeof(reserved) - 1U);
	char leading[sizeof(reserved) + 1];
	char longer[sizeof(reserved) + 8];

	leading[0] = '/';
	memcpy(&leading[1], reserved, size);
	memcpy(longer, reserved, size);
	memset(&longer[size], 'x', sizeof(longer) - size);

	zassert_true(kfsw_ftp_path_is_firmware((const uint8_t *)reserved, size));
	zassert_true(kfsw_ftp_path_is_firmware((const uint8_t *)leading, (uint16_t)(size + 1U)),
		     "a leading separator names the same file");
	zassert_false(kfsw_ftp_path_is_firmware((const uint8_t *)longer, (uint16_t)sizeof(longer)),
		      "a longer path that starts the same is a different file");
	zassert_false(kfsw_ftp_path_is_firmware((const uint8_t *)reserved, 0U));
	zassert_false(kfsw_ftp_path_is_firmware(NULL, size));
}
