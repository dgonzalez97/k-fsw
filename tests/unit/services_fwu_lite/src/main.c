#include <errno.h>
#include <string.h>

#include <zephyr/sys/crc.h>
#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>

#include <kfsw/services/fwu.h>
#include <kfsw/services/fwu_lite.h>

/* The protocol is tested without a transport: requests are handed to the
 * handler directly and the replies inspected. What a link would add is the
 * carrying, which the CSP smoke tests cover; what matters here is that every
 * malformed or ill-timed request produces a reply saying why, and that a block
 * which fails its checksum can simply be sent again.
 */

#define BLOCK KFSW_FWU_LITE_MAX_BLOCK_SIZE
#define IMAGE_BLOCKS 5U
#define IMAGE_TAIL 40U
#define IMAGE_SIZE ((BLOCK * IMAGE_BLOCKS) + IMAGE_TAIL)

static uint8_t image[IMAGE_SIZE];

static void *lite_setup(void)
{
	for (size_t index = 0U; index < sizeof(image); index++) {
		image[index] = (uint8_t)((index * 23U) + 5U);
	}
	return NULL;
}

static void lite_before(void *fixture)
{
	ARG_UNUSED(fixture);
	(void)kfsw_fwu_abort();
}

static uint8_t ask(struct kfsw_fwu_lite_message *reply, uint8_t opcode, uint16_t block_index,
		   uint32_t argument, uint32_t extra, const uint8_t *data, uint16_t data_size)
{
	struct kfsw_fwu_lite_message request = {
		.opcode = opcode,
		.block_index = block_index,
		.argument = argument,
		.extra = extra,
		.data = data,
		.data_size = data_size,
	};

	zassert_ok(kfsw_fwu_lite_handle(&request, reply));
	return reply->status;
}

static uint8_t send_block(struct kfsw_fwu_lite_message *reply, uint16_t index)
{
	uint32_t offset = (uint32_t)index * BLOCK;
	uint16_t size = (uint16_t)MIN((size_t)BLOCK, sizeof(image) - offset);

	return ask(reply, KFSW_FWU_LITE_OP_BLOCK, index, crc32_ieee(&image[offset], size), 0U,
		   &image[offset], size);
}

static void begin_transfer(struct kfsw_fwu_lite_message *reply)
{
	zassert_equal(ask(reply, KFSW_FWU_LITE_OP_BEGIN, 0U, IMAGE_SIZE,
			  crc32_ieee(image, IMAGE_SIZE), NULL, 0U),
		      KFSW_FWU_LITE_STATUS_OK);
}

/* ---------------------------------------------------------------- the codec */

ZTEST(services_fwu_lite, test_header_is_twelve_bytes_and_big_endian)
{
	/* The layout is checked byte by byte rather than by round trip, because
	 * a round trip would agree with itself even if both halves were wrong.
	 */
	struct kfsw_fwu_lite_message message = {
		.opcode = KFSW_FWU_LITE_OP_BLOCK,
		.status = 0x11,
		.block_index = 0x2233,
		.argument = 0x44556677,
		.extra = 0x8899AABB,
	};
	uint8_t buffer[32];
	size_t encoded = 0U;

	zassert_ok(kfsw_fwu_lite_encode(&message, buffer, sizeof(buffer), &encoded));
	zassert_equal(encoded, KFSW_FWU_LITE_HEADER_SIZE);

	zassert_equal(buffer[0], KFSW_FWU_LITE_OP_BLOCK);
	zassert_equal(buffer[1], 0x11);
	zassert_equal(buffer[2], 0x22);
	zassert_equal(buffer[3], 0x33);
	zassert_equal(buffer[4], 0x44);
	zassert_equal(buffer[7], 0x77);
	zassert_equal(buffer[8], 0x88);
	zassert_equal(buffer[11], 0xBB);
}

ZTEST(services_fwu_lite, test_encode_decode_round_trip_carries_a_payload)
{
	struct kfsw_fwu_lite_message message = {
		.opcode = KFSW_FWU_LITE_OP_BLOCK,
		.block_index = 7U,
		.argument = 0xDEADBEEF,
		.extra = 12345U,
		.data = image,
		.data_size = 64U,
	};
	struct kfsw_fwu_lite_message decoded;
	uint8_t buffer[KFSW_FWU_LITE_HEADER_SIZE + BLOCK];
	size_t encoded = 0U;

	zassert_ok(kfsw_fwu_lite_encode(&message, buffer, sizeof(buffer), &encoded));
	zassert_equal(encoded, KFSW_FWU_LITE_HEADER_SIZE + 64U);
	zassert_ok(kfsw_fwu_lite_decode(buffer, encoded, &decoded));

	zassert_equal(decoded.opcode, message.opcode);
	zassert_equal(decoded.block_index, message.block_index);
	zassert_equal(decoded.argument, message.argument);
	zassert_equal(decoded.extra, message.extra);
	zassert_equal(decoded.data_size, 64U);
	zassert_mem_equal(decoded.data, image, 64U);
}

