#include "gui.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "game_state.h"

/* Draws one clipped text line that never overflows the target width. */
static void draw_text_fit(const char* text,
                          int x,
                          int y,
                          int font_size,
                          int max_width,
                          Color color) {
    char buffer[192];
    size_t len;
    int ellipsis_w;

    if (text == NULL || max_width <= 0) {
        return;
    }

    if (gui_measure_text(text, font_size) <= max_width) {
        gui_draw_text(text, x, y, font_size, color);
        return;
    }

    ellipsis_w = gui_measure_text("...", font_size);
    if (ellipsis_w >= max_width) {
        return;
    }

    strncpy(buffer, text, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';
    len = strlen(buffer);

    while (len > 0) {
        buffer[len] = '\0';
        if (gui_measure_text(buffer, font_size) + ellipsis_w <= max_width) {
            break;
        }
        len--;
    }

    if (len == 0) {
        return;
    }

    strncat(buffer, "...", sizeof(buffer) - strlen(buffer) - 1);
    gui_draw_text(buffer, x, y, font_size, color);
}

/* Draws wrapped text lines inside max width to avoid overflow in modal dialogs. */
static int draw_text_wrap(const char* text,
                          int x,
                          int y,
                          int font_size,
                          int max_width,
                          int line_height,
                          int max_lines,
                          Color color) {
    const char* cursor = text;
    int lines = 0;

    if (text == NULL || max_width <= 0 || max_lines <= 0) {
        return 0;
    }

    if (line_height <= 0) {
        line_height = gui_measure_text_height(font_size) + 8;
    }

    while (*cursor != '\0' && lines < max_lines) {
        char line[256];
        int len = 0;
        int next_len = 0;
        int last_space = -1;

        while (cursor[next_len] != '\0' && cursor[next_len] != '\n' && len < (int)sizeof(line) - 1) {
            line[len] = cursor[next_len];
            line[len + 1] = '\0';

            if (line[len] == ' ') {
                last_space = len;
            }

            if (gui_measure_text(line, font_size) > max_width) {
                if (last_space >= 0) {
                    len = last_space;
                    next_len = last_space + 1;
                }
                break;
            }

            len++;
            next_len++;
        }

        while (len > 0 && line[len - 1] == ' ') {
            len--;
        }
        line[len] = '\0';

        if (len == 0 && cursor[0] != '\0' && cursor[0] != '\n') {
            line[0] = cursor[0];
            line[1] = '\0';
            next_len = 1;
        }

        if (line[0] != '\0') {
            gui_draw_text(line, x, y + lines * line_height, font_size, color);
            lines++;
        }

        cursor += next_len;
        while (*cursor == ' ') {
            cursor++;
        }
        if (*cursor == '\n') {
            cursor++;
        }
    }

    return lines;
}

/* Starts one async online action and shows blocking loading modal. */
static void start_online_loading(ChessApp* app,
                                 OnlineAsyncAction action,
                                 const char* title,
                                 const char* text,
                                 int match_index,
                                 const char* invite_code,
                                 bool reconnect_is_host) {
    if (app == NULL || app->online_loading || action == ONLINE_ASYNC_NONE) {
        return;
    }

    app_clear_network_error(app);
    app->online_loading = true;
    app->online_loading_action = action;
    app->online_loading_match_index = match_index;
    app->online_loading_reconnect_host = reconnect_is_host;

    if (invite_code != NULL) {
        strncpy(app->online_loading_code, invite_code, INVITE_CODE_LEN);
    } else {
        app->online_loading_code[0] = '\0';
    }
    app->online_loading_code[INVITE_CODE_LEN] = '\0';

    strncpy(app->online_loading_title,
            (title != NULL && title[0] != '\0') ? title : "Loading",
            sizeof(app->online_loading_title) - 1);
    app->online_loading_title[sizeof(app->online_loading_title) - 1] = '\0';

    strncpy(app->online_loading_text,
            (text != NULL && text[0] != '\0') ? text : "Please wait...",
            sizeof(app->online_loading_text) - 1);
    app->online_loading_text[sizeof(app->online_loading_text) - 1] = '\0';
}

/* Draws modal spinner/loading panel for async online actions. */
static void draw_online_loading_dialog(const ChessApp* app) {
    const GuiPalette* palette = gui_palette();
    float sw;
    float sh;
    float panel_w;
    float panel_h = 236.0f;
    Rectangle panel;
    Vector2 spinner_center;
    float t;
    float start_deg;
    int dots_count;
    char dots[4];
    char line[220];

    if (app == NULL || !app->online_loading) {
        return;
    }

    sw = (float)GetScreenWidth();
    sh = (float)GetScreenHeight();
    panel_w = sw * 0.42f;

    if (panel_w < 430.0f) {
        panel_w = 430.0f;
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

    DrawRectangle(0, 0, (int)sw, (int)sh, Fade(BLACK, 0.56f));
    DrawRectangleRounded(panel, 0.09f, 8, Fade(palette->panel, 0.98f));
    DrawRectangleRoundedLinesEx(panel, 0.09f, 8, 1.5f, palette->panel_border);

    spinner_center = (Vector2){panel.x + 54.0f, panel.y + panel.height * 0.5f + 6.0f};
    t = (float)GetTime();
    start_deg = fmodf(t * 220.0f, 360.0f);
    DrawRing(spinner_center, 18.0f, 26.0f, 0.0f, 360.0f, 48, Fade(palette->panel_border, 0.45f));
    DrawRing(spinner_center, 18.0f, 26.0f, start_deg, start_deg + 265.0f, 48, palette->accent);

    draw_text_fit(app->online_loading_title[0] != '\0' ? app->online_loading_title : "Loading",
                  (int)panel.x + 96,
                  (int)panel.y + 38,
                  34,
                  (int)panel.width - 118,
                  palette->text_primary);

    dots_count = ((int)(t * 3.0f)) % 4;
    for (int i = 0; i < 3; ++i) {
        dots[i] = (i < dots_count) ? '.' : '\0';
    }
    dots[3] = '\0';

    snprintf(line,
             sizeof(line),
             "%s%s",
             app->online_loading_text[0] != '\0' ? app->online_loading_text : "Please wait",
             dots);
    draw_text_wrap(line,
                   (int)panel.x + 96,
                   (int)panel.y + 96,
                   21,
                   (int)panel.width - 124,
                   25,
                   4,
                   palette->text_secondary);
}

/* Opens in-place modal for online display name entry. */
static void open_online_name_dialog(ChessApp* app) {
    if (app == NULL) {
        return;
    }

    app->online_name_dialog_open = true;
    app->online_name_input_active = true;
    strncpy(app->online_name_input, app->online_name, PLAYER_NAME_MAX);
    app->online_name_input[PLAYER_NAME_MAX] = '\0';
    app->online_name_error[0] = '\0';
}

/* Draws one modal to collect online display name directly from menu. */
static bool draw_online_name_dialog(ChessApp* app) {
    const GuiPalette* palette = gui_palette();
    float sw = (float)GetScreenWidth();
    float sh = (float)GetScreenHeight();
    float panel_w = sw * 0.46f;
    float panel_h = 276.0f;
    Rectangle panel;
    Rectangle input_box;
    Rectangle cancel_btn;
    Rectangle save_btn;

    if (panel_w < 420.0f) {
        panel_w = 420.0f;
    }
    if (panel_w > 640.0f) {
        panel_w = 640.0f;
    }

    panel = (Rectangle){
        sw * 0.5f - panel_w * 0.5f,
        sh * 0.5f - panel_h * 0.5f,
        panel_w,
        panel_h
    };
    input_box = (Rectangle){panel.x + 24.0f, panel.y + 104.0f, panel.width - 48.0f, 54.0f};
    cancel_btn = (Rectangle){panel.x + 24.0f, panel.y + panel.height - 62.0f, 136.0f, 42.0f};
    save_btn = (Rectangle){panel.x + panel.width - 160.0f, panel.y + panel.height - 62.0f, 136.0f, 42.0f};

    DrawRectangle(0, 0, (int)sw, (int)sh, Fade(BLACK, 0.52f));
    DrawRectangleRounded(panel, 0.08f, 8, Fade(palette->panel, 0.98f));
    DrawRectangleRoundedLinesEx(panel, 0.08f, 8, 1.4f, palette->panel_border);

    gui_draw_text("Online Name", (int)panel.x + 24, (int)panel.y + 22, 36, palette->text_primary);
    draw_text_wrap("Enter your display name to use online mode.",
                   (int)panel.x + 24,
                   (int)panel.y + 70,
                   20,
                   (int)panel.width - 48,
                   24,
                   3,
                   palette->text_secondary);

    if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) || IsMouseButtonPressed(MOUSE_RIGHT_BUTTON)) {
        app->online_name_input_active = CheckCollisionPointRec(GetMousePosition(), input_box);
    }
    gui_input_box(input_box, app->online_name_input, PLAYER_NAME_MAX + 1, app->online_name_input_active);

    if (app->online_name_error[0] != '\0') {
        draw_text_wrap(app->online_name_error,
                       (int)panel.x + 24,
                       (int)panel.y + 166,
                       18,
                       (int)panel.width - 48,
                       22,
                       2,
                       (Color){188, 42, 48, 255});
    }

    if (gui_button(cancel_btn, "Cancel")) {
        app->online_name_dialog_open = false;
        app->online_name_input_active = false;
        app->online_name_error[0] = '\0';
    }

    if (gui_button_submit(save_btn, "Save", true)) {
        if (app->online_name_input[0] == '\0') {
            strncpy(app->online_name_error,
                    "Name cannot be empty.",
                    sizeof(app->online_name_error) - 1);
            app->online_name_error[sizeof(app->online_name_error) - 1] = '\0';
        } else {
            strncpy(app->online_name, app->online_name_input, PLAYER_NAME_MAX);
            app->online_name[PLAYER_NAME_MAX] = '\0';
            app_save_settings(app);
            app->online_name_dialog_open = false;
            app->online_name_input_active = false;
            app->online_name_error[0] = '\0';
            return true;
        }
    }

    return false;
}

