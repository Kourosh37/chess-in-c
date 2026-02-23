#include "game_state.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#include "audio.h"
#include "secure_io.h"

/* Default legacy filenames used before secure storage migration. */
static const char* LEGACY_PROFILE_PATH = "profile.dat";
static const char* LEGACY_SETTINGS_PATH = "settings.dat";
static const char* LEGACY_ONLINE_SESSIONS_PATH = "online_matches.dat";

#define STORAGE_PATH_MAX 512
static char g_profile_path[STORAGE_PATH_MAX] = "profile.dat";
static char g_settings_path[STORAGE_PATH_MAX] = "settings.dat";
static char g_online_sessions_path[STORAGE_PATH_MAX] = "online_matches.dat";
static bool g_storage_paths_ready = false;

#define ONLINE_SESSIONS_MAGIC 0x43484F4EU /* CHON */
#define ONLINE_SESSIONS_VERSION 1U

typedef struct PersistedOnlineHeader {
    uint32_t magic;
    uint32_t version;
    uint32_t count;
} PersistedOnlineHeader;

typedef struct PersistedOnlineMatch {
    uint8_t used;
    uint8_t in_game;
    uint8_t is_host;
    uint8_t local_ready;
    uint8_t peer_ready;
    uint8_t local_side;
    uint8_t game_over;
    uint8_t reserved_a;
    uint8_t reserved_b;
    char invite_code[INVITE_CODE_LEN + 1];
    char opponent_name[PLAYER_NAME_MAX + 1];
    char status[128];
    char started_at[32];
    uint64_t started_epoch;
    Position position;
    int32_t last_move_from;
    int32_t last_move_to;
    int32_t move_log_count;
    int32_t move_log_scroll;
    char move_log[MOVE_LOG_MAX][64];
} PersistedOnlineMatch;

/* Resolves directory of currently running executable. */
static bool resolve_executable_dir(char out_dir[STORAGE_PATH_MAX]) {
    if (out_dir == NULL) {
        return false;
    }

#ifdef _WIN32
    {
        char module_path[STORAGE_PATH_MAX];
        char* slash;
        char* alt;
        DWORD len = GetModuleFileNameA(NULL, module_path, (DWORD)sizeof(module_path));

        if (len == 0U || len >= (DWORD)sizeof(module_path)) {
            return false;
        }
        module_path[len] = '\0';

        slash = strrchr(module_path, '\\');
        alt = strrchr(module_path, '/');
        if (slash == NULL || (alt != NULL && alt > slash)) {
            slash = alt;
        }
        if (slash == NULL) {
            return false;
        }

        *slash = '\0';
        strncpy(out_dir, module_path, STORAGE_PATH_MAX - 1);
        out_dir[STORAGE_PATH_MAX - 1] = '\0';
        return out_dir[0] != '\0';
    }
#else
    {
        char module_path[STORAGE_PATH_MAX];
        ssize_t len = readlink("/proc/self/exe", module_path, sizeof(module_path) - 1);

        if (len > 0 && (size_t)len < sizeof(module_path)) {
            char* slash;
            module_path[len] = '\0';
            slash = strrchr(module_path, '/');
            if (slash != NULL) {
                *slash = '\0';
                strncpy(out_dir, module_path, STORAGE_PATH_MAX - 1);
                out_dir[STORAGE_PATH_MAX - 1] = '\0';
                return out_dir[0] != '\0';
            }
        }

        if (getcwd(out_dir, STORAGE_PATH_MAX) != NULL) {
            out_dir[STORAGE_PATH_MAX - 1] = '\0';
            return true;
        }
    }
#endif

    return false;
}

/* Builds all storage file paths inside one target directory. */
static void set_storage_paths_from_dir(const char* dir) {
    if (dir == NULL || dir[0] == '\0') {
        return;
    }

#ifdef _WIN32
    snprintf(g_profile_path, sizeof(g_profile_path), "%s\\profile.dat", dir);
    snprintf(g_settings_path, sizeof(g_settings_path), "%s\\settings.dat", dir);
    snprintf(g_online_sessions_path, sizeof(g_online_sessions_path), "%s\\online_matches.dat", dir);
#else
    snprintf(g_profile_path, sizeof(g_profile_path), "%s/profile.dat", dir);
    snprintf(g_settings_path, sizeof(g_settings_path), "%s/settings.dat", dir);
    snprintf(g_online_sessions_path, sizeof(g_online_sessions_path), "%s/online_matches.dat", dir);
#endif
}

/* Returns true when a file path currently exists. */
static bool file_exists(const char* path) {
    FILE* file;

    if (path == NULL || path[0] == '\0') {
        return false;
    }

    file = fopen(path, "rb");
    if (file == NULL) {
        return false;
    }

    fclose(file);
    return true;
}

/* Reads one raw file payload without applying encryption/decryption. */
static bool read_raw_file(const char* path, void** out_data, size_t* out_size) {
    FILE* file;
    long length;
    uint8_t* data = NULL;

    if (path == NULL || out_data == NULL || out_size == NULL) {
        return false;
    }

    *out_data = NULL;
    *out_size = 0;

    file = fopen(path, "rb");
    if (file == NULL) {
        return false;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return false;
    }

    length = ftell(file);
    if (length < 0) {
        fclose(file);
        return false;
    }

    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return false;
    }

    if (length > 0) {
        data = (uint8_t*)malloc((size_t)length);
        if (data == NULL) {
            fclose(file);
            return false;
        }

        if (fread(data, 1, (size_t)length, file) != (size_t)length) {
            free(data);
            fclose(file);
            return false;
        }
    }

    fclose(file);
    *out_data = data;
    *out_size = (size_t)length;
    return true;
}

/* Migrates legacy plaintext file into encrypted secure-storage location. */
static void migrate_legacy_file(const char* legacy_path, const char* secure_path) {
    void* raw = NULL;
    size_t raw_size = 0;

    if (legacy_path == NULL || secure_path == NULL) {
        return;
    }

    if (strcmp(legacy_path, secure_path) == 0) {
        return;
    }

    if (file_exists(secure_path) || !file_exists(legacy_path)) {
        return;
    }

    if (!read_raw_file(legacy_path, &raw, &raw_size)) {
        return;
    }

    secure_io_write_file(secure_path, raw, raw_size);
    free(raw);
}

