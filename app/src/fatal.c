#include <zephyr/arch/cpu.h>
#include <zephyr/fatal.h>
#include <zephyr/kernel.h>

#include <kfsw/platform/lastwords.h>
#include <kfsw/services/boot.h>

/* Where a crash leaves its address.
 *
 * Zephyr's default handler ends the thread or the system and says so on the
 * console, which is no help to a node nobody is watching. This runs first,
 * writes the faulting address into the note, and then hands over: the default
 * behaviour is unchanged, and the next boot can say where the previous run
 * died rather than only that it did.
 */
void k_sys_fatal_error_handler(unsigned int reason, const struct arch_esf *esf)
{
	uint32_t address = 0U;

#if defined(CONFIG_ARM) && !defined(CONFIG_ARM64)
	/* The program counter at the fault. On this architecture it is in the
	 * stacked exception frame, which is the only place it survives.
	 */
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
