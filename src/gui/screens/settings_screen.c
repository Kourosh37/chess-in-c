#include "gui.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "audio.h"
#include "game_state.h"

/* Draws single-line text clipped with ellipsis to avoid control overlap. */
static void draw_text_fit(const char* text,
                          int x,
                          int y,
                          int font_size,
                          int max_width,
                          Color color) {
    char buffer[128];
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

/* Draws one slider row with label/value text and draggable bar. */
static bool draw_slider_row(const char* label,
                            const char* value_text,
                            Rectangle row,
                            float* value,
                            float min_value,
                            float max_value) {
    const GuiPalette* palette = gui_palette();
    float pad_x = 16.0f;
    int label_size = (row.height >= 82.0f) ? 23 : 21;
    int value_size = label_size;
    int label_y = (int)row.y + 10;
    int value_w = gui_measure_text(value_text, value_size);
    int value_x = (int)(row.x + row.width - pad_x - (float)value_w);
    Rectangle slider = {
        row.x + pad_x,
        row.y + row.height - 28.0f,
        row.width - pad_x * 2.0f,
        20.0f
    };

    DrawRectangleRounded(row, 0.10f, 8, Fade(palette->panel, 0.92f));
    DrawRectangleRoundedLinesEx(row, 0.10f, 8, 1.0f, palette->panel_border);

    gui_draw_text(label, (int)row.x + (int)pad_x, label_y, label_size, palette->text_primary);
    gui_draw_text(value_text, value_x, label_y, value_size, palette->accent);

    return gui_slider_float(slider, value, min_value, max_value);
}

static int g_missing_audio_scroll = 0;
static bool g_missing_audio_thumb_dragging = false;
static float g_missing_audio_thumb_drag_offset = 0.0f;

/* Draws scrollable panel with missing audio filenames. */
static void draw_missing_audio_panel(Rectangle panel, const char* const* entries, int entry_count) {
    const GuiPalette* palette = gui_palette();
    Vector2 mouse = GetMousePosition();
    Rectangle content;
    int line_h = 21;
    int visible;
    int max_start;
    int start;
    bool has_scrollbar = false;
    bool hovered;
    Rectangle track = {0};
    Rectangle thumb = {0};
    float track_h = 0.0f;
    float thumb_h = 0.0f;
    float t = 0.0f;
    int text_w;

    DrawRectangleRounded(panel, 0.09f, 8, Fade(palette->panel, 0.92f));
    DrawRectangleRoundedLinesEx(panel, 0.09f, 8, 1.0f, palette->panel_border);
    gui_draw_text("Missing Audio Files", (int)panel.x + 12, (int)panel.y + 8, 20, palette->text_primary);

    content = (Rectangle){panel.x + 10.0f, panel.y + 34.0f, panel.width - 20.0f, panel.height - 42.0f};
    if (content.height < 20.0f || content.width < 40.0f) {
        g_missing_audio_thumb_dragging = false;
        return;
    }

    visible = (int)(content.height / (float)line_h);
    if (visible < 1) {
        visible = 1;
    }

    max_start = entry_count - visible;
    if (max_start < 0) {
        max_start = 0;
    }

    hovered = CheckCollisionPointRec(mouse, panel);
    if (hovered) {
        float wheel = GetMouseWheelMove();
        if (wheel != 0.0f) {
            g_missing_audio_scroll -= (int)(wheel * 2.0f);
        }
    }

    if (g_missing_audio_scroll < 0) {
        g_missing_audio_scroll = 0;
    }
    if (g_missing_audio_scroll > max_start) {
        g_missing_audio_scroll = max_start;
    }

    if (entry_count > visible) {
        has_scrollbar = true;
        track_h = content.height;
        thumb_h = track_h * ((float)visible / (float)entry_count);
        if (thumb_h < 22.0f) {
            thumb_h = 22.0f;
        }

        t = (max_start > 0) ? ((float)g_missing_audio_scroll / (float)max_start) : 0.0f;
        thumb.y = content.y + (track_h - thumb_h) * t;
        track = (Rectangle){content.x + content.width - 5.0f, content.y, 4.0f, track_h};
        thumb = (Rectangle){content.x + content.width - 6.0f, thumb.y, 6.0f, thumb_h};

        if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
            if (CheckCollisionPointRec(mouse, thumb)) {
                g_missing_audio_thumb_dragging = true;
                g_missing_audio_thumb_drag_offset = mouse.y - thumb.y;
            } else if (CheckCollisionPointRec(mouse, track)) {
                float new_thumb_y;
                g_missing_audio_thumb_dragging = true;
                g_missing_audio_thumb_drag_offset = thumb_h * 0.5f;
                new_thumb_y = mouse.y - g_missing_audio_thumb_drag_offset;
                if (new_thumb_y < content.y) {
                    new_thumb_y = content.y;
                }
                if (new_thumb_y > content.y + track_h - thumb_h) {
                    new_thumb_y = content.y + track_h - thumb_h;
                }
                t = (track_h > thumb_h) ? ((new_thumb_y - content.y) / (track_h - thumb_h)) : 0.0f;
                g_missing_audio_scroll = (int)lroundf(t * (float)max_start);
            }
        }

        if (g_missing_audio_thumb_dragging) {
            if (!IsMouseButtonDown(MOUSE_LEFT_BUTTON)) {
                g_missing_audio_thumb_dragging = false;
            } else {
                float new_thumb_y = mouse.y - g_missing_audio_thumb_drag_offset;
                if (new_thumb_y < content.y) {
                    new_thumb_y = content.y;
                }
                if (new_thumb_y > content.y + track_h - thumb_h) {
                    new_thumb_y = content.y + track_h - thumb_h;
                }
                t = (track_h > thumb_h) ? ((new_thumb_y - content.y) / (track_h - thumb_h)) : 0.0f;
                g_missing_audio_scroll = (int)lroundf(t * (float)max_start);
            }
        }

        if (g_missing_audio_scroll < 0) {
            g_missing_audio_scroll = 0;
        }
        if (g_missing_audio_scroll > max_start) {
            g_missing_audio_scroll = max_start;
        }

        t = (max_start > 0) ? ((float)g_missing_audio_scroll / (float)max_start) : 0.0f;
        thumb.y = content.y + (track_h - thumb_h) * t;

        if (g_missing_audio_thumb_dragging) {
            SetMouseCursor(MOUSE_CURSOR_RESIZE_NS);
        } else if (CheckCollisionPointRec(mouse, thumb) || CheckCollisionPointRec(mouse, track)) {
            SetMouseCursor(MOUSE_CURSOR_POINTING_HAND);
        }
    } else {
        g_missing_audio_thumb_dragging = false;
    }

    start = g_missing_audio_scroll;
    text_w = (int)content.width - (has_scrollbar ? 16 : 2);
    if (text_w < 10) {
        text_w = 10;
    }

    if (entry_count == 0) {
        draw_text_fit("All audio files are available.",
                      (int)content.x,
                      (int)content.y + 4,
                      18,
                      text_w,
                      palette->text_secondary);
    } else {
        for (int i = 0; i < visible; ++i) {
            int idx = start + i;
            if (idx >= entry_count) {
                break;
            }
            draw_text_fit(entries[idx],
                          (int)content.x,
                          (int)(content.y + (float)(i * line_h)),
                          18,
                          text_w,
                          palette->text_secondary);
        }
    }

    if (has_scrollbar) {
        DrawRectangleRounded(track, 0.4f, 6, Fade(palette->panel_border, 0.55f));
        DrawRectangleRounded(thumb, 0.4f, 6, palette->accent);
    }
}

