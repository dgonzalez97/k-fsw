#include <errno.h>
#include <string.h>
#include <zephyr/ztest.h>
#include "command_retry.h"

static struct kfsw_command_retry_cache cache;
static unsigned int invoked;
static int handler_outcome;
static enum kfsw_command_status handler_status;
static K_SEM_DEFINE(other_ready, 0, 1);
static K_SEM_DEFINE(other_release, 0, 1);
static K_THREAD_STACK_DEFINE(other_stack, 2048);
static struct k_thread other_thread;
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
	out->status = args[0].value.u32 == 7U ? handler_status : KFSW_COMMAND_FAILED;
	strcpy(out->detail, "called");
	return handler_outcome;
}

static int blocking_handler(const struct kfsw_command_arg *args, size_t count,
			    const struct kfsw_command_source *source,
			    struct kfsw_command_result *out)
{
	ARG_UNUSED(args);
	ARG_UNUSED(count);
	ARG_UNUSED(source);
	ARG_UNUSED(out);
	k_sem_give(&other_ready);
	return k_sem_take(&other_release, K_SECONDS(5));
}

static void other_entry(void *claim, void *unused_b, void *unused_c)
{
	struct kfsw_command_result out;

	ARG_UNUSED(unused_b);
	ARG_UNUSED(unused_c);
	if (claim != NULL) {
		zassert_ok(kfsw_command_claim_acquire(KFSW_COMMAND_CLAIM_EXCLUSIVE, "retry"));
		k_sem_give(&other_ready);
		zassert_ok(k_sem_take(&other_release, K_SECONDS(5)));
		zassert_ok(kfsw_command_claim_release());
	} else {
		zassert_ok(kfsw_command_invoke_id(2, NULL, 0, NULL, &out));
	}
}

static void *setup(void)
{
	static const enum kfsw_command_type types[] = {KFSW_COMMAND_TYPE_U32};
	static const struct kfsw_command_definition commands[] = {
		{.id = 1, .name = "count", .arg_count = 1, .arg_types = types, .handler = handler},
		{.id = 2, .name = "block", .handler = blocking_handler},
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
	handler_outcome = 0;
	handler_status = KFSW_COMMAND_OK;
	k_sem_reset(&other_ready);
	k_sem_reset(&other_release);
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

static void refused_then_retried(bool claim)
{
	prepare();
	k_tid_t tid =
		k_thread_create(&other_thread, other_stack, K_THREAD_STACK_SIZEOF(other_stack),
				other_entry, claim ? &cache : NULL, NULL, NULL, 5, 0, K_NO_WAIT);

	zassert_ok(k_sem_take(&other_ready, K_SECONDS(1)));
	dispatch(30, 0, 1);
	k_sem_give(&other_release);
	zassert_ok(k_thread_join(tid, K_SECONDS(5)));
	zassert_equal(result.status, KFSW_COMMAND_BUSY);
	zassert_equal(ticket, 100);
	zassert_equal(invoked, 0);
	zassert_false(cache.entries[0].complete);

	dispatch(30, 0, 2);
	zassert_equal(result.status, KFSW_COMMAND_OK);
	zassert_equal(ticket, 100);
	dispatch(30, 0, 3);
	zassert_equal(result.status, KFSW_COMMAND_OK);
	zassert_equal(invoked, 1);
}

ZTEST(command_retry, test_admission_refusal_keeps_ticket_retryable)
{
	refused_then_retried(false);
}

ZTEST(command_retry, test_claim_refusal_keeps_ticket_retryable)
{
	refused_then_retried(true);
}

static void handler_busy_stays_cached(int outcome)
{
	prepare();
	handler_outcome = outcome;
	handler_status = KFSW_COMMAND_BUSY;
	dispatch(30, 0, 1);
	zassert_equal(result.status, KFSW_COMMAND_BUSY);
	handler_outcome = 0;
	handler_status = KFSW_COMMAND_OK;
	dispatch(30, 0, 2);
	zassert_equal(result.status, KFSW_COMMAND_BUSY);
	zassert_str_equal(result.detail, "called");
	zassert_equal(invoked, 1);
}

ZTEST(command_retry, test_handler_busy_stays_cached)
{
	handler_busy_stays_cached(0);
}

ZTEST(command_retry, test_handler_ebusy_stays_cached)
{
	handler_busy_stays_cached(-EBUSY);
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
