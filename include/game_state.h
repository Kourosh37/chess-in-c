#ifndef GAME_STATE_H
#define GAME_STATE_H

/*
 * Core application state container and transition helpers.
 * This layer orchestrates engine, GUI, and profile storage.
 */

#include <stdbool.h>
#include "engine.h"
#include "audio.h"

/* Runtime state shared by screens and the main loop. */
typedef struct ChessApp {
    GameMode mode;
    AppScreen screen;
    ColorTheme theme;

    Position position;
    MoveList legal_moves;

    bool has_selection;
    int selected_square;

    bool game_over;
    bool ai_thinking;
    Side human_side;
    bool touch_move_enabled;
    bool turn_timer_enabled;
    int turn_time_seconds;
    float turn_time_remaining;
    bool timeout_game_over;
    Side timeout_loser;

    SearchLimits ai_limits;
    int ai_difficulty;
    SearchResult last_ai_result;

    Profile profile;
    bool settings_dirty;
    bool settings_save_pending;
    bool settings_load_failed;
    float settings_save_delay;
    char settings_status[256];

    bool sound_enabled;
    float sfx_volume;
    float menu_music_volume;
    float game_music_volume;
    bool delayed_sfx_pending;
    AudioSfx delayed_sfx_id;
    float delayed_sfx_timer;

    int last_move_from;
    int last_move_to;

    bool move_animating;
    int move_anim_from;
    int move_anim_to;
    Side move_anim_side;
    PieceType move_anim_piece;
    float move_anim_progress;
    float move_anim_duration;

    bool leave_confirm_open;
    bool exit_confirm_open;
    bool exit_requested;

    char move_log[MOVE_LOG_MAX][64];
    int move_log_count;
    int move_log_scroll;
} ChessApp;

/* App lifecycle and game flow helpers. */
void app_init(ChessApp* app);
void app_start_game(ChessApp* app, GameMode mode);
void app_refresh_legal_moves(ChessApp* app);
void app_set_ai_difficulty(ChessApp* app, int difficulty_percent);

/* Move flow helpers used by GUI and AI event handlers. */
bool app_apply_move(ChessApp* app, Move move);
bool app_is_human_turn(const ChessApp* app);
void app_tick(ChessApp* app, float delta_time);
void app_settings_changed(ChessApp* app);
bool app_save_settings(ChessApp* app);

#endif
