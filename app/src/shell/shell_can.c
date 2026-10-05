#include <errno.h>
#include <stdbool.h>
#include <string.h>

#include <zephyr/shell/shell.h>

#include <csp/csp_error.h>

#include <kfsw/comms/can.h>
#include <kfsw/comms/csp.h>

#include "shell_command.h"
#include "shell_remote.h"

#define KFSW_CAN_TEST_TIMEOUT_MS 1000U

struct can_counters {
	const char *name;
	const struct kfsw_csp_interface_info *found;
	struct kfsw_csp_interface_info copy;
};

static bool find_interface(const struct kfsw_csp_interface_info *info, void *context)
{
	struct can_counters *counters = context;

	if (strcmp(info->name, counters->name) != 0) {
		return true;
	}
	counters->copy = *info;
	counters->found = &counters->copy;
	return false;
}

static int cmd_can_info(const struct shell *sh, size_t argc, char **argv)
{
	struct kfsw_can_info info;
	struct can_counters counters = {0};

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	kfsw_can_get_info(&info);
	shell_print(sh, "CAN interface: %s", info.ready ? info.interface_name : "-");
	shell_print(sh, "ready: %s", info.ready ? "yes" : "no");
	shell_print(sh, "bitrate: %u", info.bitrate);
	if (!info.ready) {
		return 0;
	}
	shell_print(sh, "CSP node: %u/%u", info.address, info.prefix_length);
	counters.name = info.interface_name;
	kfsw_csp_visit_interfaces(find_interface, &counters);
	if (counters.found != NULL) {
		shell_print(sh, "tx=%u rx=%u txerr=%u rxerr=%u drop=%u", counters.found->tx_packets,
			    counters.found->rx_packets, counters.found->tx_errors,
			    counters.found->rx_errors, counters.found->dropped_packets);
	}
	return 0;
}

static int cmd_can_test(const struct shell *sh, size_t argc, char **argv)
{
	struct kfsw_can_test_result test_result;
	struct kfsw_can_info info;
	uint16_t peer;
	int result;

	ARG_UNUSED(argc);
	result = kfsw_shell_parse_node(sh, argv[1], &peer);
	if (result != 0) {
		return result;
	}
	kfsw_can_get_info(&info);
	if (!info.ready) {
		shell_error(sh, "CAN CSP test: the CAN interface is not open");
		return -ENODEV;
	}
	result = kfsw_can_test_peer(peer, KFSW_CAN_TEST_TIMEOUT_MS, &test_result);
	if (result == CSP_ERR_NOTSUP) {
		shell_error(sh, "CAN CSP test: node %u is not reached through CAN", peer);
		return -ENOTSUP;
	}
	if (result == CSP_ERR_TIMEDOUT) {
		return kfsw_shell_remote_failed(sh, "comms can test", peer, -ETIMEDOUT);
	}
	if (result != CSP_ERR_NONE) {
		shell_error(sh, "CAN CSP test: FAIL (%d)", result);
		return -EIO;
	}
	shell_print(sh, "CAN CSP test: PASS");
	shell_print(sh, "peer: %u", test_result.peer);
	shell_print(sh, "rtt_ms: %u.%03u", test_result.round_trip_us / 1000U,
		    test_result.round_trip_us % 1000U);
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(can_commands,
	SHELL_CMD_ARG(info, NULL, "Show the CSP CAN interface.", cmd_can_info, 1, 0),
	SHELL_CMD_ARG(test, NULL, "Ping a node through CAN: test <node>.", cmd_can_test, 2, 0),
	SHELL_SUBCMD_SET_END);

SHELL_SUBCMD_ADD((comms), can, &can_commands, "CSP over CAN.", NULL, 1, 0);
