#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/shell/shell_uart.h>

/*
 * Colour the prompt at runtime; Kconfig strings can't hold escape codes.
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

	/* The shell measures the prompt with strlen to place the cursor, so set it to
	 * the visible width without the escape codes.
	 */
	sh->ctx->vt100_ctx.cons.name_len = sizeof(CONFIG_SHELL_PROMPT_UART) - 1U;
}
