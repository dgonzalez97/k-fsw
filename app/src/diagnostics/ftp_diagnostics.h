#ifndef KFSW_APP_FTP_DIAGNOSTICS_H
#define KFSW_APP_FTP_DIAGNOSTICS_H

#include <stdint.h>

int ftp_diagnostic_generate(const char *path, uint32_t size, uint32_t *crc32);
int ftp_diagnostic_compare(const char *first_path, const char *second_path);

#endif
