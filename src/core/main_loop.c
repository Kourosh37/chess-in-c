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

/* Handles inbound P2P packets and routes them into app state transitions. */
static void maybe_process_network(ChessApp* app) {
    NetPacket packet;
    int processed = 0;

    while (processed < MAX_NET_PACKETS_PER_FRAME && network_client_poll(&app->network, &packet)) {
        processed++;

        if (packet.type == NET_MSG_JOIN_REQUEST) {
            if (app->network.connected && app->network.is_host) {
                audio_play(AUDIO_SFX_LOBBY_JOIN);
                app->mode = MODE_ONLINE;
                app->human_side = app->network.host_side;
                app->lobby_view = LOBBY_VIEW_HOST;
                app->online_local_ready = false;
                app->online_peer_ready = false;

                if (app->lobby_code[0] != '\0') {
                    strncpy(app->online_match_code, app->lobby_code, INVITE_CODE_LEN);
                    app->online_match_code[INVITE_CODE_LEN] = '\0';
                }

                if (app->online_match_active) {
                    snprintf(app->online_runtime_status,
                             sizeof(app->online_runtime_status),
                             "Opponent reconnected.");
                    snprintf(app->lobby_status,
                             sizeof(app->lobby_status),
                             "Opponent reconnected to active match.");
                } else {
                    snprintf(app->lobby_status,
                             sizeof(app->lobby_status),
                             "Player joined room. Waiting for Ready.");
                    snprintf(app->online_runtime_status,
                             sizeof(app->online_runtime_status),
                             "Room has 2 players. Opponent must press Ready.");
                }
            }
            continue;
        }

        if (packet.type == NET_MSG_JOIN_ACCEPT) {
            if (app->network.connected && !app->network.is_host) {
                Side assigned = (packet.flags == SIDE_BLACK) ? SIDE_BLACK : SIDE_WHITE;
                audio_play(AUDIO_SFX_LOBBY_JOIN);
                app->mode = MODE_ONLINE;
                app->human_side = assigned;
                app->lobby_view = LOBBY_VIEW_JOIN;
                app->online_local_ready = false;
                app->online_peer_ready = false;

                if (packet.invite_code[0] != '\0') {
                    strncpy(app->online_match_code, packet.invite_code, INVITE_CODE_LEN);
                    app->online_match_code[INVITE_CODE_LEN] = '\0';
                } else {
                    strncpy(app->online_match_code, app->lobby_input, INVITE_CODE_LEN);
                    app->online_match_code[INVITE_CODE_LEN] = '\0';
                }

                if (app->mode == MODE_ONLINE && app->online_match_active) {
                    snprintf(app->online_runtime_status,
                             sizeof(app->online_runtime_status),
                             "Reconnected to host.");
                    snprintf(app->lobby_status,
                             sizeof(app->lobby_status),
                             "Reconnected to host.");
                } else {
                    snprintf(app->lobby_status,
                             sizeof(app->lobby_status),
                             "Connected. Press Ready and wait for host.");
                    snprintf(app->online_runtime_status,
                             sizeof(app->online_runtime_status),
                             "Connected to host. Waiting for Start.");
                }
            }
            continue;
        }

        if (packet.type == NET_MSG_JOIN_REJECT) {
            if (app->screen == SCREEN_LOBBY) {
                snprintf(app->lobby_status, sizeof(app->lobby_status), "Host rejected the join request.");
            }
            continue;
        }

        if (packet.type == NET_MSG_READY) {
            if (app->mode == MODE_ONLINE && !app->online_match_active) {
                bool ready = (packet.flags & 1U) != 0U;
                app->online_peer_ready = ready;

                if (app->network.is_host) {
                    snprintf(app->lobby_status,
                             sizeof(app->lobby_status),
                             ready ? "Opponent is Ready. You can start the game."
                                   : "Opponent is not ready yet.");
                } else {
                    snprintf(app->lobby_status,
                             sizeof(app->lobby_status),
                             ready ? "Host is ready. Waiting for Start."
                                   : "Host is not ready.");
                }
            }
            continue;
        }

        if (packet.type == NET_MSG_START) {
            if (app->mode == MODE_ONLINE && app->network.connected && !app->online_match_active) {
                app->online_match_active = true;
                app->online_local_ready = false;
                app->online_peer_ready = false;
                app->network.invite_code[0] = '\0';
                app->lobby_code[0] = '\0';
                app->online_match_code[0] = '\0';
                snprintf(app->online_runtime_status,
                         sizeof(app->online_runtime_status),
                         "Match started.");
                app_start_game(app, MODE_ONLINE);
            }
            continue;
        }

        if (packet.type == NET_MSG_MOVE) {
            if (app->mode == MODE_ONLINE && app->online_match_active && !app->game_over) {
                Move move;
                move.from = packet.from;
                move.to = packet.to;
                move.promotion = packet.promotion;
                move.flags = packet.flags;
                move.score = 0;

                app_apply_move(app, move);
            }
            continue;
        }

        if (packet.type == NET_MSG_LEAVE) {
            if (app->mode == MODE_ONLINE) {
                app->online_peer_ready = false;

                if (app->online_match_active) {
                    app_online_end_match(app, false);
                    app->screen = SCREEN_MENU;
                    audio_play(AUDIO_SFX_GAME_OVER);
                    snprintf(app->online_runtime_status,
                             sizeof(app->online_runtime_status),
                             "Opponent left the game. Match ended.");
                    snprintf(app->lobby_status,
                             sizeof(app->lobby_status),
                             "Your opponent left the game. Match ended.");
                } else if (app->network.is_host) {
                    app->network.connected = false;
                    snprintf(app->online_runtime_status,
                             sizeof(app->online_runtime_status),
                             "Room has 1 player.");
                    snprintf(app->lobby_status,
                             sizeof(app->lobby_status),
                             "Opponent left room.");
                } else {
                    app->network.connected = false;
                    app->online_local_ready = false;
                    snprintf(app->online_runtime_status,
                             sizeof(app->online_runtime_status),
                             "Disconnected from host.");
                    snprintf(app->lobby_status,
                             sizeof(app->lobby_status),
                             "Host closed the room.");
                }
            }
        }
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

        EndDrawing();

        if (app.exit_requested) {
            break;
        }
    }

    if (app.online_match_active || app.network.connected) {
        network_client_send_leave(&app.network);
    }

    ai_worker_shutdown(&worker);
    profile_save(&app.profile, "profile.dat");
    app_save_settings(&app);
    network_client_shutdown(&app.network);
    audio_shutdown();
    gui_font_shutdown();

    CloseWindow();
    return 0;
}
