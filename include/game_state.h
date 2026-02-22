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

#define ONLINE_MATCH_MAX 6

/* One persistent online match session (waiting room or started game). */
typedef struct OnlineMatch {
    bool used;
    bool in_game;
    bool connected;
    bool is_host;
    bool local_ready;
    bool peer_ready;
    Side local_side;

    NetworkClient network;
    char invite_code[INVITE_CODE_LEN + 1];
    char opponent_name[PLAYER_NAME_MAX + 1];
    char status[128];
    char started_at[32];
    uint64_t started_epoch;

    Position position;
    bool game_over;
    int last_move_from;
    int last_move_to;
    char move_log[MOVE_LOG_MAX][64];
    int move_log_count;
    int move_log_scroll;
} OnlineMatch;

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

    char online_name[PLAYER_NAME_MAX + 1];
    char online_name_input[PLAYER_NAME_MAX + 1];
    bool online_name_dialog_open;
    bool online_name_input_active;
    char online_name_error[96];

    char lobby_code[INVITE_CODE_LEN + 1];
    char lobby_input[INVITE_CODE_LEN + 1];
    bool lobby_input_active;
    char lobby_status[96];
    LobbyView lobby_view;
    int lobby_focus_match;
    int lobby_active_scroll;
    float lobby_copy_feedback_timer;
    bool lobby_copy_feedback;
    bool online_match_active;
    bool online_local_ready;
    bool online_peer_ready;
    char online_match_code[INVITE_CODE_LEN + 1];
    char online_runtime_status[128];
    bool online_leave_notice_open;
    int online_leave_notice_match;
    char online_leave_notice_title[64];
    char online_leave_notice_text[192];
    int current_online_match;
    OnlineMatch online_matches[ONLINE_MATCH_MAX];

    bool sound_enabled;
    float sfx_volume;
    float menu_music_volume;
    float game_music_volume;

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

/* Online-session management helpers. */
int app_online_active_count(const ChessApp* app);
OnlineMatch* app_online_get(ChessApp* app, int index);
const OnlineMatch* app_online_get_const(const ChessApp* app, int index);
int app_online_create_host(ChessApp* app, const char* username);
int app_online_create_join(ChessApp* app, const char* username, const char* invite_code);
bool app_online_send_ready(ChessApp* app, int index, bool ready);
bool app_online_send_start(ChessApp* app, int index);
void app_online_mark_started(ChessApp* app, int index);
void app_online_store_current_match(ChessApp* app);
bool app_online_switch_to_match(ChessApp* app, int index, bool open_play_screen);
void app_online_close_match(ChessApp* app, int index, bool notify_peer);
void app_online_close_all(ChessApp* app, bool notify_peer);
bool app_online_name_is_set(const ChessApp* app);

#endif
