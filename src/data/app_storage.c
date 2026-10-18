#include "app_storage.h"
#include "file_io.h"
#include "secure_io.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#endif

static char g_directory[CHESS_PATH_MAX];
static char g_path[CHESS_PATH_MAX];
static char g_backup[CHESS_PATH_MAX];
static char g_error[256];
static bool g_ready;
static bool g_recovered;

static bool error(const char* message) {
    snprintf(g_error, sizeof(g_error), "%s", message);
    return false;
}

const char* app_storage_last_error(void) { return g_error; }
bool app_storage_recovered_backup(void) { return g_recovered; }

static bool initialize(void) {
#ifdef _WIN32
    char custom_utf8[CHESS_PATH_MAX];
    const wchar_t* custom_wide = _wgetenv(L"CHESS_CONFIG_DIR");
    const char* custom = NULL;
    if (custom_wide != NULL && custom_wide[0] != L'\0') {
        if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, custom_wide, -1,
                                 custom_utf8, sizeof(custom_utf8), NULL, NULL))
            return error("CHESS_CONFIG_DIR is too long or invalid.");
        custom = custom_utf8;
    }
#else
    const char* custom = getenv("CHESS_CONFIG_DIR");
#endif
    char base[CHESS_PATH_MAX];
    if (g_ready) return true;
    if (custom != NULL && custom[0] != '\0') {
        if (!file_io_absolute(custom) || strlen(custom) >= sizeof(g_directory))
            return error("CHESS_CONFIG_DIR must be a valid absolute directory.");
        strcpy(g_directory, custom);
    } else {
#ifdef _WIN32
        PWSTR wide = NULL;
        HRESULT result = SHGetKnownFolderPath(&FOLDERID_LocalAppData, KF_FLAG_CREATE, NULL, &wide);
        if (FAILED(result)) return error("Could not locate your local application data folder.");
        int converted = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide, -1, base, sizeof(base), NULL, NULL);
        CoTaskMemFree(wide);
        if (!converted) return error("Your application data path is too long or invalid.");
        if (!file_io_join(g_directory, sizeof(g_directory), base, "Chess")) return error(file_io_last_error());
#else
        const char* home = getenv("HOME");
#ifdef __APPLE__
        if (home == NULL || !file_io_absolute(home)) return error("Could not locate your home directory.");
        if (!file_io_join(g_directory, sizeof(g_directory), home, "Library/Application Support/Chess"))
            return error(file_io_last_error());
#else
        const char* xdg = getenv("XDG_CONFIG_HOME");
        if (xdg != NULL && file_io_absolute(xdg)) {
            if (!file_io_join(g_directory, sizeof(g_directory), xdg, "chess")) return error(file_io_last_error());
        } else {
            if (home == NULL || !file_io_absolute(home)) return error("Could not locate your home directory.");
            if (!file_io_join(base, sizeof(base), home, ".config") ||
                !file_io_join(g_directory, sizeof(g_directory), base, "chess")) return error(file_io_last_error());
        }
#endif
#endif
    }
    if (!file_io_join(g_path, sizeof(g_path), g_directory, "settings.dat") ||
        !file_io_join(g_backup, sizeof(g_backup), g_directory, "settings.dat.bak"))
        return error(file_io_last_error());
    g_ready = true;
    return true;
}

const char* app_storage_settings_path(void) {
    return initialize() ? g_path : "";
}

/* Reject encrypted garbage or truncated containers masquerading as legacy text. */
static bool valid_payload(const void* data, size_t size) {
    static const char* keys[] = {
        "theme=", "ai_difficulty=", "ai_depth=", "ai_randomness=", "sound_enabled=",
        "sfx_volume=", "menu_music_volume=", "game_music_volume=", "sound_volume=",
        "touch_move_enabled=", "turn_timer_enabled=", "turn_time_seconds=",
        "player_name=", "profile_username=", "username=", "wins=", "losses="
    };
    const unsigned char* text = data;
    bool recognized = false;
    if (data == NULL || size == 0 || size > 65536) return false;
    for (size_t i = 0; i < size; ++i) {
        if (text[i] == 0 || (text[i] < 32 && text[i] != '\r' && text[i] != '\n' && text[i] != '\t')) return false;
        if (i == 0 || text[i - 1] == '\r' || text[i - 1] == '\n') {
            for (size_t k = 0; k < sizeof(keys) / sizeof(keys[0]); ++k) {
                size_t length = strlen(keys[k]);
                if (size - i >= length && memcmp(text + i, keys[k], length) == 0) recognized = true;
            }
        }
    }
    return recognized;
}

static bool read_valid(const char* path, void** data, size_t* size) {
    if (!secure_io_read_file(path, data, size)) return false;
    /* Text editors may add a UTF-8 BOM to a legacy plaintext settings file. */
    if (*size >= 3 && memcmp(*data, "\xEF\xBB\xBF", 3) == 0) {
        memmove(*data, (unsigned char*)*data + 3, *size - 3);
        *size -= 3;
    }
    if (valid_payload(*data, *size)) return true;
    secure_io_free(*data);
    *data = NULL;
    *size = 0;
    return false;
}

bool app_storage_read_settings(void** data, size_t* size) {
    int status;
    int backup_status;
    char dir[CHESS_PATH_MAX];
    char legacy[CHESS_PATH_MAX];
    if (data == NULL || size == NULL) return false;
    *data = NULL;
    *size = 0;
    g_error[0] = '\0';
    g_recovered = false;
    if (!initialize()) return false;
    status = file_io_path_status(g_path);
    if (status < 0) return error(file_io_last_error());
    if (status > 0 && read_valid(g_path, data, size)) return true;
    backup_status = file_io_path_status(g_backup);
    if (backup_status < 0) return error(file_io_last_error());
    if (backup_status > 0 && read_valid(g_backup, data, size)) {
        g_recovered = true;
        return true;
    }
    /* An existing user file takes precedence. Never replace it with install defaults. */
    if (status > 0 || backup_status > 0) return error("Could not read your settings or backup. Change a setting to save new preferences.");
    for (int candidate = 0; candidate < 2; ++candidate) {
        bool found = candidate == 0 ? file_io_executable_directory(dir) : file_io_working_directory(dir);
        if (!found || !file_io_join(legacy, sizeof(legacy), dir, "settings.dat") || strcmp(legacy, g_path) == 0) continue;
        if (file_io_path_status(legacy) <= 0 || !read_valid(legacy, data, size)) continue;
        /* Decrypt before re-encrypting: old files may already use DPAPI. */
        if (!app_storage_write_settings(*data, *size))
            error("Loaded previous settings, but could not migrate them to your user folder.");
        return true;
    }
    return false; /* First run: defaults, no error. */
}

bool app_storage_write_settings(const void* data, size_t size) {
    void* previous = NULL;
    size_t previous_size = 0;
    g_error[0] = '\0';
    if (!initialize()) return false;
    if (!valid_payload(data, size)) return error("Refusing to save invalid settings.");
    if (!file_io_make_directories(g_directory)) return error(file_io_last_error());
    if (file_io_path_status(g_path) > 0 && read_valid(g_path, &previous, &previous_size)) {
        bool backed_up = secure_io_write_file(g_backup, previous, previous_size);
        secure_io_free(previous);
        if (!backed_up) return error(secure_io_last_error());
    }
    if (!secure_io_write_file(g_path, data, size)) return error(secure_io_last_error());
    g_recovered = false;
    return true;
}