/* Resolves storage paths next to executable and migrates legacy data when needed. */
static void init_storage_paths(void) {
    char exe_dir[STORAGE_PATH_MAX];

    if (g_storage_paths_ready) {
        return;
    }

    strncpy(g_profile_path, LEGACY_PROFILE_PATH, sizeof(g_profile_path) - 1);
    g_profile_path[sizeof(g_profile_path) - 1] = '\0';
    strncpy(g_settings_path, LEGACY_SETTINGS_PATH, sizeof(g_settings_path) - 1);
    g_settings_path[sizeof(g_settings_path) - 1] = '\0';
    strncpy(g_online_sessions_path, LEGACY_ONLINE_SESSIONS_PATH, sizeof(g_online_sessions_path) - 1);
    g_online_sessions_path[sizeof(g_online_sessions_path) - 1] = '\0';

    if (resolve_executable_dir(exe_dir)) {
        set_storage_paths_from_dir(exe_dir);
    }

    g_storage_paths_ready = true;

    migrate_legacy_file(LEGACY_PROFILE_PATH, g_profile_path);
    migrate_legacy_file(LEGACY_SETTINGS_PATH, g_settings_path);
    migrate_legacy_file(LEGACY_ONLINE_SESSIONS_PATH, g_online_sessions_path);

#ifdef _WIN32
    {
        char local_appdata[STORAGE_PATH_MAX];
        DWORD len = GetEnvironmentVariableA("LOCALAPPDATA", local_appdata, (DWORD)sizeof(local_appdata));

        if (len > 0U && len < (DWORD)sizeof(local_appdata)) {
            char secure_dir[STORAGE_PATH_MAX];
            char old_profile_path[STORAGE_PATH_MAX];
            char old_settings_path[STORAGE_PATH_MAX];
            char old_sessions_path[STORAGE_PATH_MAX];

            snprintf(secure_dir, sizeof(secure_dir), "%s\\Chess\\SecureData", local_appdata);
            snprintf(old_profile_path, sizeof(old_profile_path), "%s\\profile.dat", secure_dir);
            snprintf(old_settings_path, sizeof(old_settings_path), "%s\\settings.dat", secure_dir);
            snprintf(old_sessions_path, sizeof(old_sessions_path), "%s\\online_matches.dat", secure_dir);

            migrate_legacy_file(old_profile_path, g_profile_path);
            migrate_legacy_file(old_settings_path, g_settings_path);
            migrate_legacy_file(old_sessions_path, g_online_sessions_path);
        }
    }
#endif
}

/* Clamps AI difficulty percentage into safe 0..100 range. */
static int clamp_difficulty_percent(int value) {
    if (value < 0) {
        return 0;
    }
    if (value > 100) {
        return 100;
    }
    return value;
}

/* Clamps persisted audio volume values to the safe 0..1 range. */
static float clamp_volume01(float value) {
    if (value < 0.0f) {
        return 0.0f;
    }
    if (value > 1.0f) {
        return 1.0f;
    }
    return value;
}

/* Writes local date/time for match metadata list and sorting. */
static void timestamp_now(char out[32], uint64_t* out_epoch) {
    time_t now = time(NULL);
    struct tm tm_value;
    size_t written;

    if (out_epoch != NULL) {
        *out_epoch = (uint64_t)now;
    }

    if (out == NULL) {
        return;
    }

#ifdef _WIN32
    localtime_s(&tm_value, &now);
#else
    localtime_r(&now, &tm_value);
#endif
    written = strftime(out, 32, "%Y-%m-%d %H:%M:%S", &tm_value);
    if (written == 0U) {
        strncpy(out, "unknown", 31);
        out[31] = '\0';
    }
}

/* Converts board square index to algebraic coordinate (e.g. e4). */
static void square_to_text(int square, char out[3]) {
    out[0] = (char)('a' + (square & 7));
    out[1] = (char)('1' + (square >> 3));
    out[2] = '\0';
}

/* Appends one human-readable move entry into a move log array. */
static void append_move_log_line(char logs[MOVE_LOG_MAX][64],
                                 int* io_count,
                                 int* io_scroll,
                                 Side side,
                                 Move move) {
    char from[3];
    char to[3];
    char line[64];
    const char* side_name = (side == SIDE_WHITE) ? "White" : "Black";

    square_to_text(move.from, from);
    square_to_text(move.to, to);

    if ((move.flags & MOVE_FLAG_PROMOTION) != 0U) {
        char promo = 'Q';
        if (move.promotion == PIECE_ROOK) {
            promo = 'R';
        } else if (move.promotion == PIECE_BISHOP) {
            promo = 'B';
        } else if (move.promotion == PIECE_KNIGHT) {
            promo = 'N';
        }
        snprintf(line, sizeof(line), "%s: %s -> %s=%c", side_name, from, to, promo);
    } else {
        snprintf(line, sizeof(line), "%s: %s -> %s", side_name, from, to);
    }

    if (*io_count >= MOVE_LOG_MAX) {
        memmove(logs[0], logs[1], (MOVE_LOG_MAX - 1) * sizeof(logs[0]));
        *io_count = MOVE_LOG_MAX - 1;
    }

    strncpy(logs[*io_count], line, sizeof(logs[0]) - 1);
    logs[*io_count][sizeof(logs[0]) - 1] = '\0';
    (*io_count)++;
    *io_scroll = *io_count;
}

/* Returns true when one index points to a live online match slot. */
static bool online_slot_valid(const ChessApp* app, int index) {
    if (app == NULL) {
        return false;
    }
    if (index < 0 || index >= ONLINE_MATCH_MAX) {
        return false;
    }
    return app->online_matches[index].used;
}

/* Transfers ownership of one network client object into destination slot. */
static void take_network_client(NetworkClient* dest, NetworkClient* src) {
    if (dest == NULL || src == NULL) {
        return;
    }

    *dest = *src;
    memset(src, 0, sizeof(*src));
}

/* Clears/initializes one online match object and optionally shuts its socket. */
static void online_match_clear(OnlineMatch* match, bool shutdown_network) {
    if (match == NULL) {
        return;
    }

    if (shutdown_network && match->network.initialized) {
        network_client_shutdown(&match->network);
    }

    memset(match, 0, sizeof(*match));
}

/* Resets board/move state for a new online match start. */
static void online_match_reset_board(OnlineMatch* match) {
    if (match == NULL) {
        return;
    }

    position_set_start(&match->position);
    match->game_over = false;
    match->last_move_from = -1;
    match->last_move_to = -1;
    match->move_log_count = 0;
    match->move_log_scroll = 0;
}

/* Finds first free online-match slot. */
static int online_find_free_slot(ChessApp* app) {
    if (app == NULL) {
        return -1;
    }

    for (int i = 0; i < ONLINE_MATCH_MAX; ++i) {
        if (!app->online_matches[i].used) {
            return i;
        }
    }
    return -1;
}

