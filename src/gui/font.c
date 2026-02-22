#include "gui.h"

#include <math.h>

static Font g_ui_font;
static bool g_ui_font_initialized = false;
static bool g_ui_font_loaded = false;

/* Tries loading one font file path and applies it if valid. */
static bool try_load_font_path(const char* path) {
    Font loaded;

    if (!FileExists(path)) {
        return false;
    }

    loaded = LoadFontEx(path, 96, NULL, 0);
    if (loaded.texture.id == 0) {
        return false;
    }

    g_ui_font = loaded;
    g_ui_font_loaded = true;
    return true;
}

/* Returns the currently active UI font, defaulting safely when needed. */
static Font active_font(void) {
    if (!g_ui_font_initialized) {
        return GetFontDefault();
    }
    return g_ui_font;
}

bool gui_font_init(void) {
    if (g_ui_font_initialized) {
        return true;
    }

    g_ui_font = GetFontDefault();
    g_ui_font_initialized = true;

    if (!try_load_font_path("assets/fonts/ui_font.ttf")) {
        if (!try_load_font_path("assets/fonts/Cinzel-Bold.ttf")) {
            if (!try_load_font_path("assets/fonts/PlayfairDisplay-Bold.ttf")) {
                if (!try_load_font_path("C:/Windows/Fonts/cambriab.ttf")) {
                    if (!try_load_font_path("C:/Windows/Fonts/georgiab.ttf")) {
                        if (!try_load_font_path("C:/Windows/Fonts/timesbd.ttf")) {
                            if (!try_load_font_path("C:/Windows/Fonts/segoeuib.ttf")) {
                                if (!try_load_font_path("/usr/share/fonts/truetype/dejavu/DejaVuSerif-Bold.ttf")) {
                                    if (!try_load_font_path("assets/fonts/NotoSans-Regular.ttf")) {
                                        try_load_font_path("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf");
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    if (g_ui_font.texture.id != 0) {
        SetTextureFilter(g_ui_font.texture, TEXTURE_FILTER_BILINEAR);
    }

    return true;
}

void gui_font_shutdown(void) {
    if (!g_ui_font_initialized) {
        return;
    }

    if (g_ui_font_loaded) {
        UnloadFont(g_ui_font);
    }

    g_ui_font = GetFontDefault();
    g_ui_font_loaded = false;
    g_ui_font_initialized = false;
}

void gui_draw_text(const char* text, int pos_x, int pos_y, int font_size, Color color) {
    float spacing = (float)font_size * 0.04f;
    Vector2 pos = {(float)pos_x, (float)pos_y};
    DrawTextEx(active_font(), text, pos, (float)font_size, spacing, color);
}

int gui_measure_text(const char* text, int font_size) {
    float spacing = (float)font_size * 0.04f;
    Vector2 size = MeasureTextEx(active_font(), text, (float)font_size, spacing);
    return (int)lroundf(size.x);
}
