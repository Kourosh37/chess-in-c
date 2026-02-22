#include "gui.h"

#include <stdio.h>

#include "audio.h"
#include "game_state.h"

/* Draws one settings value row with compact stepper controls. */
static void draw_stepper_row(const char* label,
                             const char* value,
                             Rectangle bounds,
                             bool* minus_pressed,
                             bool* plus_pressed) {
    Rectangle minus_btn = {bounds.x + bounds.width - 132.0f, bounds.y, 56.0f, bounds.height};
    Rectangle plus_btn = {bounds.x + bounds.width - 64.0f, bounds.y, 56.0f, bounds.height};
    const GuiPalette* palette = gui_palette();

    DrawText(label, (int)bounds.x, (int)bounds.y + 10, 24, palette->text_primary);
    DrawText(value, (int)bounds.x + 220, (int)bounds.y + 10, 24, palette->text_secondary);

    *minus_pressed = gui_button(minus_btn, "-");
    *plus_pressed = gui_button(plus_btn, "+");
}

void gui_screen_settings(struct ChessApp* app) {
    const GuiPalette* palette = gui_palette();
    float sw = (float)GetScreenWidth();
    float sh = (float)GetScreenHeight();
    float panel_w = sw * 0.80f;
    float panel_h = sh * 0.82f;
    Rectangle panel;
    Rectangle left_card;
    Rectangle right_card;
    Rectangle back_btn;
    bool minus = false;
    bool plus = false;

    if (panel_w < 860.0f) {
        panel_w = 860.0f;
    }
    if (panel_w > 1120.0f) {
        panel_w = 1120.0f;
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
        panel.y + 110.0f,
        panel.width * 0.48f - 30.0f,
        panel.height - 136.0f
    };
    right_card = (Rectangle){
        panel.x + panel.width * 0.50f + 6.0f,
        panel.y + 110.0f,
        panel.width * 0.48f - 30.0f,
        panel.height - 136.0f
    };
    back_btn = (Rectangle){
        panel.x + panel.width - 178.0f,
        panel.y + 30.0f,
        148.0f,
        52.0f
    };

    DrawRectangleRounded((Rectangle){panel.x + 6.0f, panel.y + 7.0f, panel.width, panel.height},
                         0.07f,
                         8,
                         Fade(BLACK, 0.16f));
    DrawRectangleRounded(panel, 0.07f, 8, Fade(palette->panel, 0.95f));
    DrawRectangleRoundedLinesEx(panel, 0.07f, 8, 1.5f, palette->panel_border);

    DrawText("Settings", (int)panel.x + 30, (int)panel.y + 28, 50, palette->text_primary);

    if (gui_button(back_btn, "Back")) {
        app->screen = SCREEN_MENU;
    }

    DrawRectangleRounded(left_card, 0.08f, 8, Fade(palette->panel_alt, 0.95f));
    DrawRectangleRoundedLinesEx(left_card, 0.08f, 8, 1.0f, palette->panel_border);
    DrawText("Gameplay & Visual", (int)left_card.x + 16, (int)left_card.y + 14, 30, palette->text_primary);

    {
        Rectangle row;
        char value[64];
        int theme_count = gui_theme_count();

        snprintf(value, sizeof(value), "%d", app->ai_limits.depth);
        row = (Rectangle){left_card.x + 16.0f, left_card.y + 62.0f, left_card.width - 32.0f, 48.0f};
        draw_stepper_row("AI Depth", value, row, &minus, &plus);
        if (minus && app->ai_limits.depth > 1) {
            app->ai_limits.depth--;
        }
        if (plus && app->ai_limits.depth < 8) {
            app->ai_limits.depth++;
        }

        snprintf(value, sizeof(value), "%d", app->ai_limits.randomness);
        row = (Rectangle){left_card.x + 16.0f, left_card.y + 122.0f, left_card.width - 32.0f, 48.0f};
        draw_stepper_row("AI Randomness", value, row, &minus, &plus);
        if (minus && app->ai_limits.randomness >= 10) {
            app->ai_limits.randomness -= 10;
        }
        if (plus && app->ai_limits.randomness <= 90) {
            app->ai_limits.randomness += 10;
        }

        DrawText("Color Theme", (int)left_card.x + 16, (int)left_card.y + 202, 24, palette->text_primary);
        DrawText(gui_theme_name(app->theme), (int)left_card.x + 220, (int)left_card.y + 202, 24, palette->accent);

        {
            Rectangle prev_btn = {left_card.x + left_card.width - 132.0f, left_card.y + 192.0f, 56.0f, 48.0f};
            Rectangle next_btn = {left_card.x + left_card.width - 64.0f, left_card.y + 192.0f, 56.0f, 48.0f};

            if (gui_button(prev_btn, "<")) {
                int next = ((int)app->theme - 1 + theme_count) % theme_count;
                app->theme = (ColorTheme)next;
                gui_set_active_theme(next);
            }
            if (gui_button(next_btn, ">")) {
                int next = ((int)app->theme + 1) % theme_count;
                app->theme = (ColorTheme)next;
                gui_set_active_theme(next);
            }
        }

        DrawText("Themes include board + UI palette together.", (int)left_card.x + 16, (int)left_card.y + 262, 20, palette->text_secondary);
    }

    DrawRectangleRounded(right_card, 0.08f, 8, Fade(palette->panel_alt, 0.95f));
    DrawRectangleRoundedLinesEx(right_card, 0.08f, 8, 1.0f, palette->panel_border);
    DrawText("Audio", (int)right_card.x + 16, (int)right_card.y + 14, 30, palette->text_primary);

    {
        const char* toggle_label = app->sound_enabled ? "Sound: On" : "Sound: Off";
        Rectangle toggle_btn = {right_card.x + 16.0f, right_card.y + 62.0f, 170.0f, 50.0f};
        Rectangle minus_btn = {right_card.x + right_card.width - 132.0f, right_card.y + 62.0f, 56.0f, 50.0f};
        Rectangle plus_btn = {right_card.x + right_card.width - 64.0f, right_card.y + 62.0f, 56.0f, 50.0f};
        char vol_text[32];
        int y = (int)right_card.y + 134;

        if (gui_button(toggle_btn, toggle_label)) {
            app->sound_enabled = !app->sound_enabled;
            audio_set_enabled(app->sound_enabled);
        }

        snprintf(vol_text, sizeof(vol_text), "Volume: %d%%", (int)(app->sound_volume * 100.0f + 0.5f));
        DrawText(vol_text, (int)right_card.x + 16, (int)right_card.y + 76, 24, palette->text_primary);

        if (gui_button(minus_btn, "-")) {
            app->sound_volume -= 0.10f;
            if (app->sound_volume < 0.0f) {
                app->sound_volume = 0.0f;
            }
            audio_set_master_volume(app->sound_volume);
        }
        if (gui_button(plus_btn, "+")) {
            app->sound_volume += 0.10f;
            if (app->sound_volume > 1.0f) {
                app->sound_volume = 1.0f;
            }
            audio_set_master_volume(app->sound_volume);
        }

        DrawText("Expected SFX files (assets/sfx):", (int)right_card.x + 16, y, 22, palette->text_secondary);
        y += 34;

        for (int i = 0; i < AUDIO_SFX_COUNT; ++i) {
            char line[128];
            const char* filename = audio_expected_filename((AudioSfx)i);
            const char* state = audio_is_loaded((AudioSfx)i) ? "loaded" : "missing";
            Color state_color = audio_is_loaded((AudioSfx)i) ? (Color){20, 138, 82, 255} : (Color){173, 95, 33, 255};

            snprintf(line, sizeof(line), "%s", filename);
            DrawText(line, (int)right_card.x + 16, y, 20, palette->text_primary);
            DrawText(state, (int)right_card.x + (int)right_card.width - 86, y, 20, state_color);
            y += 28;
        }
    }
}
