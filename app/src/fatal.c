#include <zephyr/arch/cpu.h>
#include <zephyr/fatal.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/reboot.h>

#include <kfsw/platform/lastwords.h>
#include <kfsw/services/boot.h>

/* Fatal error hook: writes the faulting address into the last words note, then
 * restarts. A halted core waits for the watchdog, and a fault before main() has
 * armed it would wait for good.
 */
void k_sys_fatal_error_handler(unsigned int reason, const struct arch_esf *esf)
{
	uint32_t address = 0U;

#if defined(CONFIG_ARM) && !defined(CONFIG_ARM64)
	/* The program counter at the fault, from the stacked exception frame. */
	if (esf != NULL) {
		address = (uint32_t)esf->basic.pc;
	}
#else
	ARG_UNUSED(esf);
#endif

#if CONFIG_KFSW_PARAM
	kfsw_lastwords_write(KFSW_LASTWORDS_FATAL, address, k_uptime_get_32(),
			     kfsw_boot_get_count());
#else
	kfsw_lastwords_write(KFSW_LASTWORDS_FATAL, address, k_uptime_get_32(), 0U);
#endif

#if CONFIG_REBOOT && !CONFIG_ARCH_POSIX
	ARG_UNUSED(reason);
	sys_reboot(SYS_REBOOT_COLD);
#else
	k_fatal_halt(reason);
#endif
}
