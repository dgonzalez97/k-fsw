#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/sys/util.h>

#include <kfsw/services/parameter.h>

#include "hk_entries.h"

int kfsw_app_hk_parse_entry(const char *text, struct kfsw_hk_entry *entry)
{
	unsigned long parts[3];
	size_t count = 0U;
	const char *cursor = text;

	if ((text == NULL) || (entry == NULL)) {
		return -EINVAL;
	}

	while ((count < ARRAY_SIZE(parts)) && (*cursor != '\0')) {
		char *end;

		parts[count] = strtoul(cursor, &end, 0);
		if (end == cursor) {
			break;
		}
		count++;
		cursor = end;
		if (*cursor == ':') {
			cursor++;
		} else {
			break;
		}
	}

	if ((*cursor != '\0') || (count < 2U)) {
		return -EINVAL;
	}

	if (count == 2U) {
		entry->node = KFSW_HK_NODE_LOCAL;
		entry->param_id = KFSW_PARAM_ID(parts[0], parts[1]);
	} else {
		entry->node = (uint16_t)parts[0];
		entry->param_id = KFSW_PARAM_ID(parts[1], parts[2]);
	}
	return 0;
}

int kfsw_app_hk_parse_entries(const char *text, struct kfsw_hk_entry *entries, size_t max,
			      size_t *count)
{
	/* One entry at a time into a local buffer, because the text arrives as
	 * a single const string and strtok would write to it.
	 */
	char token[24];
	const char *cursor = text;
	size_t parsed = 0U;

	if ((text == NULL) || (entries == NULL) || (count == NULL) || (max == 0U)) {
		return -EINVAL;
	}

	while (*cursor != '\0') {
		size_t length = 0U;

		while (*cursor == ' ') {
			cursor++;
		}
		if (*cursor == '\0') {
			break;
		}
		while ((cursor[length] != '\0') && (cursor[length] != ' ')) {
			length++;
		}
		if (length >= sizeof(token)) {
			return -EINVAL;
		}
		if (parsed == max) {
			return -E2BIG;
		}
		memcpy(token, cursor, length);
		token[length] = '\0';
		if (kfsw_app_hk_parse_entry(token, &entries[parsed]) != 0) {
			return -EINVAL;
		}
		parsed++;
		cursor += length;
	}

	if (parsed == 0U) {
		return -EINVAL;
	}
	*count = parsed;
	return 0;
}