ZTEST(services_fwu_lite, test_encode_rejects_bad_arguments)
{
	struct kfsw_fwu_lite_message message = {.opcode = KFSW_FWU_LITE_OP_STATUS};
	uint8_t buffer[KFSW_FWU_LITE_HEADER_SIZE];
	size_t encoded = 0U;

	zassert_equal(kfsw_fwu_lite_encode(NULL, buffer, sizeof(buffer), &encoded), -EINVAL);
	zassert_equal(kfsw_fwu_lite_encode(&message, NULL, sizeof(buffer), &encoded), -EINVAL);
	zassert_equal(kfsw_fwu_lite_encode(&message, buffer, sizeof(buffer), NULL), -EINVAL);

	/* A payload size with no payload would encode whatever follows in memory. */
	message.data_size = 4U;
	message.data = NULL;
	zassert_equal(kfsw_fwu_lite_encode(&message, buffer, sizeof(buffer), &encoded), -EINVAL);

	message.data = image;
	message.data_size = BLOCK + 1U;
	zassert_equal(kfsw_fwu_lite_encode(&message, buffer, sizeof(buffer), &encoded), -EMSGSIZE);

	message.data_size = 8U;
	zassert_equal(kfsw_fwu_lite_encode(&message, buffer, KFSW_FWU_LITE_HEADER_SIZE, &encoded),
		      -EMSGSIZE);
}

ZTEST(services_fwu_lite, test_decode_rejects_bad_input)
{
	uint8_t buffer[KFSW_FWU_LITE_HEADER_SIZE + 8U] = {0};
	struct kfsw_fwu_lite_message decoded;

	zassert_equal(kfsw_fwu_lite_decode(NULL, sizeof(buffer), &decoded), -EINVAL);
	zassert_equal(kfsw_fwu_lite_decode(buffer, sizeof(buffer), NULL), -EINVAL);

	/* Anything shorter than a header cannot be interpreted at all. */
	zassert_equal(kfsw_fwu_lite_decode(buffer, KFSW_FWU_LITE_HEADER_SIZE - 1U, &decoded),
		      -EBADMSG);

	buffer[0] = KFSW_FWU_LITE_OP_BEGIN;
	zassert_ok(kfsw_fwu_lite_decode(buffer, KFSW_FWU_LITE_HEADER_SIZE, &decoded));

	buffer[0] = 0xEE;
	zassert_equal(kfsw_fwu_lite_decode(buffer, sizeof(buffer), &decoded), -EBADMSG,
		      "an unknown opcode must not be interpreted as a known one");
}

/* --------------------------------------------------------------- the handler */

ZTEST(services_fwu_lite, test_an_unknown_request_is_answered_not_ignored)
{
	/* Silence is indistinguishable from a lost packet, so even a request
	 * that makes no sense gets a reply.
	 */
	struct kfsw_fwu_lite_message request = {.opcode = 0x7F};
	struct kfsw_fwu_lite_message reply;

	zassert_ok(kfsw_fwu_lite_handle(&request, &reply));
	zassert_equal(reply.status, KFSW_FWU_LITE_STATUS_INVALID);
	zassert_equal(reply.opcode, 0x7F, "a reply must say which request it answers");
}

ZTEST(services_fwu_lite, test_handle_rejects_null)
{
	struct kfsw_fwu_lite_message message = {.opcode = KFSW_FWU_LITE_OP_STATUS};

	zassert_equal(kfsw_fwu_lite_handle(NULL, &message), -EINVAL);
	zassert_equal(kfsw_fwu_lite_handle(&message, NULL), -EINVAL);
}

ZTEST(services_fwu_lite, test_begin_rejects_an_image_that_does_not_fit)
{
	struct kfsw_fwu_lite_message reply;

	zassert_equal(ask(&reply, KFSW_FWU_LITE_OP_BEGIN, 0U, kfsw_fwu_max_image_size() + 1U, 0U,
			  NULL, 0U),
		      KFSW_FWU_LITE_STATUS_TOO_LARGE);
}

