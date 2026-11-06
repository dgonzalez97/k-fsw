#include <errno.h>
#include <string.h>
#include <psa/crypto.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/crc.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/storage/stream_flash.h>
#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>
#include <kfsw/services/fwu_lite.h>

#define BLOCK KFSW_FWU_LITE_MAX_BLOCK_SIZE
#define IMAGE_SIZE (2U * BLOCK + 17U)
static uint8_t image[IMAGE_SIZE];
static uint8_t descriptor[KFSW_FWU_LITE_DESCRIPTOR_SIZE];
static uint8_t receipt[KFSW_FWU_LITE_RECEIPT_SIZE];
static struct kfsw_fwu_lite_message request;
static struct kfsw_fwu_lite_message reply;
static struct kfsw_fwu_status status;
static unsigned int stream_writes;
static int erase_error;
static uint32_t transfer;

int __real_stream_flash_buffered_write(struct stream_flash_ctx *ctx, const uint8_t *data,
				       size_t size, bool flush);
int __wrap_stream_flash_buffered_write(struct stream_flash_ctx *ctx, const uint8_t *data,
				       size_t size, bool flush)
{
	stream_writes++;
	return __real_stream_flash_buffered_write(ctx, data, size, flush);
}
int __real_flash_area_flatten(const struct flash_area *area, off_t offset, size_t size);
int __wrap_flash_area_flatten(const struct flash_area *area, off_t offset, size_t size)
{
	return erase_error != 0 ? erase_error : __real_flash_area_flatten(area, offset, size);
}

static void before(void *fixture)
{
	struct kfsw_fwu_image description = {
		.size = sizeof(image), .block_size = BLOCK, .digest_present = true};
	size_t hash_size;
	ARG_UNUSED(fixture);
	erase_error = 0;
	zassert_ok(kfsw_fwu_get_status(&status));
	if (status.handle.generation != 0U) {
		zassert_ok(kfsw_fwu_session_abort(&status.handle, &status));
	}
	for (size_t i = 0; i < sizeof(image); i++) {
		image[i] = (uint8_t)(i * 23U + 5U);
	}
	description.crc32 = crc32_ieee(image, sizeof(image));
	zassert_equal(psa_hash_compute(PSA_ALG_SHA_256, image, sizeof(image), description.sha256,
				       sizeof(description.sha256), &hash_size),
		      PSA_SUCCESS);
	kfsw_fwu_lite_pack_image(&description, descriptor);
	request = (struct kfsw_fwu_lite_message){.version = KFSW_FWU_LITE_VERSION,
						 .opcode = KFSW_FWU_LITE_OP_BEGIN,
						 .request_id = 123U,
						 .transfer_id = ++transfer,
						 .data = descriptor,
						 .data_size = sizeof(descriptor)};
	stream_writes = 0U;
}

static uint8_t ask(uint16_t peer)
{
	zassert_ok(kfsw_fwu_lite_handle_peer(peer, &request, &reply, receipt));
	zassert_ok(kfsw_fwu_lite_receipt(&reply, &status));
	return reply.status;
}

static void begin(void)
{
	zassert_equal(ask(16U), KFSW_FWU_LITE_STATUS_OK);
	request.generation = reply.generation;
	request.data = NULL;
	request.data_size = 0U;
	zassert_not_equal(request.generation, 0U);
}

static void block(uint32_t offset, size_t size)
{
	request.opcode = KFSW_FWU_LITE_OP_BLOCK;
	request.offset = offset;
	request.data = image + offset;
	request.data_size = size;
	request.crc32 = crc32_ieee(request.data, size);
	request.request_id++;
}

static void control(uint8_t opcode)
{
	request.opcode = opcode;
	request.offset = 0U;
	request.crc32 = 0U;
	request.data = NULL;
	request.data_size = 0U;
	request.request_id++;
}

static void complete(void)
{
	begin();
	for (size_t offset = 0; offset < sizeof(image); offset += BLOCK) {
		block(offset, MIN(BLOCK, sizeof(image) - offset));
		zassert_equal(ask(16U), KFSW_FWU_LITE_STATUS_OK);
	}
}