/* Copies runtime board data from current app play state into one match slot. */
static void sync_match_from_app(ChessApp* app, OnlineMatch* match) {
    if (app == NULL || match == NULL) {
        return;
    }

    match->position = app->position;
    match->game_over = app->game_over;
    match->last_move_from = app->last_move_from;
    match->last_move_to = app->last_move_to;
    match->local_ready = app->online_local_ready;
    match->peer_ready = app->online_peer_ready;
    match->local_side = app->human_side;

    strncpy(match->status, app->online_runtime_status, sizeof(match->status) - 1);
    match->status[sizeof(match->status) - 1] = '\0';

    match->move_log_count = app->move_log_count;
    if (match->move_log_count < 0) {
        match->move_log_count = 0;
    }
    if (match->move_log_count > MOVE_LOG_MAX) {
        match->move_log_count = MOVE_LOG_MAX;
    }
    match->move_log_scroll = app->move_log_scroll;
    if (match->move_log_scroll < 0) {
        match->move_log_scroll = 0;
    }
    if (match->move_log_scroll > match->move_log_count) {
        match->move_log_scroll = match->move_log_count;
    }

    for (int i = 0; i < match->move_log_count; ++i) {
        strncpy(match->move_log[i], app->move_log[i], sizeof(match->move_log[i]) - 1);
        match->move_log[i][sizeof(match->move_log[i]) - 1] = '\0';
    }
}

/* Loads one match snapshot into current play state. */
static void sync_app_from_match(ChessApp* app, const OnlineMatch* match, bool open_play_screen) {
    if (app == NULL || match == NULL) {
        return;
    }

    app->mode = MODE_ONLINE;
    app->human_side = match->local_side;
    app->position = match->position;
    app_refresh_legal_moves(app);
    app->game_over = match->game_over;
    app->last_move_from = match->last_move_from;
    app->last_move_to = match->last_move_to;

    app->online_match_active = match->in_game;
    app->online_local_ready = match->local_ready;
    app->online_peer_ready = match->peer_ready;
    strncpy(app->online_match_code, match->invite_code, INVITE_CODE_LEN);
    app->online_match_code[INVITE_CODE_LEN] = '\0';
    strncpy(app->online_runtime_status, match->status, sizeof(app->online_runtime_status) - 1);
    app->online_runtime_status[sizeof(app->online_runtime_status) - 1] = '\0';

    app->move_log_count = match->move_log_count;
    if (app->move_log_count < 0) {
        app->move_log_count = 0;
    }
    if (app->move_log_count > MOVE_LOG_MAX) {
        app->move_log_count = MOVE_LOG_MAX;
    }
    app->move_log_scroll = match->move_log_scroll;
    if (app->move_log_scroll < 0) {
        app->move_log_scroll = 0;
    }
    if (app->move_log_scroll > app->move_log_count) {
        app->move_log_scroll = app->move_log_count;
    }

    for (int i = 0; i < app->move_log_count; ++i) {
        strncpy(app->move_log[i], match->move_log[i], sizeof(app->move_log[i]) - 1);
        app->move_log[i][sizeof(app->move_log[i]) - 1] = '\0';
    }

    app->has_selection = false;
    app->selected_square = -1;
    app->move_animating = false;
    app->move_anim_progress = 1.0f;
    app->leave_confirm_open = false;
    app->exit_confirm_open = false;

    if (open_play_screen) {
        app->screen = SCREEN_PLAY;
    }
}

/* Maps one user-facing AI difficulty percent into internal search limits. */
void app_set_ai_difficulty(ChessApp* app, int difficulty_percent) {
    int difficulty;
    int depth;
    int max_time_ms;
    int randomness;

    if (app == NULL) {
        return;
    }

    difficulty = clamp_difficulty_percent(difficulty_percent);
    app->ai_difficulty = difficulty;

    depth = 1 + ((difficulty * 7 + 50) / 100);
    if (depth < 1) {
        depth = 1;
    }
    if (depth > 8) {
        depth = 8;
    }

    max_time_ms = 300 + difficulty * 20;
    if (difficulty >= 90) {
        max_time_ms += 200;
    }

    randomness = (100 - difficulty + 1) / 2;
    randomness = (randomness / 5) * 5;
    if (randomness < 0) {
        randomness = 0;
    }
    if (randomness > 50) {
        randomness = 50;
    }

    app->ai_limits.depth = depth;
    app->ai_limits.max_time_ms = max_time_ms;
    app->ai_limits.randomness = randomness;
}

/* Parses persisted settings key/value pairs into app state. */
static void load_settings(ChessApp* app) {
    void* raw_data = NULL;
    size_t raw_size = 0;
    char* text = NULL;
    char* line;
    int legacy_depth = -1;
    int legacy_randomness = -1;
    float legacy_sound_volume = -1.0f;
    bool has_ai_difficulty = false;
    bool has_sfx_volume = false;
    bool has_menu_music_volume = false;
    bool has_game_music_volume = false;

    init_storage_paths();

    if (!secure_io_read_file(g_settings_path, &raw_data, &raw_size)) {
        if (!read_raw_file(LEGACY_SETTINGS_PATH, &raw_data, &raw_size)) {
            return;
        }
    }

    text = (char*)malloc(raw_size + 1U);
    if (text == NULL) {
        secure_io_free(raw_data);
        return;
    }

    if (raw_size > 0U && raw_data != NULL) {
        memcpy(text, raw_data, raw_size);
    }
    text[raw_size] = '\0';
    secure_io_free(raw_data);

    line = strtok(text, "\r\n");
    while (line != NULL) {
        if (strncmp(line, "theme=", 6) == 0) {
            int value = atoi(line + 6);
            if (value < THEME_CLASSIC) {
                value = THEME_CLASSIC;
            }
            if (value > THEME_OCEAN) {
                value = THEME_OCEAN;
            }
            app->theme = (ColorTheme)value;
        } else if (strncmp(line, "ai_difficulty=", 14) == 0) {
            int difficulty = atoi(line + 14);
            app_set_ai_difficulty(app, difficulty);
            has_ai_difficulty = true;
        } else if (strncmp(line, "ai_depth=", 9) == 0) {
            int depth = atoi(line + 9);
            if (depth < 1) {
                depth = 1;
            }
            if (depth > 8) {
                depth = 8;
            }
            legacy_depth = depth;
        } else if (strncmp(line, "ai_randomness=", 14) == 0) {
            int randomness = atoi(line + 14);
            if (randomness < 0) {
                randomness = 0;
            }
            if (randomness > 100) {
                randomness = 100;
            }
            legacy_randomness = randomness;
        } else if (strncmp(line, "sound_enabled=", 14) == 0) {
            int enabled = atoi(line + 14);
            app->sound_enabled = (enabled != 0);
        } else if (strncmp(line, "sfx_volume=", 11) == 0) {
            app->sfx_volume = clamp_volume01((float)atof(line + 11));
            has_sfx_volume = true;
        } else if (strncmp(line, "menu_music_volume=", 18) == 0) {
            app->menu_music_volume = clamp_volume01((float)atof(line + 18));
            has_menu_music_volume = true;
        } else if (strncmp(line, "game_music_volume=", 18) == 0) {
            app->game_music_volume = clamp_volume01((float)atof(line + 18));
            has_game_music_volume = true;
        } else if (strncmp(line, "sound_volume=", 13) == 0) {
            legacy_sound_volume = clamp_volume01((float)atof(line + 13));
        } else if (strncmp(line, "online_name=", 12) == 0) {
            const char* value = line + 12;
            strncpy(app->online_name, value, PLAYER_NAME_MAX);
            app->online_name[PLAYER_NAME_MAX] = '\0';
        }
        line = strtok(NULL, "\r\n");
    }

    free(text);

    if (!has_ai_difficulty && (legacy_depth >= 0 || legacy_randomness >= 0)) {
        int depth_percent;
        int consistency_percent;
        int blended;
        int clamped_depth = (legacy_depth >= 0) ? legacy_depth : app->ai_limits.depth;
        int clamped_randomness = (legacy_randomness >= 0) ? legacy_randomness : app->ai_limits.randomness;

        if (clamped_depth < 1) {
            clamped_depth = 1;
        }
        if (clamped_depth > 8) {
            clamped_depth = 8;
        }
        if (clamped_randomness < 0) {
            clamped_randomness = 0;
        }
        if (clamped_randomness > 100) {
            clamped_randomness = 100;
        }

        depth_percent = ((clamped_depth - 1) * 100 + 3) / 7;
        consistency_percent = 100 - clamped_randomness;
        blended = (depth_percent * 65 + consistency_percent * 35 + 50) / 100;
        app_set_ai_difficulty(app, blended);
    }

    if (legacy_sound_volume >= 0.0f) {
        if (!has_sfx_volume) {
            app->sfx_volume = legacy_sound_volume;
        }
        if (!has_menu_music_volume) {
            app->menu_music_volume = legacy_sound_volume;
        }
        if (!has_game_music_volume) {
            app->game_music_volume = legacy_sound_volume;
        }
    }
}

