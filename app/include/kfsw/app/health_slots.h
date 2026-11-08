#ifndef KFSW_APP_HEALTH_SLOTS_H
#define KFSW_APP_HEALTH_SLOTS_H

/*
 * Components this composition publishes in health table 31, as SLOT(name,
 * index). The name is the one the component registers with. The index fixes
 * the offsets on the wire, so an index is never reused or renumbered; a
 * component that is removed leaves its index unused.
 */
#define KFSW_HEALTH_SLOTS(SLOT)                                                                    \
	SLOT(app, 0)                                                                               \
	SLOT(csp, 1)

#endif
