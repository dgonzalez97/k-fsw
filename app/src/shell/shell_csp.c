#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <zephyr/shell/shell.h>
#include <zephyr/shell/shell_string_conv.h>
#include <zephyr/sys/util.h>

#include <csp/csp_error.h>
#include <kfsw/comms/csp.h>
#if CONFIG_KFSW_COMMAND
#include <kfsw/services/command.h>

#include "shell_command.h"
#endif
#include "shell_remote.h"

/* libcsp's own timeout code, as the ping and identity exchanges return it. */
static int as_errno(int csp_result)
{
	return (csp_result == CSP_ERR_TIMEDOUT) ? -ETIMEDOUT : csp_result;
}

#define KFSW_CSP_PING_TIMEOUT_MS 1000U
#define KFSW_CSP_PING_PAYLOAD_SIZE 10U

static void print_clock(const struct shell *sh, const char *label,
			const struct kfsw_csp_clock *clock)
{
	struct tm broken = {0};
	time_t seconds = (time_t)clock->seconds;
	/* Wide enough for any int the fields could hold, not just a real date. */
	char text[72];

	if (!kfsw_csp_clock_is_set(clock)) {
		/* Print the raw reading as well. */
		shell_print(sh, "%s: not set (reads %" PRId32 ")", label, clock->seconds);
		return;
	}

	(void)gmtime_r(&seconds, &broken);
	/* Written out rather than through strftime, which costs 6.5 KB of flash
	 * with its tables for one fixed format.
	 */
	(void)snprintf(text, sizeof(text), "%04d-%02d-%02d %02d:%02d:%02d", broken.tm_year + 1900,
		       broken.tm_mon + 1, broken.tm_mday, broken.tm_hour, broken.tm_min,
		       broken.tm_sec);
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

	/* No node means this one, at its running address. */
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

	/* Pinging this node goes through the loopback interface. */
	result = kfsw_csp_ping((uint16_t)node, KFSW_CSP_PING_TIMEOUT_MS, KFSW_CSP_PING_PAYLOAD_SIZE,
			       &round_trip_ms);
	if (result != 0) {
		return kfsw_shell_remote_failed(sh, "csp ping", (uint16_t)node, as_errno(result));
	}

	shell_print(sh, "CSP ping %lu: success", node);
	shell_print(sh, "rtt_ms: %u", round_trip_ms);
	return 0;
}

static int cmd_csp_ifstat(const struct shell *sh, size_t argc, char **argv)
{
	struct kfsw_csp_interface_stats stats;
	int parse_error = 0;
	unsigned long node = shell_strtoul(argv[1], 10, &parse_error);
	int result;

	ARG_UNUSED(argc);
	if ((parse_error != 0) || (node > 16383U)) {
		shell_error(sh, "CSP node must be in range 0..16383");
		return -EINVAL;
	}
	result = kfsw_csp_interface_stats_read((uint16_t)node, argv[2], KFSW_CSP_PING_TIMEOUT_MS,
					       &stats);
	if (result != 0) {
		/* A node that carries no such interface does not answer either. */
		return kfsw_shell_remote_failed(sh, "csp ifstat", (uint16_t)node, result);
	}
	shell_print(sh, "CSP ifstat %lu %s", node, stats.name);
	shell_print(sh, "tx: %u", stats.tx_packets);
	shell_print(sh, "rx: %u", stats.rx_packets);
	shell_print(sh, "txerr: %u", stats.tx_errors);
	shell_print(sh, "rxerr: %u", stats.rx_errors);
	shell_print(sh, "drop: %u", stats.dropped_packets);
	shell_print(sh, "autherr: %u", stats.auth_errors);
	shell_print(sh, "frame: %u", stats.frame_errors);
	shell_print(sh, "txbytes: %u", stats.tx_bytes);
	shell_print(sh, "rxbytes: %u", stats.rx_bytes);
	shell_print(sh, "irq: %u", stats.interrupts);
	return 0;
}

static int cmd_csp_counters(const struct shell *sh, size_t argc, char **argv)
{
	struct kfsw_csp_counters counters;

	if (argc > 1U) {
		if (strcmp(argv[1], "clear") != 0) {
			shell_error(sh, "Invalid argument: %s (expected clear)", argv[1]);
			return -EINVAL;
		}
		kfsw_csp_clear_counters();
		shell_print(sh, "CSP counters cleared");
		return 0;
	}

	kfsw_csp_get_counters(&counters);
	shell_print(sh, "buffer_out=%u conn_out=%u conn_ovf=%u conn_noroute=%u invalid_reply=%u",
		    counters.buffer_out, counters.conn_out, counters.conn_overflow,
		    counters.conn_noroute, counters.invalid_reply);
	shell_print(sh, "last_error=%u (%s)", counters.last_error,
		    kfsw_csp_error_name(counters.last_error));
	shell_print(sh, "last_can_error=%u (%s)", counters.last_can_error,
		    kfsw_csp_can_error_name(counters.last_can_error));
	return 0;
}

