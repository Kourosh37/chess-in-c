#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif

#include "file_io.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#endif

static char g_error[256];

static bool fail(const char* action) {
#ifdef _WIN32
    snprintf(g_error, sizeof(g_error), "%s (Windows error %lu).", action, (unsigned long)GetLastError());
#else
    snprintf(g_error, sizeof(g_error), "%s: %s.", action, strerror(errno));
#endif
    return false;
}

const char* file_io_last_error(void) { return g_error; }

#ifdef _WIN32
static wchar_t* wide_path(const char* path) {
    int count;
    wchar_t* wide;
    if (path == NULL) return NULL;
    count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, NULL, 0);
    if (count <= 0) { fail("Invalid UTF-8 path"); return NULL; }
    wide = malloc((size_t)count * sizeof(*wide));
    if (wide == NULL) { SetLastError(ERROR_NOT_ENOUGH_MEMORY); fail("Could not allocate path"); return NULL; }
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wide, count)) {
        free(wide);
        fail("Could not convert path");
        return NULL;
    }
    for (int i = 0; i < count; ++i) if (wide[i] == L'/') wide[i] = L'\\';
    /* CreateDirectory reserves room for an 8.3 suffix before MAX_PATH. */
    if (count >= MAX_PATH - 12 && file_io_absolute(path) && wcsncmp(wide, L"\\\\?\\", 4) != 0) {
        wchar_t* extended = malloc(((size_t)count + 8) * sizeof(*extended));
        if (extended == NULL) { free(wide); SetLastError(ERROR_NOT_ENOUGH_MEMORY); fail("Could not allocate path"); return NULL; }
        if (wide[0] == L'\\' && wide[1] == L'\\')
            swprintf(extended, (size_t)count + 8, L"\\\\?\\UNC\\%ls", wide + 2);
        else swprintf(extended, (size_t)count + 8, L"\\\\?\\%ls", wide);
        free(wide);
        wide = extended;
    }
    return wide;
}

static bool utf8_path(const wchar_t* path, char out[CHESS_PATH_MAX]) {
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, path, -1, out, CHESS_PATH_MAX, NULL, NULL) == 0)
        return fail("Path is too long or invalid");
    return true;
}
#endif

bool file_io_absolute(const char* path) {
    if (path == NULL || path[0] == '\0') return false;
#ifdef _WIN32
    if ((path[0] == '\\' && path[1] == '\\') || (path[0] == '/' && path[1] == '/')) return true;
    return strlen(path) >= 3 && path[1] == ':' && (path[2] == '/' || path[2] == '\\');
#else
    return path[0] == '/';
#endif
}

bool file_io_join(char* out, size_t capacity, const char* dir, const char* name) {
    size_t length;
    int written;
    if (out == NULL || capacity == 0 || dir == NULL || name == NULL) return false;
    length = strlen(dir);
    written = snprintf(out, capacity, "%s%s%s", dir,
                       length && (dir[length - 1] == '/' || dir[length - 1] == '\\') ? "" : "/", name);
    if (written < 0 || (size_t)written >= capacity) {
        out[0] = '\0';
        snprintf(g_error, sizeof(g_error), "Settings path is too long.");
        return false;
    }
    return true;
}

int file_io_path_status(const char* path) {
#ifdef _WIN32
    wchar_t* wide = wide_path(path);
    DWORD attributes;
    DWORD error;
    if (wide == NULL) return -1;
    attributes = GetFileAttributesW(wide);
    error = GetLastError();
    free(wide);
    if (attributes != INVALID_FILE_ATTRIBUTES) return 1;
    if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) return 0;
    SetLastError(error);
#else
    struct stat info;
    if (stat(path, &info) == 0) return 1;
    if (errno == ENOENT || errno == ENOTDIR) return 0;
#endif
    fail("Could not inspect settings path");
    return -1;
}

