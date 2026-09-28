#include <errno.h>
#include <string.h>
#include <zephyr/ztest.h>
#include "command_retry.h"

static struct kfsw_command_retry_cache cache;
static unsigned int invoked;
static struct kfsw_command_result result;
static uint64_t ticket;
static const uint8_t body[] = {KFSW_COMMAND_TYPE_U32, 0, 4, 0, 0, 0, 7};
static struct kfsw_command_message request;

static int handler(const struct kfsw_command_arg *args, size_t count,
		   const struct kfsw_command_source *source, struct kfsw_command_result *out)
{
	ARG_UNUSED(count);
	zassert_true(source->via_csp);
	invoked++;
	out->status = args[0].value.u32 == 7U ? KFSW_COMMAND_OK : KFSW_COMMAND_FAILED;
	strcpy(out->detail, "called");
	return 0;
}

static void *setup(void)
{
	static const enum kfsw_command_type types[] = {KFSW_COMMAND_TYPE_U32};
	static const struct kfsw_command_definition commands[] = {
		{.id = 1, .name = "count", .arg_count = 1, .arg_types = types, .handler = handler},
	};
	static const struct kfsw_command_definition_set definitions = {
		.commands = commands,
		.count = ARRAY_SIZE(commands),
	};
	const struct kfsw_command_definition_set *sets[] = {&definitions};

	zassert_ok(kfsw_command_init(sets, ARRAY_SIZE(sets)));
	return NULL;
}

static void before(void *unused)
{
	ARG_UNUSED(unused);
	memset(&cache, 0, sizeof(cache));
	invoked = 0;
	request = (struct kfsw_command_message){.version = 2,
						.opcode = KFSW_COMMAND_OP_PREPARE,
						.command_id = 1,
						.request_id = 65535,
						.token = 10,
						.arg_count = 1,
						.payload_size = sizeof(body),
						.payload = body};
}

static void dispatch(uint16_t source, uint64_t fresh, int64_t now)
{
	kfsw_command_retry_dispatch(&cache, source, &request, fresh, now, &ticket, &result);
}

static void prepare(void)
{
	dispatch(30, 100, 0);
	zassert_equal(result.status, KFSW_COMMAND_OK);
	zassert_equal(ticket, 100);
	zassert_equal(invoked, 0);
	request.opcode = KFSW_COMMAND_OP_EXECUTE;
	request.token = ticket;
}

ZTEST(command_retry, test_lost_result_does_not_repeat_handler)
{
	prepare();
	dispatch(30, 0, 1);
	zassert_equal(result.status, KFSW_COMMAND_OK);
	dispatch(30, 0, 2);
	zassert_equal(result.status, KFSW_COMMAND_OK);
	zassert_equal(invoked, 1);
	zassert_str_equal(result.detail, "called");
}

ZTEST(command_retry, test_prepare_retry_keeps_ticket_and_deadline)
{
	dispatch(30, 100, 0);
	dispatch(30, 200, 999);
	zassert_equal(ticket, 100);
	request.opcode = KFSW_COMMAND_OP_EXECUTE;
	request.token = ticket;
	dispatch(30, 0, 1000);
	zassert_equal(result.status, KFSW_COMMAND_UNAVAILABLE);
	zassert_equal(invoked, 0);
}

ZTEST(command_retry, test_expired_completed_ticket_never_runs_again)
{
	prepare();
	dispatch(30, 0, 1);
	dispatch(30, 0, 1000);
	zassert_equal(result.status, KFSW_COMMAND_UNAVAILABLE);
	zassert_equal(invoked, 1);
}

ZTEST(command_retry, test_restart_refuses_old_ticket)
{
	prepare();
	dispatch(30, 0, 1);
	memset(&cache, 0, sizeof(cache));
	dispatch(30, 0, 0);
	zassert_equal(result.status, KFSW_COMMAND_UNAVAILABLE);
	zassert_equal(invoked, 1);
}

ZTEST(command_retry, test_changed_body_and_wrong_peer_do_not_execute)
{
	uint8_t changed[sizeof(body)];

	prepare();
	dispatch(31, 0, 1);
	zassert_equal(result.status, KFSW_COMMAND_UNAVAILABLE);
	memcpy(changed, body, sizeof(body));
	changed[6] = 8;
	request.payload = changed;
	dispatch(30, 0, 1);
	zassert_equal(result.status, KFSW_COMMAND_INVALID_ARGUMENT);
	request.payload = body;
	request.command_id = 2;
	dispatch(30, 0, 1);
	zassert_equal(result.status, KFSW_COMMAND_INVALID_ARGUMENT);
	zassert_equal(invoked, 0);
}

ZTEST(command_retry, test_two_peers_and_full_cache_preserve_active_results)
{
	dispatch(30, 100, 0);
	dispatch(31, 200, 0);
	zassert_equal(result.status, KFSW_COMMAND_OK);
	dispatch(32, 300, 0);
	zassert_equal(result.status, KFSW_COMMAND_BUSY);
	request.opcode = KFSW_COMMAND_OP_EXECUTE;
	request.token = 100;
	dispatch(30, 0, 1);
	request.token = 200;
	dispatch(31, 0, 1);
	zassert_equal(invoked, 2);
}

ZTEST(command_retry, test_nonce_separates_wrapped_ids_and_client_restart)
{
	prepare();
	dispatch(30, 0, 1);
	request.opcode = KFSW_COMMAND_OP_PREPARE;
	request.token = 11;
	request.request_id = 0;
	dispatch(30, 101, 2);
	zassert_equal(result.status, KFSW_COMMAND_OK);
	request.opcode = KFSW_COMMAND_OP_EXECUTE;
	request.token = ticket;
	dispatch(30, 0, 3);
	zassert_equal(invoked, 2);
}

ZTEST(command_retry, test_entropy_failure_and_collision_do_not_reserve)
{
	dispatch(30, 0, 0);
	zassert_equal(result.status, KFSW_COMMAND_UNAVAILABLE);
	dispatch(30, 100, 0);
	request.token = 11;
	dispatch(31, 100, 0);
	zassert_equal(result.status, KFSW_COMMAND_UNAVAILABLE);
	zassert_equal(invoked, 0);
}

ZTEST(command_retry, test_wire_versions_bounds_and_reserved_bytes)
{
	uint8_t buffer[256];
	size_t size;
	struct kfsw_command_message decoded;

	zassert_ok(kfsw_command_protocol_encode(buffer, sizeof(buffer), &request, &size));
	zassert_equal(size, 20 + sizeof(body));
	zassert_ok(kfsw_command_protocol_decode(buffer, size, &decoded));
	zassert_equal(decoded.token, 10);
	zassert_equal(decoded.version, 2);
	zassert_equal(kfsw_command_protocol_decode(buffer, 19, &decoded), -EMSGSIZE);
	buffer[10] = 1;
	zassert_equal(kfsw_command_protocol_decode(buffer, size, &decoded), -EBADMSG);
	request.version = 1;
	request.opcode = KFSW_COMMAND_OP_REQUEST;
	zassert_ok(kfsw_command_protocol_encode(buffer, sizeof(buffer), &request, &size));
	zassert_equal(size, 12 + sizeof(body));
	zassert_ok(kfsw_command_protocol_decode(buffer, size, &decoded));
	zassert_equal(decoded.version, 1);
	buffer[1] = KFSW_COMMAND_OP_EXECUTE;
	zassert_equal(kfsw_command_protocol_decode(buffer, size, &decoded), -ENOTSUP);
}

ZTEST_SUITE(command_retry, NULL, setup, before, NULL, NULL);
