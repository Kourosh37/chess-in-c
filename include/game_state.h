#ifndef GAME_STATE_H
#define GAME_STATE_H

/*
 * Core application state container and transition helpers.
 * This layer orchestrates engine, GUI, networking, and profile storage.
 */

#include <stdbool.h>
#include "engine.h"
#include "network.h"
#include "profile_mgr.h"

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

    SearchLimits ai_limits;
    int ai_difficulty;
    SearchResult last_ai_result;

    Profile profile;
    NetworkClient network;

    char lobby_code[INVITE_CODE_LEN + 1];
    char lobby_input[INVITE_CODE_LEN + 1];
    bool lobby_input_active;
    char lobby_status[96];
    LobbyView lobby_view;
    float lobby_copy_feedback_timer;
    bool lobby_copy_feedback;
    bool online_match_active;
    bool online_local_ready;
    bool online_peer_ready;
    char online_match_code[INVITE_CODE_LEN + 1];
    char online_runtime_status[128];

    bool sound_enabled;
    float sound_volume;

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

/* Move flow helpers used by GUI and network event handlers. */
bool app_apply_move(ChessApp* app, Move move);
bool app_is_human_turn(const ChessApp* app);
void app_tick(ChessApp* app, float delta_time);
void app_online_end_match(ChessApp* app, bool notify_peer);
bool app_save_settings(const ChessApp* app);

#endif
