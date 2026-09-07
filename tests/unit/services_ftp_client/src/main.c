/*
 * The FTP client's request and response handling, with the peer scripted.
 *
 * Over a real link the peer is another node running the server, so it answers
 * correctly or not at all. The answers the client is written to refuse -- a
 * reply to a different request, the wrong opcode, a status carrying a refusal,
 * a malformed stat -- take a misbehaving peer, which is what the fakes are.
 */

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/fff.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/ztest.h>

#include <kfsw/platform/storage.h>
#include <kfsw/services/ftp.h>

#include "ftp_internal.h"
#include "ftp_link.h"

DEFINE_FFF_GLOBALS;

#define STORAGE_PARTITION_NODE DT_CHOSEN(kfsw_storage_partition)
#define LOCAL_NODE 7U
#define REMOTE_NODE 42U
#define MAX_NODE 63U
#define TEST_PATH "build"

FAKE_VALUE_FUNC(int, __wrap_kfsw_ftp_link_connect, struct kfsw_ftp_link *, uint16_t);
FAKE_VALUE_FUNC(int, __wrap_kfsw_ftp_link_send, struct kfsw_ftp_link *,
		const struct kfsw_ftp_message *);
FAKE_VALUE_FUNC(int, __wrap_kfsw_ftp_link_receive, struct kfsw_ftp_link *,
		struct kfsw_ftp_link_frame *);
FAKE_VOID_FUNC(__wrap_kfsw_ftp_link_release, struct kfsw_ftp_link_frame *);
FAKE_VOID_FUNC(__wrap_kfsw_ftp_link_close, struct kfsw_ftp_link *);
FAKE_VALUE_FUNC(bool, __wrap_kfsw_ftp_link_is_ready);
FAKE_VALUE_FUNC(uint16_t, __wrap_kfsw_ftp_link_local_node);
FAKE_VALUE_FUNC(uint16_t, __wrap_kfsw_ftp_link_max_node);

/* The reply the scripted peer gives, and the request it was given. */
static struct kfsw_ftp_message reply;
static bool reply_echoes_request_id = true;
static struct kfsw_ftp_message sent_request;

static int capture_request(struct kfsw_ftp_link *link, const struct kfsw_ftp_message *message)
{
	ARG_UNUSED(link);

	sent_request = *message;
	return 0;
}

static int answer(struct kfsw_ftp_link *link, struct kfsw_ftp_link_frame *frame)
{
	ARG_UNUSED(link);

	memset(frame, 0, sizeof(*frame));
	frame->message = reply;
	if (reply_echoes_request_id) {
		frame->message.request_id = sent_request.request_id;
	}
	return 0;
}

static void erase_storage_partition(void)
{
	const struct flash_area *area;

	zassert_ok(flash_area_open(DT_FIXED_PARTITION_ID(STORAGE_PARTITION_NODE), &area));
	zassert_ok(flash_area_flatten(area, 0, area->fa_size));
	flash_area_close(area);
}

static void *client_setup(void)
{
	erase_storage_partition();
	zassert_ok(kfsw_storage_init());
	zassert_ok(kfsw_storage_mount());
	zassert_ok(kfsw_ftp_init());
	return NULL;
}

static void reset_client(void *fixture)
{
	ARG_UNUSED(fixture);

	RESET_FAKE(__wrap_kfsw_ftp_link_connect);
	RESET_FAKE(__wrap_kfsw_ftp_link_send);
	RESET_FAKE(__wrap_kfsw_ftp_link_receive);
	RESET_FAKE(__wrap_kfsw_ftp_link_release);
	RESET_FAKE(__wrap_kfsw_ftp_link_close);
	RESET_FAKE(__wrap_kfsw_ftp_link_is_ready);
	RESET_FAKE(__wrap_kfsw_ftp_link_local_node);
	RESET_FAKE(__wrap_kfsw_ftp_link_max_node);
	FFF_RESET_HISTORY();

	/* A transport that is up, and a node that is neither this one nor out
	 * of range, so each test only sets what it is about.
	 */
	__wrap_kfsw_ftp_link_is_ready_fake.return_val = true;
	__wrap_kfsw_ftp_link_local_node_fake.return_val = LOCAL_NODE;
	__wrap_kfsw_ftp_link_max_node_fake.return_val = MAX_NODE;
	__wrap_kfsw_ftp_link_send_fake.custom_fake = capture_request;
	__wrap_kfsw_ftp_link_receive_fake.custom_fake = answer;

	memset(&reply, 0, sizeof(reply));
	memset(&sent_request, 0, sizeof(sent_request));
	reply_echoes_request_id = true;
	reply.opcode = KFSW_FTP_OP_MKDIR_RESPONSE;
}

ZTEST_SUITE(services_ftp_client, NULL, client_setup, reset_client, NULL, NULL);

ZTEST(services_ftp_client, test_mkdir_asks_the_named_node_and_closes_after)
{
	zassert_ok(kfsw_ftp_mkdir(REMOTE_NODE, TEST_PATH));

	zassert_equal(__wrap_kfsw_ftp_link_connect_fake.arg1_val, REMOTE_NODE,
		      "the connection should be opened to the node that was named");
	zassert_equal(sent_request.opcode, KFSW_FTP_OP_MKDIR_REQUEST);
	zassert_equal(sent_request.path_size, (uint16_t)strlen(TEST_PATH),
		      "the path is carried by length, not as a string");
	zassert_equal(__wrap_kfsw_ftp_link_close_fake.call_count, 1U,
		      "a connection that was opened should be closed");
}

