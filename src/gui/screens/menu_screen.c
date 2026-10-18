#include "gui.h"

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

/* Draws confirmation dialog for app exit action from main menu. */
static void draw_exit_confirm_dialog(ChessApp* app) {
    const GuiPalette* palette = gui_palette();
    float sw = (float)GetScreenWidth();
    float sh = (float)GetScreenHeight();
    float panel_w = sw * 0.44f;
    float panel_h = 228.0f;
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

    draw_text_wrap("Are you sure you want to close the game?",
                   text_x,
                   text_y,
                   22,
                   text_w,
                   26,
                   3,
                   palette->text_secondary);

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
    Rectangle settings_btn;
    Rectangle exit_btn;
    bool input_locked = app->exit_confirm_open;

    if (panel_w < 520.0f) {
        panel_w = 520.0f;
    }
    if (panel_w > 760.0f) {
        panel_w = 760.0f;
    }
    panel_min_h = 550.0f;
    if (panel_h < panel_min_h) {
        panel_h = panel_min_h;
    }
    if (panel_h > 670.0f) {
        panel_h = 670.0f;
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
    settings_btn = (Rectangle){panel.x + 42.0f, panel.y + 326.0f, panel.width - 84.0f, 58.0f};
    exit_btn = (Rectangle){panel.x + 42.0f, panel.y + 397.0f, panel.width - 84.0f, 52.0f};

    if (!input_locked) {
        if (gui_button(single_btn, "Single Player")) {
            app->human_side = SIDE_WHITE;
            app_start_game(app, MODE_SINGLE);
        }

        if (gui_button(local_btn, "Local 2 Player")) {
            app_start_game(app, MODE_LOCAL);
        }

        if (gui_button(settings_btn, "Settings")) {
            app->screen = SCREEN_SETTINGS;
        }

        if (gui_button(exit_btn, "Exit")) {
            app->exit_confirm_open = true;
        }
    }

    if (app->exit_confirm_open) {
        draw_exit_confirm_dialog(app);
    }
}
