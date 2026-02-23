#include "gui.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "game_state.h"

/* True when at least one legal move starts from square. */
static bool has_move_from(const ChessApp* app, int square) {
    for (int i = 0; i < app->legal_moves.count; ++i) {
        if (app->legal_moves.moves[i].from == square) {
            return true;
        }
    }
    return false;
}

/* True when square contains a piece belonging to side-to-move. */
static bool square_has_turn_piece(const ChessApp* app, int square) {
    Side side;
    return position_piece_at(&app->position, square, &side, NULL) && side == app->position.side_to_move;
}

/* Converts side enum value to text label. */
static const char* side_to_text(Side side) {
    return (side == SIDE_WHITE) ? "White" : "Black";
}

/* Plays piece-selection click with fallback to generic UI click. */
static void play_piece_select_sfx(void) {
    if (audio_is_loaded(AUDIO_SFX_PIECE_SELECT)) {
        audio_play(AUDIO_SFX_PIECE_SELECT);
    } else {
        audio_play(AUDIO_SFX_UI_CLICK);
    }
}

/* Draws single-line text clipped with ellipsis to prevent panel overlap. */
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

    buffer[len] = '\0';
    strncat(buffer, "...", sizeof(buffer) - strlen(buffer) - 1);
    gui_draw_text(buffer, x, y, font_size, color);
}

/* Draws wrapped text lines constrained to one rectangular width. */
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

/* Finds the legal move matching UI selection state. */
static bool find_selected_move(const ChessApp* app, int from, int to, uint8_t promotion_piece, Move* out_move) {
    for (int i = 0; i < app->legal_moves.count; ++i) {
        Move move = app->legal_moves.moves[i];

        if (move.from != from || move.to != to) {
            continue;
        }

        if ((move.flags & MOVE_FLAG_PROMOTION) != 0U) {
            uint8_t wanted = (promotion_piece == PIECE_NONE) ? PIECE_QUEEN : promotion_piece;
            if (move.promotion != wanted) {
                continue;
            }
        }

        *out_move = move;
        return true;
    }

    return false;
}

/* Draws scrollable move history panel and handles mouse-wheel scrolling. */
static bool g_move_log_thumb_dragging = false;
static float g_move_log_thumb_drag_offset = 0.0f;

