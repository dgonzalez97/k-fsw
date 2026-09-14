#include <string.h>

#include <zephyr/devicetree.h>
#include <zephyr/fs/fs.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>

#include <kfsw/platform/storage.h>
#include <kfsw/services/ftp.h>

#include "ftp_internal.h"
#include "ftp_link.h"

#define STORAGE_PARTITION_NODE DT_CHOSEN(kfsw_storage_partition)
#define STORAGE_PARTITION_ID DT_FIXED_PARTITION_ID(STORAGE_PARTITION_NODE)
#define MAX_RESPONSES 16
#define TEST_PAYLOAD 64U

/*
 * File transfer server request handling over a fake link: missing files, paths
 * outside the sandbox, writes to the read-only root and unknown opcodes. Every
 * refusal must reach the peer as a status.
 */

/* The request the fake link will hand over, and the responses it collects. */
static struct {
	struct kfsw_ftp_message request;
	uint8_t path[KFSW_FTP_MAX_PATH_SIZE + 1U];
	uint8_t data[TEST_PAYLOAD];
	int receive_result;
	bool receive_consumed;

	unsigned int sends;
	unsigned int releases;
	struct kfsw_ftp_message response[MAX_RESPONSES];
} link_fake;

int __wrap_kfsw_ftp_link_receive(struct kfsw_ftp_link *link, struct kfsw_ftp_link_frame *frame)
{
	ARG_UNUSED(link);
	if (link_fake.receive_result != 0) {
		return link_fake.receive_result;
	}
	if (link_fake.receive_consumed) {
		return -ETIMEDOUT;
	}
	link_fake.receive_consumed = true;
	frame->message = link_fake.request;
	frame->buffer = &link_fake;
	return 0;
}

void __wrap_kfsw_ftp_link_release(struct kfsw_ftp_link_frame *frame)
{
	link_fake.releases++;
	frame->buffer = NULL;
}

int __wrap_kfsw_ftp_link_send(struct kfsw_ftp_link *link, const struct kfsw_ftp_message *message)
{
	ARG_UNUSED(link);
	if (link_fake.sends < MAX_RESPONSES) {
		link_fake.response[link_fake.sends] = *message;
	}
	link_fake.sends++;
	return 0;
}

uint16_t __wrap_kfsw_ftp_link_max_payload(void)
{
	return TEST_PAYLOAD;
}

static void erase_storage_partition(void)
{
	const struct flash_area *area;
	int result;

	zassert_ok(flash_area_open(STORAGE_PARTITION_ID, &area),
		   "the storage partition would not open");
	result = flash_area_flatten(area, 0, area->fa_size);
	flash_area_close(area);
	zassert_ok(result, "the storage partition would not erase");
}

/* Builds the request the fake link will deliver on the next serve. */
static void given_request(uint8_t opcode, const char *path)
{
	memset(&link_fake, 0, sizeof(link_fake));
	link_fake.request.opcode = opcode;
	link_fake.request.request_id = 0x1234U;
	if (path != NULL) {
		size_t length = strlen(path);

		zassert_true(length <= KFSW_FTP_MAX_PATH_SIZE, "the test path is too long");
		memcpy(link_fake.path, path, length);
		link_fake.request.path = link_fake.path;
		link_fake.request.path_size = (uint16_t)length;
	}
}

static void serve(void)
{
	struct kfsw_ftp_link link = {.connection = NULL};

	kfsw_ftp_serve_connection(&link);
}

static void write_file(const char *path, const char *contents)
{
	struct fs_file_t file;
	size_t length = strlen(contents);

	(void)fs_mkdir(KFSW_FTP_STORAGE_ROOT);
	fs_file_t_init(&file);
	zassert_ok(fs_open(&file, path, FS_O_CREATE | FS_O_WRITE | FS_O_TRUNC),
		   "the fixture file would not open");
	zassert_equal(fs_write(&file, contents, length), (ssize_t)length,
		      "the fixture file was not written");
	zassert_ok(fs_close(&file), "the fixture file would not close");
}

static void *ftp_setup(void)
{
	erase_storage_partition();
	zassert_ok(kfsw_storage_init(), "storage did not start");
	zassert_ok(kfsw_storage_mount(), "storage did not mount");
	zassert_ok(kfsw_ftp_init(), "the file transfer service did not start");
	return NULL;
}

static void ftp_before(void *fixture)
{
	ARG_UNUSED(fixture);
	memset(&link_fake, 0, sizeof(link_fake));
}

