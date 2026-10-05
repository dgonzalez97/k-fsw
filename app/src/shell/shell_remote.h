#ifndef KFSW_APP_SHELL_REMOTE_H
#define KFSW_APP_SHELL_REMOTE_H

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/shell/shell.h>

/** Whether a failed request means the node did not answer in time. */
bool kfsw_shell_no_answer(int result);

/**
 * Report a failed request to another node. A node that did not answer is
 * something that happened, so it is logged as a warning and kept in the log
 * history; any other failure answers the operator in the shell.
 *
 * @return @p result, so a handler can return it directly.
 */
int kfsw_shell_remote_failed(const struct shell *sh, const char *what, uint16_t node, int result);

#endif
