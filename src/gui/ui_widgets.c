#include "gui.h"

#include <ctype.h>
#include <string.h>

#include "audio.h"

/* Returns a slightly brighter color by adding a fixed offset. */
static Color brighten(Color color, int amount) {
    int r = color.r + amount;
    int g = color.g + amount;
    int b = color.b + amount;

    if (r < 0) {
        r = 0;
    }
    if (g < 0) {
        g = 0;
    }
    if (b < 0) {
        b = 0;
    }

    if (r > 255) {
        r = 255;
    }
    if (g > 255) {
        g = 255;
    }
    if (b > 255) {
        b = 255;
    }

    color.r = (unsigned char)r;
    color.g = (unsigned char)g;
    color.b = (unsigned char)b;
    return color;
}

bool gui_button(Rectangle bounds, const char* label) {
    const GuiPalette* palette = gui_palette();
    Vector2 mouse = GetMousePosition();
    bool hovered = CheckCollisionPointRec(mouse, bounds);
    bool pressed = hovered && IsMouseButtonDown(MOUSE_LEFT_BUTTON);
    bool clicked = hovered && IsMouseButtonReleased(MOUSE_LEFT_BUTTON);
    Color base = hovered ? palette->accent_hover : palette->accent;
    Color fill = pressed ? brighten(base, -18) : base;
    Color border = brighten(base, -28);
    int font_size = (bounds.height >= 56.0f) ? 24 : 20;
    int text_width = gui_measure_text(label, font_size);

    DrawRectangleRounded((Rectangle){bounds.x + 2.5f, bounds.y + 4.0f, bounds.width, bounds.height},
                         0.20f,
                         10,
                         Fade(BLACK, 0.15f));
    DrawRectangleRounded(bounds, 0.20f, 10, fill);
    DrawRectangleRoundedLinesEx(bounds, 0.20f, 10, 1.5f, border);

    gui_draw_text(label,
             (int)(bounds.x + bounds.width * 0.5f - (float)text_width * 0.5f),
             (int)(bounds.y + bounds.height * 0.5f - (float)font_size * 0.5f),
             font_size,
             RAYWHITE);

    if (clicked) {
        audio_play(AUDIO_SFX_UI_CLICK);
    }

    return clicked;
}

void gui_input_box(Rectangle bounds, char* buffer, int capacity, bool active) {
    const GuiPalette* palette = gui_palette();
    Color bg = active ? brighten(palette->panel_alt, 10) : palette->panel_alt;
    Color border = active ? palette->accent : palette->panel_border;

    DrawRectangleRounded(bounds, 0.12f, 8, bg);
    DrawRectangleRoundedLinesEx(bounds, 0.12f, 8, active ? 2.0f : 1.0f, border);

    if (active) {
        bool paste = ((IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) && IsKeyPressed(KEY_V)) ||
                     (IsKeyDown(KEY_LEFT_SHIFT) && IsKeyPressed(KEY_INSERT));

        if (paste) {
            const char* clip = GetClipboardText();
            int out = 0;

            if (clip != NULL) {
                for (int i = 0; clip[i] != '\0' && out < capacity - 1; ++i) {
                    unsigned char c = (unsigned char)clip[i];
                    if (isalnum(c) || c == '-' || c == '_') {
                        buffer[out++] = (char)toupper(c);
                    }
                }
            }
            buffer[out] = '\0';
        }

        int key = GetCharPressed();
        while (key > 0) {
            if (!paste &&
                (isalnum((unsigned char)key) || key == '-' || key == '_') &&
                (int)strlen(buffer) < capacity - 1) {
                size_t len = strlen(buffer);
                buffer[len] = (char)toupper((unsigned char)key);
                buffer[len + 1] = '\0';
            }
            key = GetCharPressed();
        }

        if (IsKeyPressed(KEY_BACKSPACE)) {
            size_t len = strlen(buffer);
            if (len > 0) {
                buffer[len - 1] = '\0';
            }
        }
    }

    gui_draw_text(buffer, (int)bounds.x + 12, (int)bounds.y + 12, 24, palette->text_primary);

    if (active && ((GetTime() * 2.0) - (int)(GetTime() * 2.0) < 0.5)) {
        int text_w = gui_measure_text(buffer, 24);
        DrawRectangle((int)(bounds.x + 12 + (float)text_w + 1), (int)bounds.y + 10, 2, 28, palette->text_primary);
    }
}