static bool make_directory(const char* path) {
#ifdef _WIN32
    wchar_t* wide = wide_path(path);
    bool ok;
    if (wide == NULL) return false;
    ok = CreateDirectoryW(wide, NULL) != 0;
    if (!ok && GetLastError() == ERROR_ALREADY_EXISTS) {
        DWORD attr = GetFileAttributesW(wide);
        ok = attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
        if (!ok) SetLastError(ERROR_DIRECTORY);
    }
    if (!ok) fail("Could not create settings directory");
    free(wide);
    return ok;
#else
    struct stat info;
    if (mkdir(path, 0700) == 0) return true;
    if (errno == EEXIST && stat(path, &info) == 0 && S_ISDIR(info.st_mode)) return true;
    return fail("Could not create settings directory");
#endif
}

bool file_io_make_directories(const char* path) {
    char part[CHESS_PATH_MAX];
    size_t length;
    size_t start = 1;
    if (!file_io_absolute(path)) {
        snprintf(g_error, sizeof(g_error), "Settings directory must be an absolute path.");
        return false;
    }
    length = strlen(path);
    if (length >= sizeof(part)) { snprintf(g_error, sizeof(g_error), "Settings directory is too long."); return false; }
    memcpy(part, path, length + 1);
#ifdef _WIN32
    if (part[1] == ':') start = 3;
    else {
        /* A UNC server/share is an existing root, not a directory to create. */
        int separators = 0;
        for (start = 2; start < length; ++start) {
            if ((part[start] == '/' || part[start] == '\\') && ++separators == 2) { start++; break; }
        }
    }
#endif
    for (size_t i = start; i < length; ++i) {
        if (part[i] == '/' || part[i] == '\\') {
            char separator = part[i];
            part[i] = '\0';
            if (!make_directory(part)) return false;
            part[i] = separator;
        }
    }
    return make_directory(part);
}

bool file_io_executable_directory(char out[CHESS_PATH_MAX]) {
    char* slash;
#ifdef _WIN32
    wchar_t wide[CHESS_PATH_MAX];
    DWORD count = GetModuleFileNameW(NULL, wide, CHESS_PATH_MAX);
    if (count == 0 || count >= CHESS_PATH_MAX || !utf8_path(wide, out)) return fail("Could not locate executable");
#elif defined(__APPLE__)
    uint32_t capacity = CHESS_PATH_MAX;
    if (_NSGetExecutablePath(out, &capacity) != 0) return fail("Could not locate executable");
#else
    ssize_t count = readlink("/proc/self/exe", out, CHESS_PATH_MAX - 1);
    if (count <= 0 || count >= CHESS_PATH_MAX - 1) return fail("Could not locate executable");
    out[count] = '\0';
#endif
    slash = strrchr(out, '/');
#ifdef _WIN32
    {
        char* other = strrchr(out, '\\');
        if (other != NULL && (slash == NULL || other > slash)) slash = other;
    }
#endif
    if (slash == NULL) return false;
#ifdef _WIN32
    if (slash == out + 2 && out[1] == ':') slash[1] = '\0';
    else
#endif
    slash[slash == out ? 1 : 0] = '\0';
    return true;
}

bool file_io_working_directory(char out[CHESS_PATH_MAX]) {
#ifdef _WIN32
    wchar_t wide[CHESS_PATH_MAX];
    if (_wgetcwd(wide, CHESS_PATH_MAX) == NULL) return fail("Could not locate working directory");
    return utf8_path(wide, out);
#else
    return getcwd(out, CHESS_PATH_MAX) != NULL || fail("Could not locate working directory");
#endif
}

bool file_io_change_directory(const char* path) {
#ifdef _WIN32
    wchar_t* wide = wide_path(path);
    int result;
    if (wide == NULL) return false;
    result = _wchdir(wide);
    free(wide);
    return result == 0 || fail("Could not open the application directory");
#else
    return chdir(path) == 0 || fail("Could not open the application directory");
#endif
}

