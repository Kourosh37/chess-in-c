#include "gui.h"

#include <stdio.h>

#include "game_state.h"

/* Draws centered title and mode buttons on top-level menu. */
void gui_screen_menu(struct ChessApp* app) {
    const GuiPalette* palette = gui_palette();
    float sw = (float)GetScreenWidth();
    float sh = (float)GetScreenHeight();
    float panel_w = sw * 0.56f;
    float panel_h = sh * 0.72f;
    Rectangle panel;
    char profile_line[128];
    Rectangle single_btn;
    Rectangle local_btn;
    Rectangle online_btn;
    Rectangle resume_btn;
    Rectangle settings_btn;
    bool has_resume = (app->mode == MODE_ONLINE && app->online_match_active);

    if (panel_w < 520.0f) {
        panel_w = 520.0f;
    }
    if (panel_w > 760.0f) {
        panel_w = 760.0f;
    }
    if (panel_h < 480.0f) {
        panel_h = 480.0f;
    }
    if (panel_h > 700.0f) {
        panel_h = 700.0f;
    }

    panel = (Rectangle){
        sw * 0.5f - panel_w * 0.5f,
        sh * 0.5f - panel_h * 0.5f,
        panel_w,
        panel_h
    };

    DrawRectangleRounded((Rectangle){panel.x + 4.0f, panel.y + 6.0f, panel.width, panel.height},
                         0.08f,
                         8,
                         Fade(BLACK, 0.14f));
    DrawRectangleRounded(panel, 0.08f, 8, Fade(palette->panel, 0.94f));
    DrawRectangleRoundedLinesEx(panel, 0.08f, 8, 1.5f, palette->panel_border);

    gui_draw_text("Chess", (int)panel.x + 40, (int)panel.y + 38, 58, palette->text_primary);

    snprintf(profile_line,
             sizeof(profile_line),
             "Player: %s    Wins: %u    Losses: %u",
             app->profile.username,
             app->profile.wins,
             app->profile.losses);
    gui_draw_text(profile_line, (int)panel.x + 42, (int)panel.y + 126, 22, palette->text_primary);

    single_btn = (Rectangle){panel.x + 42.0f, panel.y + 184.0f, panel.width - 84.0f, 58.0f};
    local_btn = (Rectangle){panel.x + 42.0f, panel.y + 255.0f, panel.width - 84.0f, 58.0f};
    online_btn = (Rectangle){panel.x + 42.0f, panel.y + 326.0f, panel.width - 84.0f, 58.0f};
    resume_btn = (Rectangle){panel.x + 42.0f, panel.y + 397.0f, panel.width - 84.0f, 58.0f};
    settings_btn = (Rectangle){panel.x + 42.0f, panel.y + (has_resume ? 468.0f : 397.0f), panel.width - 84.0f, 58.0f};

    if (gui_button(single_btn, "Single Player")) {
        if (app->online_match_active) {
            app_online_end_match(app, true);
        }
        app->human_side = SIDE_WHITE;
        app_start_game(app, MODE_SINGLE);
    }

    if (gui_button(local_btn, "Local Multiplayer")) {
        if (app->online_match_active) {
            app_online_end_match(app, true);
        }
        app_start_game(app, MODE_LOCAL);
    }

    if (gui_button(online_btn, "Online")) {
        app->mode = MODE_ONLINE;
        app->screen = SCREEN_LOBBY;
        app->lobby_input[0] = '\0';
        app->lobby_code[0] = '\0';
        snprintf(app->lobby_status, sizeof(app->lobby_status), "Host or join with invite code.");
    }

    if (has_resume && gui_button(resume_btn, "Resume Online Match")) {
        app->screen = SCREEN_PLAY;
    }

    if (gui_button(settings_btn, "Settings")) {
        app->screen = SCREEN_SETTINGS;
    }

    if (has_resume) {
        gui_draw_text(app->online_runtime_status,
                 (int)panel.x + 42,
                 (int)panel.y + panel.height - 48,
                 18,
                 palette->text_secondary);
    }
}
