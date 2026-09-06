#include <errno.h>
#include <time.h>
#include <stdint.h>
#include <stdlib.h>

#include <zephyr/shell/shell.h>
#include <zephyr/shell/shell_string_conv.h>
#include <zephyr/sys/util.h>

#include <kfsw/comms/csp.h>
#if CONFIG_KFSW_COMMAND
#include <kfsw/services/command.h>
#endif

#define KFSW_CSP_PING_TIMEOUT_MS 1000U
#define KFSW_CSP_PING_PAYLOAD_SIZE 10U

static void print_clock(const struct shell *sh, const char *label,
			const struct kfsw_csp_clock *clock)
{
	struct tm broken = {0};
	time_t seconds = (time_t)clock->seconds;
	char text[32];

	if (!kfsw_csp_clock_is_set(clock)) {
		/* Said plainly, and including the reading itself: an operator
		 * looking at a node stuck in the year 2000 should be able to
		 * see that is what happened.
		 */
		shell_print(sh, "%s: not set (reads %" PRId32 ")", label, clock->seconds);
		return;
	}

	(void)gmtime_r(&seconds, &broken);
	(void)strftime(text, sizeof(text), "%Y-%m-%d %H:%M:%S", &broken);
	shell_print(sh, "%s: %s UTC (%" PRId32 ".%09" PRIu32 ")", label, text, clock->seconds,
		    clock->nanoseconds);
}

static int cmd_csp_info(const struct shell *sh, size_t argc, char **argv)
{
	struct kfsw_csp_info info;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	kfsw_csp_get_info(&info);
	shell_print(sh, "CSP node: %u", info.address);
	shell_print(sh, "hostname: %s", info.hostname);
	shell_print(sh, "model: %s", info.model);
	shell_print(sh, "revision: %s", info.revision);
	shell_print(sh, "date: %s %s", info.build_date, info.build_time);
	shell_print(sh, "free_buffers: %zu", info.free_buffers);

	return 0;
}

static bool print_csp_interface(const struct kfsw_csp_interface_info *interface_info, void *context)
{
	const struct shell *sh = context;

	shell_print(sh,
		    "%s addr=%u/%u default=%s tx=%u rx=%u txerr=%u "
		    "rxerr=%u drop=%u",
		    interface_info->name, interface_info->address, interface_info->prefix_length,
		    interface_info->is_default ? "yes" : "no", interface_info->tx_packets,
		    interface_info->rx_packets, interface_info->tx_errors,
		    interface_info->rx_errors, interface_info->dropped_packets);

	return true;
}

static int cmd_csp_interfaces(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	kfsw_csp_visit_interfaces(print_csp_interface, (void *)sh);
	return 0;
}

static bool print_csp_route(const struct kfsw_csp_route_info *route_info, void *context)
{
	const struct shell *sh = context;

	if (route_info->has_via) {
		shell_print(sh, "%u/%u -> %s via %u", route_info->address,
			    route_info->prefix_length, route_info->interface_name, route_info->via);
	} else {
		shell_print(sh, "%u/%u -> %s direct", route_info->address,
			    route_info->prefix_length, route_info->interface_name);
	}

	return true;
}

static int cmd_csp_routes(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	kfsw_csp_visit_routes(print_csp_route, (void *)sh);
	return 0;
}

static int cmd_csp_ping(const struct shell *sh, size_t argc, char **argv)
{
	struct kfsw_csp_info info;
	unsigned long node;
	uint32_t round_trip_ms;
	int parse_error = 0;
	int result;

	/* No node means this one. The address comes from the running CSP
	 * configuration rather than the build option, so it is whatever the
	 * node actually came up as.
	 */
	kfsw_csp_get_info(&info);

	if (argc < 2U) {
		node = info.address;
		shell_print(sh, "No node given; using this node (%lu)", node);
	} else {
		node = shell_strtoul(argv[1], 10, &parse_error);
		if (parse_error != 0 || node > 16383U) {
			shell_error(sh, "CSP node must be in range 0..16383");
			return -EINVAL;
		}
	}

	/* A node pinging itself has no link to traverse: reaching the shell at
	 * all is the answer. Saying so is honest, where reporting a round-trip
	 * time would invent a measurement of nothing. Loopback traffic is also
	 * where this libcsp loses the source address, so a real self-ping never
	 * completes.
	 */
	if ((uint16_t)node == info.address) {
		shell_print(sh, "CSP ping %lu: this node, no link traversed", node);
		return 0;
	}

	result = kfsw_csp_ping((uint16_t)node, KFSW_CSP_PING_TIMEOUT_MS, KFSW_CSP_PING_PAYLOAD_SIZE,
			       &round_trip_ms);
	if (result != 0) {
		shell_error(sh, "CSP ping %lu: failed (%d)", node, result);
		return result;
	}

	shell_print(sh, "CSP ping %lu: success, rtt_ms=%u", node, round_trip_ms);
	return 0;
}

