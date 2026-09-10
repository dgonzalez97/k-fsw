#ifndef KFSW_APP_HK_ENTRIES_H
#define KFSW_APP_HK_ENTRIES_H

#include <stddef.h>

#include <kfsw/services/hk.h>

/**
 * @brief Parse one housekeeping entry written as [node:]table:offset.
 *
 * The identifier a report stores is (table << 8) | offset, which is what the
 * wire uses, but nobody wants to type it in hexadecimal. Numbers are read in
 * any base strtoul accepts, so 0x10 and 16 are the same offset.
 *
 * Says nothing on failure: the shell and the command service report a bad
 * entry differently, and this is shared by both.
 *
 * @param text Entry text.
 * @param entry Destination.
 * @return 0 on success, or -EINVAL when the text is not an entry.
 */
int kfsw_app_hk_parse_entry(const char *text, struct kfsw_hk_entry *entry);

/**
 * @brief Parse a whitespace-separated list of entries.
 *
 * @param text List text.
 * @param entries Destination array.
 * @param max Entries the array holds.
 * @param count Set to the number parsed.
 * @return 0 on success, -EINVAL when an entry is malformed or the list is
 *         empty, or -E2BIG when there are more entries than @p max.
 */
int kfsw_app_hk_parse_entries(const char *text, struct kfsw_hk_entry *entries, size_t max,
			      size_t *count);

#endif