/* Persists online session slots for resume-after-restart UX. */
static bool save_online_sessions_internal(const ChessApp* app) {
    PersistedOnlineHeader header;
    PersistedOnlineMatch records[ONLINE_MATCH_MAX];
    uint8_t* blob;
    size_t blob_size;
    bool ok;

    if (app == NULL) {
        return false;
    }

    memset(records, 0, sizeof(records));

    for (int i = 0; i < ONLINE_MATCH_MAX; ++i) {
        const OnlineMatch* match = &app->online_matches[i];
        PersistedOnlineMatch* rec = &records[i];

        rec->used = match->used ? 1U : 0U;
        rec->in_game = match->in_game ? 1U : 0U;
        rec->is_host = match->is_host ? 1U : 0U;
        rec->local_ready = match->local_ready ? 1U : 0U;
        rec->peer_ready = match->peer_ready ? 1U : 0U;
        rec->local_side = (uint8_t)match->local_side;
        rec->game_over = match->game_over ? 1U : 0U;

        strncpy(rec->invite_code, match->invite_code, INVITE_CODE_LEN);
        rec->invite_code[INVITE_CODE_LEN] = '\0';
        strncpy(rec->opponent_name, match->opponent_name, PLAYER_NAME_MAX);
        rec->opponent_name[PLAYER_NAME_MAX] = '\0';
        strncpy(rec->status, match->status, sizeof(rec->status) - 1);
        rec->status[sizeof(rec->status) - 1] = '\0';
        strncpy(rec->started_at, match->started_at, sizeof(rec->started_at) - 1);
        rec->started_at[sizeof(rec->started_at) - 1] = '\0';

        rec->started_epoch = match->started_epoch;
        rec->position = match->position;
        rec->last_move_from = (int32_t)match->last_move_from;
        rec->last_move_to = (int32_t)match->last_move_to;
        rec->move_log_count = (int32_t)match->move_log_count;
        rec->move_log_scroll = (int32_t)match->move_log_scroll;

        if (rec->move_log_count < 0) {
            rec->move_log_count = 0;
        }
        if (rec->move_log_count > MOVE_LOG_MAX) {
            rec->move_log_count = MOVE_LOG_MAX;
        }
        if (rec->move_log_scroll < 0) {
            rec->move_log_scroll = 0;
        }
        if (rec->move_log_scroll > rec->move_log_count) {
            rec->move_log_scroll = rec->move_log_count;
        }

        for (int m = 0; m < rec->move_log_count; ++m) {
            strncpy(rec->move_log[m], match->move_log[m], sizeof(rec->move_log[m]) - 1);
            rec->move_log[m][sizeof(rec->move_log[m]) - 1] = '\0';
        }
    }

    header.magic = ONLINE_SESSIONS_MAGIC;
    header.version = ONLINE_SESSIONS_VERSION;
    header.count = ONLINE_MATCH_MAX;

    init_storage_paths();

    blob_size = sizeof(header) + sizeof(records);
    blob = (uint8_t*)malloc(blob_size);
    if (blob == NULL) {
        return false;
    }

    memcpy(blob, &header, sizeof(header));
    memcpy(blob + sizeof(header), records, sizeof(records));

    ok = secure_io_write_file(g_online_sessions_path, blob, blob_size);
    free(blob);
    return ok;
}