void gui_screen_settings(struct ChessApp* app) {
    const GuiPalette* palette = gui_palette();
    float sw = (float)GetScreenWidth();
    float sh = (float)GetScreenHeight();
    float panel_w = sw * 0.82f;
    float panel_h = sh * 0.82f;
    float outer_pad = 28.0f;
    float card_gap = 16.0f;
    float header_h = 104.0f;
    float card_bottom_pad = 24.0f;
    float card_inner = 16.0f;
    float row_h = 72.0f;
    float row_gap = 10.0f;
    float cards_h;
    float left_w;
    float right_w;
    float left_rows_y;
    float right_rows_y;
    Rectangle panel;
    Rectangle left_card;
    Rectangle right_card;
    Rectangle back_btn;
    Rectangle difficulty_row;
    Rectangle theme_row;
    Rectangle touch_move_row;
    Rectangle timer_row;
    Rectangle online_name_row;
    Rectangle toggle_btn;
    Rectangle sfx_row;
    Rectangle menu_music_row;
    Rectangle game_music_row;
    bool dirty = false;

    if (panel_w < 900.0f) {
        panel_w = 900.0f;
    }
    if (panel_w > 1160.0f) {
        panel_w = 1160.0f;
    }
    if (panel_h < 600.0f) {
        panel_h = 600.0f;
    }
    if (panel_h > 760.0f) {
        panel_h = 760.0f;
    }

    panel = (Rectangle){
        sw * 0.5f - panel_w * 0.5f,
        sh * 0.5f - panel_h * 0.5f,
        panel_w,
        panel_h
    };

    cards_h = panel.height - header_h - card_bottom_pad;
    left_w = (panel.width - outer_pad * 2.0f - card_gap) * 0.54f;
    right_w = panel.width - outer_pad * 2.0f - card_gap - left_w;

    left_card = (Rectangle){panel.x + outer_pad, panel.y + header_h, left_w, cards_h};
    right_card = (Rectangle){left_card.x + left_card.width + card_gap, panel.y + header_h, right_w, cards_h};

    back_btn = (Rectangle){
        panel.x + panel.width - outer_pad - 152.0f,
        panel.y + 24.0f,
        152.0f,
        52.0f
    };

    DrawRectangleRounded((Rectangle){panel.x + 6.0f, panel.y + 7.0f, panel.width, panel.height},
                         0.07f,
                         8,
                         Fade(BLACK, 0.16f));
    DrawRectangleRounded(panel, 0.07f, 8, Fade(palette->panel, 0.95f));
    DrawRectangleRoundedLinesEx(panel, 0.07f, 8, 1.4f, palette->panel_border);

    gui_draw_text("Settings", (int)panel.x + 30, (int)panel.y + 24, 46, palette->text_primary);

    if (gui_button(back_btn, "Back")) {
        app->screen = SCREEN_MENU;
    }

    DrawRectangleRounded(left_card, 0.08f, 8, Fade(palette->panel_alt, 0.95f));
    DrawRectangleRoundedLinesEx(left_card, 0.08f, 8, 1.0f, palette->panel_border);
    gui_draw_text("Game", (int)left_card.x + 16, (int)left_card.y + 12, 32, palette->text_primary);

    left_rows_y = left_card.y + 58.0f;
    difficulty_row = (Rectangle){left_card.x + card_inner, left_rows_y, left_card.width - card_inner * 2.0f, row_h};
    theme_row = (Rectangle){
        left_card.x + card_inner,
        left_rows_y + row_h + row_gap,
        left_card.width - card_inner * 2.0f,
        row_h
    };
    touch_move_row = (Rectangle){
        left_card.x + card_inner,
        theme_row.y + row_h + row_gap,
        left_card.width - card_inner * 2.0f,
        row_h
    };
    timer_row = (Rectangle){
        left_card.x + card_inner,
        touch_move_row.y + row_h + row_gap,
        left_card.width - card_inner * 2.0f,
        row_h
    };
    online_name_row = (Rectangle){
        left_card.x + card_inner,
        timer_row.y + row_h + row_gap,
        left_card.width - card_inner * 2.0f,
        row_h
    };

    {
        char value[32];
        float ai_value = (float)app->ai_difficulty;
        int theme_count = gui_theme_count();
        float pad_x = 16.0f;
        float pad_y = 9.0f;
        float btn_w = 56.0f;
        float btn_gap = 8.0f;
        Rectangle next_btn = {
            theme_row.x + theme_row.width - pad_x - btn_w,
            theme_row.y + pad_y,
            btn_w,
            theme_row.height - pad_y * 2.0f
        };
        Rectangle prev_btn = {
            next_btn.x - btn_gap - btn_w,
            theme_row.y + pad_y,
            btn_w,
            theme_row.height - pad_y * 2.0f
        };
        const char* theme_name = gui_theme_name(app->theme);
        int font_size = 24;
        int label_w = gui_measure_text("Theme", font_size);
        int theme_min_x = (int)(theme_row.x + 16.0f + (float)label_w + 24.0f);
        int theme_max_w = (int)(prev_btn.x - 14.0f - (float)theme_min_x);
        int theme_value_w;
        int theme_value_x;
        int text_y;
        int text_h;

        snprintf(value, sizeof(value), "%d%%", app->ai_difficulty);
        if (draw_slider_row("AI Difficulty", value, difficulty_row, &ai_value, 0.0f, 100.0f)) {
            int rounded = (int)lroundf(ai_value);
            app_set_ai_difficulty(app, rounded);
            dirty = true;
        }

        if (theme_max_w < 50) {
            theme_max_w = 50;
        }

        while (font_size > 18 && gui_measure_text(theme_name, font_size) > theme_max_w) {
            font_size--;
        }
        text_h = gui_measure_text_height(font_size);

        theme_value_w = gui_measure_text(theme_name, font_size);
        theme_value_x = (int)(prev_btn.x - 14.0f - (float)theme_value_w);
        if (theme_value_x < theme_min_x) {
            theme_value_x = theme_min_x;
        }
        text_y = (int)(theme_row.y + (theme_row.height - (float)text_h) * 0.5f - 1.0f);

        DrawRectangleRounded(theme_row, 0.10f, 8, Fade(palette->panel, 0.92f));
        DrawRectangleRoundedLinesEx(theme_row, 0.10f, 8, 1.0f, palette->panel_border);
        gui_draw_text("Theme", (int)theme_row.x + 16, text_y, font_size, palette->text_primary);
        draw_text_fit(theme_name, theme_value_x, text_y, font_size, theme_max_w, palette->accent);

        if (gui_button(prev_btn, "<")) {
            int next = ((int)app->theme - 1 + theme_count) % theme_count;
            app->theme = (ColorTheme)next;
            gui_set_active_theme(next);
            dirty = true;
        }
        if (gui_button(next_btn, ">")) {
            int next = ((int)app->theme + 1) % theme_count;
            app->theme = (ColorTheme)next;
            gui_set_active_theme(next);
            dirty = true;
        }

        {
            Rectangle touch_btn = {
                touch_move_row.x + touch_move_row.width - 132.0f,
                touch_move_row.y + 10.0f,
                116.0f,
                touch_move_row.height - 20.0f
            };
            const char* touch_value = app->touch_move_enabled ? "On" : "Off";
            int hint_size = 16;
            int hint_y = (int)touch_move_row.y + (int)touch_move_row.height - 18;

            DrawRectangleRounded(touch_move_row, 0.10f, 8, Fade(palette->panel, 0.92f));
            DrawRectangleRoundedLinesEx(touch_move_row, 0.10f, 8, 1.0f, palette->panel_border);
            gui_draw_text("Touch-Move Rule",
                          (int)touch_move_row.x + 16,
                          (int)touch_move_row.y + 10,
                          21,
                          palette->text_primary);
            draw_text_fit("Selected piece must be moved.",
                          (int)touch_move_row.x + 16,
                          hint_y,
                          hint_size,
                          (int)touch_move_row.width - 164,
                          palette->text_secondary);

            if (gui_button(touch_btn, touch_value)) {
                app->touch_move_enabled = !app->touch_move_enabled;
                dirty = true;
            }
        }

        {
            char timer_text[32];
            Rectangle buttons_area;
            Rectangle off_btn;
            Rectangle b10;
            Rectangle b30;
            Rectangle b60;
            Rectangle b120;
            float pad_x = 16.0f;
            float gap = 6.0f;
            float btn_h = 28.0f;
            float btn_w;

            if (app->turn_timer_enabled && app->turn_time_seconds >= 10) {
                snprintf(timer_text, sizeof(timer_text), "%ds", app->turn_time_seconds);
            } else {
                snprintf(timer_text, sizeof(timer_text), "Off");
            }

            DrawRectangleRounded(timer_row, 0.10f, 8, Fade(palette->panel, 0.92f));
            DrawRectangleRoundedLinesEx(timer_row, 0.10f, 8, 1.0f, palette->panel_border);
            gui_draw_text("Turn Timer", (int)timer_row.x + 16, (int)timer_row.y + 8, 21, palette->text_primary);
            gui_draw_text(timer_text,
                          (int)(timer_row.x + timer_row.width - 16.0f - (float)gui_measure_text(timer_text, 20)),
                          (int)timer_row.y + 8,
                          20,
                          palette->accent);

            buttons_area = (Rectangle){
                timer_row.x + pad_x,
                timer_row.y + timer_row.height - btn_h - 8.0f,
                timer_row.width - pad_x * 2.0f,
                btn_h
            };
            btn_w = (buttons_area.width - gap * 4.0f) / 5.0f;
            off_btn = (Rectangle){buttons_area.x, buttons_area.y, btn_w, btn_h};
            b10 = (Rectangle){off_btn.x + btn_w + gap, buttons_area.y, btn_w, btn_h};
            b30 = (Rectangle){b10.x + btn_w + gap, buttons_area.y, btn_w, btn_h};
            b60 = (Rectangle){b30.x + btn_w + gap, buttons_area.y, btn_w, btn_h};
            b120 = (Rectangle){b60.x + btn_w + gap, buttons_area.y, btn_w, btn_h};

            if (gui_button(off_btn, "Off")) {
                app->turn_timer_enabled = false;
                app->turn_time_seconds = 0;
                app->turn_time_remaining = 0.0f;
                dirty = true;
            }
            if (gui_button(b10, "10s")) {
                app->turn_timer_enabled = true;
                app->turn_time_seconds = 10;
                app->turn_time_remaining = 10.0f;
                dirty = true;
            }
            if (gui_button(b30, "30s")) {
                app->turn_timer_enabled = true;
                app->turn_time_seconds = 30;
                app->turn_time_remaining = 30.0f;
                dirty = true;
            }
            if (gui_button(b60, "60s")) {
                app->turn_timer_enabled = true;
                app->turn_time_seconds = 60;
                app->turn_time_remaining = 60.0f;
                dirty = true;
            }
            if (gui_button(b120, "120s")) {
                app->turn_timer_enabled = true;
                app->turn_time_seconds = 120;
                app->turn_time_remaining = 120.0f;
                dirty = true;
            }
        }

        {
            Rectangle name_input = {
                online_name_row.x + 14.0f,
                online_name_row.y + 30.0f,
                online_name_row.width - 28.0f,
                online_name_row.height - 38.0f
            };
            char before_name[PLAYER_NAME_MAX + 1];

            DrawRectangleRounded(online_name_row, 0.10f, 8, Fade(palette->panel, 0.92f));
            DrawRectangleRoundedLinesEx(online_name_row, 0.10f, 8, 1.0f, palette->panel_border);
            gui_draw_text("Online Name",
                          (int)online_name_row.x + 16,
                          (int)online_name_row.y + 10,
                          21,
                          palette->text_primary);

            if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) || IsMouseButtonPressed(MOUSE_RIGHT_BUTTON)) {
                app->online_name_input_active = CheckCollisionPointRec(GetMousePosition(), name_input);
            }

            strncpy(before_name, app->online_name, PLAYER_NAME_MAX);
            before_name[PLAYER_NAME_MAX] = '\0';

            gui_input_box(name_input, app->online_name, PLAYER_NAME_MAX + 1, app->online_name_input_active);
            if (strcmp(before_name, app->online_name) != 0) {
                if (app->online_name[0] != '\0') {
                    strncpy(app->profile.username, app->online_name, PLAYER_NAME_MAX);
                    app->profile.username[PLAYER_NAME_MAX] = '\0';
                } else {
                    strncpy(app->profile.username, "Player", PLAYER_NAME_MAX);
                    app->profile.username[PLAYER_NAME_MAX] = '\0';
                }
                dirty = true;
            }

            if (app->online_name[0] == '\0') {
                draw_text_fit("Required for online games",
                              (int)online_name_row.x + (int)online_name_row.width - 250,
                              (int)online_name_row.y + 12,
                              16,
                              230,
                              palette->text_secondary);
            }
        }
    }

    DrawRectangleRounded(right_card, 0.08f, 8, Fade(palette->panel_alt, 0.95f));
    DrawRectangleRoundedLinesEx(right_card, 0.08f, 8, 1.0f, palette->panel_border);
    gui_draw_text("Audio", (int)right_card.x + 16, (int)right_card.y + 12, 32, palette->text_primary);

    right_rows_y = right_card.y + 58.0f;
    toggle_btn = (Rectangle){right_card.x + card_inner, right_rows_y, right_card.width - card_inner * 2.0f, 62.0f};
    sfx_row = (Rectangle){
        right_card.x + card_inner,
        right_rows_y + toggle_btn.height + row_gap,
        right_card.width - card_inner * 2.0f,
        row_h
    };
    menu_music_row = (Rectangle){
        right_card.x + card_inner,
        sfx_row.y + row_h + row_gap,
        right_card.width - card_inner * 2.0f,
        row_h
    };
    game_music_row = (Rectangle){
        right_card.x + card_inner,
        menu_music_row.y + row_h + row_gap,
        right_card.width - card_inner * 2.0f,
        row_h
    };

    {
        const char* toggle_label = app->sound_enabled ? "Sound On" : "Sound Off";
        char vol_text[32];
        float sfx_value = app->sfx_volume;
        float menu_music_value = app->menu_music_volume;
        float game_music_value = app->game_music_volume;
        Rectangle missing_panel = {
            right_card.x + 16.0f,
            game_music_row.y + game_music_row.height + 12.0f,
            right_card.width - 32.0f,
            right_card.y + right_card.height - (game_music_row.y + game_music_row.height + 12.0f) - 12.0f
        };
        const char* missing_entries[AUDIO_SFX_COUNT + 2];
        int missing_count = 0;
        char menu_missing_line[64];
        char game_missing_line[64];

        if (gui_button(toggle_btn, toggle_label)) {
            app->sound_enabled = !app->sound_enabled;
            audio_set_enabled(app->sound_enabled);
            dirty = true;
        }

        snprintf(vol_text, sizeof(vol_text), "%d%%", (int)(app->sfx_volume * 100.0f + 0.5f));
        if (draw_slider_row("SFX Volume", vol_text, sfx_row, &sfx_value, 0.0f, 1.0f)) {
            app->sfx_volume = sfx_value;
            audio_set_sfx_volume(app->sfx_volume);
            dirty = true;
        }

        snprintf(vol_text, sizeof(vol_text), "%d%%", (int)(app->menu_music_volume * 100.0f + 0.5f));
        if (draw_slider_row("Menu Music", vol_text, menu_music_row, &menu_music_value, 0.0f, 1.0f)) {
            app->menu_music_volume = menu_music_value;
            audio_set_menu_music_volume(app->menu_music_volume);
            dirty = true;
        }

        snprintf(vol_text, sizeof(vol_text), "%d%%", (int)(app->game_music_volume * 100.0f + 0.5f));
        if (draw_slider_row("Game Music", vol_text, game_music_row, &game_music_value, 0.0f, 1.0f)) {
            app->game_music_volume = game_music_value;
            audio_set_game_music_volume(app->game_music_volume);
            dirty = true;
        }

        if (!audio_is_menu_music_loaded()) {
            snprintf(menu_missing_line, sizeof(menu_missing_line), "music: menu_bgm.ogg / .mp3 / .wav");
            missing_entries[missing_count++] = menu_missing_line;
        }
        if (!audio_is_game_music_loaded()) {
            snprintf(game_missing_line, sizeof(game_missing_line), "music: game_bgm.ogg / .mp3 / .wav");
            missing_entries[missing_count++] = game_missing_line;
        }

        for (int i = 0; i < AUDIO_SFX_COUNT; ++i) {
            if (!audio_is_loaded((AudioSfx)i)) {
                missing_entries[missing_count++] = audio_expected_filename((AudioSfx)i);
            }
        }

        if (missing_panel.height < 52.0f) {
            missing_panel.height = 52.0f;
        }
        draw_missing_audio_panel(missing_panel, missing_entries, missing_count);
    }

    if (dirty) {
        app_save_settings(app);
    }
}
