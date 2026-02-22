#include "gui.h"

#include <stdio.h>

#include "game_state.h"

/* Draws confirmation dialog for app exit action from main menu. */
static void draw_exit_confirm_dialog(ChessApp* app) {
    const GuiPalette* palette = gui_palette();
    float sw = (float)GetScreenWidth();
    float sh = (float)GetScreenHeight();
    float panel_w = sw * 0.44f;
    float panel_h = app->online_match_active ? 228.0f : 196.0f;
    Rectangle panel;
    Rectangle cancel_btn;
    Rectangle exit_btn;

    if (panel_w < 420.0f) {
        panel_w = 420.0f;
    }
    if (panel_w > 620.0f) {
        panel_w = 620.0f;
    }

    panel = (Rectangle){
        sw * 0.5f - panel_w * 0.5f,
        sh * 0.5f - panel_h * 0.5f,
        panel_w,
        panel_h
    };

    DrawRectangle(0, 0, (int)sw, (int)sh, Fade(BLACK, 0.52f));
    DrawRectangleRounded(panel, 0.08f, 8, Fade(palette->panel, 0.98f));
    DrawRectangleRoundedLinesEx(panel, 0.08f, 8, 1.4f, palette->panel_border);

    gui_draw_text("Exit Chess?", (int)panel.x + 20, (int)panel.y + 20, 36, palette->text_primary);
    gui_draw_text("Are you sure you want to close the game?",
                  (int)panel.x + 20,
                  (int)panel.y + 76,
                  22,
                  palette->text_secondary);

    if (app->online_match_active) {
        gui_draw_text("Current online match will be closed and opponent will be notified.",
                      (int)panel.x + 20,
                      (int)panel.y + 108,
                      20,
                      palette->text_secondary);
    }

    cancel_btn = (Rectangle){panel.x + 20.0f, panel.y + panel.height - 64.0f, 140.0f, 44.0f};
    exit_btn = (Rectangle){panel.x + panel.width - 160.0f, panel.y + panel.height - 64.0f, 140.0f, 44.0f};

    if (gui_button(cancel_btn, "Cancel")) {
        app->exit_confirm_open = false;
    }

    if (gui_button(exit_btn, "Exit")) {
        app->exit_confirm_open = false;
        if (app->online_match_active) {
            app_online_end_match(app, true);
        }
        app->exit_requested = true;
    }
}

/* Draws centered title and mode buttons on top-level menu. */
void gui_screen_menu(struct ChessApp* app) {
    const GuiPalette* palette = gui_palette();
    float sw = (float)GetScreenWidth();
    float sh = (float)GetScreenHeight();
    float panel_w = sw * 0.56f;
    float panel_h = sh * 0.72f;
    float panel_min_h;
    Rectangle panel;
    char profile_line[128];
    Rectangle single_btn;
    Rectangle local_btn;
    Rectangle online_btn;
    Rectangle resume_btn;
    Rectangle settings_btn;
    Rectangle exit_btn;
    bool has_resume = (app->mode == MODE_ONLINE && app->online_match_active);
    bool input_locked = app->exit_confirm_open;

    if (panel_w < 520.0f) {
        panel_w = 520.0f;
    }
    if (panel_w > 760.0f) {
        panel_w = 760.0f;
    }
    panel_min_h = has_resume ? 620.0f : 560.0f;
    if (panel_h < panel_min_h) {
        panel_h = panel_min_h;
    }
    if (panel_h > 740.0f) {
        panel_h = 740.0f;
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
    exit_btn = (Rectangle){panel.x + 42.0f, panel.y + (has_resume ? 539.0f : 468.0f), panel.width - 84.0f, 52.0f};

    if (!input_locked) {
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
            app->lobby_view = LOBBY_VIEW_HOME;
            app->lobby_input[0] = '\0';
            app->lobby_code[0] = '\0';
            app->lobby_input_active = false;
            app->online_local_ready = false;
            app->online_peer_ready = false;
            app->lobby_copy_feedback = false;
            app->lobby_copy_feedback_timer = 0.0f;
            snprintf(app->lobby_status, sizeof(app->lobby_status), "Choose Host Game or Join Game.");
        }

        if (has_resume && gui_button(resume_btn, "Resume Online Match")) {
            app->screen = SCREEN_PLAY;
        }

        if (gui_button(settings_btn, "Settings")) {
            app->screen = SCREEN_SETTINGS;
        }

        if (gui_button(exit_btn, "Exit")) {
            app->exit_confirm_open = true;
        }
    }

    if (has_resume) {
        gui_draw_text(app->online_runtime_status,
                      (int)panel.x + 42,
                      (int)panel.y + panel.height - 28,
                      18,
                      palette->text_secondary);
    }

    if (app->exit_confirm_open) {
        draw_exit_confirm_dialog(app);
    }
}