static void draw_move_log_panel(ChessApp* app, Rectangle panel) {
    const GuiPalette* palette = gui_palette();
    Rectangle content = {panel.x + 10.0f, panel.y + 38.0f, panel.width - 20.0f, panel.height - 48.0f};
    Vector2 mouse = GetMousePosition();
    float wheel;
    int line_h = 22;
    int visible;
    int max_start;
    int start;
    int y;
    bool panel_hovered;
    bool has_scrollbar = false;
    Rectangle track = {0};
    Rectangle thumb = {0};
    float track_h = 0.0f;
    float thumb_h = 0.0f;
    float t = 0.0f;

    DrawRectangleRounded(panel, 0.08f, 8, Fade(palette->panel, 0.92f));
    DrawRectangleRoundedLinesEx(panel, 0.08f, 8, 1.0f, palette->panel_border);
    gui_draw_text("Moves", (int)panel.x + 12, (int)panel.y + 10, 22, palette->text_primary);

    visible = (int)(content.height / (float)line_h);
    if (visible < 1) {
        visible = 1;
    }

    max_start = app->move_log_count - visible;
    if (max_start < 0) {
        max_start = 0;
    }

    panel_hovered = CheckCollisionPointRec(mouse, panel);
    if (panel_hovered) {
        wheel = GetMouseWheelMove();
        if (wheel != 0.0f) {
            app->move_log_scroll -= (int)(wheel * 2.0f);
        }

        if (IsKeyPressed(KEY_PAGE_UP)) {
            app->move_log_scroll -= visible;
        }
        if (IsKeyPressed(KEY_PAGE_DOWN)) {
            app->move_log_scroll += visible;
        }
        if (IsKeyPressed(KEY_HOME)) {
            app->move_log_scroll = 0;
        }
        if (IsKeyPressed(KEY_END)) {
            app->move_log_scroll = max_start;
        }
    }

    if (app->move_log_scroll < 0) {
        app->move_log_scroll = 0;
    }
    if (app->move_log_scroll > max_start) {
        app->move_log_scroll = max_start;
    }

    if (app->move_log_count > visible) {
        has_scrollbar = true;
        track_h = content.height;
        thumb_h = track_h * ((float)visible / (float)app->move_log_count);
        if (thumb_h < 22.0f) {
            thumb_h = 22.0f;
        }

        t = (max_start > 0) ? ((float)app->move_log_scroll / (float)max_start) : 0.0f;
        thumb.y = content.y + (track_h - thumb_h) * t;
        track = (Rectangle){panel.x + panel.width - 10.0f, content.y, 4.0f, track_h};
        thumb = (Rectangle){panel.x + panel.width - 11.0f, thumb.y, 6.0f, thumb_h};

        if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
            if (CheckCollisionPointRec(mouse, thumb)) {
                g_move_log_thumb_dragging = true;
                g_move_log_thumb_drag_offset = mouse.y - thumb.y;
            } else if (CheckCollisionPointRec(mouse, track)) {
                float new_thumb_y;

                g_move_log_thumb_dragging = true;
                g_move_log_thumb_drag_offset = thumb_h * 0.5f;
                new_thumb_y = mouse.y - g_move_log_thumb_drag_offset;
                if (new_thumb_y < content.y) {
                    new_thumb_y = content.y;
                }
                if (new_thumb_y > content.y + track_h - thumb_h) {
                    new_thumb_y = content.y + track_h - thumb_h;
                }

                t = (track_h > thumb_h) ? ((new_thumb_y - content.y) / (track_h - thumb_h)) : 0.0f;
                app->move_log_scroll = (int)roundf(t * (float)max_start);
            }
        }

        if (g_move_log_thumb_dragging) {
            if (!IsMouseButtonDown(MOUSE_LEFT_BUTTON)) {
                g_move_log_thumb_dragging = false;
            } else {
                float new_thumb_y = mouse.y - g_move_log_thumb_drag_offset;

                if (new_thumb_y < content.y) {
                    new_thumb_y = content.y;
                }
                if (new_thumb_y > content.y + track_h - thumb_h) {
                    new_thumb_y = content.y + track_h - thumb_h;
                }

                t = (track_h > thumb_h) ? ((new_thumb_y - content.y) / (track_h - thumb_h)) : 0.0f;
                app->move_log_scroll = (int)roundf(t * (float)max_start);
            }
        }

        if (app->move_log_scroll < 0) {
            app->move_log_scroll = 0;
        }
        if (app->move_log_scroll > max_start) {
            app->move_log_scroll = max_start;
        }

        t = (max_start > 0) ? ((float)app->move_log_scroll / (float)max_start) : 0.0f;
        thumb.y = content.y + (track_h - thumb_h) * t;

        if (g_move_log_thumb_dragging) {
            SetMouseCursor(MOUSE_CURSOR_RESIZE_NS);
        } else if (CheckCollisionPointRec(mouse, thumb) || CheckCollisionPointRec(mouse, track)) {
            SetMouseCursor(MOUSE_CURSOR_POINTING_HAND);
        }
    } else {
        g_move_log_thumb_dragging = false;
    }

    start = app->move_log_scroll;
    y = (int)content.y;

    if (app->move_log_count == 0) {
        gui_draw_text("No moves yet.", (int)content.x, y + 6, 19, palette->text_secondary);
    } else {
        for (int i = 0; i < visible; ++i) {
            int index = start + i;
            if (index >= app->move_log_count) {
                break;
            }
            gui_draw_text(app->move_log[index], (int)content.x, y + i * line_h, 19, palette->text_primary);
        }
    }

    if (has_scrollbar) {
        DrawRectangleRounded(track, 0.4f, 6, Fade(palette->panel_border, 0.55f));
        DrawRectangleRounded(thumb, 0.4f, 6, palette->accent);
    }
}

