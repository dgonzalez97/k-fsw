#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/shell/shell_uart.h>

/*
 * The prompt is always on screen, so jade makes it identifiable at a glance --
 * node output from operator input, and one node's console from another's.
 *
 * Applied at runtime rather than in CONFIG_SHELL_PROMPT_UART, because Kconfig
 * strings do not carry escape sequences.
 */

/* 256-colour 36 is the closest terminal approximation to jade. */
#define KFSW_PROMPT_JADE "\033[38;5;36m"
#define KFSW_PROMPT_RESET "\033[0m"
#define KFSW_PROMPT_SIZE 64U

static char colored_prompt[KFSW_PROMPT_SIZE];

void kfsw_shell_prompt_apply(void)
{
	const struct shell *sh = shell_backend_uart_get_ptr();

	if (sh == NULL) {
		return;
	}

	/* The reset sits after the prompt's own trailing space, so the prompt
	 * text stays one contiguous run for anything matching on it.
	 */
	(void)snprintk(colored_prompt, sizeof(colored_prompt), "%s%s%s", KFSW_PROMPT_JADE,
		       CONFIG_SHELL_PROMPT_UART, KFSW_PROMPT_RESET);

	if (shell_prompt_change(sh, colored_prompt) != 0) {
		return;
	}

	/* The shell measures the prompt with a plain string length to know how
	 * far the cursor starts from the left edge. Escape sequences occupy no
	 * columns, so that count is wrong by exactly their length and every
	 * line edit -- cursor movement, wrapping, tab completion -- lands in
	 * the wrong place. Correcting it to the visible width is the price of
	 * colouring the prompt at all; the alternative is a prompt the shell
	 * cannot edit under.
	 */
	sh->ctx->vt100_ctx.cons.name_len = sizeof(CONFIG_SHELL_PROMPT_UART) - 1U;
}