ZTEST(services_fwu_lite, test_wire_layout)
{
	uint8_t wire[KFSW_FWU_LITE_WIRE_SIZE];
	size_t size;
	request.request_id = 0x11223344;
	request.transfer_id = 0x55667788;
	zassert_ok(kfsw_fwu_lite_encode(&request, wire, sizeof(wire), &size));
	zassert_equal(size, 70U);
	zassert_equal(wire[0], 1U);
	zassert_equal(wire[1], 0U);
	zassert_equal(wire[2], KFSW_FWU_LITE_OP_BEGIN);
	zassert_equal(sys_get_be32(wire + 4), 0x11223344);
	zassert_equal(sys_get_be32(wire + 8), 0x55667788);
	zassert_equal(sys_get_be16(wire + 24), 42U);
	zassert_mem_equal(wire + 28, descriptor, sizeof(descriptor));
	zassert_ok(kfsw_fwu_lite_decode(wire, size, &reply));
	zassert_equal(reply.request_id, request.request_id);
	zassert_mem_equal(reply.data, descriptor, sizeof(descriptor));
}

ZTEST(services_fwu_lite, test_codec_rejects_trailing_and_truncation)
{
	uint8_t wire[KFSW_FWU_LITE_WIRE_SIZE];
	size_t size;
	zassert_ok(kfsw_fwu_lite_encode(&request, wire, sizeof(wire), &size));
	zassert_equal(kfsw_fwu_lite_decode(wire, size + 1, &reply), -EBADMSG);
	zassert_equal(kfsw_fwu_lite_decode(wire, size - 1, &reply), -EBADMSG);
	zassert_equal(kfsw_fwu_lite_decode(wire, 27U, &reply), -EBADMSG);
	wire[26] = 1U;
	zassert_equal(kfsw_fwu_lite_decode(wire, size, &reply), -EBADMSG);
}

ZTEST(services_fwu_lite, test_oversize_before_narrowing)
{
	static uint8_t malicious[65548U];
	zassert_equal(kfsw_fwu_lite_decode(malicious, sizeof(malicious), &reply), -EMSGSIZE);
	request.data_size = 65536U;
	size_t size;
	zassert_equal(kfsw_fwu_lite_encode(&request, malicious, sizeof(malicious), &size),
		      -EMSGSIZE);
	zassert_equal(ask(16U), KFSW_FWU_LITE_STATUS_INVALID);
}

ZTEST(services_fwu_lite, test_unsupported_version)
{
	request.version++;
	zassert_ok(kfsw_fwu_lite_handle_peer(16U, &request, &reply, receipt));
	zassert_equal(reply.status, KFSW_FWU_LITE_STATUS_INVALID);
	zassert_ok(kfsw_fwu_get_status(&status));
	zassert_equal(status.state, KFSW_FWU_IDLE);
}

ZTEST(services_fwu_lite, test_reply_as_request)
{
	request.direction = KFSW_FWU_LITE_REPLY;
	zassert_equal(ask(16U), KFSW_FWU_LITE_STATUS_INVALID);
}

ZTEST(services_fwu_lite, test_illegal_begin_fields)
{
	request.generation = 1U;
	zassert_equal(ask(16U), KFSW_FWU_LITE_STATUS_INVALID);
	request.generation = 0U;
	request.offset = 1U;
	zassert_equal(ask(16U), KFSW_FWU_LITE_STATUS_INVALID);
	request.offset = 0U;
	request.request_id = 0U;
	zassert_ok(kfsw_fwu_lite_handle_peer(16U, &request, &reply, receipt));
	zassert_equal(reply.status, KFSW_FWU_LITE_STATUS_INVALID);
}

ZTEST(services_fwu_lite, test_fixed_block_descriptor)
{
	descriptor[9] ^= 1U;
	zassert_equal(ask(16U), KFSW_FWU_LITE_STATUS_INVALID);
	zassert_ok(kfsw_fwu_get_status(&status));
	zassert_equal(status.state, KFSW_FWU_IDLE);
}

ZTEST(services_fwu_lite, test_lost_begin_reply)
{
	struct kfsw_fwu_lite_message original = request;
	begin();
	struct kfsw_fwu_handle handle = status.handle;
	uint32_t started = status.started;
	request = original;
	zassert_equal(ask(16U), KFSW_FWU_LITE_STATUS_OK);
	zassert_equal(status.handle.generation, handle.generation);
	zassert_equal(status.started, started);
}

ZTEST(services_fwu_lite, test_full_duplicate_no_write)
{
	begin();
	block(0U, BLOCK);
	zassert_equal(ask(16U), KFSW_FWU_LITE_STATUS_OK);
	unsigned int writes = stream_writes;
	zassert_equal(ask(16U), KFSW_FWU_LITE_STATUS_OK);
	zassert_equal(status.received, BLOCK);
	zassert_equal(stream_writes, writes);
}

ZTEST(services_fwu_lite, test_short_tail_duplicate_no_write)
{
	complete();
	unsigned int writes = stream_writes;
	zassert_equal(ask(16U), KFSW_FWU_LITE_STATUS_OK);
	zassert_equal(status.received, sizeof(image));
	zassert_equal(stream_writes, writes);
}

