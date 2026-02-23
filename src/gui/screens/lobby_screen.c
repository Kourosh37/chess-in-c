#include "gui.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "game_state.h"

/* Draws one clipped text line to keep status text inside card bounds. */
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

/* Draws wrapped text inside width to avoid modal text overflow. */
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

/* Starts one async online action and opens loading overlay. */
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

/* Draws modal spinner/loading panel while background online task runs. */
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

/* Draws a rounded status/info block used in lobby subviews. */
static void draw_status_box(Rectangle rect, const char* title, const char* text) {
    const GuiPalette* palette = gui_palette();

    DrawRectangleRounded(rect, 0.10f, 8, Fade(palette->panel, 0.95f));
    DrawRectangleRoundedLinesEx(rect, 0.10f, 8, 1.0f, palette->panel_border);
    gui_draw_text(title, (int)rect.x + 14, (int)rect.y + 10, 24, palette->text_primary);
    draw_text_fit(text,
                  (int)rect.x + 14,
                  (int)rect.y + 44,
                  21,
                  (int)rect.width - 28,
                  palette->text_secondary);
}

/* Returns one status string for current focus match or global lobby status. */
static const char* focused_status(const ChessApp* app) {
    const OnlineMatch* match = app_online_get_const(app, app->lobby_focus_match);
    if (match != NULL && match->status[0] != '\0') {
        return match->status;
    }
    return app->lobby_status;
}

/* Draws global network error popup while user stays inside online screen. */
static void draw_network_error_dialog(ChessApp* app) {
    const GuiPalette* palette = gui_palette();
    float sw = (float)GetScreenWidth();
    float sh = (float)GetScreenHeight();
    float panel_w = sw * 0.44f;
    float panel_h = 264.0f;
    Rectangle panel;
    Rectangle ok_btn;

    if (panel_w < 430.0f) {
        panel_w = 430.0f;
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
    ok_btn = (Rectangle){panel.x + panel.width - 146.0f, panel.y + panel.height - 58.0f, 120.0f, 40.0f};

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
                       : "Unknown network error.",
                   (int)panel.x + 24,
                   (int)panel.y + 84,
                   20,
                   (int)panel.width - 48,
                   24,
                   6,
                   palette->text_secondary);

    if (gui_button_submit(ok_btn, "OK", true)) {
        app_clear_network_error(app);
    }
}

/* Opens one match in lobby context and syncs shared app runtime fields. */
static void focus_match(ChessApp* app, int index, LobbyView view) {
    const OnlineMatch* match = app_online_get_const(app, index);

    if (match == NULL) {
        app->lobby_focus_match = -1;
        return;
    }

    app->lobby_focus_match = index;
    app_online_switch_to_match(app, index, false);
    app->lobby_view = view;
    strncpy(app->lobby_status, match->status, sizeof(app->lobby_status) - 1);
    app->lobby_status[sizeof(app->lobby_status) - 1] = '\0';
}