/* The trace itself is in kfsw-comms. */
static int cmd_csp_debug(const struct shell *sh, size_t argc, char **argv)
{
	bool enabled;

	if (argc < 2U) {
		shell_print(sh, "CSP packet trace: %s", kfsw_csp_get_packet_trace() ? "on" : "off");
		return 0;
	}

	if (strcmp(argv[1], "on") == 0) {
		enabled = true;
	} else if (strcmp(argv[1], "off") == 0) {
		enabled = false;
	} else {
		shell_error(sh, "Invalid state: %s (expected on or off)", argv[1]);
		return -EINVAL;
	}

	kfsw_csp_set_packet_trace(enabled);
	shell_print(sh, "CSP packet trace: %s", enabled ? "on" : "off");
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

	/* This node's identity is answered locally, without the network. */
	if (argc < 2U) {
		kfsw_csp_get_info(&info);
		shell_print(sh, "CSP ident %u (this node)", info.address);
		shell_print(sh, "hostname: %s", info.hostname);
		shell_print(sh, "model: %s", info.model);
		shell_print(sh, "revision: %s", info.revision);
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
		return kfsw_shell_remote_failed(sh, "csp ident", (uint16_t)node, as_errno(result));
	}

	shell_print(sh, "CSP ident %lu", node);
	shell_print(sh, "hostname: %s", identity.hostname);
	shell_print(sh, "model: %s", identity.model);
	shell_print(sh, "revision: %s", identity.revision);
	/* K-FSW nodes leave it empty; another libcsp node may still send one. */
	if (identity.date[0] != '\0') {
		shell_print(sh, "built: %s %s", identity.date, identity.time);
	}

	/* A second exchange for the clock, which the identity reply doesn't carry. */
	if (kfsw_csp_clock_read((uint16_t)node, KFSW_CSP_PING_TIMEOUT_MS, &clock) == 0) {
		print_clock(sh, "clock", &clock);
	} else {
		shell_print(sh, "clock: no answer");
	}
	return 0;
}

#if CONFIG_KFSW_COMMAND && CONFIG_REBOOT
/* The pin is checked on the node that restarts. */
static int cmd_csp_reboot(const struct shell *sh, size_t argc, char **argv)
{
	uint16_t node;
	int result = kfsw_shell_parse_node(sh, argv[1], &node);

	if (result != 0) {
		return result;
	}
	return kfsw_shell_run_command(sh, node, "reboot", argc - 2U, &argv[2]);
}
#endif

/* Read or set a clock. Both use the same exchange. */
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

	/* set first: a node needs the time before it can pass it on. */
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
		/* Print what the node read back after the write. */
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
	SHELL_CMD_ARG(ifstat, NULL, "Read remote counters: ifstat <node> <interface>.",
		      cmd_csp_ifstat, 3, 0),
	SHELL_CMD_ARG(clock, NULL,
		      "Time: clock, clock set <utc>, clock <node>, clock <node> sync.",
		      cmd_csp_clock, 1, 2),
	SHELL_CMD_ARG(counters, NULL, "Show libcsp error counters: counters [clear].",
		      cmd_csp_counters, 1, 1),
	SHELL_CMD_ARG(debug, NULL, "Trace packets in and out: debug [on|off].", cmd_csp_debug, 1,
		      1),
	SHELL_CMD_ARG(ident, NULL, "Identify a node, or this one when no node is given.",
		      cmd_csp_ident, 1, 1),
	SHELL_CMD_ARG(info, NULL, "Show local CSP identity and router state.", cmd_csp_info, 1, 0),
	SHELL_CMD_ARG(interfaces, NULL, "Show registered CSP interfaces.", cmd_csp_interfaces, 1,
		      0),
	SHELL_CMD_ARG(ping, NULL, "Ping a node, or this one when no node is given.", cmd_csp_ping,
		      1, 1),
#if CONFIG_KFSW_COMMAND && CONFIG_REBOOT
	SHELL_CMD_ARG(reboot, NULL, "Restart a node: reboot <node> <pin> [--retry].",
		      cmd_csp_reboot, 3, 1),
#endif
	SHELL_CMD_ARG(routes, NULL, "Show the CSP static routing table.", cmd_csp_routes, 1, 0),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(csp, &csp_commands, "K-FSW CSP commands.", NULL);