/* Draws confirmation dialog for app exit action from main menu. */
static void draw_exit_confirm_dialog(ChessApp* app, int active_games) {
    const GuiPalette* palette = gui_palette();
    float sw = (float)GetScreenWidth();
    float sh = (float)GetScreenHeight();
    float panel_w = sw * 0.44f;
    float panel_h = (active_games > 0) ? 258.0f : 228.0f;
    Rectangle panel;
    Rectangle cancel_btn;
    Rectangle exit_btn;
    int text_x;
    int text_w;
    int text_y;

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
    text_x = (int)panel.x + 20;
    text_w = (int)panel.width - 40;
    text_y = (int)panel.y + 76;

    text_y += draw_text_wrap("Are you sure you want to close the game?",
                             text_x,
                             text_y,
                             22,
                             text_w,
                             26,
                             3,
                             palette->text_secondary) *
              26;

    if (active_games > 0) {
        draw_text_wrap("Active online sessions will be saved for later reconnect.",
                       text_x,
                       text_y + 6,
                       20,
                       text_w,
                       24,
                       3,
                       palette->text_secondary);
    }

    cancel_btn = (Rectangle){panel.x + 20.0f, panel.y + panel.height - 64.0f, 140.0f, 44.0f};
    exit_btn = (Rectangle){panel.x + panel.width - 160.0f, panel.y + panel.height - 64.0f, 140.0f, 44.0f};

    if (gui_button(cancel_btn, "Cancel")) {
        app->exit_confirm_open = false;
    }

    if (gui_button_submit(exit_btn, "Exit", true)) {
        app->exit_confirm_open = false;
        app->exit_requested = true;
    }
}

