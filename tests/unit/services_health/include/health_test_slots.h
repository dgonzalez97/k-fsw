#ifndef HEALTH_TEST_SLOTS_H
#define HEALTH_TEST_SLOTS_H

/* Index 2 is left unused, as a removed component would leave it. */
#define KFSW_HEALTH_SLOTS(SLOT)                                                                    \
	SLOT(alpha, 0)                                                                             \
	SLOT(gamma, 1)                                                                             \
	SLOT(beta, 3)

#endif
