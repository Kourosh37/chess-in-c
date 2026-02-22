#ifndef SECURE_IO_H
#define SECURE_IO_H

/*
 * Encrypted file read/write helpers used for local persisted data.
 * On Windows this uses DPAPI user-scoped protection.
 */

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Writes one buffer to path using encrypted container format. */
bool secure_io_write_file(const char* path, const void* data, size_t size);

/* Reads one file and decrypts it when needed; legacy plain files are also accepted. */
bool secure_io_read_file(const char* path, void** out_data, size_t* out_size);

/* Frees buffers returned by secure_io_read_file(). */
void secure_io_free(void* data);

#ifdef __cplusplus
}
#endif

#endif
