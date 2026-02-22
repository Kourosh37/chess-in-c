#include "main_loop.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include <raylib.h>

#include "game_state.h"
#include "gui.h"
#include "audio.h"

/* Upper bound to avoid spending too much frame time draining network queue. */
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

/* Background worker for online connectivity checks/handshakes without UI stalls. */
typedef struct OnlineWorker {
    OnlineAsyncAction action;
    int match_index;
    bool reconnect_is_host;
    char username[PLAYER_NAME_MAX + 1];
    char invite_code[INVITE_CODE_LEN + 1];
    bool success;
    char error[256];
    char out_invite_code[INVITE_CODE_LEN + 1];
    NetworkClient client;
    atomic_bool running;
    atomic_bool has_result;
    bool thread_active;
    pthread_t handle;
} OnlineWorker;

/* Copies one robust network error string into worker result buffer. */
static void online_worker_set_error(OnlineWorker* worker, const char* fallback) {
    const char* err;

    if (worker == NULL) {
        return;
    }

    err = network_last_error();
    if (err == NULL || err[0] == '\0' || strcmp(err, "No error.") == 0) {
        err = fallback;
    }

    strncpy(worker->error, err, sizeof(worker->error) - 1);
    worker->error[sizeof(worker->error) - 1] = '\0';
}

/* Thread entry for online actions (probe/host/join/reconnect). */
static void* online_worker_thread(void* data) {
    OnlineWorker* worker = (OnlineWorker*)data;
    bool ok = false;

    if (worker == NULL) {
        return NULL;
    }

    memset(&worker->client, 0, sizeof(worker->client));
    worker->out_invite_code[0] = '\0';
    worker->error[0] = '\0';

    if (worker->action == ONLINE_ASYNC_ENTER_LOBBY) {
        ok = network_relay_probe();
        if (!ok) {
            online_worker_set_error(worker, "Internet connection is not reachable.");
        }
    } else if (worker->action == ONLINE_ASYNC_HOST_ROOM) {
        if (!network_relay_probe()) {
            online_worker_set_error(worker, "Internet connection is not reachable.");
        } else if (!network_client_init(&worker->client, 0)) {
            online_worker_set_error(worker, "Could not initialize network client.");
        } else if (!network_client_host(&worker->client, worker->username, worker->out_invite_code)) {
            online_worker_set_error(worker, "Could not create host room.");
            network_client_shutdown(&worker->client);
            memset(&worker->client, 0, sizeof(worker->client));
        } else {
            ok = true;
        }
    } else if (worker->action == ONLINE_ASYNC_JOIN_ROOM) {
        if (!network_relay_probe()) {
            online_worker_set_error(worker, "Internet connection is not reachable.");
        } else if (!network_client_init(&worker->client, 0)) {
            online_worker_set_error(worker, "Could not initialize network client.");
        } else if (!network_client_join(&worker->client, worker->username, worker->invite_code)) {
            online_worker_set_error(worker, "Could not join this room.");
            network_client_shutdown(&worker->client);
            memset(&worker->client, 0, sizeof(worker->client));
        } else {
            strncpy(worker->out_invite_code, worker->invite_code, INVITE_CODE_LEN);
            worker->out_invite_code[INVITE_CODE_LEN] = '\0';
            ok = true;
        }
    } else if (worker->action == ONLINE_ASYNC_RECONNECT_ROOM) {
        if (!network_relay_probe()) {
            online_worker_set_error(worker, "Internet connection is not reachable.");
        } else if (!network_client_init(&worker->client, 0)) {
            online_worker_set_error(worker, "Could not initialize network client.");
        } else if (worker->reconnect_is_host) {
            if (!network_client_host_reconnect(&worker->client, worker->username, worker->invite_code)) {
                online_worker_set_error(worker, "Could not reconnect host room.");
                network_client_shutdown(&worker->client);
                memset(&worker->client, 0, sizeof(worker->client));
            } else {
                ok = true;
            }
        } else {
            if (!network_client_join(&worker->client, worker->username, worker->invite_code)) {
                online_worker_set_error(worker, "Could not reconnect to room.");
                network_client_shutdown(&worker->client);
                memset(&worker->client, 0, sizeof(worker->client));
            } else {
                ok = true;
            }
        }
    } else {
        online_worker_set_error(worker, "Unknown online action.");
    }

    worker->success = ok;
    if (ok) {
        strncpy(worker->error, "No error.", sizeof(worker->error) - 1);
        worker->error[sizeof(worker->error) - 1] = '\0';
    }

    atomic_store(&worker->has_result, true);
    atomic_store(&worker->running, false);
    return NULL;
}

