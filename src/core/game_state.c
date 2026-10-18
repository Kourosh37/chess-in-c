#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "game_state.h"

#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "audio.h"
#include "app_storage.h"
#include "secure_io.h"

#define CASTLE_SECOND_SFX_DELAY_SECONDS 0.11f
#define AI_MIN_DEPTH 2
#define AI_MAX_DEPTH 18
#define AI_MAX_TIME_MS 25000

/* Safely copies one text buffer into fixed-size destination storage. */
static void copy_text(char* dst, size_t dst_size, const char* src) {
    size_t len;

    if (dst == NULL || dst_size == 0U) {
        return;
    }

    if (src == NULL) {
        dst[0] = '\0';
        return;
    }

    len = strlen(src);
    if (len >= dst_size) {
        len = dst_size - 1U;
    }

    if (len > 0U) {
        memcpy(dst, src, len);
    }
    dst[len] = '\0';
}

/* Invalid numeric fields retain their defaults instead of wrapping or becoming NaN. */
static int setting_int(const char* text, int fallback) {
    char* end;
    long value;
    errno = 0;
    value = strtol(text, &end, 10);
    if (end == text || *end != '\0' || errno == ERANGE || value < INT_MIN || value > INT_MAX) return fallback;
    return (int)value;
}

static float setting_float(const char* text, float fallback) {
    char* end;
    float value;
    errno = 0;
    value = strtof(text, &end);
    if (end == text || *end != '\0' || errno == ERANGE || !isfinite(value)) return fallback;
    return value;
}

static uint32_t setting_counter(const char* text, uint32_t fallback) {
    char* end;
    unsigned long long value;
    if (text[0] == '-') return fallback;
    errno = 0;
    value = strtoull(text, &end, 10);
    if (end == text || *end != '\0' || errno == ERANGE || value > UINT32_MAX) return fallback;
    return (uint32_t)value;
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
    if (!isfinite(value) || value < 0.0f) {
        return 0.0f;
    }
    if (value > 1.0f) {
        return 1.0f;
    }
    return value;
}

/* Clamps per-turn timer seconds (0 = disabled, otherwise 10..600). */
static int clamp_turn_time_seconds(int value) {
    if (value <= 0) {
        return 0;
    }
    if (value < 10) {
        return 10;
    }
    if (value > 600) {
        return 600;
    }
    return value;
}

/* Resets active side clock from current timer settings. */
static void reset_turn_clock(ChessApp* app) {
    if (app == NULL) {
        return;
    }

    if (app->turn_timer_enabled && app->turn_time_seconds >= 10) {
        app->turn_time_remaining = (float)app->turn_time_seconds;
    } else {
        app->turn_time_remaining = 0.0f;
    }
}

