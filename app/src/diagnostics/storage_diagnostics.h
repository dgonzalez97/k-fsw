#ifndef KFSW_APP_STORAGE_DIAGNOSTICS_H
#define KFSW_APP_STORAGE_DIAGNOSTICS_H

#define KFSW_STORAGE_DIAGNOSTIC_VALUE_MAX_SIZE 48U

int storage_diagnostic_write(const char *path, const char *value);
int storage_diagnostic_read(const char *path, const char *expected);

#endif
