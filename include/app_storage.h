#ifndef APP_STORAGE_H
#define APP_STORAGE_H

#include <stdbool.h>
#include <stddef.h>

/* Per-user settings; independent of install location and working directory. */
bool app_storage_read_settings(void** data, size_t* size);
bool app_storage_write_settings(const void* data, size_t size);
const char* app_storage_settings_path(void);
const char* app_storage_last_error(void);
bool app_storage_recovered_backup(void);

#endif
