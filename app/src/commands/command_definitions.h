#ifndef KFSW_APP_COMMAND_DEFINITIONS_H
#define KFSW_APP_COMMAND_DEFINITIONS_H

#include <kfsw/services/command.h>

/**
 * Commands owned by the application composition.
 *
 * Identifiers 1-15 are reserved for this set. A service or module that gains
 * its own commands contributes a separate set with its own identifier range,
 * the same way parameter definitions are contributed.
 */
extern const struct kfsw_command_definition_set kfsw_app_command_definitions;

#endif
