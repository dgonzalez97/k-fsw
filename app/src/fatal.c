#include <zephyr/arch/cpu.h>
#include <zephyr/fatal.h>
#include <zephyr/kernel.h>

#include <kfsw/platform/lastwords.h>
#include <kfsw/services/boot.h>

/* Fatal error hook: writes the faulting address into the last words note, then
 * runs Zephyr's default handler.
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

	k_fatal_halt(reason);
}
