#include "main_loop.h"

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include <raylib.h>

#include "game_state.h"
#include "gui.h"
#include "audio.h"
#include "platform_dialog.h"
#include "threading.h"
#include "file_io.h"

/* Runtime-loaded window icon path (place PNG image here). */
#define WINDOW_ICON_PATH "assets/icons/app_icon.png"

/* Prefer bundled assets beside the executable, independently of launch directory. */
static void normalize_working_directory(void) {
    static const char* relative[] = {"", "chess", "..", "../chess", "../../chess"};
    char exe_dir[CHESS_PATH_MAX];
    char candidate[CHESS_PATH_MAX];
    char assets[CHESS_PATH_MAX];
    if (!file_io_executable_directory(exe_dir)) return;
    for (size_t i = 0; i < sizeof(relative) / sizeof(relative[0]); ++i) {
        if (!file_io_join(candidate, sizeof(candidate), exe_dir, relative[i]) ||
            !file_io_join(assets, sizeof(assets), candidate, "assets")) continue;
        if (file_io_path_status(assets) > 0 && file_io_change_directory(candidate)) return;
    }
}

/* Tries to load one icon image and apply it to the window. */
static bool try_apply_window_icon_from_path(const char* path) {
    if (path == NULL || path[0] == '\0' || !FileExists(path)) {
        return false;
    }

    Image icon = LoadImage(path);
    if (icon.data == NULL || icon.width <= 0 || icon.height <= 0) {
        if (icon.data != NULL) {
            UnloadImage(icon);
        }
        return false;
    }

    if (icon.format != PIXELFORMAT_UNCOMPRESSED_R8G8B8A8) {
        ImageFormat(&icon, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    }
    if (icon.data == NULL || icon.format != PIXELFORMAT_UNCOMPRESSED_R8G8B8A8) {
        if (icon.data != NULL) {
            UnloadImage(icon);
        }
        return false;
    }

    SetWindowIcon(icon);
    UnloadImage(icon);
    return true;
}

/* Loads and applies window icon from assets when file is available. */
static void try_set_window_icon(void) {
    const char* exe_dir = GetApplicationDirectory();
    char candidate[1024];

    if (try_apply_window_icon_from_path(WINDOW_ICON_PATH)) {
        return;
    }

    if (exe_dir != NULL && exe_dir[0] != '\0') {
        snprintf(candidate, sizeof(candidate), "%sassets/icons/app_icon.png", exe_dir);
        if (try_apply_window_icon_from_path(candidate)) {
            return;
        }

        snprintf(candidate, sizeof(candidate), "%s../assets/icons/app_icon.png", exe_dir);
        if (try_apply_window_icon_from_path(candidate)) {
            return;
        }

        snprintf(candidate, sizeof(candidate), "%s../../assets/icons/app_icon.png", exe_dir);
        if (try_apply_window_icon_from_path(candidate)) {
            return;
        }
    }
}

/* Background worker state used to keep AI search off the render thread. */
typedef struct AIWorker {
    Position position;
    SearchLimits limits;
    SearchResult result;
    atomic_bool running;
    atomic_bool has_result;
    bool thread_active;
    ChessThread handle;
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

    if (!chess_thread_create(&worker->handle, ai_worker_thread, worker)) {
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

    chess_thread_join(&worker->handle);
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

/* Application main loop: events, AI updates, and frame rendering. */
int run_main_loop(void) {
    ChessApp app;
    app_init(&app);
    normalize_working_directory();

    AIWorker worker;
    ai_worker_init(&worker);

    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
    InitWindow(1280, 820, "Chess");
    if (!IsWindowReady()) {
        platform_show_error_dialog("Chess Startup Error",
                                   "Could not initialize the graphics window.\n"
                                   "Please update GPU drivers and try again.");
        return 1;
    }
    if (app.settings_load_failed) {
        platform_show_error_dialog("Settings could not be loaded", app.settings_status);
    }
    try_set_window_icon();
    SetWindowMinSize(920, 640);
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
        } else if (app.screen == SCREEN_SETTINGS) {
            gui_screen_settings(&app);
        }

        EndDrawing();

        if (app.exit_requested) {
            break;
        }
    }

    ai_worker_shutdown(&worker);
    if (!app_save_settings(&app)) {
        platform_show_error_dialog("Settings could not be saved", app.settings_status);
    }
    audio_shutdown();
    gui_font_shutdown();

    CloseWindow();
    return 0;
}