ZTEST_SUITE(kfsw_ftp_server, NULL, ftp_setup, ftp_before, NULL, NULL);

/*
 * A request that can't be delivered still gets an answer.
 */
ZTEST(kfsw_ftp_server, test_a_request_that_never_arrived_is_still_answered)
{
	memset(&link_fake, 0, sizeof(link_fake));
	link_fake.receive_result = -EIO;

	serve();

	zassert_equal(link_fake.sends, 1U, "a failed receive produced no answer");
	zassert_equal(link_fake.response[0].status, KFSW_FTP_STATUS_INVALID_REQUEST,
		      "a failed receive was not reported as a bad request");
}

/* A transport error gets a different status, telling the peer not to resend. */
ZTEST(kfsw_ftp_server, test_an_unsupported_frame_says_unsupported)
{
	memset(&link_fake, 0, sizeof(link_fake));
	link_fake.receive_result = -EPROTONOSUPPORT;

	serve();

	zassert_equal(link_fake.sends, 1U, "an unsupported frame produced no answer");
	zassert_equal(link_fake.response[0].status, KFSW_FTP_STATUS_UNSUPPORTED,
		      "an unsupported frame was reported as merely invalid");
}

/* An opcode from a version this build does not know is refused, not ignored. */
ZTEST(kfsw_ftp_server, test_an_unknown_opcode_is_refused)
{
	given_request(200U, "/thing");

	serve();

	zassert_equal(link_fake.sends, 1U, "an unknown opcode produced no answer");
	zassert_equal(link_fake.response[0].status, KFSW_FTP_STATUS_INVALID_REQUEST,
		      "an unknown opcode was not refused");
	zassert_equal(link_fake.releases, 1U, "the request frame was not released");
}

/*
 * Paths outside the root are refused.
 */
ZTEST(kfsw_ftp_server, test_a_path_climbing_out_of_the_sandbox_is_refused)
{
	given_request(KFSW_FTP_OP_STAT_REQUEST, "/../../etc/passwd");

	serve();

	zassert_equal(link_fake.sends, 1U, "an escaping path produced no answer");
	zassert_equal(link_fake.response[0].status, KFSW_FTP_STATUS_INVALID_PATH,
		      "a path climbing out of the sandbox was not refused");
}

ZTEST(kfsw_ftp_server, test_a_request_with_no_path_is_refused)
{
	given_request(KFSW_FTP_OP_STAT_REQUEST, NULL);

	serve();

	zassert_equal(link_fake.sends, 1U, "a request with no path produced no answer");
	zassert_equal(link_fake.response[0].status, KFSW_FTP_STATUS_INVALID_PATH,
		      "a request naming nothing was accepted");
}

/*
 * Writes to the read-only root are refused before a file is opened.
 */
ZTEST(kfsw_ftp_server, test_writing_into_the_read_only_root_is_refused)
{
	given_request(KFSW_FTP_OP_MKDIR_REQUEST, "/hk/mine");

	serve();

	zassert_equal(link_fake.sends, 1U, "a write into the read-only root produced no answer");
	zassert_equal(link_fake.response[0].opcode, KFSW_FTP_OP_MKDIR_RESPONSE,
		      "the refusal did not answer the request it refused");
	zassert_not_equal(link_fake.response[0].status, KFSW_FTP_STATUS_OK,
			  "a directory was created inside the read-only root");
}

ZTEST(kfsw_ftp_server, test_uploading_into_the_read_only_root_is_refused)
{
	given_request(KFSW_FTP_OP_PUT_REQUEST, "/hk/report0.bin");
	link_fake.request.total_size = 4U;

	serve();

	zassert_equal(link_fake.sends, 1U, "an upload into the read-only root produced no answer");
	zassert_not_equal(link_fake.response[0].status, KFSW_FTP_STATUS_OK,
			  "a file was accepted into the read-only root");
}

/* Reading it is allowed. */
ZTEST(kfsw_ftp_server, test_the_read_only_root_can_still_be_read)
{
	(void)fs_mkdir(KFSW_FTP_READONLY_ROOT);
	write_file(KFSW_FTP_READONLY_ROOT "/sample.bin", "0123456789");

	given_request(KFSW_FTP_OP_STAT_REQUEST, "/hk/sample.bin");
	serve();

	zassert_equal(link_fake.sends, 1U, "a read of the read-only root produced no answer");
	zassert_equal(link_fake.response[0].status, KFSW_FTP_STATUS_OK,
		      "the read-only root could not be read");
	zassert_equal(link_fake.response[0].total_size, 10U, "the size reported is not the size");
}

