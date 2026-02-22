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

/* Converts board square index to algebraic coordinate (e.g. e4). */
static void square_to_text(int square, char out[3]) {
    out[0] = (char)('a' + (square & 7));
    out[1] = (char)('1' + (square >> 3));
    out[2] = '\0';
}

/* Appends one human-readable move entry into scrollable move log. */
static void append_move_log(ChessApp* app, Side side, Move move) {
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

    if (app->move_log_count >= MOVE_LOG_MAX) {
        memmove(app->move_log[0], app->move_log[1], (MOVE_LOG_MAX - 1) * sizeof(app->move_log[0]));
        app->move_log_count = MOVE_LOG_MAX - 1;
    }

    strncpy(app->move_log[app->move_log_count], line, sizeof(app->move_log[0]) - 1);
    app->move_log[app->move_log_count][sizeof(app->move_log[0]) - 1] = '\0';
    app->move_log_count++;
    app->move_log_scroll = app->move_log_count;
}

/* Parses persisted settings key/value pairs into app state. */
static void load_settings(ChessApp* app) {
    FILE* file = fopen(SETTINGS_PATH, "r");
    char line[128];
    int legacy_depth = -1;
    int legacy_randomness = -1;
    bool has_ai_difficulty = false;

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
        } else if (strncmp(line, "sound_volume=", 13) == 0) {
            float volume = (float)atof(line + 13);
            if (volume < 0.0f) {
                volume = 0.0f;
            }
            if (volume > 1.0f) {
                volume = 1.0f;
            }
            app->sound_volume = volume;
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
    app->sound_volume = 1.0f;

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
    app->move_log_count = 0;
    app->move_log_scroll = 0;
    app->online_match_code[0] = '\0';
    app->online_match_active = false;
    app->leave_confirm_open = false;
    app->exit_confirm_open = false;
    snprintf(app->online_runtime_status,
             sizeof(app->online_runtime_status),
             "No active online match.");

    if (network_client_init(&app->network, 0)) {
        snprintf(app->lobby_status, sizeof(app->lobby_status), "Online is ready.");
    } else {
        snprintf(app->lobby_status, sizeof(app->lobby_status), "Network unavailable.");
    }
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
        app->online_match_active = true;
        if (app->network.connected) {
            snprintf(app->online_runtime_status,
                     sizeof(app->online_runtime_status),
                     "Online match active.");
        } else {
            snprintf(app->online_runtime_status,
                     sizeof(app->online_runtime_status),
                     "Online match disconnected.");
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

    append_move_log(app, moving_side, move);

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

/* Leaves and clears currently tracked online match session state. */
void app_online_end_match(ChessApp* app, bool notify_peer) {
    if (app == NULL) {
        return;
    }

    if (notify_peer) {
        network_client_send_leave(&app->network);
    }

    app->network.connected = false;
    app->network.peer_addr_len = 0;
    app->network.is_host = false;

    app->online_match_active = false;
    app->online_match_code[0] = '\0';
    app->lobby_code[0] = '\0';
    app->leave_confirm_open = false;
    app->mode = MODE_SINGLE;

    snprintf(app->online_runtime_status,
             sizeof(app->online_runtime_status),
             "Online match closed.");
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
    fprintf(file, "sound_volume=%.3f\n", app->sound_volume);

    fclose(file);
    return true;
}
