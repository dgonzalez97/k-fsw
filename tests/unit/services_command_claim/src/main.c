/*
 * Copyright (c) 2026 K-FSW
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include <kfsw/services/command.h>

#define OTHER_STACK_SIZE 2048
#define OTHER_PRIORITY 5

enum {
	ID_OPEN = 1,
	ID_BLOCK,
	ID_NESTED,
};

static atomic_t open_calls;
static K_SEM_DEFINE(handler_entered, 0, 1);
static K_SEM_DEFINE(handler_release, 0, 1);
static k_timeout_t handler_wait;

static int handler_open(const struct kfsw_command_arg *args, size_t arg_count,
			const struct kfsw_command_source *source,
			struct kfsw_command_result *result)
{
	ARG_UNUSED(args);
	ARG_UNUSED(arg_count);
	ARG_UNUSED(source);

	atomic_inc(&open_calls);
	result->status = KFSW_COMMAND_OK;
	return 0;
}

static int handler_block(const struct kfsw_command_arg *args, size_t arg_count,
			 const struct kfsw_command_source *source,
			 struct kfsw_command_result *result)
{
	ARG_UNUSED(args);
	ARG_UNUSED(arg_count);
	ARG_UNUSED(source);

	k_sem_give(&handler_entered);
	(void)k_sem_take(&handler_release, handler_wait);
	result->status = KFSW_COMMAND_OK;
	return 0;
}

static int handler_nested(const struct kfsw_command_arg *args, size_t arg_count,
			  const struct kfsw_command_source *source,
			  struct kfsw_command_result *result)
{
	int outcome = kfsw_command_invoke("open_cmd", NULL, 0U, result);

	if (outcome != 0) {
		return outcome;
	}
	return handler_block(args, arg_count, source, result);
}

static const struct kfsw_command_definition commands[] = {
	{
		.id = ID_OPEN,
		.name = "open_cmd",
		.help = "Reachable from anywhere.",
		.handler = handler_open,
	},
	{
		.id = ID_BLOCK,
		.name = "block_cmd",
		.handler = handler_block,
	},
	{
		.id = ID_NESTED,
		.name = "nested_cmd",
		.handler = handler_nested,
	},
};

static const struct kfsw_command_definition_set definitions = {
	.commands = commands,
	.count = ARRAY_SIZE(commands),
};

static void *suite_setup(void)
{
	const struct kfsw_command_definition_set *const sets[] = {&definitions};

	zassert_equal(kfsw_command_init(sets, ARRAY_SIZE(sets)), 0, "registry did not freeze");
	return NULL;
}

static void suite_before(void *fixture)
{
	ARG_UNUSED(fixture);

	(void)kfsw_command_claim_release();
	atomic_clear(&open_calls);
	k_sem_reset(&handler_entered);
	k_sem_reset(&handler_release);
	handler_wait = K_SECONDS(2);
}

static int invoke(uint16_t id, const struct kfsw_command_source *source,
		  struct kfsw_command_result *result)
{
	memset(result, 0, sizeof(*result));
	return kfsw_command_invoke_id(id, NULL, 0U, source, result);
}

ZTEST(command_claim, test_a_command_from_another_node_reaches_its_handler)
{
	const struct kfsw_command_source remote = {.node = 7U, .via_csp = true};
	struct kfsw_command_result result;

	zassert_equal(invoke(ID_OPEN, &remote, &result), 0,
		      "a request from another node was refused");
	zassert_equal(atomic_get(&open_calls), 1, "the handler did not run");
}

static K_THREAD_STACK_DEFINE(other_stack, OTHER_STACK_SIZE);
static struct k_thread other_thread;
static int other_outcome;
static struct kfsw_command_result other_result;

static void other_entry(void *a, void *b, void *c)
{
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	other_outcome = invoke((uint16_t)POINTER_TO_UINT(a), NULL, &other_result);
}

static k_tid_t start_other_command(uint16_t id)
{
	return k_thread_create(&other_thread, other_stack, OTHER_STACK_SIZE, other_entry,
			       UINT_TO_POINTER(id), NULL, NULL, OTHER_PRIORITY, 0, K_NO_WAIT);
}

static void run_other_thread(void)
{
	k_tid_t tid = start_other_command(ID_OPEN);

	zassert_equal(k_thread_join(tid, K_SECONDS(5)), 0, "the second thread did not finish");
}

ZTEST(command_claim, test_shared_claim_serves_another_thread_and_counts_it)
{
	struct kfsw_command_claim_state state;

	zassert_equal(kfsw_command_claim_acquire(KFSW_COMMAND_CLAIM_SHARED, "procedure"), 0,
		      "the claim was refused");
	run_other_thread();
	zassert_equal(other_outcome, 0, "a shared claim refused another thread");
	zassert_equal(atomic_get(&open_calls), 1, "the handler did not run for the other thread");

	zassert_equal(kfsw_command_claim_get(&state), 0, "the claim could not be read");
	zassert_true(state.held, "the claim was not reported as held");
	zassert_equal(state.mode, KFSW_COMMAND_CLAIM_SHARED, "the mode was not reported");
	zassert_equal(state.intrusions, 1U, "the intrusion was not counted");
	zassert_equal(state.refusals, 0U, "a shared claim counted a refusal");
	zassert_equal(strcmp(state.owner, "procedure"), 0, "the owner was not reported: %s",
		      state.owner);
}

ZTEST(command_claim, test_exclusive_claim_refuses_another_thread_and_names_the_holder)
{
	struct kfsw_command_claim_state state;

	zassert_equal(kfsw_command_claim_acquire(KFSW_COMMAND_CLAIM_EXCLUSIVE, "procedure"), 0,
		      "the claim was refused");
	run_other_thread();
	zassert_equal(other_outcome, -EBUSY, "an exclusive claim served another thread");
	zassert_equal(other_result.status, KFSW_COMMAND_BUSY, "the refusal did not report BUSY");
	zassert_not_null(strstr(other_result.detail, "procedure"),
			 "the refusal does not name the holder: %s", other_result.detail);
	zassert_equal(atomic_get(&open_calls), 0, "the handler ran despite the refusal");

	zassert_equal(kfsw_command_claim_get(&state), 0, "the claim could not be read");
	zassert_equal(state.refusals, 1U, "the refusal was not counted");
	zassert_equal(state.intrusions, 0U, "an exclusive claim counted an intrusion");
}

ZTEST(command_claim, test_holder_is_unaffected_by_its_own_claim)
{
	struct kfsw_command_result result;

	zassert_equal(kfsw_command_claim_acquire(KFSW_COMMAND_CLAIM_EXCLUSIVE, "procedure"), 0,
		      "the claim was refused");
	zassert_equal(invoke(ID_OPEN, NULL, &result), 0, "the holder refused itself");
	zassert_equal(atomic_get(&open_calls), 1, "the holder's invocation did not run");
}

ZTEST(command_claim, test_release_reopens_the_path)
{
	struct kfsw_command_claim_state state;

	zassert_equal(kfsw_command_claim_acquire(KFSW_COMMAND_CLAIM_EXCLUSIVE, "procedure"), 0,
		      "the claim was refused");
	zassert_equal(kfsw_command_claim_release(), 0, "the holder could not release");
	zassert_equal(kfsw_command_claim_get(&state), 0, "the claim could not be read");
	zassert_false(state.held, "the claim is still held after release");

	run_other_thread();
	zassert_equal(other_outcome, 0, "a released claim still refused another thread");
}

ZTEST(command_claim, test_a_second_thread_cannot_take_a_held_claim)
{
	zassert_equal(kfsw_command_claim_acquire(KFSW_COMMAND_CLAIM_SHARED, "procedure"), 0,
		      "the claim was refused");
	zassert_equal(kfsw_command_claim_acquire(KFSW_COMMAND_CLAIM_EXCLUSIVE, "procedure"), 0,
		      "the holder could not change the mode");
	zassert_equal(kfsw_command_claim_acquire(KFSW_COMMAND_CLAIM_SHARED, ""), -EINVAL,
		      "an empty owner was accepted");
}

ZTEST(command_claim, test_blocked_handler_bounds_admission_and_recovers)
{
	struct kfsw_command_result result;
	struct kfsw_command_stats before;
	struct kfsw_command_stats after;
	k_tid_t tid = start_other_command(ID_BLOCK);

	zassert_equal(k_sem_take(&handler_entered, K_SECONDS(1)), 0,
		      "blocking handler did not start");
	zassert_equal(kfsw_command_get_stats(&before), 0);
	int64_t started = k_uptime_get();
	int outcome = kfsw_command_invoke("open_cmd", NULL, 0U, &result);
	int64_t elapsed = k_uptime_get() - started;

	zassert_equal(kfsw_command_get_stats(&after), 0);
	k_sem_give(&handler_release);
	zassert_equal(k_thread_join(tid, K_SECONDS(5)), 0);
	zassert_equal(outcome, -EBUSY, "a blocked handler did not refuse admission");
	zassert_equal(result.status, KFSW_COMMAND_BUSY);
	zassert_equal(strcmp(result.detail, "open_cmd refused while block_cmd runs"), 0);
	zassert_true(elapsed >= CONFIG_KFSW_COMMAND_ADMISSION_MS, "admission refused too soon");
	zassert_true(elapsed <= CONFIG_KFSW_COMMAND_ADMISSION_MS + k_ticks_to_ms_ceil64(1),
		     "admission took %lld ms", elapsed);
	zassert_equal(atomic_get(&open_calls), 0, "refused handler ran");
	zassert_equal(after.invoked, before.invoked + 1U);
	zassert_equal(after.failed, before.failed + 1U);
	zassert_equal(other_outcome, 0);
	zassert_equal(other_result.status, KFSW_COMMAND_OK);
	zassert_equal(invoke(ID_OPEN, NULL, &result), 0);
	zassert_equal(result.status, KFSW_COMMAND_OK);
	zassert_equal(atomic_get(&open_calls), 1, "handler did not recover");
}

ZTEST(command_claim, test_handler_released_within_budget_allows_admission)
{
	struct kfsw_command_result result;

	handler_wait = K_MSEC(CONFIG_KFSW_COMMAND_ADMISSION_MS / 4);
	k_tid_t tid = start_other_command(ID_BLOCK);

	zassert_equal(k_sem_take(&handler_entered, K_SECONDS(1)), 0);
	int64_t started = k_uptime_get();
	int outcome = invoke(ID_OPEN, NULL, &result);
	int64_t elapsed = k_uptime_get() - started;

	zassert_equal(k_thread_join(tid, K_SECONDS(5)), 0);
	zassert_equal(outcome, 0, "admission did not wait for the short handler");
	zassert_equal(result.status, KFSW_COMMAND_OK);
	zassert_equal(other_outcome, 0);
	zassert_equal(atomic_get(&open_calls), 1);
	zassert_true(elapsed >= CONFIG_KFSW_COMMAND_ADMISSION_MS / 4);
	zassert_true(elapsed <= CONFIG_KFSW_COMMAND_ADMISSION_MS + k_ticks_to_ms_ceil64(1));
}

ZTEST(command_claim, test_nested_invocation_restores_running_command)
{
	struct kfsw_command_result result;
	k_tid_t tid = start_other_command(ID_NESTED);

	zassert_equal(k_sem_take(&handler_entered, K_SECONDS(1)), 0,
		      "nested invocation did not return");
	int outcome = invoke(ID_OPEN, NULL, &result);

	k_sem_give(&handler_release);
	zassert_equal(k_thread_join(tid, K_SECONDS(5)), 0);
	zassert_equal(other_outcome, 0, "nested invocation was refused");
	zassert_equal(atomic_get(&open_calls), 1);
	zassert_equal(outcome, -EBUSY);
	zassert_equal(result.status, KFSW_COMMAND_BUSY);
	zassert_equal(strcmp(result.detail, "open_cmd refused while nested_cmd runs"), 0);
}

ZTEST_SUITE(command_claim, NULL, suite_setup, suite_before, NULL, NULL);
