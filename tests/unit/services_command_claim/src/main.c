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
};

static atomic_t open_calls;

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

static const struct kfsw_command_definition commands[] = {
	{
		.id = ID_OPEN,
		.name = "open_cmd",
		.help = "Reachable from anywhere.",
		.handler = handler_open,
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
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	other_outcome = invoke(ID_OPEN, NULL, &other_result);
}

static void run_other_thread(void)
{
	k_tid_t tid = k_thread_create(&other_thread, other_stack, OTHER_STACK_SIZE, other_entry,
				      NULL, NULL, NULL, OTHER_PRIORITY, 0, K_NO_WAIT);

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

ZTEST_SUITE(command_claim, NULL, suite_setup, suite_before, NULL, NULL);