/* Initializes async online worker object. */
static void online_worker_init(OnlineWorker* worker) {
    memset(worker, 0, sizeof(*worker));
    worker->action = ONLINE_ASYNC_NONE;
    worker->match_index = -1;
    atomic_init(&worker->running, false);
    atomic_init(&worker->has_result, false);
}

/* Starts one async online job from app request fields. */
static bool online_worker_start(OnlineWorker* worker, const ChessApp* app) {
    if (worker == NULL || app == NULL || worker->thread_active || !app->online_loading) {
        return false;
    }
    if (app->online_loading_action == ONLINE_ASYNC_NONE) {
        return false;
    }

    worker->action = app->online_loading_action;
    worker->match_index = app->online_loading_match_index;
    worker->reconnect_is_host = app->online_loading_reconnect_host;
    worker->success = false;
    worker->error[0] = '\0';
    worker->out_invite_code[0] = '\0';
    memset(&worker->client, 0, sizeof(worker->client));

    strncpy(worker->username, app->online_name, PLAYER_NAME_MAX);
    worker->username[PLAYER_NAME_MAX] = '\0';
    strncpy(worker->invite_code, app->online_loading_code, INVITE_CODE_LEN);
    worker->invite_code[INVITE_CODE_LEN] = '\0';

    atomic_store(&worker->running, true);
    atomic_store(&worker->has_result, false);

    if (pthread_create(&worker->handle, NULL, online_worker_thread, worker) != 0) {
        atomic_store(&worker->running, false);
        return false;
    }

    worker->thread_active = true;
    return true;
}

/* Joins online worker thread if it is finished. */
static bool online_worker_try_join(OnlineWorker* worker) {
    if (worker == NULL || !worker->thread_active) {
        return false;
    }
    if (atomic_load(&worker->running)) {
        return false;
    }

    pthread_join(worker->handle, NULL);
    worker->thread_active = false;
    return true;
}

/* Cleanup for async online worker during app shutdown. */
static void online_worker_shutdown(OnlineWorker* worker) {
    if (worker == NULL) {
        return;
    }

    if (worker->thread_active) {
        pthread_join(worker->handle, NULL);
        worker->thread_active = false;
    }

    if (worker->client.initialized) {
        network_client_shutdown(&worker->client);
        memset(&worker->client, 0, sizeof(worker->client));
    }
}

/* Clears request/loading fields after async online action is resolved. */
static void clear_online_loading(ChessApp* app) {
    if (app == NULL) {
        return;
    }

    app->online_loading = false;
    app->online_loading_action = ONLINE_ASYNC_NONE;
    app->online_loading_match_index = -1;
    app->online_loading_reconnect_host = false;
    app->online_loading_code[0] = '\0';
    app->online_loading_title[0] = '\0';
    app->online_loading_text[0] = '\0';
}

/* Syncs lobby selection to one specific online match slot after async success. */
static void focus_lobby_match(ChessApp* app, int index, LobbyView view) {
    const OnlineMatch* match;

    if (app == NULL) {
        return;
    }

    match = app_online_get_const(app, index);
    if (match == NULL) {
        return;
    }

    app->lobby_focus_match = index;
    app_online_switch_to_match(app, index, false);
    app->lobby_view = view;
    strncpy(app->lobby_status, match->status, sizeof(app->lobby_status) - 1);
    app->lobby_status[sizeof(app->lobby_status) - 1] = '\0';
}