/* Loads persisted online sessions and marks them disconnected for reconnect. */
static void load_online_sessions_internal(ChessApp* app) {
    PersistedOnlineHeader header;
    PersistedOnlineMatch records[ONLINE_MATCH_MAX];
    void* blob = NULL;
    size_t blob_size = 0;
    const uint8_t* bytes;

    if (app == NULL) {
        return;
    }

    init_storage_paths();

    if (!secure_io_read_file(g_online_sessions_path, &blob, &blob_size)) {
        return;
    }

    if (blob_size < sizeof(header) + sizeof(records)) {
        secure_io_free(blob);
        return;
    }

    bytes = (const uint8_t*)blob;
    memcpy(&header, bytes, sizeof(header));
    memcpy(records, bytes + sizeof(header), sizeof(records));
    secure_io_free(blob);

    if (header.magic != ONLINE_SESSIONS_MAGIC ||
        header.version != ONLINE_SESSIONS_VERSION ||
        header.count != ONLINE_MATCH_MAX) {
        return;
    }

    for (int i = 0; i < ONLINE_MATCH_MAX; ++i) {
        OnlineMatch* match = &app->online_matches[i];
        const PersistedOnlineMatch* rec = &records[i];

        if (!rec->used) {
            memset(match, 0, sizeof(*match));
            continue;
        }

        memset(match, 0, sizeof(*match));
        match->used = true;
        match->in_game = (rec->in_game != 0U);
        match->connected = false;
        match->is_host = (rec->is_host != 0U);
        match->local_ready = (rec->local_ready != 0U);
        match->peer_ready = (rec->peer_ready != 0U);
        match->local_side = (rec->local_side == SIDE_BLACK) ? SIDE_BLACK : SIDE_WHITE;
        match->game_over = (rec->game_over != 0U);

        strncpy(match->invite_code, rec->invite_code, INVITE_CODE_LEN);
        match->invite_code[INVITE_CODE_LEN] = '\0';
        strncpy(match->opponent_name, rec->opponent_name, PLAYER_NAME_MAX);
        match->opponent_name[PLAYER_NAME_MAX] = '\0';
        strncpy(match->status, rec->status, sizeof(match->status) - 1);
        match->status[sizeof(match->status) - 1] = '\0';
        strncpy(match->started_at, rec->started_at, sizeof(match->started_at) - 1);
        match->started_at[sizeof(match->started_at) - 1] = '\0';
        match->started_epoch = rec->started_epoch;

        match->position = rec->position;
        match->last_move_from = rec->last_move_from;
        match->last_move_to = rec->last_move_to;
        match->move_log_count = rec->move_log_count;
        match->move_log_scroll = rec->move_log_scroll;

        if (match->move_log_count < 0) {
            match->move_log_count = 0;
        }
        if (match->move_log_count > MOVE_LOG_MAX) {
            match->move_log_count = MOVE_LOG_MAX;
        }
        if (match->move_log_scroll < 0) {
            match->move_log_scroll = 0;
        }
        if (match->move_log_scroll > match->move_log_count) {
            match->move_log_scroll = match->move_log_count;
        }

        for (int m = 0; m < match->move_log_count; ++m) {
            strncpy(match->move_log[m], rec->move_log[m], sizeof(match->move_log[m]) - 1);
            match->move_log[m][sizeof(match->move_log[m]) - 1] = '\0';
        }

        if (match->status[0] == '\0') {
            strncpy(match->status,
                    "Saved session loaded. Open and reconnect when online.",
                    sizeof(match->status) - 1);
            match->status[sizeof(match->status) - 1] = '\0';
        }
    }
}

/* Initializes a profile object with safe defaults. */
static void set_default_profile(Profile* profile) {
    memset(profile, 0, sizeof(*profile));
    strncpy(profile->username, "Player", PLAYER_NAME_MAX);
    profile->username[PLAYER_NAME_MAX] = '\0';
}

/* Recomputes legal moves and updates terminal game-state flag. */
void app_refresh_legal_moves(ChessApp* app) {
    generate_legal_moves(&app->position, &app->legal_moves);
    app->game_over = (app->legal_moves.count == 0);
}

/* Returns pointer to one online match slot, or NULL when invalid. */
OnlineMatch* app_online_get(ChessApp* app, int index) {
    if (!online_slot_valid(app, index)) {
        return NULL;
    }
    return &app->online_matches[index];
}

/* Returns const pointer to one online match slot, or NULL when invalid. */
const OnlineMatch* app_online_get_const(const ChessApp* app, int index) {
    if (!online_slot_valid(app, index)) {
        return NULL;
    }
    return &app->online_matches[index];
}

/* Counts currently alive online match sessions (started or waiting). */
int app_online_active_count(const ChessApp* app) {
    int count = 0;

    if (app == NULL) {
        return 0;
    }

    for (int i = 0; i < ONLINE_MATCH_MAX; ++i) {
        if (app->online_matches[i].used) {
            count++;
        }
    }
    return count;
}

/* Returns true when user has set online display name. */
bool app_online_name_is_set(const ChessApp* app) {
    if (app == NULL) {
        return false;
    }
    return app->online_name[0] != '\0';
}

/* Persists active online sessions to local storage. */
bool app_online_save_sessions(const ChessApp* app) {
    return save_online_sessions_internal(app);
}

/* Opens one global network error popup with title and detail text. */
void app_show_network_error(ChessApp* app, const char* title, const char* message) {
    if (app == NULL) {
        return;
    }

    app->network_error_popup_open = true;
    strncpy(app->network_error_popup_title,
            (title != NULL && title[0] != '\0') ? title : "Network Error",
            sizeof(app->network_error_popup_title) - 1);
    app->network_error_popup_title[sizeof(app->network_error_popup_title) - 1] = '\0';
    strncpy(app->network_error_popup_text,
            (message != NULL && message[0] != '\0') ? message : "Unknown network failure.",
            sizeof(app->network_error_popup_text) - 1);
    app->network_error_popup_text[sizeof(app->network_error_popup_text) - 1] = '\0';
}

/* Closes currently shown network error popup. */
void app_clear_network_error(ChessApp* app) {
    if (app == NULL) {
        return;
    }

    app->network_error_popup_open = false;
    app->network_error_popup_title[0] = '\0';
    app->network_error_popup_text[0] = '\0';
}

/* Exposes resolved encrypted profile storage path for shutdown save flow. */
const char* app_profile_storage_path(void) {
    init_storage_paths();
    return g_profile_path;
}

/* Saves current on-screen online match board/log into persistent slot. */
void app_online_store_current_match(ChessApp* app) {
    OnlineMatch* match;

    if (app == NULL || app->mode != MODE_ONLINE) {
        return;
    }

    match = app_online_get(app, app->current_online_match);
    if (match == NULL) {
        return;
    }

    sync_match_from_app(app, match);
    save_online_sessions_internal(app);
}

/* Switches app context to another online match slot (play or lobby). */
bool app_online_switch_to_match(ChessApp* app, int index, bool open_play_screen) {
    OnlineMatch* match;

    if (app == NULL) {
        return false;
    }

    match = app_online_get(app, index);
    if (match == NULL) {
        return false;
    }

    if (app->current_online_match >= 0 && app->current_online_match != index) {
        app_online_store_current_match(app);
    }

    app->current_online_match = index;
    sync_app_from_match(app, match, open_play_screen);
    return true;
}

