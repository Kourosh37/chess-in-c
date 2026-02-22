#include "main_loop.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include <raylib.h>

#include "game_state.h"
#include "gui.h"
#include "audio.h"

/* Upper bound to avoid spending too much frame time draining UDP queue. */
#define MAX_NET_PACKETS_PER_FRAME 16

/* Background worker state used to keep AI search off the render thread. */
typedef struct AIWorker {
    Position position;
    SearchLimits limits;
    SearchResult result;
    atomic_bool running;
    atomic_bool has_result;
    bool thread_active;
    pthread_t handle;
} AIWorker;

/* Worker thread entry point: run search and publish result atomically. */
static void* ai_worker_thread(void* data) {
    AIWorker* worker = (AIWorker*)data;

    search_best_move(&worker->position, &worker->limits, &worker->result);
    atomic_store(&worker->has_result, true);
    atomic_store(&worker->running, false);

    return NULL;
}

/* Initializes worker runtime state. */
static void ai_worker_init(AIWorker* worker) {
    memset(worker, 0, sizeof(*worker));
    atomic_init(&worker->running, false);
    atomic_init(&worker->has_result, false);
}

/* Starts asynchronous AI search for a copied position snapshot. */
static bool ai_worker_start(AIWorker* worker, const Position* position, const SearchLimits* limits) {
    if (worker->thread_active) {
        return false;
    }

    worker->position = *position;
    worker->limits = *limits;
    memset(&worker->result, 0, sizeof(worker->result));

    atomic_store(&worker->running, true);
    atomic_store(&worker->has_result, false);

    if (pthread_create(&worker->handle, NULL, ai_worker_thread, worker) != 0) {
        atomic_store(&worker->running, false);
        return false;
    }

    worker->thread_active = true;
    return true;
}

/* Joins running AI thread and marks worker as idle. */
static void ai_worker_join(AIWorker* worker) {
    if (!worker->thread_active) {
        return;
    }

    pthread_join(worker->handle, NULL);
    worker->thread_active = false;
}

/* Ensures no worker thread is left alive on shutdown. */
static void ai_worker_shutdown(AIWorker* worker) {
    if (worker->thread_active) {
        ai_worker_join(worker);
    }
}

/* Drives AI turn flow in single-player mode. */
static void maybe_process_ai_turn(ChessApp* app, AIWorker* worker) {
    if (app->mode != MODE_SINGLE || app->screen != SCREEN_PLAY || app->game_over) {
        if (worker->thread_active && !atomic_load(&worker->running)) {
            ai_worker_join(worker);
        }
        app->ai_thinking = false;
        return;
    }

    {
        bool ai_turn = app->position.side_to_move != app->human_side;
        if (ai_turn && !worker->thread_active) {
            ai_worker_start(worker, &app->position, &app->ai_limits);
        }
    }

    app->ai_thinking = worker->thread_active && atomic_load(&worker->running);

    if (worker->thread_active && !atomic_load(&worker->running)) {
        ai_worker_join(worker);

        if (atomic_load(&worker->has_result)) {
            app->last_ai_result = worker->result;
            app_apply_move(app, worker->result.best_move);
        }

        app->ai_thinking = false;
    }
}

/* Converts board square index to algebraic coordinate for online snapshot logs. */
static void square_to_text(int square, char out[3]) {
    out[0] = (char)('a' + (square & 7));
    out[1] = (char)('1' + (square >> 3));
    out[2] = '\0';
}

/* Appends one move line into per-match online history. */
static void append_online_move_log(OnlineMatch* match, Side side, Move move) {
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

    if (match->move_log_count >= MOVE_LOG_MAX) {
        memmove(match->move_log[0], match->move_log[1], (MOVE_LOG_MAX - 1) * sizeof(match->move_log[0]));
        match->move_log_count = MOVE_LOG_MAX - 1;
    }

    strncpy(match->move_log[match->move_log_count], line, sizeof(match->move_log[0]) - 1);
    match->move_log[match->move_log_count][sizeof(match->move_log[0]) - 1] = '\0';
    match->move_log_count++;
    match->move_log_scroll = match->move_log_count;
}

/* Applies one network move to an off-screen match snapshot. */
static bool apply_move_to_snapshot(OnlineMatch* match, Move move) {
    Side moving_side;
    MoveList legal;

    if (match == NULL || !match->used || !match->in_game || match->game_over) {
        return false;
    }

    moving_side = match->position.side_to_move;
    if (!engine_make_move(&match->position, move)) {
        return false;
    }

    match->last_move_from = move.from;
    match->last_move_to = move.to;
    append_online_move_log(match, moving_side, move);

    generate_legal_moves(&match->position, &legal);
    match->game_over = (legal.count == 0);
    if (match->game_over) {
        match->in_game = false;
        strncpy(match->status,
                engine_in_check(&match->position, match->position.side_to_move)
                    ? "Match ended by checkmate."
                    : "Match ended by draw.",
                sizeof(match->status) - 1);
        match->status[sizeof(match->status) - 1] = '\0';
    }

    return true;
}