static int cmd_csp_ident(const struct shell *sh, size_t argc, char **argv)
{
	struct kfsw_csp_identity identity;
	struct kfsw_csp_clock clock = {0};
	struct kfsw_csp_info info;
	unsigned long node;
	char *end = NULL;
	int result;

	/* Asking this node who it is needs no network. Answering locally keeps
	 * the question working on a node whose links are all down, which is
	 * exactly when an operator is most likely to be asking it.
	 */
	if (argc < 2U) {
		kfsw_csp_get_info(&info);
		shell_print(sh, "CSP ident %u (this node)", info.address);
		shell_print(sh, "hostname: %s", info.hostname);
		shell_print(sh, "model: %s", info.model);
		shell_print(sh, "revision: %s", info.revision);
		shell_print(sh, "built: %s %s", info.build_date, info.build_time);
		kfsw_csp_clock_get(&clock);
		print_clock(sh, "clock", &clock);
		return 0;
	}

	node = strtoul(argv[1], &end, 0);
	if ((end == argv[1]) || (*end != '\0') || (node > 16383UL)) {
		shell_error(sh, "Invalid node: %s", argv[1]);
		return -EINVAL;
	}

	result = kfsw_csp_identify((uint16_t)node, KFSW_CSP_PING_TIMEOUT_MS, &identity);
	if (result != 0) {
		shell_error(sh, "CSP ident %lu: failed (%d)", node, result);
		return result;
	}

	shell_print(sh, "CSP ident %lu", node);
	shell_print(sh, "hostname: %s", identity.hostname);
	shell_print(sh, "model: %s", identity.model);
	shell_print(sh, "revision: %s", identity.revision);
	shell_print(sh, "built: %s %s", identity.date, identity.time);

	/* A second exchange, because the identity reply has no room for a
	 * clock and the wire format is libcsp's rather than ours. Asked here so
	 * one question answers both halves of "what are you and when do you
	 * think it is", which is what an operator actually wants to know before
	 * trusting a timestamp in a downlink.
	 */
	if (kfsw_csp_clock_read((uint16_t)node, KFSW_CSP_PING_TIMEOUT_MS, &clock) == 0) {
		print_clock(sh, "clock", &clock);
	} else {
		shell_print(sh, "clock: no answer");
	}
	return 0;
}

#if CONFIG_KFSW_COMMAND && CONFIG_REBOOT
/* Thin, like every adapter here: it parses, hands the pin to the command
 * service and prints. The pin is checked on the node that would restart, not
 * here, because a guard applied by the caller guards nothing.
 */
static int cmd_csp_reboot(const struct shell *sh, size_t argc, char **argv)
{
	struct kfsw_command_result result = {0};
	struct kfsw_command_arg args[1];
	unsigned long node;
	char *end;
	int outcome;

	node = strtoul(argv[1], &end, 0);
	if ((end == argv[1]) || (*end != '\0') || (node > 16383UL)) {
		shell_error(sh, "Invalid node: %s", argv[1]);
		return -EINVAL;
	}

	args[0].type = KFSW_COMMAND_TYPE_TEXT;
	args[0].value.text = argv[2];

	outcome = kfsw_command_invoke_remote((uint16_t)node, "reboot", args, ARRAY_SIZE(args),
					     &result);
	if (outcome != 0) {
		shell_error(sh, "reboot node=%lu: %d", node, outcome);
		return outcome;
	}
	if (result.status != KFSW_COMMAND_OK) {
		shell_error(sh, "reboot node=%lu: %s%s%s", node,
			    kfsw_command_status_name(result.status),
			    (result.detail[0] != '\0') ? " " : "", result.detail);
		return -EACCES;
	}
	shell_print(sh, "reboot node=%lu: OK %s", node, result.detail);
	return 0;
}
#endif

