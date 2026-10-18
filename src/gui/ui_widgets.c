#include "gui.h"

#include <math.h>
#include <stddef.h>

#include "audio.h"

static float* g_active_slider_value = NULL;
static bool g_submit_pressed_this_frame = false;
static bool g_submit_consumed_this_frame = false;

/* True while enter/numpad-enter is currently held down. */
static bool submit_key_is_down(void) {
    return IsKeyDown(KEY_ENTER) || IsKeyDown(KEY_KP_ENTER);
}

/* Consumes one submit press once per frame to prevent double-activation. */
static bool submit_key_take_press(void) {
    if (g_submit_pressed_this_frame && !g_submit_consumed_this_frame) {
        g_submit_consumed_this_frame = true;
        return true;
    }
    return false;
}

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

static bool gui_button_internal(Rectangle bounds, const char* label, bool submit_hotkey) {
    const GuiPalette* palette = gui_palette();
    Vector2 mouse = GetMousePosition();
    bool hovered = CheckCollisionPointRec(mouse, bounds);
    bool submit_activate = false;
    bool key_activate = false;
    bool pressed = false;
    bool clicked = false;
    Color base;
    Color fill;
    Color border;
    int font_size = (int)lroundf(bounds.height * 0.42f);
    int text_width;
    int text_h;
    int text_max_w = (int)bounds.width - 22;

    if (font_size < 15) {
        font_size = 15;
    }
    if (font_size > 24) {
        font_size = 24;
    }
    if (text_max_w < 10) {
        text_max_w = 10;
    }

    text_width = gui_measure_text(label, font_size);
    while (font_size > 14 && text_width > text_max_w) {
        font_size--;
        text_width = gui_measure_text(label, font_size);
    }
    text_h = gui_measure_text_height(font_size);

    if (submit_hotkey) {
        submit_activate = submit_key_take_press();
    }

    key_activate = (hovered && IsKeyPressed(KEY_SPACE)) || submit_activate;
    pressed = hovered && (IsMouseButtonDown(MOUSE_LEFT_BUTTON) || IsKeyDown(KEY_SPACE));
    if (submit_hotkey && submit_key_is_down()) {
        pressed = true;
    }
    clicked = (hovered && IsMouseButtonReleased(MOUSE_LEFT_BUTTON)) || key_activate;
    base = hovered ? palette->accent_hover : palette->accent;
    fill = pressed ? brighten(base, -18) : base;
    border = brighten(base, -28);

    if (hovered) {
        SetMouseCursor(MOUSE_CURSOR_POINTING_HAND);
    }

    DrawRectangleRounded((Rectangle){bounds.x + 2.5f, bounds.y + 4.0f, bounds.width, bounds.height},
                         0.20f,
                         10,
                         Fade(BLACK, 0.15f));
    DrawRectangleRounded(bounds, 0.20f, 10, fill);
    DrawRectangleRoundedLinesEx(bounds, 0.20f, 10, 1.5f, border);

    gui_draw_text(label,
             (int)(bounds.x + bounds.width * 0.5f - (float)text_width * 0.5f),
             (int)(bounds.y + (bounds.height - (float)text_h) * 0.5f),
             font_size,
             RAYWHITE);

    if (clicked) {
        audio_play(AUDIO_SFX_UI_CLICK);
    }

    return clicked;
}

bool gui_button(Rectangle bounds, const char* label) {
    return gui_button_internal(bounds, label, false);
}

bool gui_button_submit(Rectangle bounds, const char* label, bool submit_hotkey) {
    return gui_button_internal(bounds, label, submit_hotkey);
}

bool gui_slider_float(Rectangle bounds, float* value, float min_value, float max_value) {
    const GuiPalette* palette = gui_palette();
    Vector2 mouse = GetMousePosition();
    bool hovered = CheckCollisionPointRec(mouse, bounds);
    bool changed = false;
    float range;
    float t;
    float knob_x;
    float track_y;
    float track_h = 6.0f;
    float knob_r;
    Rectangle track;

    if (value == NULL) {
        return false;
    }

    if (max_value < min_value) {
        float temp = min_value;
        min_value = max_value;
        max_value = temp;
    }

    range = max_value - min_value;
    if (range <= 0.0001f) {
        *value = min_value;
    } else {
        if (*value < min_value) {
            *value = min_value;
        }
        if (*value > max_value) {
            *value = max_value;
        }
    }

    t = (range <= 0.0001f) ? 0.0f : ((*value - min_value) / range);
    if (t < 0.0f) {
        t = 0.0f;
    }
    if (t > 1.0f) {
        t = 1.0f;
    }

    track_y = bounds.y + bounds.height * 0.5f - track_h * 0.5f;
    track = (Rectangle){bounds.x + 8.0f, track_y, bounds.width - 16.0f, track_h};
    if (track.width < 24.0f) {
        track.x = bounds.x;
        track.width = bounds.width;
    }

    knob_r = bounds.height * 0.30f;
    if (knob_r < 7.0f) {
        knob_r = 7.0f;
    }
    if (knob_r > 11.0f) {
        knob_r = 11.0f;
    }
    knob_x = track.x + track.width * t;

    if (hovered && IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
        g_active_slider_value = value;
    }

    if (g_active_slider_value == value) {
        if (!IsMouseButtonDown(MOUSE_LEFT_BUTTON)) {
            g_active_slider_value = NULL;
        } else {
            float nt = (track.width > 1.0f) ? ((mouse.x - track.x) / track.width) : 0.0f;
            float next;

            if (nt < 0.0f) {
                nt = 0.0f;
            }
            if (nt > 1.0f) {
                nt = 1.0f;
            }

            next = min_value + nt * range;
            if (fabsf(next - *value) > 0.0001f) {
                *value = next;
                changed = true;
            }
            knob_x = track.x + track.width * nt;
        }
    } else if (hovered) {
        float wheel = GetMouseWheelMove();
        if (wheel != 0.0f && range > 0.0001f) {
            float step = range * 0.02f;
            float next;

            if (step < 0.01f) {
                step = 0.01f;
            }

            next = *value + wheel * step;
            if (next < min_value) {
                next = min_value;
            }
            if (next > max_value) {
                next = max_value;
            }

            if (fabsf(next - *value) > 0.0001f) {
                *value = next;
                changed = true;
                t = (range <= 0.0001f) ? 0.0f : ((*value - min_value) / range);
                knob_x = track.x + track.width * t;
            }
        }
    }

    if (hovered || g_active_slider_value == value) {
        SetMouseCursor(MOUSE_CURSOR_POINTING_HAND);
    }

    DrawRectangleRounded(track, 1.0f, 10, Fade(palette->panel_border, 0.60f));
    DrawRectangleRounded((Rectangle){track.x, track.y, knob_x - track.x, track.height},
                         1.0f,
                         10,
                         Fade(palette->accent, 0.95f));

    DrawCircleV((Vector2){knob_x, bounds.y + bounds.height * 0.5f}, knob_r + 2.0f, Fade(BLACK, 0.16f));
    DrawCircleV((Vector2){knob_x, bounds.y + bounds.height * 0.5f}, knob_r, palette->accent_hover);
    DrawCircleLines((int)knob_x,
                    (int)(bounds.y + bounds.height * 0.5f),
                    knob_r,
                    brighten(palette->accent, -26));

    return changed;
}

void gui_widgets_begin_frame(void) {
    g_submit_pressed_this_frame = IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER);
    g_submit_consumed_this_frame = false;
}
