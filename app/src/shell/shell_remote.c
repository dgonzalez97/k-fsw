#include <errno.h>

#include <zephyr/shell/shell.h>

#define KFSW_LOG_MODULE KFSW_LOG_MODULE_APP
#include <kfsw/services/log.h>

#include "shell_remote.h"

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