ZTEST(services_ftp_client, test_a_refusal_from_the_peer_becomes_its_errno)
{
	reply.status = KFSW_FTP_STATUS_ALREADY_EXISTS;

	zassert_equal(kfsw_ftp_mkdir(REMOTE_NODE, TEST_PATH), -EEXIST,
		      "the wire status should be reported as the matching errno");
	zassert_equal(__wrap_kfsw_ftp_link_close_fake.call_count, 1U,
		      "a refused request still closes its connection");
}

ZTEST(services_ftp_client, test_an_answer_to_another_request_is_refused)
{
	reply_echoes_request_id = false;
	reply.request_id = 0xDEADBEEFU;

	zassert_equal(kfsw_ftp_mkdir(REMOTE_NODE, TEST_PATH), -EBADMSG,
		      "a reply carrying another request id is not this reply");
}

ZTEST(services_ftp_client, test_an_answer_of_the_wrong_kind_is_refused)
{
	reply.opcode = KFSW_FTP_OP_STAT_RESPONSE;

	zassert_equal(kfsw_ftp_mkdir(REMOTE_NODE, TEST_PATH), -EBADMSG,
		      "a stat response does not answer a mkdir");
}

ZTEST(services_ftp_client, test_a_connection_that_cannot_open_sends_nothing)
{
	__wrap_kfsw_ftp_link_connect_fake.return_val = -ENETUNREACH;

	zassert_equal(kfsw_ftp_mkdir(REMOTE_NODE, TEST_PATH), -ENETUNREACH);
	zassert_equal(__wrap_kfsw_ftp_link_send_fake.call_count, 0U,
		      "nothing should be sent down a connection that never opened");
}

ZTEST(services_ftp_client, test_a_send_that_fails_is_not_waited_on)
{
	__wrap_kfsw_ftp_link_send_fake.custom_fake = NULL;
	__wrap_kfsw_ftp_link_send_fake.return_val = -ENOTCONN;

	zassert_equal(kfsw_ftp_mkdir(REMOTE_NODE, TEST_PATH), -ENOTCONN);
	zassert_equal(__wrap_kfsw_ftp_link_receive_fake.call_count, 0U,
		      "a request that was not sent has no reply to wait for");
	zassert_equal(__wrap_kfsw_ftp_link_close_fake.call_count, 1U,
		      "the connection is closed even when the request failed");
}

ZTEST(services_ftp_client, test_a_peer_that_never_answers_reports_the_timeout)
{
	__wrap_kfsw_ftp_link_receive_fake.custom_fake = NULL;
	__wrap_kfsw_ftp_link_receive_fake.return_val = -ETIMEDOUT;

	zassert_equal(kfsw_ftp_mkdir(REMOTE_NODE, TEST_PATH), -ETIMEDOUT);
	zassert_equal(__wrap_kfsw_ftp_link_close_fake.call_count, 1U);
}

ZTEST(services_ftp_client, test_a_transport_that_is_down_is_refused_before_connecting)
{
	__wrap_kfsw_ftp_link_is_ready_fake.return_val = false;

	zassert_not_equal(kfsw_ftp_mkdir(REMOTE_NODE, TEST_PATH), 0,
			  "a client with no transport cannot serve a remote request");
	zassert_equal(__wrap_kfsw_ftp_link_connect_fake.call_count, 0U,
		      "nothing should be dialled when the transport is down");
}

ZTEST(services_ftp_client, test_a_node_beyond_the_transport_is_refused)
{
	zassert_not_equal(kfsw_ftp_mkdir(MAX_NODE + 1U, TEST_PATH), 0,
			  "a node the transport cannot address is not a request to send");
	zassert_equal(__wrap_kfsw_ftp_link_connect_fake.call_count, 0U);
}

ZTEST(services_ftp_client, test_stat_reads_the_type_size_and_checksum_it_was_given)
{
	struct kfsw_ftp_stat info = {0};

	reply.opcode = KFSW_FTP_OP_STAT_RESPONSE;
	reply.flags = KFSW_FTP_ENTRY_FILE;
	reply.total_size = 4096U;
	reply.crc32 = 0x1234ABCDU;

	zassert_ok(kfsw_ftp_stat(REMOTE_NODE, TEST_PATH, &info));

	zassert_equal(info.type, KFSW_FTP_ENTRY_FILE);
	zassert_equal(info.size, 4096U);
	zassert_equal(info.crc32, 0x1234ABCDU);
}

ZTEST(services_ftp_client, test_a_stat_answer_that_is_neither_file_nor_directory_is_refused)
{
	struct kfsw_ftp_stat info = {0};

	reply.opcode = KFSW_FTP_OP_STAT_RESPONSE;
	reply.flags = 0x7FU;

	zassert_equal(kfsw_ftp_stat(REMOTE_NODE, TEST_PATH, &info), -EBADMSG,
		      "an entry type that is not one of the two is not an answer");
}

ZTEST(services_ftp_client, test_a_stat_answer_carrying_a_body_is_refused)
{
	struct kfsw_ftp_stat info = {0};
	static const uint8_t body[] = {0xAAU, 0xBBU};

	reply.opcode = KFSW_FTP_OP_STAT_RESPONSE;
	reply.flags = KFSW_FTP_ENTRY_FILE;
	reply.data = body;
	reply.data_size = (uint16_t)sizeof(body);

	zassert_equal(kfsw_ftp_stat(REMOTE_NODE, TEST_PATH, &info), -EBADMSG,
		      "a stat answer carries its values in the header, not a body");
}