/* Reconnects one persisted/disconnected online match slot to direct P2P room. */
bool app_online_reconnect_match(ChessApp* app, int index) {
    OnlineMatch* match;

    if (app == NULL || !app_online_name_is_set(app)) {
        return false;
    }

    match = app_online_get(app, index);
    if (match == NULL || !match->used || match->invite_code[0] == '\0') {
        return false;
    }
    if (match->network.initialized) {
        network_client_shutdown(&match->network);
    }

    if (!network_client_init(&match->network, 0)) {
        return false;
    }

    if (match->is_host) {
        if (!network_client_host_reconnect(&match->network, app->online_name, match->invite_code)) {
            network_client_shutdown(&match->network);
            return false;
        }
        match->network.host_side = match->local_side;
        match->connected = false;
        strncpy(match->status,
                "Reconnected as host. Waiting for opponent.",
                sizeof(match->status) - 1);
        match->status[sizeof(match->status) - 1] = '\0';
    } else {
        if (!network_client_join(&match->network, app->online_name, match->invite_code)) {
            network_client_shutdown(&match->network);
            return false;
        }
        match->connected = match->network.connected;
        strncpy(match->status,
                match->connected ? "Reconnected to room."
                                 : "Reconnect request sent.",
                sizeof(match->status) - 1);
        match->status[sizeof(match->status) - 1] = '\0';
    }

    if (app->current_online_match == index) {
        sync_app_from_match(app, match, false);
    }

    save_online_sessions_internal(app);
    return true;
}

/* Attaches one pre-connected host client (built by async worker) into a slot. */
int app_online_attach_host_client(ChessApp* app, NetworkClient* client, const char* invite_code) {
    int slot;
    OnlineMatch* match;

    if (app == NULL || client == NULL || !client->initialized) {
        return -1;
    }

    slot = online_find_free_slot(app);
    if (slot < 0) {
        return -1;
    }

    match = &app->online_matches[slot];
    online_match_clear(match, false);
    take_network_client(&match->network, client);

    if (!match->network.initialized) {
        return -1;
    }

    match->used = true;
    match->in_game = false;
    match->connected = false;
    match->is_host = true;
    match->local_ready = false;
    match->peer_ready = false;
    match->local_side = match->network.host_side;
    strncpy(match->opponent_name, "Waiting...", PLAYER_NAME_MAX);
    match->opponent_name[PLAYER_NAME_MAX] = '\0';

    if (invite_code != NULL && invite_code[0] != '\0') {
        strncpy(match->invite_code, invite_code, INVITE_CODE_LEN);
    } else {
        strncpy(match->invite_code, match->network.invite_code, INVITE_CODE_LEN);
    }
    match->invite_code[INVITE_CODE_LEN] = '\0';

    strncpy(match->status, "Waiting for player to join room.", sizeof(match->status) - 1);
    match->status[sizeof(match->status) - 1] = '\0';
    timestamp_now(match->started_at, &match->started_epoch);
    online_match_reset_board(match);
    save_online_sessions_internal(app);

    return slot;
}

/* Attaches one pre-connected join client (built by async worker) into a slot. */
int app_online_attach_join_client(ChessApp* app, NetworkClient* client, const char* invite_code) {
    int slot;
    OnlineMatch* match;

    if (app == NULL || client == NULL || !client->initialized || invite_code == NULL) {
        return -1;
    }
    if (!matchmaker_is_valid_code(invite_code)) {
        return -1;
    }

    slot = online_find_free_slot(app);
    if (slot < 0) {
        return -1;
    }

    match = &app->online_matches[slot];
    online_match_clear(match, false);
    take_network_client(&match->network, client);

    if (!match->network.initialized) {
        return -1;
    }

    match->used = true;
    match->in_game = false;
    match->connected = match->network.connected;
    match->is_host = false;
    match->local_ready = false;
    match->peer_ready = false;
    match->local_side = SIDE_BLACK;
    strncpy(match->invite_code, invite_code, INVITE_CODE_LEN);
    match->invite_code[INVITE_CODE_LEN] = '\0';
    strncpy(match->opponent_name, "Host", PLAYER_NAME_MAX);
    match->opponent_name[PLAYER_NAME_MAX] = '\0';
    strncpy(match->status, "Join request sent.", sizeof(match->status) - 1);
    match->status[sizeof(match->status) - 1] = '\0';
    timestamp_now(match->started_at, &match->started_epoch);
    online_match_reset_board(match);
    save_online_sessions_internal(app);

    return slot;
}

/* Replaces one existing match socket with async reconnect result. */
bool app_online_attach_reconnect_client(ChessApp* app,
                                        int index,
                                        NetworkClient* client,
                                        bool is_host_reconnect) {
    OnlineMatch* match;

    if (app == NULL || client == NULL || !client->initialized) {
        return false;
    }

    match = app_online_get(app, index);
    if (match == NULL || !match->used) {
        return false;
    }

    if (match->network.initialized) {
        network_client_shutdown(&match->network);
    }

    take_network_client(&match->network, client);
    match->is_host = is_host_reconnect;
    if (is_host_reconnect) {
        match->network.host_side = match->local_side;
    }

    if (is_host_reconnect) {
        match->connected = false;
        strncpy(match->status,
                "Reconnected as host. Waiting for opponent.",
                sizeof(match->status) - 1);
    } else {
        match->connected = match->network.connected;
        strncpy(match->status,
                match->connected ? "Reconnected to room."
                                 : "Reconnect request sent.",
                sizeof(match->status) - 1);
    }
    match->status[sizeof(match->status) - 1] = '\0';

    if (app->current_online_match == index) {
        sync_app_from_match(app, match, false);
    }

    save_online_sessions_internal(app);
    return true;
}

/* Creates one new host room as an active online match slot. */
int app_online_create_host(ChessApp* app, const char* username) {
    int slot;
    OnlineMatch* match;

    if (app == NULL || username == NULL || username[0] == '\0') {
        return -1;
    }

    slot = online_find_free_slot(app);
    if (slot < 0) {
        return -1;
    }

    match = &app->online_matches[slot];
    online_match_clear(match, false);

    if (!network_client_init(&match->network, 0)) {
        online_match_clear(match, false);
        return -1;
    }
    if (!network_client_host(&match->network, username, match->invite_code)) {
        online_match_clear(match, true);
        return -1;
    }

    match->used = true;
    match->in_game = false;
    match->connected = false;
    match->is_host = true;
    match->local_ready = false;
    match->peer_ready = false;
    match->local_side = match->network.host_side;
    strncpy(match->opponent_name, "Waiting...", PLAYER_NAME_MAX);
    match->opponent_name[PLAYER_NAME_MAX] = '\0';
    strncpy(match->status, "Waiting for player to join room.", sizeof(match->status) - 1);
    match->status[sizeof(match->status) - 1] = '\0';
    timestamp_now(match->started_at, &match->started_epoch);
    online_match_reset_board(match);
    save_online_sessions_internal(app);

    return slot;
}

