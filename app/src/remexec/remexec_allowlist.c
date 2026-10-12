#include <zephyr/sys/util.h>

#include "remexec_allowlist.h"

/*
 * Only commands that read state, finish promptly and print little. Marking a
 * command marks its arguments too, so nothing here takes an argument that can
 * write, erase or reset anything. A command that is not listed cannot be
 * reached remotely, and adding one is a composition decision, not a runtime
 * one.
 */
static const struct kfsw_remexec_entry remexec_entries[] = {
	{
		.command = "time",
		.help = "Monotonic time since boot",
	},
	{
		.command = "version",
		.help = "Image version, revisions and board",
	},
#if CONFIG_KFSW_STORAGE
	{
		.command = "storage info",
		.help = "Filesystem totals and mount state",
	},
#endif
#if CONFIG_KFSW_RESMON
	{
		.command = "resmon show",
		.help = "What the last stack sweep found",
	},
#endif
#if CONFIG_KFSW_EVENT
	{
		.command = "event stats",
		.help = "Event record counters",
	},
#endif
};

const struct kfsw_remexec_allowlist kfsw_app_remexec_allowlist = {
	.entries = remexec_entries,
	.count = ARRAY_SIZE(remexec_entries),
};