/* Draws a blocking confirmation dialog when user attempts to leave a running game. */
static void draw_leave_confirm_dialog(ChessApp* app) {
    const GuiPalette* palette = gui_palette();
    bool online_mode = (app->mode == MODE_ONLINE && app->online_match_active);
    float sw = (float)GetScreenWidth();
    float sh = (float)GetScreenHeight();
    float panel_w = sw * 0.56f;
    float title_size;
    float body_size;
    float body_line_h;
    float body_block_h;
    float button_h;
    float button_gap;
    float actions_h;
    float panel_h;
    float content_x;
    float content_w;
    float text_y;
    float actions_y;
    bool stack_online_buttons;
    Rectangle panel;
    Rectangle stay_btn;
    Rectangle menu_btn;
    Rectangle leave_btn;

    if (panel_w < 360.0f) {
        panel_w = 360.0f;
    }
    if (panel_w > 720.0f) {
        panel_w = 720.0f;
    }
    if (panel_w > sw - 20.0f) {
        panel_w = sw - 20.0f;
    }

    title_size = (panel_w < 460.0f) ? 30.0f : 34.0f;
    body_size = (panel_w < 460.0f) ? 18.0f : 20.0f;
    body_line_h = body_size + 8.0f;
    body_block_h = online_mode ? (body_line_h * 4.0f) : (body_line_h * 3.0f);
    button_h = (panel_w < 460.0f) ? 42.0f : 46.0f;
    button_gap = (panel_w < 460.0f) ? 10.0f : 12.0f;
    stack_online_buttons = online_mode && panel_w < 640.0f;

    if (online_mode) {
        actions_h = stack_online_buttons ? (button_h * 3.0f + button_gap * 2.0f) : button_h;
    } else {
        actions_h = button_h;
    }

    panel_h = 24.0f + title_size + 14.0f + body_block_h + 20.0f + actions_h + 20.0f;
    if (panel_h < (online_mode ? 292.0f : 244.0f)) {
        panel_h = online_mode ? 292.0f : 244.0f;
    }
    if (panel_h > sh - 20.0f) {
        panel_h = sh - 20.0f;
    }

    panel = (Rectangle){
        sw * 0.5f - panel_w * 0.5f,
        sh * 0.5f - panel_h * 0.5f,
        panel_w,
        panel_h
    };

    DrawRectangle(0, 0, (int)sw, (int)sh, Fade(BLACK, 0.50f));
    DrawRectangleRounded(panel, 0.08f, 8, Fade(palette->panel, 0.98f));
    DrawRectangleRoundedLinesEx(panel, 0.08f, 8, 1.4f, palette->panel_border);

    content_x = panel.x + 20.0f;
    content_w = panel.width - 40.0f;
    text_y = panel.y + 24.0f + title_size + 14.0f;

    gui_draw_text("Leave Current Game?", (int)content_x, (int)panel.y + 24, (int)title_size, palette->text_primary);

    if (online_mode) {
        int lines_used = 0;
        lines_used += draw_text_wrap("Menu (Keep Match): keep match in background.",
                                     (int)content_x,
                                     (int)text_y,
                                     (int)body_size,
                                     (int)content_w,
                                     (int)body_line_h,
                                     3,
                                     palette->text_secondary);
        lines_used += draw_text_wrap("Resume later from Active Games.",
                                     (int)content_x,
                                     (int)text_y + lines_used * (int)body_line_h,
                                     (int)body_size,
                                     (int)content_w,
                                     (int)body_line_h,
                                     2,
                                     palette->text_secondary);
        draw_text_wrap("Leave Match: notify opponent and end this match.",
                       (int)content_x,
                       (int)text_y + lines_used * (int)body_line_h,
                       (int)body_size,
                       (int)content_w,
                       (int)body_line_h,
                       3,
                       palette->text_secondary);
    } else {
        int lines_used = 0;
        lines_used += draw_text_wrap("If you leave now, this match will be closed.",
                                     (int)content_x,
                                     (int)text_y,
                                     (int)body_size,
                                     (int)content_w,
                                     (int)body_line_h,
                                     3,
                                     palette->text_secondary);
        draw_text_wrap("You can start a new game from the main menu.",
                       (int)content_x,
                       (int)text_y + lines_used * (int)body_line_h,
                       (int)body_size,
                       (int)content_w,
                       (int)body_line_h,
                       3,
                       palette->text_secondary);
    }

    actions_y = panel.y + panel.height - 20.0f - actions_h;

    if (online_mode && stack_online_buttons) {
        stay_btn = (Rectangle){content_x, actions_y, content_w, button_h};
        menu_btn = (Rectangle){content_x, actions_y + button_h + button_gap, content_w, button_h};
        leave_btn = (Rectangle){content_x, actions_y + (button_h + button_gap) * 2.0f, content_w, button_h};
    } else if (online_mode) {
        float available = content_w - button_gap * 2.0f;
        float stay_w = available * 0.22f;
        float leave_w = available * 0.26f;
        float menu_w = available - stay_w - leave_w;

        stay_btn = (Rectangle){content_x, actions_y, stay_w, button_h};
        menu_btn = (Rectangle){stay_btn.x + stay_btn.width + button_gap, actions_y, menu_w, button_h};
        leave_btn = (Rectangle){menu_btn.x + menu_btn.width + button_gap, actions_y, leave_w, button_h};
    } else {
        float each = (content_w - button_gap) * 0.5f;
        stay_btn = (Rectangle){content_x, actions_y, each, button_h};
        leave_btn = (Rectangle){stay_btn.x + stay_btn.width + button_gap, actions_y, each, button_h};
    }

    if (gui_button(stay_btn, "Stay")) {
        app->leave_confirm_open = false;
    }

    if (online_mode) {
        if (gui_button_submit(menu_btn, "Menu (Keep Match)", true)) {
            app_online_store_current_match(app);
            app->screen = SCREEN_MENU;
            app->leave_confirm_open = false;
            app->has_selection = false;
            app->selected_square = -1;
            app->move_animating = false;
            snprintf(app->online_runtime_status,
                     sizeof(app->online_runtime_status),
                     "Match running in background. Resume any time.");
        }

        if (gui_button(leave_btn, "Leave Match")) {
            app_online_end_match(app, true);
            app->screen = SCREEN_MENU;
            app->leave_confirm_open = false;
            app->has_selection = false;
            app->selected_square = -1;
            app->move_animating = false;
        }
    } else {
        if (gui_button_submit(leave_btn, "Leave", true)) {
            app->screen = SCREEN_MENU;
            app->leave_confirm_open = false;
            app->has_selection = false;
            app->selected_square = -1;
            app->ai_thinking = false;
            app->move_animating = false;
        }
    }
}

