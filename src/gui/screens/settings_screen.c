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
    float row_h = 84.0f;
    float row_gap = 12.0f;
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

        theme_value_w = gui_measure_text(theme_name, font_size);
        theme_value_x = (int)(prev_btn.x - 14.0f - (float)theme_value_w);
        if (theme_value_x < theme_min_x) {
            theme_value_x = theme_min_x;
        }
        text_y = (int)(theme_row.y + (theme_row.height - (float)font_size) * 0.5f - 1.0f);

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
        float status_y = game_music_row.y + game_music_row.height + 18.0f;
        int status_w = (int)right_card.width - 36;

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
            draw_text_fit("Missing menu music: menu_bgm.ogg / .mp3 / .wav",
                          (int)right_card.x + 18,
                          (int)status_y,
                          19,
                          status_w,
                          palette->text_secondary);
            status_y += 23.0f;
        }
        if (!audio_is_game_music_loaded()) {
            draw_text_fit("Missing game music: game_bgm.ogg / .mp3 / .wav",
                          (int)right_card.x + 18,
                          (int)status_y,
                          19,
                          status_w,
                          palette->text_secondary);
            status_y += 23.0f;
        }

        {
            int missing_sfx[AUDIO_SFX_COUNT];
            int missing_count = 0;
            float bottom_y = right_card.y + right_card.height - 14.0f;
            int line_h = 21;
            int drawn = 0;

            for (int i = 0; i < AUDIO_SFX_COUNT; ++i) {
                if (!audio_is_loaded((AudioSfx)i)) {
                    missing_sfx[missing_count++] = i;
                }
            }

            if (missing_count > 0 && status_y + 18.0f < bottom_y) {
                draw_text_fit("Missing SFX files:",
                              (int)right_card.x + 18,
                              (int)status_y,
                              19,
                              status_w,
                              palette->text_secondary);
                status_y += 23.0f;
            }

            while (drawn < missing_count && status_y + (float)line_h <= bottom_y) {
                int remaining_after_this = missing_count - (drawn + 1);
                int lines_left = (int)((bottom_y - status_y) / (float)line_h);

                if (lines_left <= 1 && remaining_after_this > 0) {
                    char more_line[32];
                    snprintf(more_line, sizeof(more_line), "+%d more...", remaining_after_this + 1);
                    draw_text_fit(more_line,
                                  (int)right_card.x + 26,
                                  (int)status_y,
                                  18,
                                  status_w - 8,
                                  palette->text_secondary);
                    break;
                }

                draw_text_fit(audio_expected_filename((AudioSfx)missing_sfx[drawn]),
                              (int)right_card.x + 26,
                              (int)status_y,
                              18,
                              status_w - 8,
                              palette->text_secondary);
                status_y += (float)line_h;
                drawn++;
            }
        }
    }

    if (dirty) {
        app_save_settings(app);
    }
}