ZTEST(services_fwu_lite, test_a_block_before_begin_is_rejected)
{
	struct kfsw_fwu_lite_message reply;

	zassert_equal(send_block(&reply, 0U), KFSW_FWU_LITE_STATUS_INVALID);
}

ZTEST(services_fwu_lite, test_a_block_out_of_order_is_named_not_written)
{
	struct kfsw_fwu_lite_message reply;

	begin_transfer(&reply);
	zassert_equal(send_block(&reply, 0U), KFSW_FWU_LITE_STATUS_OK);

	zassert_equal(send_block(&reply, 3U), KFSW_FWU_LITE_STATUS_OUT_OF_ORDER);
	zassert_equal(reply.block_index, 1U, "the reply must say which block is actually wanted");
	zassert_equal(reply.extra, BLOCK, "a rejected block must not advance the transfer");
}

ZTEST(services_fwu_lite, test_a_corrupt_block_can_simply_be_sent_again)
{
	/* This is the whole point of the per-block checksum: a bad block costs
	 * one block, not the eight minute upload around it.
	 */
	struct kfsw_fwu_lite_message reply;
	uint8_t corrupted[BLOCK];

	begin_transfer(&reply);
	zassert_equal(send_block(&reply, 0U), KFSW_FWU_LITE_STATUS_OK);

	memcpy(corrupted, &image[BLOCK], sizeof(corrupted));
	corrupted[10] ^= 0xFFU;

	/* The checksum sent is the one for the good block, so the node sees the
	 * damage rather than trusting what arrived. */
	zassert_equal(ask(&reply, KFSW_FWU_LITE_OP_BLOCK, 1U, crc32_ieee(&image[BLOCK], BLOCK), 0U,
			  corrupted, BLOCK),
		      KFSW_FWU_LITE_STATUS_BAD_BLOCK);
	zassert_equal(reply.extra, BLOCK, "a bad block must not be written");
	zassert_equal(reply.block_index, 1U, "the same block must still be wanted");

	zassert_equal(send_block(&reply, 1U), KFSW_FWU_LITE_STATUS_OK,
		      "resending the block must simply work");
	zassert_equal(reply.extra, 2U * BLOCK);
}

ZTEST(services_fwu_lite, test_a_short_block_in_the_middle_is_rejected)
{
	/* Block indices are derived from how much the node holds, so a short
	 * block that is not the last would desynchronise both ends silently.
	 */
	struct kfsw_fwu_lite_message reply;

	begin_transfer(&reply);
	zassert_equal(
		ask(&reply, KFSW_FWU_LITE_OP_BLOCK, 0U, crc32_ieee(image, 100U), 0U, image, 100U),
		KFSW_FWU_LITE_STATUS_INVALID);
}

ZTEST(services_fwu_lite, test_a_short_last_block_is_accepted)
{
	struct kfsw_fwu_lite_message reply;

	begin_transfer(&reply);
	for (uint16_t index = 0U; index < IMAGE_BLOCKS; index++) {
		zassert_equal(send_block(&reply, index), KFSW_FWU_LITE_STATUS_OK);
	}
	zassert_equal(send_block(&reply, IMAGE_BLOCKS), KFSW_FWU_LITE_STATUS_OK,
		      "the final partial block must be accepted");
	zassert_equal(reply.extra, IMAGE_SIZE);
}

ZTEST(services_fwu_lite, test_verify_before_the_image_is_complete_is_rejected)
{
	struct kfsw_fwu_lite_message reply;

	begin_transfer(&reply);
	zassert_equal(send_block(&reply, 0U), KFSW_FWU_LITE_STATUS_OK);
	zassert_equal(ask(&reply, KFSW_FWU_LITE_OP_VERIFY, 0U, 0U, 0U, NULL, 0U),
		      KFSW_FWU_LITE_STATUS_INVALID);
}

ZTEST(services_fwu_lite, test_verify_reports_a_wrong_whole_image_checksum)
{
	struct kfsw_fwu_lite_message reply;

	zassert_equal(ask(&reply, KFSW_FWU_LITE_OP_BEGIN, 0U, IMAGE_SIZE,
			  crc32_ieee(image, IMAGE_SIZE) ^ 0xFFFFFFFFU, NULL, 0U),
		      KFSW_FWU_LITE_STATUS_OK);
	for (uint16_t index = 0U; index <= IMAGE_BLOCKS; index++) {
		zassert_equal(send_block(&reply, index), KFSW_FWU_LITE_STATUS_OK);
	}

	/* Every block passed its own check, so only the whole-image checksum
	 * can catch a sender that declared the wrong image. */
	zassert_equal(ask(&reply, KFSW_FWU_LITE_OP_VERIFY, 0U, 0U, 0U, NULL, 0U),
		      KFSW_FWU_LITE_STATUS_BAD_IMAGE);
}