/* Shows modal notification when opponent leaves an online match. */
static void draw_online_leave_notice_dialog(ChessApp* app) {
    const GuiPalette* palette = gui_palette();
    float sw = (float)GetScreenWidth();
    float sh = (float)GetScreenHeight();
    float panel_w = sw * 0.44f;
    float panel_h = 236.0f;
    Rectangle panel;
    Rectangle ok_btn;
    int text_x;
    int text_w;
    int text_y;
    int match_index;

    if (panel_w < 420.0f) {
        panel_w = 420.0f;
    }
    if (panel_w > 660.0f) {
        panel_w = 660.0f;
    }

    panel = (Rectangle){
        sw * 0.5f - panel_w * 0.5f,
        sh * 0.5f - panel_h * 0.5f,
        panel_w,
        panel_h
    };
    ok_btn = (Rectangle){panel.x + panel.width - 152.0f, panel.y + panel.height - 60.0f, 128.0f, 40.0f};
    text_x = (int)panel.x + 24;
    text_w = (int)panel.width - 48;
    text_y = (int)panel.y + 74;

    DrawRectangle(0, 0, (int)sw, (int)sh, Fade(BLACK, 0.52f));
    DrawRectangleRounded(panel, 0.08f, 8, Fade(palette->panel, 0.98f));
    DrawRectangleRoundedLinesEx(panel, 0.08f, 8, 1.4f, palette->panel_border);

    draw_text_fit(app->online_leave_notice_title[0] != '\0' ? app->online_leave_notice_title : "Match Ended",
                  (int)panel.x + 24,
                  (int)panel.y + 22,
                  34,
                  text_w,
                  palette->text_primary);
    draw_text_wrap(app->online_leave_notice_text[0] != '\0'
                       ? app->online_leave_notice_text
                       : "Your opponent left the match. Press OK to return to menu.",
                   text_x,
                   text_y,
                   20,
                   text_w,
                   24,
                   4,
                   palette->text_secondary);

    if (gui_button_submit(ok_btn, "OK", true)) {
        match_index = app->online_leave_notice_match;
        app->online_leave_notice_open = false;
        app->online_leave_notice_match = -1;
        app->online_leave_notice_title[0] = '\0';
        app->online_leave_notice_text[0] = '\0';

        if (match_index >= 0) {
            app_online_close_match(app, match_index, false);
        } else {
            app->screen = SCREEN_MENU;
        }
    }
}

