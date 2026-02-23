#include "gui.h"

#include <ctype.h>
#include <math.h>
#include <string.h>

#include "audio.h"

typedef enum InputMenuItem {
    INPUT_MENU_PASTE = 0,
    INPUT_MENU_COPY = 1,
    INPUT_MENU_CUT = 2,
    INPUT_MENU_SELECT_ALL = 3,
    INPUT_MENU_CLEAR = 4,
    INPUT_MENU_COUNT = 5
} InputMenuItem;

typedef struct InputContextMenu {
    bool open;
    Rectangle rect;
    char* buffer;
    int capacity;
} InputContextMenu;

static InputContextMenu g_input_menu = {0};
static char* g_selected_input_buffer = NULL;
static float* g_active_slider_value = NULL;
static bool g_input_box_used_this_frame = false;
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

/* True when user currently has select-all state on this input buffer. */
static bool input_has_selection(char* buffer) {
    return buffer != NULL && g_selected_input_buffer == buffer;
}

/* Sets select-all state for one input buffer. */
static void input_set_selection(char* buffer, bool selected) {
    if (selected) {
        g_selected_input_buffer = buffer;
    } else if (g_selected_input_buffer == buffer) {
        g_selected_input_buffer = NULL;
    }
}

/* Clears select-all state if input content is replaced. */
static void input_clear_selection(char* buffer) {
    input_set_selection(buffer, false);
}

/* True when key should be accepted for invite-style input. */
static bool is_valid_input_char(int key) {
    unsigned char c = (unsigned char)key;
    return (isalnum(c) || c == '-' || c == '_');
}