ZTEST(services_fwu_lite, test_a_whole_image_arrives_and_verifies)
{
	struct kfsw_fwu_lite_message reply;

	begin_transfer(&reply);
	for (uint16_t index = 0U; index <= IMAGE_BLOCKS; index++) {
		zassert_equal(send_block(&reply, index), KFSW_FWU_LITE_STATUS_OK,
			      "block %u was rejected", index);
	}

	zassert_equal(reply.extra, IMAGE_SIZE);
	zassert_equal(reply.argument, crc32_ieee(image, IMAGE_SIZE));
	zassert_equal(ask(&reply, KFSW_FWU_LITE_OP_VERIFY, 0U, 0U, 0U, NULL, 0U),
		      KFSW_FWU_LITE_STATUS_OK);
}

ZTEST(services_fwu_lite, test_status_reports_progress_without_changing_it)
{
	struct kfsw_fwu_lite_message reply;

	begin_transfer(&reply);
	zassert_equal(send_block(&reply, 0U), KFSW_FWU_LITE_STATUS_OK);

	zassert_equal(ask(&reply, KFSW_FWU_LITE_OP_STATUS, 0U, 0U, 0U, NULL, 0U),
		      KFSW_FWU_LITE_STATUS_OK);
	zassert_equal(reply.extra, BLOCK);
	zassert_equal(reply.block_index, 1U);

	zassert_equal(ask(&reply, KFSW_FWU_LITE_OP_STATUS, 0U, 0U, 0U, NULL, 0U),
		      KFSW_FWU_LITE_STATUS_OK);
	zassert_equal(reply.extra, BLOCK, "asking must not advance anything");
}

ZTEST(services_fwu_lite, test_abort_returns_to_idle_and_a_new_transfer_can_start)
{
	struct kfsw_fwu_lite_message reply;

	begin_transfer(&reply);
	zassert_equal(send_block(&reply, 0U), KFSW_FWU_LITE_STATUS_OK);

	zassert_equal(ask(&reply, KFSW_FWU_LITE_OP_ABORT, 0U, 0U, 0U, NULL, 0U),
		      KFSW_FWU_LITE_STATUS_OK);
	zassert_equal(reply.extra, 0U);

	begin_transfer(&reply);
	zassert_equal(send_block(&reply, 0U), KFSW_FWU_LITE_STATUS_OK);
}

ZTEST(services_fwu_lite, test_a_second_begin_while_receiving_is_refused)
{
	/* Both upload routes feed one update service, so a second sender must
	 * be told the node is busy rather than quietly resetting the first.
	 */
	struct kfsw_fwu_lite_message reply;

	begin_transfer(&reply);
	zassert_equal(send_block(&reply, 0U), KFSW_FWU_LITE_STATUS_OK);

	zassert_equal(ask(&reply, KFSW_FWU_LITE_OP_BEGIN, 0U, IMAGE_SIZE,
			  crc32_ieee(image, IMAGE_SIZE), NULL, 0U),
		      KFSW_FWU_LITE_STATUS_BUSY);
	zassert_equal(reply.extra, BLOCK, "the running transfer must be untouched");
}

ZTEST(services_fwu_lite, test_status_names_are_reported)
{
	zassert_str_equal(kfsw_fwu_lite_status_name(KFSW_FWU_LITE_STATUS_OK), "ok");
	zassert_str_equal(kfsw_fwu_lite_status_name(KFSW_FWU_LITE_STATUS_BAD_BLOCK), "bad-block");
	zassert_str_equal(kfsw_fwu_lite_status_name(KFSW_FWU_LITE_STATUS_BAD_IMAGE), "bad-image");
	zassert_str_equal(kfsw_fwu_lite_status_name(KFSW_FWU_LITE_STATUS_OUT_OF_ORDER),
			  "out-of-order");
	zassert_str_equal(kfsw_fwu_lite_status_name(KFSW_FWU_LITE_STATUS_BUSY), "busy");
	zassert_str_equal(kfsw_fwu_lite_status_name(200U), "unknown");
}

ZTEST_SUITE(services_fwu_lite, NULL, lite_setup, lite_before, NULL, NULL);
