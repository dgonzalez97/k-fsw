#include <stdbool.h>

#include <zephyr/shell/shell.h>
#include <zephyr/shell/shell_uart.h>

#include <kfsw/services/command.h>

#include "shell_echo.h"

/*
 * Applies console echo for the command service.
 */
static void apply_echo(bool enabled)
{
	const struct shell *sh = shell_backend_uart_get_ptr();

	if (sh != NULL) {
		(void)shell_echo_set(sh, enabled);
	}
}

void kfsw_shell_echo_apply(void)
{
	/* Registering applies the current value. */
	kfsw_command_set_echo_handler(apply_echo);
}