/* Reads a clock, or hands one over. Which of the two depends on the arguments,
 * because they are the same exchange on the wire: the far side answers with
 * what it has either way.
 */
static int cmd_csp_clock(const struct shell *sh, size_t argc, char **argv)
{
	struct kfsw_csp_clock clock = {0};
	unsigned long node;
	char *end;
	int result;

	if (argc < 2U) {
		kfsw_csp_clock_get(&clock);
		print_clock(sh, "clock", &clock);
		return 0;
	}

	/* `set` first, because a node has to be given the time before it can
	 * pass it on, and nothing on a board knows the date at power-on.
	 */
	if (strcmp(argv[1], "set") == 0) {
		long long seconds;

		if (argc < 3U) {
			shell_error(sh, "Usage: csp clock set <seconds since 1970 UTC>");
			return -EINVAL;
		}
		seconds = strtoll(argv[2], &end, 0);
		if ((end == argv[2]) || (*end != '\0') || (seconds <= 0LL) ||
		    (seconds > (long long)INT32_MAX)) {
			shell_error(sh, "Invalid time: %s", argv[2]);
			return -EINVAL;
		}
		clock.seconds = (int32_t)seconds;
		clock.nanoseconds = 0U;

		result = kfsw_csp_clock_set(&clock);
		if (result != 0) {
			shell_error(sh, "clock set: %d", result);
			return result;
		}
		kfsw_csp_clock_get(&clock);
		print_clock(sh, "clock", &clock);
		return 0;
	}

	node = strtoul(argv[1], &end, 0);
	if ((end == argv[1]) || (*end != '\0') || (node > 16383UL)) {
		shell_error(sh, "Invalid node: %s", argv[1]);
		return -EINVAL;
	}

	if ((argc > 2U) && (strcmp(argv[2], "sync") == 0)) {
		struct kfsw_csp_clock mine = {0};

		kfsw_csp_clock_get(&mine);
		if (!kfsw_csp_clock_is_set(&mine)) {
			shell_error(sh, "This node's clock is not set; nothing to hand over");
			return -EINVAL;
		}
		clock = mine;
		result = kfsw_csp_clock_write((uint16_t)node, KFSW_CSP_PING_TIMEOUT_MS, &clock);
		if (result != 0) {
			shell_error(sh, "clock node=%lu: %d", node, result);
			return result;
		}
		/* What came back is the node's own reading after the write, so
		 * this reports what landed rather than what was sent.
		 */
		print_clock(sh, "clock", &clock);
		return 0;
	}

	result = kfsw_csp_clock_read((uint16_t)node, KFSW_CSP_PING_TIMEOUT_MS, &clock);
	if (result != 0) {
		shell_error(sh, "clock node=%lu: %d", node, result);
		return result;
	}
	print_clock(sh, "clock", &clock);
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(csp_commands,
	SHELL_CMD_ARG(clock, NULL,
		      "Time: clock, clock set <utc>, clock <node>, clock <node> sync.",
		      cmd_csp_clock, 1, 2),
	SHELL_CMD_ARG(ident, NULL, "Identify a node, or this one when no node is given.",
		      cmd_csp_ident, 1, 1),
	SHELL_CMD_ARG(info, NULL, "Show local CSP identity and router state.", cmd_csp_info, 1, 0),
	SHELL_CMD_ARG(interfaces, NULL, "Show registered CSP interfaces.", cmd_csp_interfaces, 1,
		      0),
	SHELL_CMD_ARG(ping, NULL, "Ping a node, or this one when no node is given.", cmd_csp_ping,
		      1, 1),
#if CONFIG_KFSW_COMMAND && CONFIG_REBOOT
	SHELL_CMD_ARG(reboot, NULL, "Restart a node: reboot <node> <pin>.", cmd_csp_reboot, 3,
		      0),
#endif
	SHELL_CMD_ARG(routes, NULL, "Show the CSP static routing table.", cmd_csp_routes, 1, 0),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(csp, &csp_commands, "K-FSW CSP commands.", NULL);