/* Creates one join-room request as a new active online match slot. */
int app_online_create_join(ChessApp* app, const char* username, const char* invite_code) {
    int slot;
    OnlineMatch* match;

    if (app == NULL || username == NULL || username[0] == '\0' || invite_code == NULL) {
        return -1;
    }
    if (!matchmaker_is_valid_code(invite_code)) {
        return -1;
    }

    slot = online_find_free_slot(app);
    if (slot < 0) {
        return -1;
    }

    match = &app->online_matches[slot];
    online_match_clear(match, false);

    if (!network_client_init(&match->network, 0)) {
        online_match_clear(match, false);
        return -1;
    }
    if (!network_client_join(&match->network, username, invite_code)) {
        online_match_clear(match, true);
        return -1;
    }

    match->used = true;
    match->in_game = false;
    match->connected = match->network.connected;
    match->is_host = false;
    match->local_ready = false;
    match->peer_ready = false;
    match->local_side = SIDE_BLACK;
    strncpy(match->invite_code, invite_code, INVITE_CODE_LEN);
    match->invite_code[INVITE_CODE_LEN] = '\0';
    strncpy(match->opponent_name, "Host", PLAYER_NAME_MAX);
    match->opponent_name[PLAYER_NAME_MAX] = '\0';
    strncpy(match->status, "Join request sent.", sizeof(match->status) - 1);
    match->status[sizeof(match->status) - 1] = '\0';
    timestamp_now(match->started_at, &match->started_epoch);
    online_match_reset_board(match);
    save_online_sessions_internal(app);

    return slot;
}

/* Sends ready/unready flag for one online room slot. */
bool app_online_send_ready(ChessApp* app, int index, bool ready) {
    OnlineMatch* match = app_online_get(app, index);

    if (match == NULL || !match->used || match->in_game || !match->network.connected) {
        return false;
    }
    if (!network_client_send_ready(&match->network, ready)) {
        return false;
    }

    match->local_ready = ready;
    if (app->current_online_match == index) {
        app->online_local_ready = ready;
    }
    return true;
}

/* Sends match-start packet from host room slot. */
bool app_online_send_start(ChessApp* app, int index) {
    OnlineMatch* match = app_online_get(app, index);

    if (match == NULL || !match->used || !match->is_host || !match->network.connected || match->in_game) {
        return false;
    }
    return network_client_send_start(&match->network);
}

/* Marks one online slot as started and resets board for a new game. */
void app_online_mark_started(ChessApp* app, int index) {
    OnlineMatch* match = app_online_get(app, index);

    if (match == NULL) {
        return;
    }

    match->in_game = true;
    match->local_ready = false;
    match->peer_ready = false;
    strncpy(match->status, "Match started.", sizeof(match->status) - 1);
    match->status[sizeof(match->status) - 1] = '\0';
    timestamp_now(match->started_at, &match->started_epoch);
    online_match_reset_board(match);

    if (app->current_online_match == index) {
        sync_app_from_match(app, match, true);
    }
    save_online_sessions_internal(app);
}

/* Closes one online slot and optionally notifies current peer with LEAVE. */
void app_online_close_match(ChessApp* app, int index, bool notify_peer) {
    OnlineMatch* match;
    bool was_current;

    if (!online_slot_valid(app, index)) {
        return;
    }

    match = &app->online_matches[index];
    was_current = (app->current_online_match == index);

    if (notify_peer && match->network.initialized && match->network.relay_connected && match->connected) {
        network_client_send_leave(&match->network);
    }

    online_match_clear(match, true);

    if (app->lobby_focus_match == index) {
        app->lobby_focus_match = -1;
    }

    if (was_current) {
        app->current_online_match = -1;
        app->mode = MODE_SINGLE;
        app->online_match_active = false;
        app->online_local_ready = false;
        app->online_peer_ready = false;
        app->online_match_code[0] = '\0';
        snprintf(app->online_runtime_status,
                 sizeof(app->online_runtime_status),
                 "No active online match.");
        if (app->screen == SCREEN_PLAY) {
            app->screen = SCREEN_MENU;
        }
    }

    if (app->online_leave_notice_match == index) {
        app->online_leave_notice_match = -1;
        app->online_leave_notice_open = false;
        app->online_leave_notice_title[0] = '\0';
        app->online_leave_notice_text[0] = '\0';
    }

    save_online_sessions_internal(app);
}

/* Closes all active online slots. */
void app_online_close_all(ChessApp* app, bool notify_peer) {
    if (app == NULL) {
        return;
    }

    for (int i = 0; i < ONLINE_MATCH_MAX; ++i) {
        if (app->online_matches[i].used) {
            app_online_close_match(app, i, notify_peer);
        }
    }
}

/* Initializes full application state and dependent modules. */
void app_init(ChessApp* app) {
    memset(app, 0, sizeof(*app));

    srand((unsigned int)time(NULL));

    engine_init();
    engine_reset_transposition_table();

    app->mode = MODE_SINGLE;
    app->screen = SCREEN_MENU;
    app->theme = THEME_CLASSIC;

    app->human_side = SIDE_WHITE;
    app->ai_difficulty = 60;
    app_set_ai_difficulty(app, app->ai_difficulty);
    app->sound_enabled = true;
    app->sfx_volume = 1.0f;
    app->menu_music_volume = 0.55f;
    app->game_music_volume = 0.55f;
    app->online_name[0] = '\0';
    app->online_name_input[0] = '\0';

    init_storage_paths();
    load_settings(app);

    set_default_profile(&app->profile);
    if (!profile_load(&app->profile, g_profile_path)) {
        profile_save(&app->profile, g_profile_path);
    }

    position_set_start(&app->position);
    app->selected_square = -1;
    app->last_move_from = -1;
    app->last_move_to = -1;
    app->move_anim_duration = 0.18f;
    app->move_anim_progress = 1.0f;
    app_refresh_legal_moves(app);

    app->lobby_input[0] = '\0';
    app->lobby_code[0] = '\0';
    app->lobby_view = LOBBY_VIEW_HOME;
    app->lobby_focus_match = -1;
    app->lobby_active_scroll = 0;
    app->lobby_copy_feedback_timer = 0.0f;
    app->lobby_copy_feedback = false;
    app->lobby_input_active = false;
    app->move_log_count = 0;
    app->move_log_scroll = 0;

    app->online_match_code[0] = '\0';
    app->online_match_active = false;
    app->online_local_ready = false;
    app->online_peer_ready = false;
    app->online_leave_notice_open = false;
    app->online_leave_notice_match = -1;
    app->online_leave_notice_title[0] = '\0';
    app->online_leave_notice_text[0] = '\0';
    app->network_error_popup_open = false;
    app->network_error_popup_title[0] = '\0';
    app->network_error_popup_text[0] = '\0';
    app->online_loading = false;
    app->online_loading_action = ONLINE_ASYNC_NONE;
    app->online_loading_match_index = -1;
    app->online_loading_reconnect_host = false;
    app->online_loading_code[0] = '\0';
    app->online_loading_title[0] = '\0';
    app->online_loading_text[0] = '\0';
    app->current_online_match = -1;
    app->leave_confirm_open = false;
    app->exit_confirm_open = false;
    snprintf(app->online_runtime_status,
             sizeof(app->online_runtime_status),
             "No active online match.");
    snprintf(app->lobby_status,
             sizeof(app->lobby_status),
             "Choose Host Game or Join Game.");

    load_online_sessions_internal(app);
}

