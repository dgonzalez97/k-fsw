#include <zephyr/shell/shell.h>
#include <zephyr/sys/util.h>

#include <kfsw/comms/uart.h>

#define KFSW_UART_TEST_TIMEOUT_MS 1000U

static int cmd_uart_info(const struct shell *sh, size_t argc, char **argv)
{
	struct kfsw_uart_info info;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	kfsw_uart_get_info(&info);
	shell_print(sh, "UART transport");
	shell_print(sh, "device: %s", info.device_name);
	shell_print(sh, "baudrate: %u", info.baudrate);
	shell_print(sh, "configuration: 8N1, flow control none");
	shell_print(sh, "ready: %s", info.ready ? "yes" : "no");
	shell_print(sh, "CSP interface: %s", info.interface_name);
	shell_print(sh, "CSP node: %u", info.node);
	shell_print(sh, "CSP peer: %u", info.peer);
	shell_print(sh, "KISS tx=%u rx=%u txerr=%u rxerr=%u drop=%u frame=%u", info.tx_packets,
		    info.rx_packets, info.tx_errors, info.rx_errors, info.dropped_packets,
		    info.frame_errors);

	return 0;
}

static int cmd_uart_test(const struct shell *sh, size_t argc, char **argv)
{
	struct kfsw_uart_test_result test_result;
	int result;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	result = kfsw_uart_test(KFSW_UART_TEST_TIMEOUT_MS, &test_result);
	if (result != 0) {
		shell_error(sh, "UART CSP test: FAIL (%d)", result);
		return result;
	}

	shell_print(sh, "UART CSP test: PASS");
	shell_print(sh, "peer: %u", test_result.peer);
	shell_print(sh, "interface: %s", test_result.interface_name);
	shell_print(sh, "rtt_ms: %u", test_result.round_trip_ms);

	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(uart_commands,
	SHELL_CMD_ARG(info, NULL, "Show dedicated CSP UART status.", cmd_uart_info, 1, 0),
	SHELL_CMD_ARG(test, NULL, "Ping the configured peer over UART/KISS.", cmd_uart_test, 1, 0),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(uart, &uart_commands, "K-FSW CSP UART commands.", NULL);
