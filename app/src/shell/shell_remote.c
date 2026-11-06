#include <errno.h>

#include <zephyr/shell/shell.h>
#include <zephyr/shell/shell_string_conv.h>

#include <kfsw/comms/csp.h>

#define KFSW_LOG_MODULE KFSW_LOG_MODULE_APP
#include <kfsw/services/log.h>

#include "shell_remote.h"

int kfsw_shell_parse_node(const struct shell *sh, const char *text, uint16_t *node)
{
	unsigned long parsed;
	int parse_error = 0;

	parsed = shell_strtoul(text, 10, &parse_error);
	if ((parse_error != 0) || (parsed == 0U) || (parsed >= KFSW_CSP_BROADCAST_ADDRESS)) {
		shell_error(sh, "Node must be 1..%u: %s", KFSW_CSP_BROADCAST_ADDRESS - 1U, text);
		return -EINVAL;
	}
	*node = (uint16_t)parsed;
	return 0;
}

bool kfsw_shell_no_answer(int result)
{
	return (result == -ETIMEDOUT) || (result == -EAGAIN) || (result == -ENOTCONN) ||
	       (result == -ECONNRESET) || (result == -EHOSTUNREACH);
}

int kfsw_shell_remote_failed(const struct shell *sh, const char *what, uint16_t node, int result)
{
	if (kfsw_shell_no_answer(result)) {
		kfsw_log_warning("%s: node %u did not answer", what, node);
	} else {
		shell_error(sh, "%s: node %u failed (%d)", what, node, result);
	}
	return result;
}