/* Mirrors selected match metadata into shared app fields used by current UI. */
static void sync_current_match_runtime(ChessApp* app, int index) {
    OnlineMatch* match = app_online_get(app, index);

    if (app == NULL ||
        match == NULL ||
        app->current_online_match != index ||
        app->mode != MODE_ONLINE) {
        return;
    }

    app->online_match_active = match->in_game;
    app->online_local_ready = match->local_ready;
    app->online_peer_ready = match->peer_ready;
    app->human_side = match->local_side;
    strncpy(app->online_match_code, match->invite_code, INVITE_CODE_LEN);
    app->online_match_code[INVITE_CODE_LEN] = '\0';
    strncpy(app->online_runtime_status, match->status, sizeof(app->online_runtime_status) - 1);
    app->online_runtime_status[sizeof(app->online_runtime_status) - 1] = '\0';
}

/* Handles inbound packets for all active online sessions. */
static void maybe_process_network(ChessApp* app) {
    NetPacket packet;

    for (int index = 0; index < ONLINE_MATCH_MAX; ++index) {
        OnlineMatch* match = app_online_get(app, index);
        int processed = 0;

        if (match == NULL) {
            continue;
        }

        while (processed < MAX_NET_PACKETS_PER_FRAME && network_client_poll(&match->network, &packet)) {
            bool removed = false;
            processed++;
            match->connected = match->network.connected;

            if (packet.username[0] != '\0') {
                strncpy(match->opponent_name, packet.username, PLAYER_NAME_MAX);
                match->opponent_name[PLAYER_NAME_MAX] = '\0';
            }

            if (packet.type == NET_MSG_JOIN_REQUEST) {
                if (match->network.connected && match->network.is_host) {
                    audio_play(AUDIO_SFX_LOBBY_JOIN);
                    match->is_host = true;
                    match->connected = true;
                    match->local_side = match->network.host_side;
                    match->local_ready = false;
                    match->peer_ready = false;

                    if (match->in_game) {
                        strncpy(match->status, "Opponent reconnected.", sizeof(match->status) - 1);
                    } else {
                        strncpy(match->status,
                                "Player joined room. Waiting for Ready.",
                                sizeof(match->status) - 1);
                    }
                    match->status[sizeof(match->status) - 1] = '\0';
                }
            } else if (packet.type == NET_MSG_JOIN_ACCEPT) {
                if (match->network.connected && !match->network.is_host) {
                    Side assigned = (packet.flags == SIDE_BLACK) ? SIDE_BLACK : SIDE_WHITE;

                    audio_play(AUDIO_SFX_LOBBY_JOIN);
                    match->is_host = false;
                    match->connected = true;
                    match->local_side = assigned;
                    match->local_ready = false;
                    match->peer_ready = false;

                    if (packet.invite_code[0] != '\0') {
                        strncpy(match->invite_code, packet.invite_code, INVITE_CODE_LEN);
                        match->invite_code[INVITE_CODE_LEN] = '\0';
                    }

                    if (match->in_game) {
                        strncpy(match->status, "Reconnected to host.", sizeof(match->status) - 1);
                    } else {
                        strncpy(match->status,
                                "Connected. Press Ready and wait for host.",
                                sizeof(match->status) - 1);
                    }
                    match->status[sizeof(match->status) - 1] = '\0';
                }
            } else if (packet.type == NET_MSG_JOIN_REJECT) {
                strncpy(match->status, "Host rejected the join request.", sizeof(match->status) - 1);
                match->status[sizeof(match->status) - 1] = '\0';
            } else if (packet.type == NET_MSG_READY) {
                if (!match->in_game) {
                    bool ready = (packet.flags & 1U) != 0U;
                    match->peer_ready = ready;
                    if (match->is_host) {
                        strncpy(match->status,
                                ready ? "Opponent is Ready. You can start the game."
                                      : "Opponent is not ready yet.",
                                sizeof(match->status) - 1);
                    } else {
                        strncpy(match->status,
                                ready ? "Host is ready. Waiting for Start."
                                      : "Host is not ready.",
                                sizeof(match->status) - 1);
                    }
                    match->status[sizeof(match->status) - 1] = '\0';
                }
            } else if (packet.type == NET_MSG_START) {
                if (match->network.connected && !match->in_game) {
                    app_online_mark_started(app, index);
                    match = app_online_get(app, index);
                    if (match != NULL && (app->lobby_focus_match == index || app->current_online_match == index)) {
                        app_online_switch_to_match(app, index, true);
                    }
                }
            } else if (packet.type == NET_MSG_MOVE) {
                if (match->in_game && !match->game_over) {
                    Move move;
                    move.from = packet.from;
                    move.to = packet.to;
                    move.promotion = packet.promotion;
                    move.flags = packet.flags;
                    move.score = 0;

                    if (index == app->current_online_match &&
                        app->mode == MODE_ONLINE &&
                        app->screen == SCREEN_PLAY) {
                        app_apply_move(app, move);
                        app_online_store_current_match(app);
                    } else {
                        apply_move_to_snapshot(match, move);
                    }
                }
            } else if (packet.type == NET_MSG_LEAVE) {
                if (match->in_game) {
                    if (index == app->current_online_match &&
                        app->mode == MODE_ONLINE &&
                        app->screen == SCREEN_PLAY) {
                        audio_play(AUDIO_SFX_GAME_OVER);
                        match->in_game = false;
                        match->connected = false;
                        match->network.connected = false;
                        match->network.peer_addr_len = 0;
                        match->peer_ready = false;
                        strncpy(match->status,
                                "Opponent left the game. Match ended.",
                                sizeof(match->status) - 1);
                        match->status[sizeof(match->status) - 1] = '\0';
                        snprintf(app->online_runtime_status,
                                 sizeof(app->online_runtime_status),
                                 "Opponent left the game. Match ended.");
                        snprintf(app->lobby_status,
                                 sizeof(app->lobby_status),
                                 "Your opponent left the game. Match ended.");
                        app->online_match_active = false;
                        app->online_peer_ready = false;
                        app->leave_confirm_open = false;
                        app->online_leave_notice_open = true;
                        app->online_leave_notice_match = index;
                        strncpy(app->online_leave_notice_title,
                                "Match Ended",
                                sizeof(app->online_leave_notice_title) - 1);
                        app->online_leave_notice_title[sizeof(app->online_leave_notice_title) - 1] = '\0';
                        strncpy(app->online_leave_notice_text,
                                "Your opponent left the match. Press OK to return to menu.",
                                sizeof(app->online_leave_notice_text) - 1);
                        app->online_leave_notice_text[sizeof(app->online_leave_notice_text) - 1] = '\0';
                    } else {
                        app_online_close_match(app, index, false);
                        removed = true;
                    }
                } else if (match->is_host) {
                    match->connected = false;
                    match->network.connected = false;
                    match->network.peer_addr_len = 0;
                    match->peer_ready = false;
                    strncpy(match->opponent_name, "Waiting...", PLAYER_NAME_MAX);
                    match->opponent_name[PLAYER_NAME_MAX] = '\0';
                    strncpy(match->status, "Opponent left room.", sizeof(match->status) - 1);
                    match->status[sizeof(match->status) - 1] = '\0';
                } else {
                    if (index == app->current_online_match) {
                        snprintf(app->lobby_status,
                                 sizeof(app->lobby_status),
                                 "Host closed the room.");
                    }
                    app_online_close_match(app, index, false);
                    removed = true;
                }
            }

            if (removed) {
                break;
            }
        }

        sync_current_match_runtime(app, index);
    }
}