/* Draws generic blocking network error popup. */
static void draw_network_error_dialog(ChessApp* app) {
    const GuiPalette* palette = gui_palette();
    float sw = (float)GetScreenWidth();
    float sh = (float)GetScreenHeight();
    float panel_w = sw * 0.46f;
    float panel_h = 268.0f;
    Rectangle panel;
    Rectangle ok_btn;

    if (panel_w < 440.0f) {
        panel_w = 440.0f;
    }
    if (panel_w > 680.0f) {
        panel_w = 680.0f;
    }

    panel = (Rectangle){
        sw * 0.5f - panel_w * 0.5f,
        sh * 0.5f - panel_h * 0.5f,
        panel_w,
        panel_h
    };
    ok_btn = (Rectangle){panel.x + panel.width - 146.0f, panel.y + panel.height - 60.0f, 120.0f, 40.0f};

    DrawRectangle(0, 0, (int)sw, (int)sh, Fade(BLACK, 0.52f));
    DrawRectangleRounded(panel, 0.08f, 8, Fade(palette->panel, 0.98f));
    DrawRectangleRoundedLinesEx(panel, 0.08f, 8, 1.4f, palette->panel_border);

    draw_text_fit(app->network_error_popup_title[0] != '\0' ? app->network_error_popup_title : "Network Error",
                  (int)panel.x + 24,
                  (int)panel.y + 24,
                  34,
                  (int)panel.width - 48,
                  palette->text_primary);

    draw_text_wrap(app->network_error_popup_text[0] != '\0'
                       ? app->network_error_popup_text
                       : "Unknown network failure.",
                   (int)panel.x + 24,
                   (int)panel.y + 82,
                   20,
                   (int)panel.width - 48,
                   24,
                   6,
                   palette->text_secondary);

    if (gui_button_submit(ok_btn, "OK", true)) {
        app_clear_network_error(app);
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
    Rectangle settings_btn;
    Rectangle exit_btn;
    int active_games = app_online_active_count(app);
    bool input_locked = app->exit_confirm_open || app->online_name_dialog_open || app->network_error_popup_open ||
                        app->online_loading;

    if (panel_w < 520.0f) {
        panel_w = 520.0f;
    }
    if (panel_w > 760.0f) {
        panel_w = 760.0f;
    }
    panel_min_h = 620.0f;
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
    draw_text_fit(profile_line,
                  (int)panel.x + 42,
                  (int)panel.y + 126,
                  22,
                  (int)panel.width - 84,
                  palette->text_primary);

    single_btn = (Rectangle){panel.x + 42.0f, panel.y + 184.0f, panel.width - 84.0f, 58.0f};
    local_btn = (Rectangle){panel.x + 42.0f, panel.y + 255.0f, panel.width - 84.0f, 58.0f};
    online_btn = (Rectangle){panel.x + 42.0f, panel.y + 326.0f, panel.width - 84.0f, 58.0f};
    settings_btn = (Rectangle){panel.x + 42.0f, panel.y + 397.0f, panel.width - 84.0f, 58.0f};
    exit_btn = (Rectangle){panel.x + 42.0f, panel.y + 468.0f, panel.width - 84.0f, 52.0f};

    if (!input_locked) {
        if (gui_button(single_btn, "Single Player")) {
            app->human_side = SIDE_WHITE;
            app_start_game(app, MODE_SINGLE);
        }

        if (gui_button(local_btn, "Local 2 Player")) {
            app_start_game(app, MODE_LOCAL);
        }

        if (gui_button(online_btn, "Online")) {
            if (!app_online_name_is_set(app)) {
                open_online_name_dialog(app);
            } else {
                start_online_loading(app,
                                     ONLINE_ASYNC_ENTER_LOBBY,
                                     "Connecting Online",
                                     "Checking online service",
                                     -1,
                                     NULL,
                                     false);
            }
        }

        if (gui_button(settings_btn, "Settings")) {
            app->screen = SCREEN_SETTINGS;
        }

        if (gui_button(exit_btn, "Exit")) {
            app->exit_confirm_open = true;
        }
    }

    if (active_games > 0) {
        const char* status_text = app->online_runtime_status;
        if (app->current_online_match >= 0) {
            const OnlineMatch* current = app_online_get_const(app, app->current_online_match);
            if (current != NULL && current->status[0] != '\0') {
                status_text = current->status;
            }
        }
        draw_text_fit(status_text,
                      (int)panel.x + 42,
                      (int)panel.y + panel.height - 28,
                      18,
                      (int)panel.width - 84,
                      palette->text_secondary);
    }

    if (app->exit_confirm_open) {
        draw_exit_confirm_dialog(app, active_games);
    }

    if (app->online_name_dialog_open) {
        if (draw_online_name_dialog(app)) {
            start_online_loading(app,
                                 ONLINE_ASYNC_ENTER_LOBBY,
                                 "Connecting Online",
                                 "Checking online service",
                                 -1,
                                 NULL,
                                 false);
        }
    }

    if (app->online_loading) {
        draw_online_loading_dialog(app);
    }

    if (app->network_error_popup_open) {
        draw_network_error_dialog(app);
    }
}