/* Drives async online request lifecycle and applies finished results on main thread. */
static void maybe_process_online_actions(ChessApp* app, OnlineWorker* worker) {
    if (app == NULL || worker == NULL) {
        return;
    }

    if (app->online_loading && !worker->thread_active) {
        if (!online_worker_start(worker, app)) {
            app_show_network_error(app, "Online Error", "Could not start background online task.");
            clear_online_loading(app);
        }
        return;
    }

    if (!online_worker_try_join(worker)) {
        return;
    }

    if (!atomic_load(&worker->has_result)) {
        return;
    }

    if (!worker->success) {
        const char* title = (worker->action == ONLINE_ASYNC_ENTER_LOBBY) ? "Offline" : "Online Error";
        app_show_network_error(app, title, worker->error);
        clear_online_loading(app);
        return;
    }

    if (worker->action == ONLINE_ASYNC_ENTER_LOBBY) {
        app->mode = MODE_ONLINE;
        app->screen = SCREEN_LOBBY;
        app->lobby_view = LOBBY_VIEW_HOME;
        app->lobby_focus_match = -1;
        app->lobby_input[0] = '\0';
        app->lobby_code[0] = '\0';
        app->lobby_input_active = false;
        app->online_local_ready = false;
        app->online_peer_ready = false;
        app->lobby_copy_feedback = false;
        app->lobby_copy_feedback_timer = 0.0f;
        snprintf(app->lobby_status, sizeof(app->lobby_status), "Choose Host Game or Join Game.");
    } else if (worker->action == ONLINE_ASYNC_HOST_ROOM) {
        int idx = app_online_attach_host_client(app, &worker->client, worker->out_invite_code);
        if (idx >= 0) {
            focus_lobby_match(app, idx, LOBBY_VIEW_HOST);
        } else {
            if (worker->client.initialized) {
                network_client_shutdown(&worker->client);
                memset(&worker->client, 0, sizeof(worker->client));
            }
            app_show_network_error(app, "Online Error", "Could not allocate a new active match slot.");
        }
    } else if (worker->action == ONLINE_ASYNC_JOIN_ROOM) {
        int idx = app_online_attach_join_client(app, &worker->client, worker->invite_code);
        if (idx >= 0) {
            focus_lobby_match(app, idx, LOBBY_VIEW_JOIN);
        } else {
            if (worker->client.initialized) {
                network_client_shutdown(&worker->client);
                memset(&worker->client, 0, sizeof(worker->client));
            }
            app_show_network_error(app, "Online Error", "Could not allocate a new active match slot.");
        }
    } else if (worker->action == ONLINE_ASYNC_RECONNECT_ROOM) {
        if (app_online_attach_reconnect_client(app,
                                               worker->match_index,
                                               &worker->client,
                                               worker->reconnect_is_host)) {
            focus_lobby_match(app,
                              worker->match_index,
                              worker->reconnect_is_host ? LOBBY_VIEW_HOST : LOBBY_VIEW_JOIN);
        } else {
            if (worker->client.initialized) {
                network_client_shutdown(&worker->client);
                memset(&worker->client, 0, sizeof(worker->client));
            }
            app_show_network_error(app, "Online Error", "Could not apply reconnect result.");
        }
    }

    clear_online_loading(app);
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

    if (app == NULL || app->online_loading) {
        return;
    }

    for (int index = 0; index < ONLINE_MATCH_MAX; ++index) {
        OnlineMatch* match = app_online_get(app, index);
        int processed = 0;

        if (match == NULL) {
            continue;
        }
        match->connected = match->network.connected;

        while (processed < MAX_NET_PACKETS_PER_FRAME && network_client_poll(&match->network, &packet)) {
            bool removed = false;
            processed++;
            match->connected = match->network.connected;

            if (packet.username[0] != '\0' && packet.type != NET_MSG_ERROR) {
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
            } else if (packet.type == NET_MSG_ERROR) {
                if (packet.username[0] != '\0') {
                    strncpy(match->status, packet.username, sizeof(match->status) - 1);
                    match->status[sizeof(match->status) - 1] = '\0';
                    if (index == app->current_online_match) {
                        strncpy(app->lobby_status, packet.username, sizeof(app->lobby_status) - 1);
                        app->lobby_status[sizeof(app->lobby_status) - 1] = '\0';
                    }
                }
                match->connected = match->network.connected;
            } else if (packet.type == NET_MSG_LEAVE) {
                if (match->in_game) {
                    if (index == app->current_online_match &&
                        app->mode == MODE_ONLINE &&
                        app->screen == SCREEN_PLAY) {
                        audio_play(AUDIO_SFX_GAME_OVER);
                        match->in_game = false;
                        match->connected = false;
                        match->network.connected = false;
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
        match->connected = match->network.connected;

        sync_current_match_runtime(app, index);
    }
}

/* Application main loop: events, AI/network updates, and frame rendering. */
int run_main_loop(void) {
    ChessApp app;
    app_init(&app);

    AIWorker worker;
    ai_worker_init(&worker);
    OnlineWorker online_worker;
    online_worker_init(&online_worker);

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
        maybe_process_online_actions(&app, &online_worker);
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
    online_worker_shutdown(&online_worker);
    profile_save(&app.profile, "profile.dat");
    app_save_settings(&app);
    app_online_store_current_match(&app);
    app_online_save_sessions(&app);
    for (int i = 0; i < ONLINE_MATCH_MAX; ++i) {
        OnlineMatch* match = app_online_get(&app, i);
        if (match != NULL && match->network.initialized) {
            network_client_shutdown(&match->network);
            match->connected = false;
        }
    }
    audio_shutdown();
    gui_font_shutdown();

    CloseWindow();
    return 0;
}
