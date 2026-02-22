#include "gui.h"

#include <stdio.h>

#include "audio.h"
#include "game_state.h"

/* Draws a standard settings row with label, value and +/- controls. */
static void draw_stepper_row(const char* label,
                             const char* value,
                             Rectangle row,
                             bool* minus_pressed,
                             bool* plus_pressed) {
    const GuiPalette* palette = gui_palette();
    Rectangle minus_btn = {row.x + row.width - 126.0f, row.y + 8.0f, 52.0f, row.height - 16.0f};
    Rectangle plus_btn = {row.x + row.width - 64.0f, row.y + 8.0f, 52.0f, row.height - 16.0f};

    DrawRectangleRounded(row, 0.10f, 8, Fade(palette->panel, 0.92f));
    DrawRectangleRoundedLinesEx(row, 0.10f, 8, 1.0f, palette->panel_border);

    gui_draw_text(label, (int)row.x + 14, (int)row.y + 14, 24, palette->text_primary);
    gui_draw_text(value, (int)row.x + 230, (int)row.y + 14, 24, palette->accent);

    *minus_pressed = gui_button(minus_btn, "-");
    *plus_pressed = gui_button(plus_btn, "+");
}

void gui_screen_settings(struct ChessApp* app) {
    const GuiPalette* palette = gui_palette();
    float sw = (float)GetScreenWidth();
    float sh = (float)GetScreenHeight();
    float panel_w = sw * 0.80f;
    float panel_h = sh * 0.80f;
    Rectangle panel;
    Rectangle left_card;
    Rectangle right_card;
    Rectangle back_btn;
    bool minus;
    bool plus;
    bool dirty = false;

    if (panel_w < 860.0f) {
        panel_w = 860.0f;
    }
    if (panel_w > 1140.0f) {
        panel_w = 1140.0f;
    }
    if (panel_h < 560.0f) {
        panel_h = 560.0f;
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

    left_card = (Rectangle){
        panel.x + 24.0f,
        panel.y + 98.0f,
        panel.width * 0.52f - 32.0f,
        panel.height - 124.0f
    };
    right_card = (Rectangle){
        panel.x + panel.width * 0.52f + 8.0f,
        panel.y + 98.0f,
        panel.width * 0.48f - 32.0f,
        panel.height - 124.0f
    };
    back_btn = (Rectangle){
        panel.x + panel.width - 170.0f,
        panel.y + 24.0f,
        140.0f,
        48.0f
    };

    DrawRectangleRounded((Rectangle){panel.x + 6.0f, panel.y + 7.0f, panel.width, panel.height},
                         0.07f,
                         8,
                         Fade(BLACK, 0.16f));
    DrawRectangleRounded(panel, 0.07f, 8, Fade(palette->panel, 0.95f));
    DrawRectangleRoundedLinesEx(panel, 0.07f, 8, 1.4f, palette->panel_border);

    gui_draw_text("Settings", (int)panel.x + 28, (int)panel.y + 24, 44, palette->text_primary);

    if (gui_button(back_btn, "Back")) {
        if (dirty) {
            app_save_settings(app);
        }
        app->screen = SCREEN_MENU;
    }

    DrawRectangleRounded(left_card, 0.08f, 8, Fade(palette->panel_alt, 0.95f));
    DrawRectangleRoundedLinesEx(left_card, 0.08f, 8, 1.0f, palette->panel_border);
    gui_draw_text("Game", (int)left_card.x + 14, (int)left_card.y + 10, 30, palette->text_primary);

    {
        Rectangle row;
        char value[64];
        int theme_count = gui_theme_count();

        snprintf(value, sizeof(value), "%d", app->ai_limits.depth);
        row = (Rectangle){left_card.x + 14.0f, left_card.y + 58.0f, left_card.width - 28.0f, 64.0f};
        draw_stepper_row("AI Depth", value, row, &minus, &plus);
        if (minus && app->ai_limits.depth > 1) {
            app->ai_limits.depth--;
            dirty = true;
        }
        if (plus && app->ai_limits.depth < 8) {
            app->ai_limits.depth++;
            dirty = true;
        }

        snprintf(value, sizeof(value), "%d", app->ai_limits.randomness);
        row = (Rectangle){left_card.x + 14.0f, left_card.y + 134.0f, left_card.width - 28.0f, 64.0f};
        draw_stepper_row("AI Random", value, row, &minus, &plus);
        if (minus && app->ai_limits.randomness >= 10) {
            app->ai_limits.randomness -= 10;
            dirty = true;
        }
        if (plus && app->ai_limits.randomness <= 90) {
            app->ai_limits.randomness += 10;
            dirty = true;
        }

        DrawRectangleRounded((Rectangle){left_card.x + 14.0f, left_card.y + 210.0f, left_card.width - 28.0f, 64.0f},
                             0.10f,
                             8,
                             Fade(palette->panel, 0.92f));
        DrawRectangleRoundedLinesEx((Rectangle){left_card.x + 14.0f, left_card.y + 210.0f, left_card.width - 28.0f, 64.0f},
                                    0.10f,
                                    8,
                                    1.0f,
                                    palette->panel_border);
        gui_draw_text("Theme", (int)left_card.x + 28, (int)left_card.y + 226, 24, palette->text_primary);
        gui_draw_text(gui_theme_name(app->theme), (int)left_card.x + 230, (int)left_card.y + 226, 24, palette->accent);

        {
            Rectangle prev_btn = {left_card.x + left_card.width - 124.0f, left_card.y + 218.0f, 50.0f, 48.0f};
            Rectangle next_btn = {left_card.x + left_card.width - 64.0f, left_card.y + 218.0f, 50.0f, 48.0f};

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
    }

    DrawRectangleRounded(right_card, 0.08f, 8, Fade(palette->panel_alt, 0.95f));
    DrawRectangleRoundedLinesEx(right_card, 0.08f, 8, 1.0f, palette->panel_border);
    gui_draw_text("Audio", (int)right_card.x + 14, (int)right_card.y + 10, 30, palette->text_primary);

    {
        const char* toggle_label = app->sound_enabled ? "Sound On" : "Sound Off";
        Rectangle toggle_btn = {right_card.x + 14.0f, right_card.y + 58.0f, right_card.width - 28.0f, 56.0f};
        Rectangle vol_row = {right_card.x + 14.0f, right_card.y + 128.0f, right_card.width - 28.0f, 64.0f};
        Rectangle minus_btn = {vol_row.x + vol_row.width - 126.0f, vol_row.y + 8.0f, 52.0f, vol_row.height - 16.0f};
        Rectangle plus_btn = {vol_row.x + vol_row.width - 64.0f, vol_row.y + 8.0f, 52.0f, vol_row.height - 16.0f};
        char vol_text[32];

        if (gui_button(toggle_btn, toggle_label)) {
            app->sound_enabled = !app->sound_enabled;
            audio_set_enabled(app->sound_enabled);
            dirty = true;
        }

        DrawRectangleRounded(vol_row, 0.10f, 8, Fade(palette->panel, 0.92f));
        DrawRectangleRoundedLinesEx(vol_row, 0.10f, 8, 1.0f, palette->panel_border);

        snprintf(vol_text, sizeof(vol_text), "%d%%", (int)(app->sound_volume * 100.0f + 0.5f));
        gui_draw_text("Volume", (int)vol_row.x + 14, (int)vol_row.y + 14, 24, palette->text_primary);
        gui_draw_text(vol_text, (int)vol_row.x + 230, (int)vol_row.y + 14, 24, palette->accent);

        if (gui_button(minus_btn, "-")) {
            app->sound_volume -= 0.10f;
            if (app->sound_volume < 0.0f) {
                app->sound_volume = 0.0f;
            }
            audio_set_master_volume(app->sound_volume);
            dirty = true;
        }
        if (gui_button(plus_btn, "+")) {
            app->sound_volume += 0.10f;
            if (app->sound_volume > 1.0f) {
                app->sound_volume = 1.0f;
            }
            audio_set_master_volume(app->sound_volume);
            dirty = true;
        }

        gui_draw_text("Sound files path:", (int)right_card.x + 16, (int)right_card.y + 218, 20, palette->text_secondary);
        gui_draw_text("assets/sfx", (int)right_card.x + 16, (int)right_card.y + 244, 24, palette->text_primary);
    }

    if (dirty) {
        app_save_settings(app);
    }
}