/* Draws list of active sessions sorted by latest start timestamp. */
static void draw_active_matches(ChessApp* app, Rectangle list_rect, bool input_locked) {
    const GuiPalette* palette = gui_palette();
    int sorted[ONLINE_MATCH_MAX];
    int count = 0;
    Vector2 mouse = GetMousePosition();
    int item_h = 90;
    int visible;
    int max_start;
    int start;

    for (int i = 0; i < ONLINE_MATCH_MAX; ++i) {
        if (app_online_get_const(app, i) != NULL) {
            sorted[count++] = i;
        }
    }

    for (int i = 0; i < count; ++i) {
        for (int j = i + 1; j < count; ++j) {
            const OnlineMatch* a = app_online_get_const(app, sorted[i]);
            const OnlineMatch* b = app_online_get_const(app, sorted[j]);
            if (a != NULL && b != NULL && b->started_epoch > a->started_epoch) {
                int tmp = sorted[i];
                sorted[i] = sorted[j];
                sorted[j] = tmp;
            }
        }
    }

    DrawRectangleRounded(list_rect, 0.08f, 8, Fade(palette->panel, 0.92f));
    DrawRectangleRoundedLinesEx(list_rect, 0.08f, 8, 1.0f, palette->panel_border);

    visible = (int)((list_rect.height - 12.0f) / (float)item_h);
    if (visible < 1) {
        visible = 1;
    }
    max_start = count - visible;
    if (max_start < 0) {
        max_start = 0;
    }

    if (CheckCollisionPointRec(mouse, list_rect)) {
        float wheel = GetMouseWheelMove();
        if (wheel != 0.0f) {
            app->lobby_active_scroll -= (int)(wheel * 2.0f);
        }
    }
    if (app->lobby_active_scroll < 0) {
        app->lobby_active_scroll = 0;
    }
    if (app->lobby_active_scroll > max_start) {
        app->lobby_active_scroll = max_start;
    }

    start = app->lobby_active_scroll;

    if (count == 0) {
        gui_draw_text("No active online games.", (int)list_rect.x + 14, (int)list_rect.y + 16, 24, palette->text_secondary);
        return;
    }

    for (int i = 0; i < visible; ++i) {
        int item_index = start + i;
        int slot_index;
        const OnlineMatch* match;
        Rectangle row;
        Rectangle open_btn;
        Rectangle close_btn;
        char line[192];
        const char* opp_name;

        if (item_index >= count) {
            break;
        }

        slot_index = sorted[item_index];
        match = app_online_get_const(app, slot_index);
        if (match == NULL) {
            continue;
        }

        row = (Rectangle){
            list_rect.x + 10.0f,
            list_rect.y + 6.0f + (float)i * (float)item_h,
            list_rect.width - 20.0f,
            (float)item_h - 6.0f
        };
        open_btn = (Rectangle){row.x + row.width - 232.0f, row.y + row.height - 42.0f, 108.0f, 34.0f};
        close_btn = (Rectangle){row.x + row.width - 116.0f, row.y + row.height - 42.0f, 100.0f, 34.0f};
        opp_name = (match->opponent_name[0] != '\0') ? match->opponent_name : "Unknown";

        DrawRectangleRounded(row, 0.10f, 8, Fade(palette->panel_alt, 0.95f));
        DrawRectangleRoundedLinesEx(row, 0.10f, 8, 1.0f, palette->panel_border);

        snprintf(line, sizeof(line), "Opponent: %s", opp_name);
        draw_text_fit(line, (int)row.x + 12, (int)row.y + 8, 21, (int)row.width - 260, palette->text_primary);

        snprintf(line, sizeof(line), "Start: %s", match->started_at[0] != '\0' ? match->started_at : "unknown");
        draw_text_fit(line, (int)row.x + 12, (int)row.y + 34, 18, (int)row.width - 260, palette->text_secondary);

        snprintf(line,
                 sizeof(line),
                 "State: %s | %s",
                 match->in_game ? "In Game" : "Waiting Room",
                 match->is_host ? "Host" : "Guest");
        draw_text_fit(line, (int)row.x + 12, (int)row.y + 56, 18, (int)row.width - 260, palette->text_secondary);

        if (!input_locked &&
            gui_button(open_btn,
                       (!match->connected && match->invite_code[0] != '\0')
                           ? "Reconnect"
                           : (match->in_game ? "Resume" : "Open"))) {
            if (!match->connected && match->invite_code[0] != '\0') {
                start_online_loading(app,
                                     ONLINE_ASYNC_RECONNECT_ROOM,
                                     "Reconnecting Match",
                                     "Restoring room connection",
                                     slot_index,
                                     match->invite_code,
                                     match->is_host);
                return;
            }

            if (match->in_game) {
                app_online_switch_to_match(app, slot_index, true);
                return;
            }
            focus_match(app, slot_index, match->is_host ? LOBBY_VIEW_HOST : LOBBY_VIEW_JOIN);
            return;
        }

        if (!input_locked && gui_button(close_btn, "Close")) {
            app_online_close_match(app, slot_index, true);
            snprintf(app->lobby_status, sizeof(app->lobby_status), "Match closed.");
            return;
        }
    }
}