/* Starts a fresh game for the selected mode. */
void app_start_game(ChessApp* app, GameMode mode) {
    app->mode = mode;
    app->screen = SCREEN_PLAY;
    app->has_selection = false;
    app->selected_square = -1;
    app->game_over = false;
    app->ai_thinking = false;
    app->move_animating = false;
    app->move_anim_progress = 1.0f;
    app->last_move_from = -1;
    app->last_move_to = -1;
    app->leave_confirm_open = false;
    app->exit_confirm_open = false;
    app->move_log_count = 0;
    app->move_log_scroll = 0;

    if (mode == MODE_ONLINE) {
        OnlineMatch* match = app_online_get(app, app->current_online_match);
        if (match != NULL) {
            sync_app_from_match(app, match, true);
            return;
        }
    }

    position_set_start(&app->position);
    app_refresh_legal_moves(app);
}

/* Returns true when local user is expected to play the current move. */
bool app_is_human_turn(const ChessApp* app) {
    if (app->mode == MODE_SINGLE || app->mode == MODE_ONLINE) {
        return app->position.side_to_move == app->human_side;
    }

    return true;
}

/* Applies a validated move and updates profile counters for single-player endgames. */
bool app_apply_move(ChessApp* app, Move move) {
    Side moving_side = app->position.side_to_move;
    PieceType moving_piece = PIECE_NONE;
    AudioSfx move_sfx = AUDIO_SFX_MOVE;

    position_piece_at(&app->position, move.from, NULL, &moving_piece);

    if (!engine_make_move(&app->position, move)) {
        return false;
    }

    if ((move.flags & MOVE_FLAG_PROMOTION) != 0U) {
        if (move.promotion >= PIECE_KNIGHT && move.promotion <= PIECE_QUEEN) {
            moving_piece = (PieceType)move.promotion;
        } else {
            moving_piece = PIECE_QUEEN;
        }
    } else if (moving_piece == PIECE_NONE) {
        moving_piece = PIECE_PAWN;
    }

    app->last_move_from = move.from;
    app->last_move_to = move.to;
    app->move_animating = true;
    app->move_anim_from = move.from;
    app->move_anim_to = move.to;
    app->move_anim_side = moving_side;
    app->move_anim_piece = moving_piece;
    app->move_anim_progress = 0.0f;

    if ((move.flags & MOVE_FLAG_PROMOTION) != 0U) {
        move_sfx = AUDIO_SFX_PROMOTION;
    } else if ((move.flags & (MOVE_FLAG_KING_CASTLE | MOVE_FLAG_QUEEN_CASTLE)) != 0U) {
        move_sfx = AUDIO_SFX_CASTLE;
    } else if ((move.flags & MOVE_FLAG_CAPTURE) != 0U) {
        move_sfx = AUDIO_SFX_CAPTURE;
    }

    audio_play(move_sfx);

    append_move_log_line(app->move_log, &app->move_log_count, &app->move_log_scroll, moving_side, move);

    app->has_selection = false;
    app->selected_square = -1;
    app_refresh_legal_moves(app);

    if (engine_in_check(&app->position, app->position.side_to_move)) {
        audio_play(AUDIO_SFX_CHECK);
    }

    if (app->game_over) {
        audio_play(AUDIO_SFX_GAME_OVER);
    }

    if (app->game_over && app->mode == MODE_SINGLE) {
        Side loser = app->position.side_to_move;
        bool checkmate = engine_in_check(&app->position, loser);

        if (checkmate) {
            Side winner = (loser == SIDE_WHITE) ? SIDE_BLACK : SIDE_WHITE;
            profile_record_result(&app->profile, winner == app->human_side);
        }

        profile_save(&app->profile, g_profile_path);
    }

    if (app->mode == MODE_ONLINE) {
        OnlineMatch* match = app_online_get(app, app->current_online_match);
        if (match != NULL) {
            sync_match_from_app(app, match);
            if (app->game_over) {
                match->in_game = false;
                strncpy(match->status,
                        engine_in_check(&app->position, app->position.side_to_move)
                            ? "Match ended by checkmate."
                            : "Match ended by draw.",
                        sizeof(match->status) - 1);
                match->status[sizeof(match->status) - 1] = '\0';
            }
            save_online_sessions_internal(app);
        }
    }

    return true;
}

/* Advances transient UI animation state. */
void app_tick(ChessApp* app, float delta_time) {
    if (!app->move_animating) {
        return;
    }

    if (app->move_anim_duration <= 0.0f) {
        app->move_animating = false;
        app->move_anim_progress = 1.0f;
        return;
    }

    app->move_anim_progress += delta_time / app->move_anim_duration;
    if (app->move_anim_progress >= 1.0f) {
        app->move_anim_progress = 1.0f;
        app->move_animating = false;
    }
}

/* Leaves and clears currently selected online match session state. */
void app_online_end_match(ChessApp* app, bool notify_peer) {
    if (app == NULL || app->current_online_match < 0) {
        return;
    }

    app_online_close_match(app, app->current_online_match, notify_peer);
    snprintf(app->lobby_status, sizeof(app->lobby_status), "Online match closed.");
}

/* Persists selected UI/audio/gameplay settings to local settings file. */
bool app_save_settings(const ChessApp* app) {
    char payload[1024];
    int written;

    if (app == NULL) {
        return false;
    }

    init_storage_paths();

    written = snprintf(payload,
                       sizeof(payload),
                       "theme=%d\n"
                       "ai_difficulty=%d\n"
                       "sound_enabled=%d\n"
                       "sfx_volume=%.3f\n"
                       "menu_music_volume=%.3f\n"
                       "game_music_volume=%.3f\n"
                       "online_name=%s\n",
                       (int)app->theme,
                       app->ai_difficulty,
                       app->sound_enabled ? 1 : 0,
                       app->sfx_volume,
                       app->menu_music_volume,
                       app->game_music_volume,
                       app->online_name);
    if (written < 0 || (size_t)written >= sizeof(payload)) {
        return false;
    }

    return secure_io_write_file(g_settings_path, payload, (size_t)written);
}
