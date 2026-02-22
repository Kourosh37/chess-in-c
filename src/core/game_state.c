#include "game_state.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Default local profile persistence file. */
static const char* PROFILE_PATH = "profile.dat";

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

    app->human_side = SIDE_WHITE;
    app->ai_limits.depth = 4;
    app->ai_limits.max_time_ms = 1500;
    app->ai_limits.randomness = 0;

    set_default_profile(&app->profile);
    if (!profile_load(&app->profile, PROFILE_PATH)) {
        profile_save(&app->profile, PROFILE_PATH);
    }

    position_set_start(&app->position);
    app->selected_square = -1;
    app_refresh_legal_moves(app);

    app->lobby_input[0] = '\0';
    app->lobby_code[0] = '\0';

    if (network_client_init(&app->network, 0)) {
        snprintf(app->lobby_status, sizeof(app->lobby_status), "Direct P2P mode: no central server.");
    } else {
        snprintf(app->lobby_status, sizeof(app->lobby_status), "Network init failed. Online mode unavailable.");
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
    if (!engine_make_move(&app->position, move)) {
        return false;
    }

    app->has_selection = false;
    app->selected_square = -1;
    app_refresh_legal_moves(app);

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
