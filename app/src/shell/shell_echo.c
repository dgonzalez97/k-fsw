#include <stdbool.h>

#include <zephyr/shell/shell.h>
#include <zephyr/shell/shell_uart.h>

#include <kfsw/services/command.h>

#include "shell_echo.h"

/*
 * The console belongs to the composition, so this is where echo is applied.
 * The command service holds the setting and calls back here, which is what
 * lets the parameter take effect while somebody is watching the console rather
 * than at the next boot.
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
	/* Registering applies the current value, so the default reaches the
	 * shell without waiting for anyone to write the parameter. */
	kfsw_command_set_echo_handler(apply_echo);
}