/* Application main loop: events, AI/network updates, and frame rendering. */
int run_main_loop(void) {
    ChessApp app;
    app_init(&app);

    AIWorker worker;
    ai_worker_init(&worker);

    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
    InitWindow(1280, 820, "Chess");
    SetWindowMinSize(980, 680);
    SetTargetFPS(60);
    gui_font_init();
    audio_init();
    audio_set_enabled(app.sound_enabled);
    audio_set_sfx_volume(app.sfx_volume);
    audio_set_menu_music_volume(app.menu_music_volume);
    audio_set_game_music_volume(app.game_music_volume);
    audio_set_menu_music_active(app.screen != SCREEN_PLAY);
    audio_set_game_music_active(app.screen == SCREEN_PLAY);

    while (!WindowShouldClose()) {
        maybe_process_network(&app);
        maybe_process_ai_turn(&app, &worker);
        app_tick(&app, GetFrameTime());
        gui_set_active_theme(app.theme);
        audio_set_menu_music_active(app.screen != SCREEN_PLAY);
        audio_set_game_music_active(app.screen == SCREEN_PLAY);
        audio_update();

        BeginDrawing();
        SetMouseCursor(MOUSE_CURSOR_DEFAULT);
        gui_widgets_begin_frame();
        gui_draw_background();

        if (app.screen == SCREEN_MENU) {
            gui_screen_menu(&app);
        } else if (app.screen == SCREEN_PLAY) {
            gui_screen_play(&app);
        } else if (app.screen == SCREEN_LOBBY) {
            gui_screen_lobby(&app);
        } else if (app.screen == SCREEN_SETTINGS) {
            gui_screen_settings(&app);
        }

        gui_draw_input_overlays();
        EndDrawing();

        if (app.exit_requested) {
            break;
        }
    }

    ai_worker_shutdown(&worker);
    profile_save(&app.profile, "profile.dat");
    app_save_settings(&app);
    app_online_close_all(&app, true);
    audio_shutdown();
    gui_font_shutdown();

    CloseWindow();
    return 0;
}
