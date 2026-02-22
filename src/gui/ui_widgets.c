#include "gui.h"

#include <ctype.h>
#include <string.h>

/* Draws a button and returns true on left-click release inside bounds. */
bool gui_button(Rectangle bounds, const char* label) {
    Vector2 mouse = GetMousePosition();
    bool hovered = CheckCollisionPointRec(mouse, bounds);
    Color base = (Color){52, 73, 94, 255};
    Color hover = (Color){69, 98, 127, 255};
    int text_width;

    DrawRectangleRounded(bounds, 0.18f, 10, hovered ? hover : base);
    DrawRectangleLinesEx(bounds, 1.5f, (Color){20, 28, 38, 255});

    text_width = MeasureText(label, 20);
    DrawText(label,
             (int)(bounds.x + bounds.width * 0.5f - text_width * 0.5f),
             (int)(bounds.y + bounds.height * 0.5f - 10),
             20,
             RAYWHITE);

    return hovered && IsMouseButtonReleased(MOUSE_LEFT_BUTTON);
}

/* Draws an input box and edits its backing buffer while active. */
void gui_input_box(Rectangle bounds, char* buffer, int capacity, bool active) {
    Color bg = active ? (Color){240, 248, 255, 255} : (Color){226, 231, 236, 255};

    DrawRectangleRounded(bounds, 0.12f, 8, bg);
    DrawRectangleLinesEx(bounds, active ? 2.0f : 1.0f, (Color){35, 35, 35, 255});

    if (active) {
        int key = GetCharPressed();
        while (key > 0) {
            if ((isalnum((unsigned char)key) || key == '-' || key == '_') &&
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

    DrawText(buffer, (int)bounds.x + 10, (int)bounds.y + 10, 22, BLACK);

    /* Blink a caret at 2 Hz for active input fields. */
    if (active && ((GetTime() * 2.0) - (int)(GetTime() * 2.0) < 0.5)) {
        int text_w = MeasureText(buffer, 22);
        DrawRectangle((int)(bounds.x + 10 + text_w + 1), (int)bounds.y + 8, 2, 24, BLACK);
    }
}
