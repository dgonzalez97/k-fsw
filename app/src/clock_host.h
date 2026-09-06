#ifndef KFSW_APP_CLOCK_HOST_H
#define KFSW_APP_CLOCK_HOST_H

/**
 * @brief Take the wall clock from the machine this node runs on.
 *
 * Only meaningful for a node running as a Linux process. A board has nothing
 * to take the time from and has to be told.
 */
int kfsw_clock_from_host(void);

#endif /* KFSW_APP_CLOCK_HOST_H */
