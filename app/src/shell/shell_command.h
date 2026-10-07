#ifndef KFSW_APP_SHELL_COMMAND_H
#define KFSW_APP_SHELL_COMMAND_H

#include <stddef.h>
#include <stdint.h>

#include <zephyr/shell/shell.h>

/** Node meaning "run it here" for kfsw_shell_run_command(). */
#define KFSW_SHELL_THIS_NODE 0U

/**
 * Parse a node for a remote request: 1 up to the broadcast address, which is
 * left out because a request goes to one node (1..30 in CSP 1, 1..16382 in
 * CSP 2).
 */
int kfsw_shell_parse_node(const struct shell *sh, const char *text, uint16_t *node);

/**
 * Run a registered command here or on another node and print its reply, one
 * field per line. Arguments are text and converted to the command's types. On a
 * remote node a last argument "--retry" sends the request with a ticket.
 */
int kfsw_shell_run_command(const struct shell *sh, uint16_t node, const char *name, size_t argc,
			   char **argv);

/** Print "key=value ..." one field per line; anything else as one line. */
void kfsw_shell_print_fields(const struct shell *sh, const char *text);

#endif
