#include "gui.h"

#include <stdio.h>

#include "game_state.h"

/* Renders and updates the main menu screen. */
void gui_screen_menu(struct ChessApp* app) {
    DrawText("ChessProject", 70, 36, 48, (Color){29, 53, 87, 255});
    DrawText("C11 + Raylib + P2P", 74, 86, 20, (Color){66, 86, 111, 255});

    {
        char profile_line[128];
        snprintf(profile_line,
                 sizeof(profile_line),
                 "User: %s   Wins: %u   Losses: %u",
                 app->profile.username,
                 app->profile.wins,
                 app->profile.losses);
        DrawText(profile_line, 70, 126, 20, (Color){40, 40, 40, 255});
    }

    {
        Rectangle single_btn = {70, 190, 300, 56};
        Rectangle local_btn = {70, 260, 300, 56};
        Rectangle online_btn = {70, 330, 300, 56};

        if (gui_button(single_btn, "Single Player")) {
            app->human_side = SIDE_WHITE;
            app_start_game(app, MODE_SINGLE);
        }

        if (gui_button(local_btn, "Local Multiplayer")) {
            app_start_game(app, MODE_LOCAL);
        }

        if (gui_button(online_btn, "Online (P2P)")) {
            app->mode = MODE_ONLINE;
            app->screen = SCREEN_LOBBY;
            app->lobby_input[0] = '\0';
            app->lobby_code[0] = '\0';
            snprintf(app->lobby_status, sizeof(app->lobby_status), "Host or join with invite code.");
        }
    }

    DrawText("AI Difficulty", 430, 205, 26, (Color){30, 30, 30, 255});

    {
        Rectangle minus_btn = {430, 245, 60, 52};
        Rectangle plus_btn = {610, 245, 60, 52};

        if (gui_button(minus_btn, "-") && app->ai_limits.depth > 1) {
            app->ai_limits.depth--;
        }

        if (gui_button(plus_btn, "+") && app->ai_limits.depth < 8) {
            app->ai_limits.depth++;
        }

        {
            char depth_text[64];
            snprintf(depth_text, sizeof(depth_text), "Depth: %d", app->ai_limits.depth);
            DrawText(depth_text, 515, 260, 26, (Color){20, 20, 20, 255});
        }
    }

    {
        Rectangle random_minus_btn = {430, 340, 60, 52};
        Rectangle random_plus_btn = {610, 340, 60, 52};

        if (gui_button(random_minus_btn, "-") && app->ai_limits.randomness >= 10) {
            app->ai_limits.randomness -= 10;
        }

        if (gui_button(random_plus_btn, "+") && app->ai_limits.randomness <= 90) {
            app->ai_limits.randomness += 10;
        }

        {
            char rnd_text[64];
            snprintf(rnd_text, sizeof(rnd_text), "AI Randomness: %d", app->ai_limits.randomness);
            DrawText(rnd_text, 430, 307, 22, (Color){30, 30, 30, 255});
        }
    }

    DrawText("0 = strongest deterministic play", 430, 404, 18, (Color){70, 70, 70, 255});
}