/* Appends clipboard text with filtering and uppercase normalization. */
static void input_paste_filtered(char* buffer, int capacity, bool replace_all) {
    const char* clip = GetClipboardText();
    int out;

    if (buffer == NULL || capacity <= 0) {
        return;
    }

    out = replace_all ? 0 : (int)strlen(buffer);
    if (replace_all) {
        buffer[0] = '\0';
    }

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

/* Copies the whole input buffer into system clipboard. */
static void input_copy_all(const char* buffer) {
    if (buffer != NULL) {
        SetClipboardText(buffer);
    }
}

/* Clears all text in input buffer. */
static void input_clear_all(char* buffer) {
    if (buffer != NULL) {
        buffer[0] = '\0';
    }
}

/* Opens context menu for this input at mouse position with screen clamping. */
static void input_menu_open(char* buffer, int capacity) {
    const float menu_w = 176.0f;
    const float item_h = 31.0f;
    const float menu_h = 10.0f + item_h * (float)INPUT_MENU_COUNT;
    float x = GetMousePosition().x;
    float y = GetMousePosition().y;
    float sw = (float)GetScreenWidth();
    float sh = (float)GetScreenHeight();

    if (x + menu_w > sw - 8.0f) {
        x = sw - menu_w - 8.0f;
    }
    if (y + menu_h > sh - 8.0f) {
        y = sh - menu_h - 8.0f;
    }
    if (x < 8.0f) {
        x = 8.0f;
    }
    if (y < 8.0f) {
        y = 8.0f;
    }

    g_input_menu.open = true;
    g_input_menu.buffer = buffer;
    g_input_menu.capacity = capacity;
    g_input_menu.rect = (Rectangle){x, y, menu_w, menu_h};
}

/* Draws and handles input context menu interactions on top of all widgets. */
static void input_menu_update(void) {
    const GuiPalette* palette = gui_palette();
    const char* labels[INPUT_MENU_COUNT] = {"Paste", "Copy", "Cut", "Select All", "Clear"};
    const float item_h = 31.0f;
    const float pad = 5.0f;
    Vector2 mouse = GetMousePosition();
    bool inside_menu;

    if (!g_input_menu.open || g_input_menu.buffer == NULL) {
        return;
    }

    inside_menu = CheckCollisionPointRec(mouse, g_input_menu.rect);
    if (inside_menu) {
        SetMouseCursor(MOUSE_CURSOR_POINTING_HAND);
    }

    DrawRectangleRounded((Rectangle){g_input_menu.rect.x + 2.0f,
                                     g_input_menu.rect.y + 3.0f,
                                     g_input_menu.rect.width,
                                     g_input_menu.rect.height},
                         0.12f,
                         8,
                         Fade(BLACK, 0.16f));
    DrawRectangleRounded(g_input_menu.rect, 0.12f, 8, Fade(palette->panel, 0.98f));
    DrawRectangleRoundedLinesEx(g_input_menu.rect, 0.12f, 8, 1.2f, palette->panel_border);

    for (int i = 0; i < INPUT_MENU_COUNT; ++i) {
        Rectangle item = {
            g_input_menu.rect.x + pad,
            g_input_menu.rect.y + pad + item_h * (float)i,
            g_input_menu.rect.width - pad * 2.0f,
            item_h
        };
        bool hovered = CheckCollisionPointRec(mouse, item);
        int font_size = 20;
        int text_h = gui_measure_text_height(font_size);

        if (hovered) {
            DrawRectangleRounded(item, 0.14f, 8, Fade(palette->accent, 0.18f));
        }

        gui_draw_text(labels[i],
                      (int)item.x + 12,
                      (int)(item.y + (item.height - (float)text_h) * 0.5f),
                      font_size,
                      palette->text_primary);

        if (hovered && IsMouseButtonReleased(MOUSE_LEFT_BUTTON)) {
            if (i == INPUT_MENU_PASTE) {
                bool replace_all = input_has_selection(g_input_menu.buffer);
                input_paste_filtered(g_input_menu.buffer, g_input_menu.capacity, replace_all);
                input_clear_selection(g_input_menu.buffer);
            } else if (i == INPUT_MENU_COPY) {
                input_copy_all(g_input_menu.buffer);
            } else if (i == INPUT_MENU_CUT) {
                input_copy_all(g_input_menu.buffer);
                input_clear_all(g_input_menu.buffer);
                input_clear_selection(g_input_menu.buffer);
            } else if (i == INPUT_MENU_SELECT_ALL) {
                input_set_selection(g_input_menu.buffer, true);
            } else if (i == INPUT_MENU_CLEAR) {
                input_clear_all(g_input_menu.buffer);
                input_clear_selection(g_input_menu.buffer);
            }

            audio_play(AUDIO_SFX_UI_CLICK);
            g_input_menu.open = false;
            return;
        }
    }

    if ((IsMouseButtonPressed(MOUSE_LEFT_BUTTON) || IsMouseButtonPressed(MOUSE_RIGHT_BUTTON)) && !inside_menu) {
        g_input_menu.open = false;
    }
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
    bool blocked_by_input_menu = g_input_menu.open && CheckCollisionPointRec(mouse, g_input_menu.rect);
    bool input_menu_open = g_input_menu.open;
    bool hovered = !blocked_by_input_menu && CheckCollisionPointRec(mouse, bounds);
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

    if (!input_menu_open && (hovered || submit_hotkey)) {
        submit_activate = submit_key_take_press();
    }

    key_activate = (hovered && IsKeyPressed(KEY_SPACE)) || submit_activate;
    pressed = hovered && (IsMouseButtonDown(MOUSE_LEFT_BUTTON) || submit_key_is_down() || IsKeyDown(KEY_SPACE));
    if (!input_menu_open && submit_hotkey && submit_key_is_down()) {
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

void gui_input_box(Rectangle bounds, char* buffer, int capacity, bool active) {
    const GuiPalette* palette = gui_palette();
    Vector2 mouse = GetMousePosition();
    bool hovered = CheckCollisionPointRec(mouse, bounds);
    Color bg = active ? brighten(palette->panel_alt, 10) : palette->panel_alt;
    Color border = active ? palette->accent : palette->panel_border;
    bool ctrl_down = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    bool has_selection = input_has_selection(buffer);
    int text_size = 24;
    int text_h = gui_measure_text_height(text_size);

    g_input_box_used_this_frame = true;

    DrawRectangleRounded(bounds, 0.12f, 8, bg);
    DrawRectangleRoundedLinesEx(bounds, 0.12f, 8, active ? 2.0f : 1.0f, border);

    if (hovered || active) {
        SetMouseCursor(MOUSE_CURSOR_IBEAM);
    }

    if (hovered && IsMouseButtonPressed(MOUSE_RIGHT_BUTTON)) {
        input_menu_open(buffer, capacity);
    }

    if (active) {
        bool paste = (ctrl_down && IsKeyPressed(KEY_V)) ||
                     (IsKeyDown(KEY_LEFT_SHIFT) && IsKeyPressed(KEY_INSERT));
        bool select_all = ctrl_down && IsKeyPressed(KEY_A);
        bool copy = ctrl_down && IsKeyPressed(KEY_C);
        bool cut = ctrl_down && IsKeyPressed(KEY_X);
        bool backspace = IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE);
        bool del = IsKeyPressed(KEY_DELETE) || IsKeyPressedRepeat(KEY_DELETE);

        if (select_all) {
            input_set_selection(buffer, true);
            has_selection = true;
        }

        if (copy && has_selection) {
            input_copy_all(buffer);
        }

        if (cut && has_selection) {
            input_copy_all(buffer);
            input_clear_all(buffer);
            input_clear_selection(buffer);
            has_selection = false;
        }

        if (paste) {
            input_paste_filtered(buffer, capacity, has_selection);
            input_clear_selection(buffer);
            has_selection = false;
        }

        if ((backspace || del) && has_selection) {
            input_clear_all(buffer);
            input_clear_selection(buffer);
            has_selection = false;
        } else if (backspace) {
            size_t len = strlen(buffer);
            if (len > 0) {
                buffer[len - 1] = '\0';
            }
        }

        int key = GetCharPressed();
        while (key > 0) {
            if (!paste && is_valid_input_char(key) && (int)strlen(buffer) < capacity - 1) {
                if (has_selection) {
                    input_clear_all(buffer);
                    input_clear_selection(buffer);
                    has_selection = false;
                }
                size_t len = strlen(buffer);
                buffer[len] = (char)toupper((unsigned char)key);
                buffer[len + 1] = '\0';
            }
            key = GetCharPressed();
        }
    }

    if (has_selection) {
        float select_pad = 9.0f;
        Rectangle selection = {
            bounds.x + select_pad,
            bounds.y + 8.0f,
            bounds.width - select_pad * 2.0f,
            bounds.height - 16.0f
        };
        DrawRectangleRounded(selection, 0.10f, 8, Fade(palette->accent, 0.18f));
    }

    gui_draw_text(buffer,
                  (int)bounds.x + 12,
                  (int)(bounds.y + (bounds.height - (float)text_h) * 0.5f - 1.0f),
                  text_size,
                  palette->text_primary);

    if (active && !has_selection && ((GetTime() * 2.0) - (int)(GetTime() * 2.0) < 0.5)) {
        int text_w = gui_measure_text(buffer, text_size);
        int cursor_h = text_h + 4;
        int cursor_y = (int)(bounds.y + (bounds.height - (float)cursor_h) * 0.5f);
        DrawRectangle((int)(bounds.x + 12 + (float)text_w + 1), cursor_y, 2, cursor_h, palette->text_primary);
    }

}

void gui_widgets_begin_frame(void) {
    g_input_box_used_this_frame = false;
    g_submit_pressed_this_frame = IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER);
    g_submit_consumed_this_frame = false;
}

void gui_draw_input_overlays(void) {
    if (!g_input_box_used_this_frame) {
        g_input_menu.open = false;
        return;
    }

    input_menu_update();
}
