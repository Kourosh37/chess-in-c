#include "game_state.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "audio.h"

/* Default local profile persistence file. */
static const char* PROFILE_PATH = "profile.dat";
static const char* SETTINGS_PATH = "settings.dat";

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
    FILE* file = fopen(SETTINGS_PATH, "r");
    char line[192];
    int legacy_depth = -1;
    int legacy_randomness = -1;
    float legacy_sound_volume = -1.0f;
    bool has_ai_difficulty = false;
    bool has_sfx_volume = false;
    bool has_menu_music_volume = false;
    bool has_game_music_volume = false;

    if (file == NULL) {
        return;
    }

    while (fgets(line, sizeof(line), file) != NULL) {
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
            char* value = line + 12;
            value[strcspn(value, "\r\n")] = '\0';
            strncpy(app->online_name, value, PLAYER_NAME_MAX);
            app->online_name[PLAYER_NAME_MAX] = '\0';
        }
    }

    fclose(file);

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
    match->connected = false;
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
    match->network.invite_code[0] = '\0';
    strncpy(match->status, "Match started.", sizeof(match->status) - 1);
    match->status[sizeof(match->status) - 1] = '\0';
    timestamp_now(match->started_at, &match->started_epoch);
    online_match_reset_board(match);

    if (app->current_online_match == index) {
        sync_app_from_match(app, match, true);
    }
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

    if (notify_peer && match->network.initialized && match->network.peer_addr_len > 0) {
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

    load_settings(app);

    set_default_profile(&app->profile);
    if (!profile_load(&app->profile, PROFILE_PATH)) {
        profile_save(&app->profile, PROFILE_PATH);
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
    app->current_online_match = -1;
    app->leave_confirm_open = false;
    app->exit_confirm_open = false;
    snprintf(app->online_runtime_status,
             sizeof(app->online_runtime_status),
             "No active online match.");
    snprintf(app->lobby_status,
             sizeof(app->lobby_status),
             "Choose Host Game or Join Game.");
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

        profile_save(&app->profile, PROFILE_PATH);
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
    FILE* file;

    if (app == NULL) {
        return false;
    }

    file = fopen(SETTINGS_PATH, "w");
    if (file == NULL) {
        return false;
    }

    fprintf(file, "theme=%d\n", (int)app->theme);
    fprintf(file, "ai_difficulty=%d\n", app->ai_difficulty);
    fprintf(file, "sound_enabled=%d\n", app->sound_enabled ? 1 : 0);
    fprintf(file, "sfx_volume=%.3f\n", app->sfx_volume);
    fprintf(file, "menu_music_volume=%.3f\n", app->menu_music_volume);
    fprintf(file, "game_music_volume=%.3f\n", app->game_music_volume);
    fprintf(file, "online_name=%s\n", app->online_name);

    fclose(file);
    return true;
}
