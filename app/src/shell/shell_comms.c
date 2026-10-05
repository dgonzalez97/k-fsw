#include <zephyr/shell/shell.h>

/*
 * Link commands, one subcommand per link. Each link adds itself from its own
 * file with SHELL_SUBCMD_ADD((comms), ...), modules included.
 */
SHELL_SUBCMD_SET_CREATE(comms_commands, (comms));

SHELL_CMD_REGISTER(comms, &comms_commands, "Links: UART, CAN and radio.", NULL);
