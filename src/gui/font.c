#include "gui.h"

#include <math.h>
#include <stddef.h>

enum {
    UI_FONT_VARIANT_COUNT = 7
};
static const int g_ui_font_variant_sizes[UI_FONT_VARIANT_COUNT] = {
    20, 26, 32, 40, 48, 56, 66
};
static Font g_ui_fonts[UI_FONT_VARIANT_COUNT];
static bool g_ui_font_variant_loaded[UI_FONT_VARIANT_COUNT];
static bool g_ui_font_initialized = false;
static bool g_ui_custom_font_loaded = false;

/* Returns a small spacing value tuned for readable UI text. */
static float text_spacing_for_size(int font_size) {
    if (font_size >= 50) {
        return 0.24f;
    }
    if (font_size >= 34) {
        return 0.14f;
    }
    if (font_size >= 24) {
        return 0.08f;
    }
    return 0.0f;
}

/* Applies high-quality scaling filter to one font atlas texture. */
static void configure_font_texture(Font* font) {
    if (font == NULL || font->texture.id == 0) {
        return;
    }

    GenTextureMipmaps(&font->texture);
    SetTextureFilter(font->texture, TEXTURE_FILTER_TRILINEAR);
}

/* Loads all font variants from one path, returns true when at least one works. */
static bool try_load_font_path(const char* path) {
    bool any_loaded = false;

    if (!FileExists(path)) {
        return false;
    }

    for (int i = 0; i < UI_FONT_VARIANT_COUNT; ++i) {
        Font loaded = LoadFontEx(path, g_ui_font_variant_sizes[i], NULL, 0);
        if (loaded.texture.id == 0) {
            continue;
        }
        g_ui_fonts[i] = loaded;
        g_ui_font_variant_loaded[i] = true;
        configure_font_texture(&g_ui_fonts[i]);
        any_loaded = true;
    }

    if (any_loaded) {
        g_ui_custom_font_loaded = true;
    }
    return any_loaded;
}

/* Selects best loaded font variant for requested size to avoid blur. */
static Font active_font_for_size(int font_size, bool* out_fake_bold) {
    int best_index = -1;
    float best_score = 99999.0f;

    if (out_fake_bold != NULL) {
        *out_fake_bold = false;
    }

    if (!g_ui_font_initialized || !g_ui_custom_font_loaded) {
        return GetFontDefault();
    }

    if (font_size < 8) {
        font_size = 8;
    }

    for (int i = 0; i < UI_FONT_VARIANT_COUNT; ++i) {
        float base;
        float scale;
        float score;

        if (!g_ui_font_variant_loaded[i]) {
            continue;
        }

        base = (float)g_ui_font_variant_sizes[i];
        scale = (float)font_size / base;
        score = fabsf(scale - 1.0f);

        /* Upscaling gets extra penalty because it softens glyph edges. */
        if (scale > 1.0f) {
            score += (scale - 1.0f) * 2.3f;
        }

        if (score < best_score) {
            best_score = score;
            best_index = i;
        }
    }

    if (best_index < 0) {
        return GetFontDefault();
    }

    if (out_fake_bold != NULL && font_size >= 56) {
        *out_fake_bold = true;
    }
    return g_ui_fonts[best_index];
}

bool gui_font_init(void) {
    if (g_ui_font_initialized) {
        return true;
    }

    for (int i = 0; i < UI_FONT_VARIANT_COUNT; ++i) {
        g_ui_font_variant_loaded[i] = false;
    }
    g_ui_custom_font_loaded = false;
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

    if (!g_ui_custom_font_loaded) {
        Font fallback = GetFontDefault();
        if (fallback.texture.id != 0) {
            SetTextureFilter(fallback.texture, TEXTURE_FILTER_BILINEAR);
        }
    }

    return true;
}

void gui_font_shutdown(void) {
    if (!g_ui_font_initialized) {
        return;
    }

    for (int i = 0; i < UI_FONT_VARIANT_COUNT; ++i) {
        if (g_ui_font_variant_loaded[i]) {
            UnloadFont(g_ui_fonts[i]);
            g_ui_font_variant_loaded[i] = false;
        }
    }

    g_ui_custom_font_loaded = false;
    g_ui_font_initialized = false;
}

void gui_draw_text(const char* text, int pos_x, int pos_y, int font_size, Color color) {
    bool fake_bold = false;
    Font font = active_font_for_size(font_size, &fake_bold);
    float spacing = text_spacing_for_size(font_size);
    Vector2 pos = {(float)pos_x, (float)pos_y};

    if (fake_bold) {
        Color under = color;
        under.a = (unsigned char)((int)color.a * 28 / 100);
        DrawTextEx(font, text, (Vector2){pos.x - 1.0f, pos.y}, (float)font_size, spacing, under);
        DrawTextEx(font, text, (Vector2){pos.x + 1.0f, pos.y}, (float)font_size, spacing, under);
    }

    DrawTextEx(font, text, pos, (float)font_size, spacing, color);
}

int gui_measure_text(const char* text, int font_size) {
    bool fake_bold = false;
    Font font = active_font_for_size(font_size, &fake_bold);
    float spacing = text_spacing_for_size(font_size);
    Vector2 size = MeasureTextEx(font, text, (float)font_size, spacing);
    float extra = fake_bold ? 2.0f : 0.0f;
    return (int)lroundf(size.x + extra);
}

int gui_measure_text_height(int font_size) {
    Font font = active_font_for_size(font_size, NULL);
    float spacing = text_spacing_for_size(font_size);
    Vector2 size = MeasureTextEx(font, "Ag", (float)font_size, spacing);
    return (int)lroundf(size.y);
}
