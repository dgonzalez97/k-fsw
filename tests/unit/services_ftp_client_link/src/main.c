#include <string.h>

#include <zephyr/devicetree.h>
#include <zephyr/fs/fs.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/crc.h>
#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>

#include <kfsw/comms/csp.h>
#include <kfsw/platform/storage.h>
#include <kfsw/services/ftp.h>

#include "ftp_internal.h"
#include "ftp_link.h"

#define STORAGE_PARTITION_NODE DT_CHOSEN(kfsw_storage_partition)
#define STORAGE_PARTITION_ID DT_FIXED_PARTITION_ID(STORAGE_PARTITION_NODE)
#define LOCAL_NODE 1U
#define PEER_NODE 2U
#define MAX_NODE KFSW_CSP_BROADCAST_ADDRESS
#define TEST_PAYLOAD 64U
#define MAX_SCRIPTED 8

/*
 * Client behaviour with bad replies: a reply to an old request, a reply of the
 * wrong kind, an error status, and a peer that never answers.
 */

static struct {
	/* What the client sent. */
	unsigned int sends;
	struct kfsw_ftp_message sent[MAX_SCRIPTED];

	/* What the peer will answer, in order. */
	struct kfsw_ftp_message scripted[MAX_SCRIPTED];
	int scripted_result[MAX_SCRIPTED];
	unsigned int scripted_count;
	unsigned int received;

	unsigned int releases;
	unsigned int connects;
	unsigned int closes;

	int connect_result;
	int send_result;
	bool ready;

	/* 0 makes the peer echo the id it was asked with, as a real one does.
	 * A case that wants them to disagree sets this instead.
	 */
	uint32_t force_request_id;
} link_fake;

int __wrap_kfsw_ftp_link_connect(struct kfsw_ftp_link *link, uint16_t node)
{
	ARG_UNUSED(node);
	link_fake.connects++;
	if (link_fake.connect_result != 0) {
		return link_fake.connect_result;
	}
	link->connection = &link_fake;
	return 0;
}

void __wrap_kfsw_ftp_link_close(struct kfsw_ftp_link *link)
{
	link_fake.closes++;
	link->connection = NULL;
}

bool __wrap_kfsw_ftp_link_is_ready(void)
{
	return link_fake.ready;
}

uint16_t __wrap_kfsw_ftp_link_local_node(void)
{
	return LOCAL_NODE;
}

uint16_t __wrap_kfsw_ftp_link_max_node(void)
{
	return MAX_NODE;
}

uint16_t __wrap_kfsw_ftp_link_max_payload(void)
{
	return TEST_PAYLOAD;
}

int __wrap_kfsw_ftp_link_send(struct kfsw_ftp_link *link, const struct kfsw_ftp_message *message)
{
	ARG_UNUSED(link);
	if (link_fake.sends < MAX_SCRIPTED) {
		link_fake.sent[link_fake.sends] = *message;
	}
	link_fake.sends++;
	return link_fake.send_result;
}

int __wrap_kfsw_ftp_link_receive(struct kfsw_ftp_link *link, struct kfsw_ftp_link_frame *frame)
{
	ARG_UNUSED(link);
	if (link_fake.received >= link_fake.scripted_count) {
		/* A lost reply. */
		return -ETIMEDOUT;
	}
	if (link_fake.scripted_result[link_fake.received] != 0) {
		return link_fake.scripted_result[link_fake.received++];
	}
	frame->message = link_fake.scripted[link_fake.received++];
	/* Echo the client's request ID unless the test overrides it.
	 */
	frame->message.request_id = (link_fake.force_request_id != 0U)
					    ? link_fake.force_request_id
					    : link_fake.sent[link_fake.sends - 1U].request_id;
	frame->buffer = &link_fake;
	return 0;
}

/*
 * The real release does nothing for an empty frame, so a second release is
 * harmless. The fake does the same.
 */
void __wrap_kfsw_ftp_link_release(struct kfsw_ftp_link_frame *frame)
{
	if ((frame == NULL) || (frame->buffer == NULL)) {
		return;
	}
	link_fake.releases++;
	frame->buffer = NULL;
}

