#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/shell/shell.h>
#include <zephyr/sys/util.h>

#include <kfsw/platform/watchdog.h>

static const char *state_name(uint8_t state)
{
	switch (state) {
	case KFSW_PLATFORM_WATCHDOG_UNCONFIGURED:
		return "unconfigured";
	case KFSW_PLATFORM_WATCHDOG_CONFIGURED:
		return "configured";
	case KFSW_PLATFORM_WATCHDOG_RUNNING:
		return "running";
	case KFSW_PLATFORM_WATCHDOG_STARVED:
		return "starved";
	default:
		return "unknown";
	}
}

static int cmd_watchdog_status(const struct shell *sh, size_t argc, char **argv)
{
	struct kfsw_platform_watchdog_info info;
	int result;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	result = kfsw_platform_watchdog_get_info(&info);
	if (result != 0) {
		shell_error(sh, "watchdog status unavailable (%d)", result);
		return result;
	}

	shell_print(sh, "K-FSW watchdog");
	shell_print(sh, "device: %s", info.device_bound ? "bound" : "absent");
	shell_print(sh, "state: %s", state_name(info.state));
	shell_print(sh, "timeout_ms: %" PRIu32, info.timeout_ms);
	shell_print(sh, "feed_interval_ms: %" PRIu32, info.feed_interval_ms);
	shell_print(sh, "feeds: %" PRIu32, info.feeds);
	shell_print(sh, "since_feed_ms: %" PRIu32, info.since_feed_ms);
	return 0;
}

static int cmd_watchdog_feed(const struct shell *sh, size_t argc, char **argv)
{
	int result;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	result = kfsw_platform_watchdog_feed();
	if (result != 0) {
		shell_error(sh, "watchdog feed failed (%d)", result);
		return result;
	}

	shell_print(sh, "Watchdog fed");
	return 0;
}

static int cmd_watchdog_starve(const struct shell *sh, size_t argc, char **argv)
{
	struct kfsw_platform_watchdog_info info;
	int result;

	ARG_UNUSED(argv);

	/* This ends in a reset that cannot be called off, so it is not
	 * something to trip over by pressing tab and enter. The confirmation
	 * word is required rather than a yes/no prompt so the command stays
	 * usable from a script and over a link with no interactive echo.
	 */
	if (argc != 2U || strcmp(argv[1], "confirm") != 0) {
		shell_error(sh, "This stops feeding the watchdog and the board will reset.");
		shell_error(sh, "Run: watchdog starve confirm");
		return -EINVAL;
	}

	result = kfsw_platform_watchdog_get_info(&info);
	if (result == 0 && !info.device_bound) {
		shell_error(sh, "No watchdog device is bound");
		return -ENODEV;
	}

	result = kfsw_platform_watchdog_stop_feeding();
	if (result != 0) {
		shell_error(sh, "watchdog starve failed (%d)", result);
		return result;
	}

	shell_print(sh, "Watchdog feed stopped; reset expected within %" PRIu32 " ms",
		    info.timeout_ms);
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
	watchdog_commands,
	SHELL_CMD_ARG(feed, NULL, "Feed the watchdog once.", cmd_watchdog_feed, 1, 0),
	SHELL_CMD_ARG(starve, NULL, "Stop feeding so the board resets: starve confirm.",
		      cmd_watchdog_starve, 1, 1),
	SHELL_CMD_ARG(status, NULL, "Show watchdog configuration and activity.",
		      cmd_watchdog_status, 1, 0),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(watchdog, &watchdog_commands, "K-FSW hardware watchdog.", NULL);
