#ifndef FILE_IO_H
#define FILE_IO_H

#include <stdbool.h>
#include <stddef.h>

#define CHESS_PATH_MAX 4096

/* Paths use UTF-8 on every platform, including Windows. */
bool file_io_join(char* out, size_t capacity, const char* dir, const char* name);
bool file_io_absolute(const char* path);
bool file_io_make_directories(const char* path);
int file_io_path_status(const char* path); /* 0 missing, 1 present, -1 error */
bool file_io_executable_directory(char out[CHESS_PATH_MAX]);
bool file_io_working_directory(char out[CHESS_PATH_MAX]);
bool file_io_change_directory(const char* path);
bool file_io_read(const char* path, void** data, size_t* size);
/* Flush a temporary file in the destination directory, then atomically replace. */
bool file_io_write_atomic(const char* path, const void* data, size_t size);
const char* file_io_last_error(void);

#endif