/* Queue one reply, with a field the plain helper leaves zero. */
static void peer_will_reply_with_offset(uint8_t opcode, uint8_t status, uint32_t offset,
					uint32_t total_size)
{
	struct kfsw_ftp_message *reply = &link_fake.scripted[link_fake.scripted_count];

	memset(reply, 0, sizeof(*reply));
	reply->opcode = opcode;
	reply->status = status;
	reply->offset = offset;
	reply->total_size = total_size;
	link_fake.scripted_result[link_fake.scripted_count] = 0;
	link_fake.scripted_count++;
}

/* Queue one reply. The request ID is copied from the client's request. */
static void peer_will_reply(uint8_t opcode, uint8_t status)
{
	struct kfsw_ftp_message *reply = &link_fake.scripted[link_fake.scripted_count];

	memset(reply, 0, sizeof(*reply));
	reply->opcode = opcode;
	reply->status = status;
	link_fake.scripted_result[link_fake.scripted_count] = 0;
	link_fake.scripted_count++;
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

static void *client_setup(void)
{
	erase_storage_partition();
	zassert_ok(kfsw_storage_init(), "storage did not start");
	zassert_ok(kfsw_storage_mount(), "storage did not mount");
	zassert_ok(kfsw_ftp_init(), "the file transfer service did not start");
	return NULL;
}

static void client_before(void *fixture)
{
	ARG_UNUSED(fixture);
	memset(&link_fake, 0, sizeof(link_fake));
	link_fake.ready = true;
}

ZTEST_SUITE(kfsw_ftp_client_link, NULL, client_setup, client_before, NULL, NULL);

/* Nothing should reach the link before the request is known to be sane. */
ZTEST(kfsw_ftp_client_link, test_an_address_that_cannot_exist_is_refused)
{
	zassert_equal(kfsw_ftp_mkdir(MAX_NODE + 1U, "/somewhere"), -EINVAL,
		      "a node past the address space was accepted");
	zassert_equal(link_fake.connects, 0U, "the client dialled an impossible address");
}

ZTEST(kfsw_ftp_client_link, test_a_link_that_is_not_up_is_refused)
{
	link_fake.ready = false;

	zassert_equal(kfsw_ftp_mkdir(PEER_NODE, "/somewhere"), -EACCES,
		      "a request went out over a link that is not up");
	zassert_equal(link_fake.connects, 0U, "the client dialled over a link that is not up");
}

/*
 * Paths outside the sandbox are refused before anything is sent.
 */
ZTEST(kfsw_ftp_client_link, test_a_path_climbing_out_is_refused_before_it_is_sent)
{
	zassert_not_equal(kfsw_ftp_mkdir(PEER_NODE, "/../../etc/passwd"), 0,
			  "a path climbing out of the sandbox was sent");
	zassert_equal(link_fake.sends, 0U, "the escaping path reached the link");
	zassert_equal(link_fake.connects, 0U, "the client connected to send a refused path");
}

ZTEST(kfsw_ftp_client_link, test_no_path_is_refused)
{
	zassert_equal(kfsw_ftp_mkdir(PEER_NODE, NULL), -EINVAL, "a NULL path was accepted");
	zassert_equal(link_fake.sends, 0U, "a NULL path reached the link");
}

ZTEST(kfsw_ftp_client_link, test_a_connection_that_will_not_open_is_reported)
{
	link_fake.connect_result = -ENETUNREACH;

	zassert_equal(kfsw_ftp_mkdir(PEER_NODE, "/somewhere"), -ENETUNREACH,
		      "a connection failure was not reported");
	zassert_equal(link_fake.sends, 0U, "the client sent over a connection it never opened");
}

/* Check a valid request before the rejection cases. */
ZTEST(kfsw_ftp_client_link, test_a_directory_request_is_sent_and_answered)
{
	peer_will_reply(KFSW_FTP_OP_MKDIR_RESPONSE, KFSW_FTP_STATUS_OK);

	zassert_ok(kfsw_ftp_mkdir(PEER_NODE, "/made"), "a good exchange was refused");
	zassert_equal(link_fake.sent[0].opcode, KFSW_FTP_OP_MKDIR_REQUEST,
		      "the client sent the wrong request");
	zassert_true(link_fake.closes > 0U, "the client left the connection open");
}

/*
 * A reply to an earlier request must not be accepted for the current one.
 */
ZTEST(kfsw_ftp_client_link, test_a_reply_to_a_different_request_is_refused)
{
	peer_will_reply(KFSW_FTP_OP_MKDIR_RESPONSE, KFSW_FTP_STATUS_OK);
	link_fake.force_request_id = 0xDEADBEEFU;

	zassert_equal(kfsw_ftp_mkdir(PEER_NODE, "/made"), -EBADMSG,
		      "a reply to another request was accepted");
	zassert_equal(link_fake.releases, 1U, "the stale reply was not released");
}

ZTEST(kfsw_ftp_client_link, test_a_reply_of_the_wrong_kind_is_refused)
{
	peer_will_reply(KFSW_FTP_OP_GET_RESULT, KFSW_FTP_STATUS_OK);

	zassert_equal(kfsw_ftp_mkdir(PEER_NODE, "/made"), -EBADMSG,
		      "an answer to a different question was accepted");
}

/* A refusal from the peer arrives as a status and has to become an errno. */
ZTEST(kfsw_ftp_client_link, test_a_refusal_from_the_peer_becomes_an_errno)
{
	peer_will_reply(KFSW_FTP_OP_MKDIR_RESPONSE, KFSW_FTP_STATUS_ALREADY_EXISTS);

	zassert_equal(kfsw_ftp_mkdir(PEER_NODE, "/made"), -EEXIST,
		      "a peer's refusal did not reach the caller as an errno");
	zassert_equal(link_fake.releases, 1U, "the refusal was not released");
}

/* A peer that accepts the connection and then says nothing. */
ZTEST(kfsw_ftp_client_link, test_a_peer_that_says_nothing_times_out)
{
	zassert_equal(kfsw_ftp_mkdir(PEER_NODE, "/made"), -ETIMEDOUT,
		      "silence was not reported as a timeout");
	zassert_true(link_fake.closes > 0U, "a timed-out exchange left the connection open");
}

ZTEST(kfsw_ftp_client_link, test_a_send_that_fails_is_reported)
{
	link_fake.send_result = -EIO;

	zassert_equal(kfsw_ftp_mkdir(PEER_NODE, "/made"), -EIO, "a send failure was not reported");
	zassert_true(link_fake.closes > 0U, "a failed send left the connection open");
}

/* stat carries a body, so its answer is checked for sense as well as status. */
ZTEST(kfsw_ftp_client_link, test_a_stat_answer_that_makes_no_sense_is_refused)
{
	struct kfsw_ftp_stat info;

	peer_will_reply(KFSW_FTP_OP_STAT_RESPONSE, KFSW_FTP_STATUS_OK);
	link_fake.scripted[0].flags = 0x7FU;

	zassert_equal(kfsw_ftp_stat(PEER_NODE, "/thing", &info), -EBADMSG,
		      "a stat answer naming no known entry type was accepted");
}

ZTEST(kfsw_ftp_client_link, test_a_stat_answer_is_reported_to_the_caller)
{
	struct kfsw_ftp_stat info = {0};

	peer_will_reply(KFSW_FTP_OP_STAT_RESPONSE, KFSW_FTP_STATUS_OK);
	link_fake.scripted[0].flags = KFSW_FTP_ENTRY_FILE;
	link_fake.scripted[0].total_size = 4321U;
	link_fake.scripted[0].crc32 = 0xA5A5A5A5U;

	zassert_ok(kfsw_ftp_stat(PEER_NODE, "/thing", &info), "a good stat was refused");
	zassert_equal(info.size, 4321U, "the size reported is not the size answered");
	zassert_equal(info.crc32, 0xA5A5A5A5U, "the checksum reported is not the one answered");
	zassert_equal(info.type, KFSW_FTP_ENTRY_FILE, "the entry type changed");
}

ZTEST(kfsw_ftp_client_link, test_a_stat_with_nowhere_to_put_the_answer_is_refused)
{
	zassert_equal(kfsw_ftp_stat(PEER_NODE, "/thing", NULL), -EINVAL,
		      "a stat with no destination was accepted");
	zassert_equal(link_fake.sends, 0U, "the client asked for a stat it could not store");
}

/* The connection is released once per open, whatever the outcome. */
ZTEST(kfsw_ftp_client_link, test_every_connection_is_closed)
{
	peer_will_reply(KFSW_FTP_OP_MKDIR_RESPONSE, KFSW_FTP_STATUS_OK);
	link_fake.force_request_id = 0xDEADBEEFU;

	(void)kfsw_ftp_mkdir(PEER_NODE, "/one");
	(void)kfsw_ftp_mkdir(PEER_NODE, "/two");
	(void)kfsw_ftp_stat(PEER_NODE, "/three", NULL);

	zassert_equal(link_fake.closes, link_fake.connects,
		      "%u connections were opened and %u closed", link_fake.connects,
		      link_fake.closes);
}

/* ------------------------------------------------------- transfers */

/*
 * Upload and download failures: a refusal before the first byte, a peer that
 * goes quiet, and a byte count that doesn't match.
 */

#define UPLOAD_LOCAL "/upload.bin"
#define UPLOAD_REMOTE "/remote.bin"

static void write_local_file(const char *virtual_path, size_t size)
{
	char full[KFSW_FTP_FULL_PATH_SIZE];
	struct fs_file_t file;
	uint8_t byte = 0xA5U;

	(void)snprintf(full, sizeof(full), "%s%s", KFSW_FTP_STORAGE_ROOT, virtual_path);
	(void)fs_mkdir(KFSW_FTP_STORAGE_ROOT);
	fs_file_t_init(&file);
	zassert_ok(fs_open(&file, full, FS_O_CREATE | FS_O_WRITE | FS_O_TRUNC),
		   "the upload fixture would not open");
	for (size_t written = 0U; written < size; written++) {
		zassert_equal(fs_write(&file, &byte, 1), 1, "the upload fixture was not written");
	}
	zassert_ok(fs_close(&file), "the upload fixture would not close");
}

/*
 * An empty file has nothing to send and still has to be created, so a ready
 * reply whose offset equals the size is correct rather than nonsense.
 */
ZTEST(kfsw_ftp_client_link, test_an_empty_file_is_uploaded)
{
	struct kfsw_ftp_transfer_result result = {0};

	write_local_file("/empty.bin", 0U);
	peer_will_reply_with_offset(KFSW_FTP_OP_PUT_READY, KFSW_FTP_STATUS_OK, 0U, 0U);
	peer_will_reply_with_offset(KFSW_FTP_OP_PUT_RESULT, KFSW_FTP_STATUS_OK, 0U, 0U);

	zassert_ok(kfsw_ftp_put(PEER_NODE, "/empty.bin", "/empty.bin", &result),
		   "an empty upload was refused");
	zassert_equal(result.bytes, 0U);
	zassert_equal(link_fake.sent[0].flags, KFSW_FTP_FLAG_RESUME,
		      "the client should always offer to continue");
}

/* A start past the end would leave the transfer unable to finish. */
ZTEST(kfsw_ftp_client_link, test_a_start_offset_past_the_end_is_refused)
{
	struct kfsw_ftp_transfer_result result = {0};

	write_local_file("/short.bin", 8U);
	peer_will_reply_with_offset(KFSW_FTP_OP_PUT_READY, KFSW_FTP_STATUS_OK, 9U, 8U);

	zassert_equal(kfsw_ftp_put(PEER_NODE, "/short.bin", "/short.bin", &result), -EBADMSG,
		      "an impossible start offset was accepted");
}

ZTEST(kfsw_ftp_client_link, test_an_upload_with_nowhere_to_report_is_refused)
{
	zassert_equal(kfsw_ftp_put(PEER_NODE, UPLOAD_LOCAL, UPLOAD_REMOTE, NULL), -EINVAL,
		      "an upload with no result destination was accepted");
	zassert_equal(link_fake.connects, 0U, "the client dialled for a refused upload");
}

/* A node cannot upload to itself: there is no link to carry it. */
ZTEST(kfsw_ftp_client_link, test_an_upload_to_this_node_is_refused)
{
	struct kfsw_ftp_transfer_result outcome;

	zassert_equal(kfsw_ftp_put(LOCAL_NODE, UPLOAD_LOCAL, UPLOAD_REMOTE, &outcome), -ENOTSUP,
		      "a node accepted an upload to itself");
}

ZTEST(kfsw_ftp_client_link, test_an_upload_of_a_file_that_is_not_there_is_refused)
{
	struct kfsw_ftp_transfer_result outcome;

	zassert_not_equal(kfsw_ftp_put(PEER_NODE, "/absent.bin", UPLOAD_REMOTE, &outcome), 0,
			  "a file that does not exist was uploaded");
	zassert_equal(link_fake.connects, 0U, "the client dialled before it knew the file existed");
}

/* The peer refuses before the first byte: PUT_RESULT instead of PUT_READY. */
ZTEST(kfsw_ftp_client_link, test_a_peer_that_refuses_an_upload_is_believed)
{
	struct kfsw_ftp_transfer_result outcome;

	write_local_file(UPLOAD_LOCAL, 32U);
	peer_will_reply(KFSW_FTP_OP_PUT_RESULT, KFSW_FTP_STATUS_NO_SPACE);

	zassert_equal(kfsw_ftp_put(PEER_NODE, UPLOAD_LOCAL, UPLOAD_REMOTE, &outcome), -ENOSPC,
		      "a peer's refusal did not reach the caller");
	zassert_equal(outcome.bytes, 0U, "a refused upload reported bytes transferred");
	zassert_true(link_fake.closes > 0U, "a refused upload left the connection open");
}

/* PUT_READY with the wrong request ID. */
ZTEST(kfsw_ftp_client_link, test_an_upload_answered_with_the_wrong_opcode_is_refused)
{
	struct kfsw_ftp_transfer_result outcome;

	write_local_file(UPLOAD_LOCAL, 32U);
	peer_will_reply(KFSW_FTP_OP_GET_INFO, KFSW_FTP_STATUS_OK);

	zassert_equal(kfsw_ftp_put(PEER_NODE, UPLOAD_LOCAL, UPLOAD_REMOTE, &outcome), -EBADMSG,
		      "an upload accepted an answer of the wrong kind");
}

ZTEST(kfsw_ftp_client_link, test_an_upload_answered_for_another_request_is_refused)
{
	struct kfsw_ftp_transfer_result outcome;

	write_local_file(UPLOAD_LOCAL, 32U);
	peer_will_reply(KFSW_FTP_OP_PUT_READY, KFSW_FTP_STATUS_OK);
	link_fake.force_request_id = 0xDEADBEEFU;

	zassert_equal(kfsw_ftp_put(PEER_NODE, UPLOAD_LOCAL, UPLOAD_REMOTE, &outcome), -EBADMSG,
		      "an upload accepted a reply to another request");
}

/* A peer that goes quiet once the transfer has started. */
ZTEST(kfsw_ftp_client_link, test_an_upload_to_a_peer_that_goes_quiet_times_out)
{
	struct kfsw_ftp_transfer_result outcome;

	write_local_file(UPLOAD_LOCAL, 32U);
	peer_will_reply(KFSW_FTP_OP_PUT_READY, KFSW_FTP_STATUS_OK);

	/* Ready, then nothing: the result never arrives. */
	zassert_equal(kfsw_ftp_put(PEER_NODE, UPLOAD_LOCAL, UPLOAD_REMOTE, &outcome), -ETIMEDOUT,
		      "silence after the data was not reported as a timeout");
	zassert_true(link_fake.sends > 1U, "the client never sent the file");
}

/*
 * The peer reports what it committed; a mismatch must fail the transfer.
 */
ZTEST(kfsw_ftp_client_link, test_an_upload_the_peer_counted_differently_is_an_integrity_failure)
{
	struct kfsw_ftp_transfer_result outcome;

	write_local_file(UPLOAD_LOCAL, 32U);
	peer_will_reply(KFSW_FTP_OP_PUT_READY, KFSW_FTP_STATUS_OK);
	peer_will_reply(KFSW_FTP_OP_PUT_RESULT, KFSW_FTP_STATUS_OK);
	link_fake.scripted[1].total_size = 31U;
	link_fake.scripted[1].crc32 = 0U;

	zassert_equal(kfsw_ftp_put(PEER_NODE, UPLOAD_LOCAL, UPLOAD_REMOTE, &outcome), -EILSEQ,
		      "the peer committed a different file and the client agreed");
}

ZTEST(kfsw_ftp_client_link, test_a_download_with_nowhere_to_report_is_refused)
{
	zassert_equal(kfsw_ftp_get(PEER_NODE, UPLOAD_REMOTE, UPLOAD_LOCAL, NULL), -EINVAL,
		      "a download with no result destination was accepted");
}

ZTEST(kfsw_ftp_client_link, test_a_download_of_what_is_not_there_is_believed)
{
	struct kfsw_ftp_transfer_result outcome;

	peer_will_reply(KFSW_FTP_OP_GET_RESULT, KFSW_FTP_STATUS_NOT_FOUND);

	zassert_equal(kfsw_ftp_get(PEER_NODE, "/absent.bin", "/landing.bin", &outcome), -ENOENT,
		      "a peer saying the file is missing was not believed");
	zassert_equal(outcome.bytes, 0U, "a failed download reported bytes transferred");
}

ZTEST(kfsw_ftp_client_link, test_a_download_answered_with_the_wrong_opcode_is_refused)
{
	struct kfsw_ftp_transfer_result outcome;

	peer_will_reply(KFSW_FTP_OP_PUT_READY, KFSW_FTP_STATUS_OK);

	zassert_equal(kfsw_ftp_get(PEER_NODE, "/thing.bin", "/landing.bin", &outcome), -EBADMSG,
		      "a download accepted an answer of the wrong kind");
}

ZTEST(kfsw_ftp_client_link, test_a_download_from_a_peer_that_says_nothing_times_out)
{
	struct kfsw_ftp_transfer_result outcome;

	zassert_equal(kfsw_ftp_get(PEER_NODE, "/thing.bin", "/landing.bin", &outcome), -ETIMEDOUT,
		      "silence was not reported as a timeout");
	zassert_true(link_fake.closes > 0U, "a timed-out download left the connection open");
}

ZTEST(kfsw_ftp_client_link, test_a_download_to_a_path_climbing_out_is_refused)
{
	struct kfsw_ftp_transfer_result outcome;

	zassert_not_equal(kfsw_ftp_get(PEER_NODE, "/thing.bin", "/../escape.bin", &outcome), 0,
			  "a download landing outside the sandbox was accepted");
	zassert_equal(link_fake.sends, 0U, "the escaping download reached the link");
}

/* ------------------------------------------- a download that took two passes */

/*
 * The receiving node owns the partial, so these cases watch the local side of
 * an interrupted download: what is left on disk, what the next request claims,
 * and what happens when the sending node refuses that claim.
 */

#define DOWNLOAD_LOCAL "/landed.bin"
#define DOWNLOAD_FULL KFSW_FTP_STORAGE_ROOT DOWNLOAD_LOCAL
#define DOWNLOAD_SIZE 16U
#define DOWNLOAD_HALF 8U

static const uint8_t download_content[DOWNLOAD_SIZE] = {
	0x10U, 0x11U, 0x12U, 0x13U, 0x14U, 0x15U, 0x16U, 0x17U,
	0x20U, 0x21U, 0x22U, 0x23U, 0x24U, 0x25U, 0x26U, 0x27U,
};

/* Queue one chunk of a download. The data stays where the caller put it, as a
 * real frame's does until it is released. */
static void peer_will_send_data(uint32_t offset, const uint8_t *data, uint16_t size,
				uint32_t total_size, uint32_t crc32)
{
	struct kfsw_ftp_message *reply = &link_fake.scripted[link_fake.scripted_count];

	memset(reply, 0, sizeof(*reply));
	reply->opcode = KFSW_FTP_OP_GET_DATA;
	reply->offset = offset;
	reply->total_size = total_size;
	reply->crc32 = crc32;
	reply->data = data;
	reply->data_size = size;
	link_fake.scripted_result[link_fake.scripted_count] = 0;
	link_fake.scripted_count++;
}

static bool file_exists(const char *full_path)
{
	struct fs_dirent entry;

	return fs_stat(full_path, &entry) == 0;
}

static uint32_t file_size_of(const char *full_path)
{
	struct fs_dirent entry;

	zassert_ok(fs_stat(full_path, &entry), "%s is not there", full_path);
	return (uint32_t)entry.size;
}

static void no_download_in_progress(void)
{
	(void)fs_mkdir(KFSW_FTP_STORAGE_ROOT);
	(void)fs_unlink(DOWNLOAD_FULL);
	(void)fs_unlink(DOWNLOAD_FULL ".part");
	(void)fs_unlink(DOWNLOAD_FULL ".part.map");
}

/* The state an interrupted pass leaves behind, written directly. */
static void given_partial(uint32_t size, uint32_t whole_crc32)
{
	struct fs_file_t file;

	no_download_in_progress();
	fs_file_t_init(&file);
	zassert_ok(fs_open(&file, DOWNLOAD_FULL ".part", FS_O_CREATE | FS_O_WRITE),
		   "the partial fixture would not open");
	zassert_equal(fs_write(&file, download_content, size), (ssize_t)size,
		      "the partial fixture was not written");
	zassert_ok(fs_close(&file), "the partial fixture would not close");
	zassert_ok(kfsw_ftp_partial_note_write(DOWNLOAD_FULL, DOWNLOAD_SIZE, whole_crc32),
		   "the partial fixture has no note");
}

/*
 * A pass that ends mid-file keeps what arrived, and keeps the note that says
 * which file it belongs to. The destination must not appear until the whole
 * file is there.
 */
ZTEST(kfsw_ftp_client_link, test_an_interrupted_download_keeps_its_partial)
{
	const uint32_t whole_crc = crc32_ieee(download_content, DOWNLOAD_SIZE);
	struct kfsw_ftp_transfer_result outcome;
	uint32_t noted_size = 0U;
	uint32_t noted_crc = 0U;

	no_download_in_progress();
	peer_will_reply_with_offset(KFSW_FTP_OP_GET_INFO, KFSW_FTP_STATUS_OK, 0U, DOWNLOAD_SIZE);
	link_fake.scripted[0].crc32 = whole_crc;
	peer_will_send_data(0U, download_content, DOWNLOAD_HALF, DOWNLOAD_SIZE, whole_crc);

	zassert_equal(kfsw_ftp_get(PEER_NODE, "/remote.bin", DOWNLOAD_LOCAL, &outcome), -ETIMEDOUT,
		      "a download cut off mid-file was not reported");
	zassert_false(file_exists(DOWNLOAD_FULL), "an unfinished download appeared as the file");
	zassert_equal(file_size_of(DOWNLOAD_FULL ".part"), DOWNLOAD_HALF,
		      "the bytes that did arrive were not kept");
	zassert_ok(kfsw_ftp_partial_note_read(DOWNLOAD_FULL, &noted_size, &noted_crc),
		   "the partial was left without a note");
	zassert_equal(noted_size, DOWNLOAD_SIZE);
	zassert_equal(noted_crc, whole_crc, "the note describes a different file");
}

/* The next pass claims what it holds and stitches the rest onto it. */
ZTEST(kfsw_ftp_client_link, test_the_next_download_continues_from_the_partial)
{
	const uint32_t whole_crc = crc32_ieee(download_content, DOWNLOAD_SIZE);
	struct kfsw_ftp_transfer_result outcome;

	given_partial(DOWNLOAD_HALF, whole_crc);
	peer_will_reply_with_offset(KFSW_FTP_OP_GET_INFO, KFSW_FTP_STATUS_OK, DOWNLOAD_HALF,
				    DOWNLOAD_SIZE);
	link_fake.scripted[0].crc32 = whole_crc;
	peer_will_send_data(DOWNLOAD_HALF, &download_content[DOWNLOAD_HALF], DOWNLOAD_HALF,
			    DOWNLOAD_SIZE, whole_crc);
	peer_will_reply_with_offset(KFSW_FTP_OP_GET_RESULT, KFSW_FTP_STATUS_OK, 0U, DOWNLOAD_SIZE);
	link_fake.scripted[2].crc32 = whole_crc;

	zassert_ok(kfsw_ftp_get(PEER_NODE, "/remote.bin", DOWNLOAD_LOCAL, &outcome),
		   "the second pass did not finish the file");
	zassert_equal(link_fake.sent[0].flags, KFSW_FTP_FLAG_RESUME,
		      "the request did not offer to continue");
	zassert_equal(link_fake.sent[0].offset, DOWNLOAD_HALF,
		      "the request did not say how much it already had");
	zassert_equal(link_fake.sent[0].crc32, whole_crc,
		      "the request did not say which file the partial belongs to");
	zassert_equal(outcome.bytes, DOWNLOAD_SIZE, "the result counted only the second half");
	zassert_equal(outcome.crc32, whole_crc,
		      "the committed file is not the one both ends agreed on");
	zassert_equal(file_size_of(DOWNLOAD_FULL), DOWNLOAD_SIZE);
	zassert_false(file_exists(DOWNLOAD_FULL ".part"), "the partial outlived its transfer");
	zassert_false(file_exists(DOWNLOAD_FULL ".part.map"), "the note outlived its partial");
}

/*
 * The sending node has the last word. When it refuses the claim the partial is
 * discarded, and the note is rewritten for the file now coming in.
 */
ZTEST(kfsw_ftp_client_link, test_a_refused_claim_downloads_the_file_again)
{
	const uint32_t whole_crc = crc32_ieee(download_content, DOWNLOAD_SIZE);
	struct kfsw_ftp_transfer_result outcome;

	given_partial(DOWNLOAD_HALF, 0xA5A5A5A5U);
	peer_will_reply_with_offset(KFSW_FTP_OP_GET_INFO, KFSW_FTP_STATUS_OK, 0U, DOWNLOAD_SIZE);
	link_fake.scripted[0].crc32 = whole_crc;
	peer_will_send_data(0U, download_content, DOWNLOAD_SIZE, DOWNLOAD_SIZE, whole_crc);
	peer_will_reply_with_offset(KFSW_FTP_OP_GET_RESULT, KFSW_FTP_STATUS_OK, 0U, DOWNLOAD_SIZE);
	link_fake.scripted[2].crc32 = whole_crc;

	zassert_ok(kfsw_ftp_get(PEER_NODE, "/remote.bin", DOWNLOAD_LOCAL, &outcome),
		   "a download that had to start again was refused");
	zassert_equal(outcome.crc32, whole_crc, "the restarted download kept the stale bytes");
	zassert_equal(file_size_of(DOWNLOAD_FULL), DOWNLOAD_SIZE);
}

/* A start the client never claimed belongs to some other conversation. */
ZTEST(kfsw_ftp_client_link, test_a_download_starting_somewhere_unasked_is_refused)
{
	struct kfsw_ftp_transfer_result outcome;

	no_download_in_progress();
	peer_will_reply_with_offset(KFSW_FTP_OP_GET_INFO, KFSW_FTP_STATUS_OK, DOWNLOAD_HALF,
				    DOWNLOAD_SIZE);

	zassert_equal(kfsw_ftp_get(PEER_NODE, "/remote.bin", DOWNLOAD_LOCAL, &outcome), -EBADMSG,
		      "a download accepted a start it had not asked for");
	zassert_false(file_exists(DOWNLOAD_FULL), "the refused download left the file behind");
}
