#include <errno.h>

#include <zephyr/shell/shell.h>
#include <zephyr/shell/shell_string_conv.h>
#include <zephyr/sys/util.h>
#include <zephyr/version.h>

#if CONFIG_KFSW_CSP
#include <kfsw/comms/csp.h>
#endif
#include <kfsw/platform/time.h>
#include <kfsw/services/log.h>

#if CONFIG_KFSW_CSP
#define KFSW_CSP_PING_TIMEOUT_MS 1000U
#define KFSW_CSP_PING_PAYLOAD_SIZE 10U

static int cmd_kfsw_csp_info(const struct shell *sh, size_t argc, char **argv)
{
	struct kfsw_csp_info info;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	kfsw_csp_get_info(&info);
	shell_print(sh, "CSP node: %u", info.address);
	shell_print(sh, "hostname: %s", info.hostname);
	shell_print(sh, "model: %s", info.model);
	shell_print(sh, "revision: %s", info.revision);
	shell_print(sh, "initialized: %s", info.initialized ? "yes" : "no");
	shell_print(sh, "router: %s", info.router_running ? "running" : "stopped");
	shell_print(sh, "free_buffers: %zu", info.free_buffers);

	return 0;
}

static bool print_csp_interface(
	const struct kfsw_csp_interface_info *interface_info, void *context)
{
	const struct shell *sh = context;

	shell_print(sh,
		    "%s addr=%u/%u default=%s tx=%u rx=%u txerr=%u "
		    "rxerr=%u drop=%u",
		    interface_info->name, interface_info->address,
		    interface_info->prefix_length,
		    interface_info->is_default ? "yes" : "no",
		    interface_info->tx_packets, interface_info->rx_packets,
		    interface_info->tx_errors, interface_info->rx_errors,
		    interface_info->dropped_packets);

	return true;
}

static int cmd_kfsw_csp_interfaces(const struct shell *sh, size_t argc,
				   char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	kfsw_csp_visit_interfaces(print_csp_interface, (void *)sh);
	return 0;
}

static bool print_csp_route(const struct kfsw_csp_route_info *route_info,
			    void *context)
{
	const struct shell *sh = context;

	if (route_info->has_via) {
		shell_print(sh, "%u/%u -> %s via %u", route_info->address,
			    route_info->prefix_length,
			    route_info->interface_name, route_info->via);
	} else {
		shell_print(sh, "%u/%u -> %s direct", route_info->address,
			    route_info->prefix_length,
			    route_info->interface_name);
	}

	return true;
}

static int cmd_kfsw_csp_routes(const struct shell *sh, size_t argc,
			       char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	kfsw_csp_visit_routes(print_csp_route, (void *)sh);
	return 0;
}

static int cmd_kfsw_csp_ping(const struct shell *sh, size_t argc, char **argv)
{
	unsigned long node;
	uint32_t round_trip_ms;
	int parse_error = 0;
	int result;

	ARG_UNUSED(argc);

	node = shell_strtoul(argv[1], 10, &parse_error);
	if (parse_error != 0 || node > 16383U) {
		shell_error(sh, "CSP node must be in range 0..16383");
		return -EINVAL;
	}

	result = kfsw_csp_ping((uint16_t)node, KFSW_CSP_PING_TIMEOUT_MS,
			       KFSW_CSP_PING_PAYLOAD_SIZE, &round_trip_ms);
	if (result != 0) {
		shell_error(sh, "CSP ping %lu: failed (%d)", node, result);
		return result;
	}

	shell_print(sh, "CSP ping %lu: success, rtt_ms=%u", node,
		    round_trip_ms);
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(kfsw_csp_commands,
	SHELL_CMD_ARG(info, NULL, "Show local CSP identity and router state.",
		      cmd_kfsw_csp_info, 1, 0),
	SHELL_CMD_ARG(interfaces, NULL, "Show registered CSP interfaces.",
		      cmd_kfsw_csp_interfaces, 1, 0),
	SHELL_CMD_ARG(ping, NULL, "Ping a CSP node: ping <node>.",
		      cmd_kfsw_csp_ping, 2, 0),
	SHELL_CMD_ARG(routes, NULL, "Show the CSP static routing table.",
		      cmd_kfsw_csp_routes, 1, 0),
	SHELL_SUBCMD_SET_END
);
#endif

static int cmd_kfsw_status(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(sh, "K-FSW status");
	shell_print(sh, "board: %s", CONFIG_BOARD_TARGET);
	shell_print(sh, "uptime_ms: %llu",
		    (unsigned long long)kfsw_time_monotonic_ms());

	return 0;
}

static int cmd_kfsw_time(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(sh, "monotonic_ms: %llu",
		    (unsigned long long)kfsw_time_monotonic_ms());
	shell_print(sh, "monotonic_us: %llu",
		    (unsigned long long)kfsw_time_monotonic_us());

	return 0;
}

static int cmd_kfsw_version(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(sh, "K-FSW: kfsw-dev");
	shell_print(sh, "Zephyr: %s", KERNEL_VERSION_STRING);
	shell_print(sh, "Board: %s", CONFIG_BOARD_TARGET);

	return 0;
}

static int cmd_kfsw_log_test(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	ARG_UNUSED(sh);

	kfsw_log_error("K-FSW shell log test: error");
	kfsw_log_warning("K-FSW shell log test: warning");
	kfsw_log_info("K-FSW shell log test: info");
	kfsw_log_debug("K-FSW shell log test: debug");

	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(kfsw_log_commands,
	SHELL_CMD_ARG(test, NULL, "Exercise all K-FSW log levels.",
		      cmd_kfsw_log_test, 1, 0),
	SHELL_SUBCMD_SET_END
);

SHELL_STATIC_SUBCMD_SET_CREATE(kfsw_commands,
#if CONFIG_KFSW_CSP
	SHELL_CMD(csp, &kfsw_csp_commands, "K-FSW CSP commands.", NULL),
#endif
	SHELL_CMD(log, &kfsw_log_commands, "K-FSW logging commands.", NULL),
	SHELL_CMD_ARG(status, NULL, "Show basic K-FSW runtime status.",
		      cmd_kfsw_status, 1, 0),
	SHELL_CMD_ARG(time, NULL, "Show K-FSW monotonic time.",
		      cmd_kfsw_time, 1, 0),
	SHELL_CMD_ARG(version, NULL, "Show K-FSW build information.",
		      cmd_kfsw_version, 1, 0),
	SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(kfsw, &kfsw_commands,
		   "K-FSW developer/debug commands.", NULL);