ZTEST(kfsw_ftp_server, test_a_file_that_is_not_there_says_so)
{
	given_request(KFSW_FTP_OP_STAT_REQUEST, "/absent.bin");

	serve();

	zassert_equal(link_fake.sends, 1U, "a missing file produced no answer");
	zassert_equal(link_fake.response[0].status, KFSW_FTP_STATUS_NOT_FOUND,
		      "a missing file was not reported missing");
}

ZTEST(kfsw_ftp_server, test_a_directory_is_made_and_then_already_exists)
{
	given_request(KFSW_FTP_OP_MKDIR_REQUEST, "/made");
	serve();
	zassert_equal(link_fake.response[0].status, KFSW_FTP_STATUS_OK,
		      "the directory was not created");

	given_request(KFSW_FTP_OP_MKDIR_REQUEST, "/made");
	serve();
	zassert_equal(link_fake.response[0].status, KFSW_FTP_STATUS_ALREADY_EXISTS,
		      "making a directory twice was not reported as already existing");
}

ZTEST(kfsw_ftp_server, test_a_file_is_reported_with_its_size)
{
	write_file(KFSW_FTP_STORAGE_ROOT "/sized.bin", "abcdefghij");

	given_request(KFSW_FTP_OP_STAT_REQUEST, "/sized.bin");
	serve();

	zassert_equal(link_fake.response[0].status, KFSW_FTP_STATUS_OK, "the file was not found");
	zassert_equal(link_fake.response[0].total_size, 10U, "the size reported is not the size");
}

/* A listing ends with an explicit end marker. */
ZTEST(kfsw_ftp_server, test_a_listing_says_where_it_ends)
{
	bool saw_end = false;

	write_file(KFSW_FTP_STORAGE_ROOT "/listed.bin", "x");

	given_request(KFSW_FTP_OP_LIST_REQUEST, "/");
	serve();

	zassert_true(link_fake.sends >= 1U, "a listing produced nothing at all");
	for (unsigned int i = 0U; (i < link_fake.sends) && (i < MAX_RESPONSES); i++) {
		if (link_fake.response[i].opcode == KFSW_FTP_OP_LIST_END) {
			saw_end = true;
		}
	}
	zassert_true(saw_end, "a listing never said it had ended");
}

ZTEST(kfsw_ftp_server, test_listing_something_that_is_not_there_says_so)
{
	given_request(KFSW_FTP_OP_LIST_REQUEST, "/nowhere");
	serve();

	zassert_true(link_fake.sends >= 1U, "listing a missing directory produced no answer");
	zassert_not_equal(link_fake.response[0].status, KFSW_FTP_STATUS_OK,
			  "a directory that does not exist was listed");
}

/* A download answers with what it is about to send before it sends it. */
ZTEST(kfsw_ftp_server, test_a_download_announces_the_file_first)
{
	write_file(KFSW_FTP_STORAGE_ROOT "/download.bin", "hello ftp");

	given_request(KFSW_FTP_OP_GET_REQUEST, "/download.bin");
	serve();

	zassert_true(link_fake.sends >= 1U, "a download produced nothing");
	zassert_equal(link_fake.response[0].opcode, KFSW_FTP_OP_GET_INFO,
		      "a download did not announce the file before sending it");
	zassert_equal(link_fake.response[0].total_size, 9U,
		      "the download announced the wrong size");
}

ZTEST(kfsw_ftp_server, test_downloading_what_is_not_there_says_so)
{
	given_request(KFSW_FTP_OP_GET_REQUEST, "/missing.bin");
	serve();

	zassert_true(link_fake.sends >= 1U, "a download of a missing file produced no answer");
	zassert_equal(link_fake.response[0].status, KFSW_FTP_STATUS_NOT_FOUND,
		      "downloading a missing file was not reported missing");
}

/* The frame is released once, however the request ends. */
ZTEST(kfsw_ftp_server, test_a_served_request_is_given_back_once)
{
	write_file(KFSW_FTP_STORAGE_ROOT "/released.bin", "z");

	given_request(KFSW_FTP_OP_STAT_REQUEST, "/released.bin");
	serve();

	zassert_equal(link_fake.releases, 1U, "the request frame was released %u times",
		      link_fake.releases);
}

ZTEST(kfsw_ftp_server, test_a_refused_request_is_given_back_too)
{
	given_request(KFSW_FTP_OP_MKDIR_REQUEST, "/hk/nope");
	serve();

	zassert_equal(link_fake.releases, 1U,
		      "a request refused before it was served leaked its frame");
}