/* Renders and updates online lobby flow (host/join/multi-active sessions). */
void gui_screen_lobby(struct ChessApp* app) {
    const GuiPalette* palette = gui_palette();
    float sw = (float)GetScreenWidth();
    float sh = (float)GetScreenHeight();
    float panel_w = sw * 0.72f;
    float panel_h = sh * 0.78f;
    Rectangle panel;
    Rectangle back_btn;
    Rectangle card;
    bool input_locked = app->online_loading || app->network_error_popup_open;

    if (panel_w < 780.0f) {
        panel_w = 780.0f;
    }
    if (panel_w > 1020.0f) {
        panel_w = 1020.0f;
    }
    if (panel_h < 620.0f) {
        panel_h = 620.0f;
    }
    if (panel_h > 740.0f) {
        panel_h = 740.0f;
    }

    if (app->lobby_copy_feedback_timer > 0.0f) {
        app->lobby_copy_feedback_timer -= GetFrameTime();
        if (app->lobby_copy_feedback_timer <= 0.0f) {
            app->lobby_copy_feedback_timer = 0.0f;
            app->lobby_copy_feedback = false;
        }
    }

    panel = (Rectangle){
        sw * 0.5f - panel_w * 0.5f,
        sh * 0.5f - panel_h * 0.5f,
        panel_w,
        panel_h
    };
    card = (Rectangle){
        panel.x + 28.0f,
        panel.y + 106.0f,
        panel.width - 56.0f,
        panel.height - 132.0f
    };

    DrawRectangleRounded((Rectangle){panel.x + 5.0f, panel.y + 6.0f, panel.width, panel.height},
                         0.08f,
                         8,
                         Fade(BLACK, 0.16f));
    DrawRectangleRounded(panel, 0.08f, 8, Fade(palette->panel, 0.95f));
    DrawRectangleRoundedLinesEx(panel, 0.08f, 8, 1.4f, palette->panel_border);

    gui_draw_text("Online", (int)panel.x + 30, (int)panel.y + 28, 48, palette->text_primary);
    {
        char name_line[96];
        snprintf(name_line, sizeof(name_line), "Name: %s", app->online_name[0] != '\0' ? app->online_name : "(not set)");
        gui_draw_text(name_line, (int)panel.x + 34, (int)panel.y + 74, 20, palette->text_secondary);
    }

    back_btn = (Rectangle){panel.x + panel.width - 176.0f, panel.y + 24.0f, 146.0f, 50.0f};
    if (!input_locked && gui_button(back_btn, "Back")) {
        app->screen = SCREEN_MENU;
        if (app->network_error_popup_open) {
            draw_network_error_dialog(app);
        }
        return;
    }

    DrawRectangleRounded(card, 0.08f, 8, Fade(palette->panel_alt, 0.95f));
    DrawRectangleRoundedLinesEx(card, 0.08f, 8, 1.0f, palette->panel_border);

    if (app->network_error_popup_open) {
        draw_network_error_dialog(app);
        return;
    }

    if (app->lobby_view == LOBBY_VIEW_ACTIVE) {
        Rectangle list_rect = {card.x + 28.0f, card.y + 78.0f, card.width - 56.0f, card.height - 160.0f};
        Rectangle lobby_btn = {card.x + 28.0f, card.y + card.height - 68.0f, 220.0f, 42.0f};

        gui_draw_text("Active Games", (int)card.x + 28, (int)card.y + 30, 34, palette->text_primary);
        draw_active_matches(app, list_rect, input_locked);

        if (!input_locked && gui_button(lobby_btn, "Open Online Lobby")) {
            app->lobby_view = LOBBY_VIEW_HOME;
            app->lobby_focus_match = -1;
            if (app->network_error_popup_open) {
                draw_network_error_dialog(app);
            }
            return;
        }
        if (app->network_error_popup_open) {
            draw_network_error_dialog(app);
        }
        if (app->online_loading) {
            draw_online_loading_dialog(app);
        }
        return;
    }

    if (app->lobby_view == LOBBY_VIEW_HOME) {
        Rectangle join_btn = {card.x + 36.0f, card.y + 116.0f, card.width - 72.0f, 64.0f};
        Rectangle host_btn = {card.x + 36.0f, card.y + 196.0f, card.width - 72.0f, 64.0f};
        Rectangle active_btn = {card.x + 36.0f, card.y + 276.0f, card.width - 72.0f, 56.0f};
        Rectangle status_box = {card.x + 36.0f, card.y + card.height - 150.0f, card.width - 72.0f, 108.0f};

        gui_draw_text("Choose one option", (int)card.x + 36, (int)card.y + 38, 34, palette->text_primary);

        if (!input_locked && gui_button(join_btn, "Join Game")) {
            app->lobby_view = LOBBY_VIEW_JOIN;
            app->lobby_focus_match = -1;
            app->lobby_input[0] = '\0';
            app->lobby_input_active = true;
            snprintf(app->lobby_status, sizeof(app->lobby_status), "Enter invite code and press Join.");
        }

        if (!input_locked && gui_button(host_btn, "Host Game")) {
            start_online_loading(app,
                                 ONLINE_ASYNC_HOST_ROOM,
                                 "Creating Room",
                                 "Preparing your host room",
                                 -1,
                                 NULL,
                                 false);
        }

        if (!input_locked && gui_button(active_btn, "Active Games")) {
            app->lobby_view = LOBBY_VIEW_ACTIVE;
            app->lobby_focus_match = -1;
            if (app->network_error_popup_open) {
                draw_network_error_dialog(app);
            }
            return;
        }

        draw_status_box(status_box, "Status", app->lobby_status);
        if (app->network_error_popup_open) {
            draw_network_error_dialog(app);
        }
        if (app->online_loading) {
            draw_online_loading_dialog(app);
        }
        return;
    }

    if (app->lobby_view == LOBBY_VIEW_JOIN) {
        OnlineMatch* focus = app_online_get(app, app->lobby_focus_match);
        Rectangle input_box = {card.x + 36.0f, card.y + 88.0f, card.width - 72.0f, 56.0f};
        Rectangle join_btn = {card.x + 36.0f, card.y + 154.0f, card.width - 72.0f, 54.0f};
        Rectangle ready_btn = {card.x + 36.0f, card.y + 218.0f, card.width - 72.0f, 52.0f};
        Rectangle open_btn = {card.x + 36.0f, card.y + 282.0f, card.width - 72.0f, 52.0f};
        Rectangle mode_btn = {card.x + 36.0f, card.y + card.height - 66.0f, 186.0f, 44.0f};
        Rectangle status_box = {
            card.x + 36.0f,
            card.y + 344.0f,
            card.width - 72.0f,
            mode_btn.y - (card.y + 344.0f) - 8.0f
        };

        gui_draw_text("Join Game", (int)card.x + 36, (int)card.y + 36, 34, palette->text_primary);

        if (!input_locked &&
            (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) || IsMouseButtonPressed(MOUSE_RIGHT_BUTTON))) {
            app->lobby_input_active = CheckCollisionPointRec(GetMousePosition(), input_box);
        }
        gui_input_box(input_box, app->lobby_input, INVITE_CODE_LEN + 1, app->lobby_input_active && !input_locked);

        if (!input_locked && gui_button_submit(join_btn, "Join", app->lobby_input_active)) {
            if (!matchmaker_is_valid_code(app->lobby_input)) {
                snprintf(app->lobby_status, sizeof(app->lobby_status), "Invite code is invalid.");
            } else {
                start_online_loading(app,
                                     ONLINE_ASYNC_JOIN_ROOM,
                                     "Joining Room",
                                     "Connecting to room",
                                     -1,
                                     app->lobby_input,
                                     false);
            }
        }

        if (focus != NULL && focus->connected && !focus->in_game) {
            const char* ready_label = focus->local_ready ? "Ready (On)" : "Ready";

            if (!input_locked && gui_button(ready_btn, ready_label)) {
                bool next_ready = !focus->local_ready;
                if (!app_online_send_ready(app, app->lobby_focus_match, next_ready)) {
                    app_show_network_error(app, "Online Error", network_last_error());
                    snprintf(app->lobby_status, sizeof(app->lobby_status), "Failed to update ready status.");
                } else {
                    snprintf(app->lobby_status,
                             sizeof(app->lobby_status),
                             next_ready ? "You are Ready. Waiting for host to start."
                                        : "You are not ready.");
                }
            }
        } else {
            DrawRectangleRounded(ready_btn, 0.20f, 10, Fade(palette->panel, 0.85f));
            DrawRectangleRoundedLinesEx(ready_btn, 0.20f, 10, 1.0f, palette->panel_border);
            gui_draw_text("Ready", (int)ready_btn.x + 24, (int)ready_btn.y + 14, 24, palette->text_secondary);
        }

        if (!input_locked && focus != NULL && focus->in_game && gui_button(open_btn, "Open Match")) {
            app_online_switch_to_match(app, app->lobby_focus_match, true);
            if (app->network_error_popup_open) {
                draw_network_error_dialog(app);
            }
            return;
        } else if (focus == NULL || !focus->in_game) {
            DrawRectangleRounded(open_btn, 0.20f, 10, Fade(palette->panel, 0.85f));
            DrawRectangleRoundedLinesEx(open_btn, 0.20f, 10, 1.0f, palette->panel_border);
            gui_draw_text("Open Match", (int)open_btn.x + 24, (int)open_btn.y + 14, 24, palette->text_secondary);
        }

        if (!input_locked && gui_button(mode_btn, "Change Mode")) {
            app->lobby_view = LOBBY_VIEW_HOME;
            app->lobby_focus_match = -1;
            if (app->network_error_popup_open) {
                draw_network_error_dialog(app);
            }
            return;
        }

        draw_status_box(status_box, "Status", focused_status(app));
        if (app->network_error_popup_open) {
            draw_network_error_dialog(app);
        }
        if (app->online_loading) {
            draw_online_loading_dialog(app);
        }
        return;
    }

    {
        OnlineMatch* focus = app_online_get(app, app->lobby_focus_match);
        Rectangle code_box;
        Rectangle room_box;
        Rectangle start_btn;
        Rectangle mode_btn;
        Rectangle status_box;
        Rectangle copy_btn;
        char members_line[64];
        char opp_line[96];
        const char* copy_label = app->lobby_copy_feedback ? "Copied" : "Copy";

        if (focus == NULL) {
            app->lobby_view = LOBBY_VIEW_HOME;
            app->lobby_focus_match = -1;
            if (app->network_error_popup_open) {
                draw_network_error_dialog(app);
            }
            return;
        }

        code_box = (Rectangle){card.x + 36.0f, card.y + 78.0f, card.width - 72.0f, 74.0f};
        room_box = (Rectangle){card.x + 36.0f, card.y + 162.0f, card.width - 72.0f, 98.0f};
        start_btn = (Rectangle){card.x + 36.0f, card.y + 272.0f, card.width - 72.0f, 52.0f};
        mode_btn = (Rectangle){card.x + 36.0f, card.y + card.height - 66.0f, 186.0f, 44.0f};
        status_box = (Rectangle){
            card.x + 36.0f,
            start_btn.y + start_btn.height + 10.0f,
            card.width - 72.0f,
            mode_btn.y - (start_btn.y + start_btn.height + 10.0f) - 8.0f
        };
        copy_btn = (Rectangle){code_box.x + code_box.width - 130.0f, code_box.y + 24.0f, 112.0f, 40.0f};

        gui_draw_text("Host Game", (int)card.x + 36, (int)card.y + 30, 34, palette->text_primary);

        DrawRectangleRounded(code_box, 0.10f, 8, Fade(palette->panel, 0.95f));
        DrawRectangleRoundedLinesEx(code_box, 0.10f, 8, 1.0f, palette->panel_border);
        gui_draw_text("Invite Code", (int)code_box.x + 14, (int)code_box.y + 8, 22, palette->text_secondary);
        gui_draw_text(focus->invite_code, (int)code_box.x + 14, (int)code_box.y + 36, 31, palette->accent);

        if (!input_locked && gui_button(copy_btn, copy_label)) {
            SetClipboardText(focus->invite_code);
            app->lobby_copy_feedback = true;
            app->lobby_copy_feedback_timer = 1.6f;
        }

        DrawRectangleRounded(room_box, 0.10f, 8, Fade(palette->panel, 0.95f));
        DrawRectangleRoundedLinesEx(room_box, 0.10f, 8, 1.0f, palette->panel_border);
        gui_draw_text("Room", (int)room_box.x + 14, (int)room_box.y + 8, 24, palette->text_primary);
        snprintf(members_line, sizeof(members_line), "Players: %d / 2", focus->connected ? 2 : 1);
        gui_draw_text(members_line, (int)room_box.x + 14, (int)room_box.y + 36, 22, palette->text_secondary);
        snprintf(opp_line,
                 sizeof(opp_line),
                 "Opponent: %s",
                 focus->opponent_name[0] != '\0' ? focus->opponent_name : "Waiting...");
        gui_draw_text(opp_line, (int)room_box.x + 14, (int)room_box.y + 62, 22, palette->text_secondary);

        if (!input_locked && gui_button(start_btn, "Start Game")) {
            if (!focus->connected) {
                snprintf(app->lobby_status, sizeof(app->lobby_status), "Need 2 players in room first.");
            } else if (!focus->peer_ready) {
                snprintf(app->lobby_status, sizeof(app->lobby_status), "Opponent must press Ready first.");
            } else if (!app_online_send_start(app, app->lobby_focus_match)) {
                app_show_network_error(app, "Online Error", network_last_error());
                snprintf(app->lobby_status, sizeof(app->lobby_status), "Could not send start packet.");
            } else {
                app_online_mark_started(app, app->lobby_focus_match);
                app_online_switch_to_match(app, app->lobby_focus_match, true);
                if (app->network_error_popup_open) {
                    draw_network_error_dialog(app);
                }
                return;
            }
        }

        if (!input_locked && gui_button(mode_btn, "Change Mode")) {
            app->lobby_view = LOBBY_VIEW_HOME;
            app->lobby_focus_match = -1;
            if (app->network_error_popup_open) {
                draw_network_error_dialog(app);
            }
            return;
        }

        draw_status_box(status_box, "Status", focused_status(app));
    }

    if (app->network_error_popup_open) {
        draw_network_error_dialog(app);
    }

    if (app->online_loading) {
        draw_online_loading_dialog(app);
    }
}
