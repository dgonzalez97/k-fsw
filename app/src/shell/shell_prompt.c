#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/shell/shell_uart.h>

/*
 * The prompt is the one piece of the console that is always on screen, so it
 * is worth making it identifiable at a glance: jade separates what the node
 * says from what the operator typed, and separates one node's console from
 * another's when several are open at once.
 *
 * The colour is applied at runtime rather than baked into
 * CONFIG_SHELL_PROMPT_UART because Kconfig strings do not carry escape
 * sequences, and because the prompt text itself stays readable in the build
 * configuration that way.
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