/* Plays win/lose/draw result SFX based on local player perspective. */
static void play_match_result_sfx(const ChessApp* app, bool has_winner, Side loser) {
    if (!has_winner) {
        audio_play(AUDIO_SFX_GAME_OVER);
        return;
    }

    if (app != NULL && app->mode == MODE_SINGLE) {
        Side winner = (loser == SIDE_WHITE) ? SIDE_BLACK : SIDE_WHITE;
        if (winner == app->human_side) {
            if (audio_is_loaded(AUDIO_SFX_VICTORY)) {
                audio_play(AUDIO_SFX_VICTORY);
            } else {
                audio_play(AUDIO_SFX_GAME_OVER);
            }
        } else {
            audio_play(AUDIO_SFX_GAME_OVER);
        }
        return;
    }

    if (audio_is_loaded(AUDIO_SFX_VICTORY)) {
        audio_play(AUDIO_SFX_VICTORY);
    } else {
        audio_play(AUDIO_SFX_GAME_OVER);
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

    copy_text(logs[*io_count], sizeof(logs[0]), line);
    (*io_count)++;
    *io_scroll = *io_count;
}

/* Maps one user-facing AI difficulty percent into internal search limits. */
void app_set_ai_difficulty(ChessApp* app, int difficulty_percent) {
    int difficulty;
    int depth_span;
    int depth;
    int max_time_ms;

    if (app == NULL) {
        return;
    }

    difficulty = clamp_difficulty_percent(difficulty_percent);
    app->ai_difficulty = difficulty;

    depth_span = AI_MAX_DEPTH - AI_MIN_DEPTH;
    depth = AI_MIN_DEPTH + ((difficulty * depth_span + 50) / 100); /* AI_MIN_DEPTH..AI_MAX_DEPTH */
    if (depth < AI_MIN_DEPTH) {
        depth = AI_MIN_DEPTH;
    }
    if (depth > AI_MAX_DEPTH) {
        depth = AI_MAX_DEPTH;
    }

    max_time_ms = 450 + difficulty * 95;
    if (difficulty >= 80) {
        max_time_ms += 1200;
    }
    if (difficulty >= 90) {
        max_time_ms += 2600;
    }
    if (difficulty >= 97) {
        max_time_ms += 4200;
    }
    if (max_time_ms > AI_MAX_TIME_MS) {
        max_time_ms = AI_MAX_TIME_MS;
    }

    app->ai_limits.depth = depth;
    app->ai_limits.max_time_ms = max_time_ms;
    app->ai_limits.randomness = 0;
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
    bool has_turn_timer_enabled = false;
    bool has_turn_time_seconds = false;
    bool has_settings_payload = false;
    int parsed_turn_timer_enabled = 0;
    int parsed_turn_time_seconds = 0;

    has_settings_payload = app_storage_read_settings(&raw_data, &raw_size);
    if (app_storage_last_error()[0] != '\0') {
        copy_text(app->settings_status, sizeof(app->settings_status), app_storage_last_error());
        app->settings_load_failed = !has_settings_payload;
    } else if (app_storage_recovered_backup()) {
        copy_text(app->settings_status, sizeof(app->settings_status), "Recovered settings from backup.");
    }

    if (has_settings_payload) {
        text = (char*)malloc(raw_size + 1U);
        if (text == NULL) {
            secure_io_free(raw_data);
            app->settings_load_failed = true;
            copy_text(app->settings_status, sizeof(app->settings_status), "Not enough memory to load settings.");
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
                int value = setting_int(line + 6, (int)app->theme);
                if (value < THEME_CLASSIC) {
                    value = THEME_CLASSIC;
                }
                if (value > THEME_OCEAN) {
                    value = THEME_OCEAN;
                }
                app->theme = (ColorTheme)value;
            } else if (strncmp(line, "ai_difficulty=", 14) == 0) {
                int difficulty = setting_int(line + 14, app->ai_difficulty);
                app_set_ai_difficulty(app, difficulty);
                has_ai_difficulty = true;
            } else if (strncmp(line, "ai_depth=", 9) == 0) {
                int depth = setting_int(line + 9, app->ai_limits.depth);
                if (depth < AI_MIN_DEPTH) {
                    depth = AI_MIN_DEPTH;
                }
                if (depth > AI_MAX_DEPTH) {
                    depth = AI_MAX_DEPTH;
                }
                legacy_depth = depth;
            } else if (strncmp(line, "ai_randomness=", 14) == 0) {
                int randomness = setting_int(line + 14, app->ai_limits.randomness);
                if (randomness < 0) {
                    randomness = 0;
                }
                if (randomness > 100) {
                    randomness = 100;
                }
                legacy_randomness = randomness;
            } else if (strncmp(line, "sound_enabled=", 14) == 0) {
                int enabled = setting_int(line + 14, app->sound_enabled);
                app->sound_enabled = (enabled != 0);
            } else if (strncmp(line, "sfx_volume=", 11) == 0) {
                app->sfx_volume = clamp_volume01(setting_float(line + 11, app->sfx_volume));
                has_sfx_volume = true;
            } else if (strncmp(line, "menu_music_volume=", 18) == 0) {
                app->menu_music_volume = clamp_volume01(setting_float(line + 18, app->menu_music_volume));
                has_menu_music_volume = true;
            } else if (strncmp(line, "game_music_volume=", 18) == 0) {
                app->game_music_volume = clamp_volume01(setting_float(line + 18, app->game_music_volume));
                has_game_music_volume = true;
            } else if (strncmp(line, "sound_volume=", 13) == 0) {
                legacy_sound_volume = setting_float(line + 13, -1.0f);
                if (legacy_sound_volume >= 0.0f) legacy_sound_volume = clamp_volume01(legacy_sound_volume);
            } else if (strncmp(line, "touch_move_enabled=", 19) == 0) {
                app->touch_move_enabled = (setting_int(line + 19, 0) != 0);
            } else if (strncmp(line, "turn_timer_enabled=", 19) == 0) {
                has_turn_timer_enabled = true;
                parsed_turn_timer_enabled = (setting_int(line + 19, 0) != 0) ? 1 : 0;
            } else if (strncmp(line, "turn_time_seconds=", 18) == 0) {
                has_turn_time_seconds = true;
                parsed_turn_time_seconds = clamp_turn_time_seconds(setting_int(line + 18, 0));
            } else if (strncmp(line, "player_name=", 12) == 0) {
                const char* value = line + 12;
                strncpy(app->profile.username, value, PLAYER_NAME_MAX);
                app->profile.username[PLAYER_NAME_MAX] = '\0';
            } else if (strncmp(line, "profile_username=", 17) == 0) {
                const char* value = line + 17;
                strncpy(app->profile.username, value, PLAYER_NAME_MAX);
                app->profile.username[PLAYER_NAME_MAX] = '\0';
            } else if (strncmp(line, "username=", 9) == 0) {
                const char* value = line + 9;
                strncpy(app->profile.username, value, PLAYER_NAME_MAX);
                app->profile.username[PLAYER_NAME_MAX] = '\0';
            } else if (strncmp(line, "wins=", 5) == 0) {
                app->profile.wins = setting_counter(line + 5, app->profile.wins);
            } else if (strncmp(line, "losses=", 7) == 0) {
                app->profile.losses = setting_counter(line + 7, app->profile.losses);
            }

            line = strtok(NULL, "\r\n");
        }

        free(text);
    }

    if (!has_ai_difficulty && (legacy_depth >= 0 || legacy_randomness >= 0)) {
        int depth_percent;
        int consistency_percent;
        int blended;
        int clamped_depth = (legacy_depth >= 0) ? legacy_depth : app->ai_limits.depth;
        int clamped_randomness = (legacy_randomness >= 0) ? legacy_randomness : app->ai_limits.randomness;
        int depth_span = AI_MAX_DEPTH - AI_MIN_DEPTH;

        if (clamped_depth < AI_MIN_DEPTH) {
            clamped_depth = AI_MIN_DEPTH;
        }
        if (clamped_depth > AI_MAX_DEPTH) {
            clamped_depth = AI_MAX_DEPTH;
        }
        if (clamped_randomness < 0) {
            clamped_randomness = 0;
        }
        if (clamped_randomness > 100) {
            clamped_randomness = 100;
        }

        if (depth_span <= 0) {
            depth_percent = 100;
        } else {
            depth_percent = ((clamped_depth - AI_MIN_DEPTH) * 100 + (depth_span / 2)) / depth_span;
        }
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

    if (has_turn_time_seconds) {
        app->turn_time_seconds = (parsed_turn_time_seconds >= 10) ? parsed_turn_time_seconds : 0;
    }

    if (has_turn_timer_enabled) {
        if (parsed_turn_timer_enabled == 0) {
            app->turn_timer_enabled = false;
            app->turn_time_seconds = 0;
        } else {
            app->turn_timer_enabled = true;
            if (app->turn_time_seconds < 10) {
                app->turn_time_seconds = 60;
            }
        }
    }

    if (app->profile.username[0] == '\0') {
        copy_text(app->profile.username, sizeof(app->profile.username), "Player");
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
    if (!app->game_over) {
        app->timeout_game_over = false;
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
    app->touch_move_enabled = false;
    app->turn_timer_enabled = false;
    app->turn_time_seconds = 0;
    app->turn_time_remaining = 0.0f;
    app->timeout_game_over = false;
    app->timeout_loser = SIDE_WHITE;
    app->ai_difficulty = 60;
    app_set_ai_difficulty(app, app->ai_difficulty);
    app->sound_enabled = true;
    app->sfx_volume = 1.0f;
    app->menu_music_volume = 0.55f;
    app->game_music_volume = 0.55f;
    set_default_profile(&app->profile);

    load_settings(app);

    position_set_start(&app->position);
    app->selected_square = -1;
    app->last_move_from = -1;
    app->last_move_to = -1;
    app->move_anim_duration = 0.18f;
    app->move_anim_progress = 1.0f;
    app->delayed_sfx_pending = false;
    app->delayed_sfx_timer = 0.0f;
    app->delayed_sfx_id = AUDIO_SFX_MOVE;
    app_refresh_legal_moves(app);
    reset_turn_clock(app);
}

/* Starts a fresh game for the selected mode. */
void app_start_game(ChessApp* app, GameMode mode) {
    app->mode = mode;
    app->screen = SCREEN_PLAY;
    app->has_selection = false;
    app->selected_square = -1;
    app->game_over = false;
    app->timeout_game_over = false;
    app->timeout_loser = SIDE_WHITE;
    app->ai_thinking = false;
    app->move_animating = false;
    app->move_anim_progress = 1.0f;
    app->delayed_sfx_pending = false;
    app->delayed_sfx_timer = 0.0f;
    app->delayed_sfx_id = AUDIO_SFX_MOVE;
    app->last_move_from = -1;
    app->last_move_to = -1;
    app->leave_confirm_open = false;
    app->exit_confirm_open = false;
    app->move_log_count = 0;
    app->move_log_scroll = 0;
    reset_turn_clock(app);

    position_set_start(&app->position);
    app_refresh_legal_moves(app);
    reset_turn_clock(app);
}

/* Returns true when local user is expected to play the current move. */
bool app_is_human_turn(const ChessApp* app) {
    if (app->mode == MODE_SINGLE) {
        return app->position.side_to_move == app->human_side;
    }

    return true;
}

/* Applies a validated move and updates profile counters for single-player endgames. */
bool app_apply_move(ChessApp* app, Move move) {
    Side moving_side = app->position.side_to_move;
    PieceType moving_piece = PIECE_NONE;
    AudioSfx move_sfx = AUDIO_SFX_MOVE;
    bool is_castle = (move.flags & (MOVE_FLAG_KING_CASTLE | MOVE_FLAG_QUEEN_CASTLE)) != 0U;
    bool in_check_after;

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

    app->delayed_sfx_pending = false;
    app->delayed_sfx_timer = 0.0f;
    app->delayed_sfx_id = AUDIO_SFX_MOVE;

    if ((move.flags & MOVE_FLAG_PROMOTION) != 0U) {
        move_sfx = AUDIO_SFX_PROMOTION;
    } else if (is_castle) {
        AudioSfx castle_step_sfx = AUDIO_SFX_MOVE;
        move_sfx = castle_step_sfx;
        app->delayed_sfx_pending = true;
        app->delayed_sfx_timer = CASTLE_SECOND_SFX_DELAY_SECONDS;
        app->delayed_sfx_id = castle_step_sfx;
    } else if ((move.flags & MOVE_FLAG_CAPTURE) != 0U) {
        move_sfx = AUDIO_SFX_CAPTURE;
    }

    audio_play(move_sfx);

    append_move_log_line(app->move_log, &app->move_log_count, &app->move_log_scroll, moving_side, move);

    app->has_selection = false;
    app->selected_square = -1;
    app->timeout_game_over = false;
    app->timeout_loser = SIDE_WHITE;
    app_refresh_legal_moves(app);
    in_check_after = engine_in_check(&app->position, app->position.side_to_move);

    if (!app->game_over && in_check_after) {
        audio_play(AUDIO_SFX_CHECK);
    }

    if (app->game_over) {
        Side loser = app->position.side_to_move;
        play_match_result_sfx(app, in_check_after, loser);
        app->turn_time_remaining = 0.0f;
    } else {
        reset_turn_clock(app);
    }

    if (app->game_over && app->mode == MODE_SINGLE) {
        Side loser = app->position.side_to_move;
        bool checkmate = in_check_after;

        if (checkmate) {
            Side winner = (loser == SIDE_WHITE) ? SIDE_BLACK : SIDE_WHITE;
            if (winner == app->human_side) {
                app->profile.wins++;
            } else {
                app->profile.losses++;
            }
        }

        app_save_settings(app);
    }

    return true;
}

/* Advances transient UI animation state. */
void app_tick(ChessApp* app, float delta_time) {
    if (app->settings_save_pending) {
        app->settings_save_delay -= delta_time;
        if (app->settings_save_delay <= 0.0f) {
            app->settings_save_pending = false;
            app_save_settings(app);
        }
    }

    if (app->delayed_sfx_pending) {
        app->delayed_sfx_timer -= delta_time;
        if (app->delayed_sfx_timer <= 0.0f) {
            audio_play(app->delayed_sfx_id);
            app->delayed_sfx_pending = false;
            app->delayed_sfx_timer = 0.0f;
        }
    }

    if (app->turn_timer_enabled &&
        app->turn_time_seconds >= 10 &&
        app->screen == SCREEN_PLAY &&
        !app->game_over) {
        app->turn_time_remaining -= delta_time;
        if (app->turn_time_remaining <= 0.0f) {
            Side loser = app->position.side_to_move;
            Side winner = (loser == SIDE_WHITE) ? SIDE_BLACK : SIDE_WHITE;

            app->turn_time_remaining = 0.0f;
            app->game_over = true;
            app->timeout_game_over = true;
            app->timeout_loser = loser;
            app->has_selection = false;
            app->selected_square = -1;
            app->move_animating = false;
            app->move_anim_progress = 1.0f;
            app->ai_thinking = false;

            play_match_result_sfx(app, true, loser);

            if (app->mode == MODE_SINGLE) {
                if (winner == app->human_side) {
                    app->profile.wins++;
                } else {
                    app->profile.losses++;
                }
                app_save_settings(app);
            }
        }
    }

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

/* Coalesce slider updates while still saving promptly after any settings change. */
void app_settings_changed(ChessApp* app) {
    app->settings_dirty = true;
    app->settings_save_pending = true;
    app->settings_save_delay = 0.35f;
}

/* Persists selected UI/audio/gameplay settings to the current user's directory. */
bool app_save_settings(ChessApp* app) {
    char payload[1024];
    int written;
    const char* player_name;

    if (app == NULL) {
        return false;
    }


    if (app->settings_load_failed && !app->settings_dirty) return false;

    player_name = (app->profile.username[0] != '\0') ? app->profile.username : "Player";

    written = snprintf(payload,
                       sizeof(payload),
                       "theme=%d\n"
                       "ai_difficulty=%d\n"
                       "touch_move_enabled=%d\n"
                       "turn_timer_enabled=%d\n"
                       "turn_time_seconds=%d\n"
                       "sound_enabled=%d\n"
                       "sfx_volume=%.3f\n"
                       "menu_music_volume=%.3f\n"
                       "game_music_volume=%.3f\n"
                       "player_name=%s\n"
                       "wins=%u\n"
                       "losses=%u\n",
                       (int)app->theme,
                       app->ai_difficulty,
                       app->touch_move_enabled ? 1 : 0,
                       app->turn_timer_enabled ? 1 : 0,
                       app->turn_timer_enabled ? app->turn_time_seconds : 0,
                       app->sound_enabled ? 1 : 0,
                       app->sfx_volume,
                       app->menu_music_volume,
                       app->game_music_volume,
                       player_name,
                       app->profile.wins,
                       app->profile.losses);
    if (written < 0 || (size_t)written >= sizeof(payload)) {
        copy_text(app->settings_status, sizeof(app->settings_status), "Could not encode the settings.");
        return false;
    }

    if (!app_storage_write_settings(payload, (size_t)written)) {
        copy_text(app->settings_status, sizeof(app->settings_status), app_storage_last_error());
        return false;
    }
    app->settings_dirty = false;
    app->settings_save_pending = false;
    app->settings_load_failed = false;
    app->settings_status[0] = '\0';
    return true;
}