bool file_io_read(const char* path, void** data, size_t* size) {
    FILE* file;
    long length;
    void* buffer;
    if (data == NULL || size == NULL || path == NULL) return false;
    *data = NULL;
    *size = 0;
#ifdef _WIN32
    {
        wchar_t* wide = wide_path(path);
        if (wide == NULL) return false;
        file = _wfopen(wide, L"rb");
        free(wide);
    }
#else
    file = fopen(path, "rb");
#endif
    if (file == NULL) {
        snprintf(g_error, sizeof(g_error), "Could not open settings file: %s.", strerror(errno));
        return false;
    }
    if (fseek(file, 0, SEEK_END) != 0 || (length = ftell(file)) < 0 || length > 16 * 1024 * 1024 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        snprintf(g_error, sizeof(g_error), "Settings file has an invalid size.");
        return false;
    }
    buffer = malloc(length > 0 ? (size_t)length : 1);
    if (buffer == NULL) { fclose(file); snprintf(g_error, sizeof(g_error), "Not enough memory to read settings."); return false; }
    if (fread(buffer, 1, (size_t)length, file) != (size_t)length) {
        fclose(file);
        free(buffer);
        return fail("Could not read complete settings file");
    }
    if (fclose(file) != 0) { free(buffer); return fail("Could not close settings file"); }
    *data = buffer;
    *size = (size_t)length;
    return true;
}

bool file_io_write_atomic(const char* path, const void* data, size_t size) {
    bool ok = false;
    if (path == NULL || (data == NULL && size != 0)) return false;
#ifdef _WIN32
    wchar_t* target = wide_path(path);
    wchar_t* temporary;
    HANDLE handle = INVALID_HANDLE_VALUE;
    static LONG counter;
    if (target == NULL) return false;
    temporary = malloc((wcslen(target) + 64) * sizeof(*temporary));
    if (temporary == NULL) { free(target); SetLastError(ERROR_NOT_ENOUGH_MEMORY); return fail("Could not allocate settings path"); }
    for (int attempt = 0; attempt < 32; ++attempt) {
        swprintf(temporary, wcslen(target) + 64, L"%ls.tmp-%lu-%ld", target,
                 (unsigned long)GetCurrentProcessId(), (long)InterlockedIncrement(&counter));
        handle = CreateFileW(temporary, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
        if (handle != INVALID_HANDLE_VALUE || GetLastError() != ERROR_FILE_EXISTS) break;
    }
    if (handle == INVALID_HANDLE_VALUE) fail("Could not create temporary settings file");
    else {
        const unsigned char* cursor = data;
        size_t remaining = size;
        ok = true;
        while (remaining > 0) {
            DWORD written = 0;
            DWORD chunk = remaining > 1024 * 1024 ? 1024 * 1024 : (DWORD)remaining;
            if (!WriteFile(handle, cursor, chunk, &written, NULL) || written == 0) { ok = fail("Could not write settings"); break; }
            cursor += written;
            remaining -= written;
        }
        if (ok && !FlushFileBuffers(handle)) ok = fail("Could not flush settings to disk");
        if (!CloseHandle(handle) && ok) ok = fail("Could not close temporary settings file");
        if (ok && !MoveFileExW(temporary, target, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            ok = fail("Could not replace settings file");
        if (!ok) DeleteFileW(temporary);
    }
    free(temporary);
    free(target);
#else
    size_t length = strlen(path);
    char* temporary = malloc(length + 16);
    int fd;
    if (temporary == NULL) return fail("Could not allocate settings path");
    snprintf(temporary, length + 16, "%s.tmp-XXXXXX", path);
    fd = mkstemp(temporary);
    if (fd < 0) fail("Could not create temporary settings file");
    else {
        const unsigned char* cursor = data;
        size_t remaining = size;
        ok = true;
        while (remaining > 0) {
            ssize_t written = write(fd, cursor, remaining);
            if (written < 0 && errno == EINTR) continue;
            if (written <= 0) { ok = fail("Could not write settings"); break; }
            cursor += written;
            remaining -= (size_t)written;
        }
        if (ok && fsync(fd) != 0) ok = fail("Could not flush settings to disk");
        if (close(fd) != 0 && ok) ok = fail("Could not close temporary settings file");
        if (ok && rename(temporary, path) != 0) ok = fail("Could not replace settings file");
        if (!ok) unlink(temporary);
        else {
            char* slash = strrchr(temporary, '/');
            if (slash != NULL) {
                int parent;
                slash[slash == temporary ? 1 : 0] = '\0';
                parent = open(temporary, O_RDONLY);
                if (parent >= 0) { (void)fsync(parent); close(parent); }
            }
        }
    }
    free(temporary);
#endif
    return ok;
}
