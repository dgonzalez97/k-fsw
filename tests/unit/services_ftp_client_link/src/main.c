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
#define LOCAL_NODE 1U
#define PEER_NODE 2U
#define MAX_NODE 16383U
#define TEST_PAYLOAD 64U
#define MAX_SCRIPTED 8

/*
 * What the client does with the answers it gets.
 *
 * A real peer is cooperative: it replies to the request you sent, once, with
 * the opcode you expected. The failures worth designing against are the ones
 * it will not produce on demand — a reply to a request that already timed out,
 * a reply of a different kind, a status that is not success, a peer that
 * accepts the connection and then says nothing.
 *
 * Each of those, mishandled, is a client that believes a stale answer. Over a
 * radio where a slow reply and a lost one look identical from this end, that
 * is not a remote possibility; it is what a bad pass does.
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
		/* A peer that says nothing is what a lost reply looks like. */
		return -ETIMEDOUT;
	}
	if (link_fake.scripted_result[link_fake.received] != 0) {
		return link_fake.scripted_result[link_fake.received++];
	}
	frame->message = link_fake.scripted[link_fake.received++];
	/* A cooperative peer answers the request it was sent. Reading the id
	 * back from the request removes any need for a test to know what the
	 * client allocated.
	 */
	frame->message.request_id = (link_fake.force_request_id != 0U)
					    ? link_fake.force_request_id
					    : link_fake.sent[link_fake.sends - 1U].request_id;
	frame->buffer = &link_fake;
	return 0;
}

/*
 * The real one no-ops on a frame that holds nothing, which is how releasing
 * twice is made harmless: the contract is enforced here rather than by every
 * caller. A fake that counted both calls would invent a double-free that the
 * code does not have.
 */
void __wrap_kfsw_ftp_link_release(struct kfsw_ftp_link_frame *frame)
{
	if ((frame == NULL) || (frame->buffer == NULL)) {
		return;
	}
	link_fake.releases++;
	frame->buffer = NULL;
}

/* Queues one reply. The request id is filled in from what the client actually
 * sent, so a case only has to say when it wants them to disagree.
 */
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
 * The sandbox is enforced before anything is transmitted, which matters twice
 * over: a path that climbs out is refused, and the peer never sees it.
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

/* The happy path, so the refusals below are known to be refusals. */
ZTEST(kfsw_ftp_client_link, test_a_directory_request_is_sent_and_answered)
{
	peer_will_reply(KFSW_FTP_OP_MKDIR_RESPONSE, KFSW_FTP_STATUS_OK);

	zassert_ok(kfsw_ftp_mkdir(PEER_NODE, "/made"), "a good exchange was refused");
	zassert_equal(link_fake.sent[0].opcode, KFSW_FTP_OP_MKDIR_REQUEST,
		      "the client sent the wrong request");
	zassert_true(link_fake.closes > 0U, "the client left the connection open");
}

/*
 * The one that matters most over a radio.
 *
 * A slow reply and a lost one look identical from this end, so a client that
 * accepts any reply will eventually pair an answer with the wrong question:
 * the status of a mkdir read as the status of the delete that followed it.
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

/* Whatever the outcome, the connection is given back exactly as often as it
 * was taken. A leak here is a node that stops answering after a noisy pass.
 */
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
 * Upload and download are where the client spends most of its lines, and they
 * are the paths a bad pass actually exercises: a peer that refuses before the
 * first byte, one that goes quiet halfway, one that acknowledges a different
 * number of bytes than were sent.
 *
 * That last one is the reason the client checks at all. The peer echoes what
 * it committed, so a disagreement means the file on the far side is not the
 * file that was sent — and a transfer that reports success there is worse than
 * one that fails, because nobody looks again.
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

/* Ready, but answering a question nobody asked. */
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
 * The integrity check, and the reason it exists: the peer echoes what it
 * committed. A transfer that reported success while the far side holds
 * different bytes is worse than one that failed, because nobody looks again.
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