void gui_screen_play(struct ChessApp* app) {
    const GuiPalette* palette = gui_palette();
    OnlineMatch* current_match = app_online_get(app, app->current_online_match);
    bool online_connected = (current_match != NULL) && current_match->network.connected;
    bool online_active = (current_match != NULL) && current_match->in_game;
    GuiPlayLayout layout = gui_get_play_layout();
    float capture_height = layout.sidebar.height * 0.26f;
    float top_capture_bottom = layout.sidebar.y + 70.0f + capture_height;
    float bottom_capture_top = layout.sidebar.y + layout.sidebar.height - capture_height - 14.0f;
    float middle_y = top_capture_bottom + 14.0f;
    float middle_h = bottom_capture_top - middle_y - 10.0f;
    Rectangle back_btn = {
        layout.sidebar.x + 12.0f,
        layout.sidebar.y + 12.0f,
        layout.sidebar.width - 24.0f,
        48.0f
    };
    Rectangle middle = {
        layout.sidebar.x + 12.0f,
        middle_y,
        layout.sidebar.width - 24.0f,
        middle_h
    };

    if (capture_height < 138.0f) {
        capture_height = 138.0f;
    }
    if (capture_height > 210.0f) {
        capture_height = 210.0f;
    }
    top_capture_bottom = layout.sidebar.y + 70.0f + capture_height;
    bottom_capture_top = layout.sidebar.y + layout.sidebar.height - capture_height - 14.0f;
    middle_y = top_capture_bottom + 14.0f;
    middle_h = bottom_capture_top - middle_y - 10.0f;
    if (middle_h < 124.0f) {
        middle_h = 124.0f;
    }
    middle.y = middle_y;
    middle.height = middle_h;

    gui_draw_board(app);

    if (gui_button(back_btn, "Menu")) {
        app->leave_confirm_open = true;
    }

    DrawRectangleRounded(middle, 0.09f, 8, Fade(palette->panel_alt, 0.95f));
    DrawRectangleRoundedLinesEx(middle, 0.09f, 8, 1.0f, palette->panel_border);

    {
        int y = (int)middle.y + 14;
        int content_x = (int)middle.x + 12;
        int content_w = (int)middle.width - 24;
        int title_size = 24;
        int line_size = 21;
        int sub_size = 20;
        int status_size = 22;
        int tiny_size = 18;
        bool is_check = engine_in_check(&app->position, app->position.side_to_move);
        bool is_timeout = app->game_over && app->timeout_game_over;
        bool is_mate = app->game_over && !is_timeout && is_check;
        int info_limit_y;
        int info_end_y;
        char line[128];

        if (middle.height < 280.0f) {
            title_size = 22;
            line_size = 19;
            sub_size = 18;
            status_size = 20;
            tiny_size = 17;
        }
        if (middle.height < 220.0f) {
            title_size = 20;
            line_size = 17;
            sub_size = 16;
            status_size = 18;
            tiny_size = 15;
        }
        if (middle.height < 180.0f) {
            title_size = 18;
            line_size = 16;
            sub_size = 15;
            status_size = 16;
            tiny_size = 14;
        }

        info_limit_y = (int)(middle.y + middle.height * ((is_mate || is_timeout) ? 0.76f : 0.68f));
        if (info_limit_y > (int)(middle.y + middle.height - 86.0f)) {
            info_limit_y = (int)(middle.y + middle.height - 86.0f);
        }

        snprintf(line, sizeof(line), "Turn: %s", side_to_text(app->position.side_to_move));
        draw_text_fit(line, content_x, y, title_size, content_w, palette->text_primary);
        y += title_size + 10;

        if (app->mode == MODE_SINGLE) {
            draw_text_fit("Mode: Single Player", content_x, y, line_size, content_w, palette->text_secondary);
        } else if (app->mode == MODE_LOCAL) {
            draw_text_fit("Mode: Local 2 Player", content_x, y, line_size, content_w, palette->text_secondary);
        } else {
            draw_text_fit("Mode: Online", content_x, y, line_size, content_w, palette->text_secondary);
        }
        y += line_size + 9;

        if (app->mode == MODE_ONLINE) {
            draw_text_fit(app->online_runtime_status, content_x, y, sub_size, content_w, palette->text_secondary);
            y += sub_size + 8;
        } else if (app->mode == MODE_SINGLE) {
            char ai_line[96];
            snprintf(ai_line, sizeof(ai_line), "AI Difficulty: %d%%", app->ai_difficulty);
            draw_text_fit(ai_line, content_x, y, sub_size, content_w, palette->text_secondary);
            y += sub_size + 8;
        }

        if (app->turn_timer_enabled && app->turn_time_seconds >= 10) {
            int remaining = (int)ceilf(app->turn_time_remaining);
            Color timer_color = palette->text_secondary;

            if (remaining < 0) {
                remaining = 0;
            }
            if (!app->game_over && remaining <= 10) {
                timer_color = (Color){198, 39, 45, 255};
            }

            snprintf(line, sizeof(line), "Turn Time: %02d:%02d", remaining / 60, remaining % 60);
            draw_text_fit(line, content_x, y, sub_size, content_w, timer_color);
            y += sub_size + 8;
        }

        if (is_check && !app->game_over) {
            const char* check_text = (content_w < 290 || status_size <= 17)
                                         ? "Check!"
                                         : "Check! King is under attack.";
            int check_size = status_size + ((status_size >= 18) ? 1 : 0);

            if (y + check_size + 6 > info_limit_y) {
                check_size = tiny_size;
            }

            if (y + check_size + 6 <= (int)(middle.y + middle.height - 28.0f)) {
                draw_text_fit(check_text,
                              content_x,
                              y,
                              check_size,
                              content_w,
                              (Color){198, 39, 45, 255});
                y += check_size + 8;
            }
        }

        if (app->mode == MODE_SINGLE && app->ai_thinking) {
            draw_text_fit("AI is thinking...", content_x, y, status_size, content_w, palette->accent);
            y += status_size + 8;
        } else if (app->mode == MODE_ONLINE && online_active && online_connected && !app_is_human_turn(app)) {
            draw_text_fit("Waiting for opponent...", content_x, y, status_size, content_w, palette->accent);
            y += status_size + 8;
        } else if (app->mode == MODE_ONLINE && !online_connected) {
            draw_text_fit("Opponent disconnected.",
                          content_x,
                          y,
                          status_size,
                          content_w,
                          (Color){176, 78, 29, 255});
            y += status_size + 8;
        }

        if (app->mode == MODE_SINGLE && app->last_ai_result.depth_reached > 0 && y + tiny_size + 8 < info_limit_y) {
            char info[128];
            snprintf(info,
                     sizeof(info),
                     "Last AI: depth %d | score %d | nodes %llu",
                     app->last_ai_result.depth_reached,
                     app->last_ai_result.score,
                     (unsigned long long)app->last_ai_result.nodes);
            draw_text_fit(info, content_x, y, tiny_size, content_w, palette->text_secondary);
            y += tiny_size + 8;
        }

        if (is_timeout && y + status_size + 10 < info_limit_y) {
            Side loser = app->timeout_loser;
            Side winner = (loser == SIDE_WHITE) ? SIDE_BLACK : SIDE_WHITE;
            const char* title = "TIME OUT!";
            int big_size = (middle.height < 240.0f) ? 30 : 36;
            int sub_win_size = big_size - 10;
            int title_w = gui_measure_text(title, big_size);
            int title_x = content_x + (content_w - title_w) / 2;
            int sub_y;

            gui_draw_text(title, title_x, y + 4, big_size, (Color){191, 34, 46, 255});
            sub_y = y + big_size + 10;
            snprintf(line, sizeof(line), "%s wins on time", side_to_text(winner));
            {
                int sub_w = gui_measure_text(line, sub_win_size);
                int sub_x = content_x + (content_w - sub_w) / 2;
                gui_draw_text(line, sub_x, sub_y, sub_win_size, (Color){219, 60, 70, 255});
            }
            y = sub_y + sub_win_size + 8;
        } else if (is_mate && y + status_size + 10 < info_limit_y) {
            Side loser = app->position.side_to_move;
            Side winner = (loser == SIDE_WHITE) ? SIDE_BLACK : SIDE_WHITE;
            const char* title = "CHECKMATE!";
            int big_size = (middle.height < 240.0f) ? 30 : 36;
            int sub_win_size = big_size - 10;
            int title_w = gui_measure_text(title, big_size);
            int title_x = content_x + (content_w - title_w) / 2;
            int sub_y;

            gui_draw_text(title, title_x, y + 4, big_size, (Color){191, 34, 46, 255});
            sub_y = y + big_size + 10;
            snprintf(line, sizeof(line), "%s wins", side_to_text(winner));
            {
                int sub_w = gui_measure_text(line, sub_win_size);
                int sub_x = content_x + (content_w - sub_w) / 2;
                gui_draw_text(line, sub_x, sub_y, sub_win_size, (Color){219, 60, 70, 255});
            }
            y = sub_y + sub_win_size + 8;
        } else if (app->game_over && y + status_size + 10 < info_limit_y) {
            draw_text_fit("Draw (stalemate).", content_x, y + 4, status_size + 2, content_w, palette->text_primary);
            y += status_size + 8;
        }

        info_end_y = y;

        {
            float min_log_h = middle.height * 0.30f;
            float log_y = (float)info_end_y + 8.0f;
            float log_bottom = middle.y + middle.height - 8.0f;
            Rectangle log_panel;

            if (min_log_h < 82.0f) {
                min_log_h = 82.0f;
            }
            if (min_log_h > 170.0f) {
                min_log_h = 170.0f;
            }

            if (log_y + min_log_h > log_bottom) {
                log_y = log_bottom - min_log_h;
            }

            if (log_y < (float)info_end_y + 6.0f) {
                log_y = (float)info_end_y + 6.0f;
            }

            if (log_y < middle.y + 8.0f) {
                log_y = middle.y + 8.0f;
            }

            log_panel = (Rectangle){
                middle.x + 10.0f,
                log_y,
                middle.width - 20.0f,
                log_bottom - log_y
            };

            if (log_panel.height >= 52.0f) {
                draw_move_log_panel(app, log_panel);
            }
        }
    }

    if (app->leave_confirm_open) {
        draw_leave_confirm_dialog(app);
        return;
    }

    if (app->online_leave_notice_open) {
        draw_online_leave_notice_dialog(app);
        return;
    }

    if (app->game_over) {
        return;
    }

    {
        bool online_input_ok = (app->mode != MODE_ONLINE) || (online_active && online_connected);
        bool input_allowed = app_is_human_turn(app) &&
                             !app->move_animating &&
                             !gui_board_is_rotating() &&
                             online_input_ok &&
                             !(app->mode == MODE_SINGLE && app->ai_thinking);

        if (!input_allowed) {
            return;
        }
    }

    if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
        int square = gui_square_from_mouse(GetMousePosition());

        if (square < 0) {
            return;
        }

        if (!app->has_selection) {
            if (square_has_turn_piece(app, square) && has_move_from(app, square)) {
                app->has_selection = true;
                app->selected_square = square;
                play_piece_select_sfx();
            }
            return;
        }

        {
            int from = app->selected_square;
            int to = square;

            if (from == to) {
                if (!app->touch_move_enabled) {
                    app->has_selection = false;
                    app->selected_square = -1;
                }
                return;
            }

            {
                Move selected_move;
                if (find_selected_move(app, from, to, PIECE_QUEEN, &selected_move) &&
                    app_apply_move(app, selected_move)) {
                    app->has_selection = false;
                    app->selected_square = -1;

                    if (app->mode == MODE_ONLINE && current_match != NULL && current_match->network.connected) {
                        network_client_send_move(&current_match->network, selected_move);
                    }
                } else if (square_has_turn_piece(app, square) && has_move_from(app, square)) {
                    if (!app->touch_move_enabled) {
                        app->selected_square = square;
                        play_piece_select_sfx();
                    }
                } else {
                    if (!app->touch_move_enabled) {
                        app->has_selection = false;
                        app->selected_square = -1;
                    }
                }
            }
        }
    }
}