ZTEST(services_fwu_lite, test_changed_duplicate)
{
	begin();
	block(0U, BLOCK);
	zassert_equal(ask(16U), KFSW_FWU_LITE_STATUS_OK);
	image[0] ^= 1U;
	request.crc32 = crc32_ieee(image, BLOCK);
	zassert_equal(ask(16U), KFSW_FWU_LITE_STATUS_BAD_IMAGE);
	zassert_equal(status.received, BLOCK);
}

ZTEST(services_fwu_lite, test_older_block_never_accepted)
{
	begin();
	block(0U, BLOCK);
	zassert_equal(ask(16U), KFSW_FWU_LITE_STATUS_OK);
	block(BLOCK, BLOCK);
	zassert_equal(ask(16U), KFSW_FWU_LITE_STATUS_OK);
	block(0U, BLOCK);
	zassert_equal(ask(16U), KFSW_FWU_LITE_STATUS_OUT_OF_ORDER);
	zassert_equal(status.received, 2U * BLOCK);
}

ZTEST(services_fwu_lite, test_bad_block_crc)
{
	begin();
	block(0U, BLOCK);
	request.crc32++;
	zassert_equal(ask(16U), KFSW_FWU_LITE_STATUS_BAD_BLOCK);
	zassert_equal(stream_writes, 0U);
}

ZTEST(services_fwu_lite, test_short_nonfinal_rejected)
{
	begin();
	block(0U, 17U);
	zassert_equal(ask(16U), KFSW_FWU_LITE_STATUS_INVALID);
	zassert_equal(stream_writes, 0U);
}

ZTEST(services_fwu_lite, test_owner_is_transport_peer)
{
	begin();
	block(0U, BLOCK);
	zassert_equal(ask(17U), KFSW_FWU_LITE_STATUS_OWNER);
	zassert_equal(stream_writes, 0U);
	zassert_equal(ask(16U), KFSW_FWU_LITE_STATUS_OK);
}

ZTEST(services_fwu_lite, test_old_generation)
{
	begin();
	request.generation++;
	block(0U, BLOCK);
	zassert_equal(ask(16U), KFSW_FWU_LITE_STATUS_STALE);
	zassert_equal(stream_writes, 0U);
}

ZTEST(services_fwu_lite, test_status_discovers_exact_owned_handle)
{
	begin();
	struct kfsw_fwu_handle handle = status.handle;
	control(KFSW_FWU_LITE_OP_STATUS);
	request.transfer_id = 0U;
	request.generation = 0U;
	zassert_equal(ask(16U), KFSW_FWU_LITE_STATUS_OK);
	zassert_equal(status.handle.generation, handle.generation);
	zassert_equal(status.handle.transfer_id, handle.transfer_id);
	zassert_equal(ask(17U), KFSW_FWU_LITE_STATUS_OWNER);
}

ZTEST(services_fwu_lite, test_different_descriptor_busy)
{
	struct kfsw_fwu_lite_message original = request;
	begin();
	request = original;
	descriptor[10] ^= 1U;
	zassert_equal(ask(16U), KFSW_FWU_LITE_STATUS_BUSY);
}

ZTEST(services_fwu_lite, test_local_and_ftp_conflicts)
{
	begin();
	zassert_equal(kfsw_fwu_begin(sizeof(image), 0U), -EBUSY);
	zassert_equal(kfsw_fwu_abort(), -EACCES);
	struct kfsw_fwu_owner owner = {.kind = KFSW_FWU_OWNER_FTP, .peer = 16U};
	struct kfsw_fwu_image other = {.size = sizeof(image)};
	struct kfsw_fwu_handle handle = {0};
	zassert_equal(kfsw_fwu_session_begin(&owner, &other, 0U, &handle, &status), -EBUSY);
	zassert_equal(handle.generation, 0U);
}

ZTEST(services_fwu_lite, test_verify_requires_complete)
{
	begin();
	control(KFSW_FWU_LITE_OP_VERIFY);
	zassert_equal(ask(16U), KFSW_FWU_LITE_STATUS_INVALID);
	zassert_equal(status.state, KFSW_FWU_RECEIVING);
}

ZTEST(services_fwu_lite, test_explicit_verify_then_start_replay)
{
	complete();
	zassert_equal(status.state, KFSW_FWU_RECEIVING);
	control(KFSW_FWU_LITE_OP_START_FLASHING);
	zassert_equal(ask(16U), KFSW_FWU_LITE_STATUS_INVALID);
	control(KFSW_FWU_LITE_OP_VERIFY);
	zassert_equal(ask(16U), KFSW_FWU_LITE_STATUS_OK);
	zassert_equal(status.state, KFSW_FWU_VERIFIED);
	zassert_equal(status.committed, sizeof(image));
	zassert_equal(ask(16U), KFSW_FWU_LITE_STATUS_OK);
	control(KFSW_FWU_LITE_OP_START_FLASHING);
	zassert_equal(ask(16U), KFSW_FWU_LITE_STATUS_OK);
	zassert_equal(status.state, KFSW_FWU_READY);
	zassert_equal(ask(16U), KFSW_FWU_LITE_STATUS_OK);
}

ZTEST(services_fwu_lite, test_abort_tombstone_and_successor)
{
	begin();
	struct kfsw_fwu_lite_message old = request;
	control(KFSW_FWU_LITE_OP_ABORT);
	zassert_equal(ask(16U), KFSW_FWU_LITE_STATUS_OK);
	zassert_equal(ask(16U), KFSW_FWU_LITE_STATUS_OK);
	struct kfsw_fwu_lite_message abort = request;
	request = old;
	request.generation = 0U;
	request.opcode = KFSW_FWU_LITE_OP_BEGIN;
	request.transfer_id = ++transfer;
	request.data = descriptor;
	request.data_size = sizeof(descriptor);
	begin();
	block(0U, BLOCK);
	zassert_equal(ask(16U), KFSW_FWU_LITE_STATUS_OK);
	request = abort;
	zassert_equal(ask(16U), KFSW_FWU_LITE_STATUS_STALE);
	zassert_ok(kfsw_fwu_get_status(&status));
	zassert_equal(status.received, BLOCK);
}

ZTEST(services_fwu_lite, test_begin_erase_failure_retains_owner)
{
	erase_error = -EIO;
	zassert_equal(ask(16U), KFSW_FWU_LITE_STATUS_FAILED);
	zassert_ok(kfsw_fwu_get_status(&status));
	zassert_equal(status.state, KFSW_FWU_FAILED);
	zassert_equal(status.handle.owner.peer, 16U);
}

ZTEST(services_fwu_lite, test_aborted_duplicate_never_claims_acceptance)
{
	begin();
	block(0U, BLOCK);
	zassert_equal(ask(16U), KFSW_FWU_LITE_STATUS_OK);
	struct kfsw_fwu_lite_message accepted = request;
	control(KFSW_FWU_LITE_OP_ABORT);
	zassert_equal(ask(16U), KFSW_FWU_LITE_STATUS_OK);
	request = accepted;
	zassert_equal(ask(16U), KFSW_FWU_LITE_STATUS_INVALID);
	zassert_equal(status.received, 0U);
}

ZTEST(services_fwu_lite, test_mutation_rejects_trailing_fields)
{
	begin();
	control(KFSW_FWU_LITE_OP_ABORT);
	request.offset = 1U;
	zassert_equal(ask(16U), KFSW_FWU_LITE_STATUS_INVALID);
	zassert_ok(kfsw_fwu_get_status(&status));
	zassert_equal(status.state, KFSW_FWU_RECEIVING);
	request.offset = 0U;
	request.data = image;
	request.data_size = 1U;
	zassert_equal(ask(16U), KFSW_FWU_LITE_STATUS_INVALID);
}

ZTEST(services_fwu_lite, test_ftp_owner_blocks_lite)
{
	struct kfsw_fwu_owner owner = {.kind = KFSW_FWU_OWNER_FTP, .peer = 16U};
	struct kfsw_fwu_image other = {.size = sizeof(image)};
	struct kfsw_fwu_handle handle = {0};
	zassert_ok(kfsw_fwu_session_begin(&owner, &other, 0U, &handle, &status));
	zassert_equal(ask(16U), KFSW_FWU_LITE_STATUS_BUSY);
	zassert_ok(kfsw_fwu_get_status(&status));
	zassert_equal(status.handle.owner.kind, KFSW_FWU_OWNER_FTP);
}

ZTEST(services_fwu_lite, test_null_arguments)
{
	size_t size;
	uint8_t wire[KFSW_FWU_LITE_WIRE_SIZE];
	zassert_equal(kfsw_fwu_lite_encode(NULL, wire, sizeof(wire), &size), -EINVAL);
	zassert_equal(kfsw_fwu_lite_decode(NULL, 0U, &reply), -EINVAL);
	zassert_equal(kfsw_fwu_lite_handle_peer(16U, NULL, &reply, receipt), -EINVAL);
	zassert_equal(kfsw_fwu_session_lookup(NULL, &status), -EINVAL);
}

ZTEST_SUITE(services_fwu_lite, NULL, NULL, before, NULL, NULL);
