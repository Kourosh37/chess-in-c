#include "gui.h"

#include <math.h>
#include <string.h>

#include <rlgl.h>

#include "game_state.h"

typedef struct PieceDrawStyle {
    Color fill;
    Color stroke;
} PieceDrawStyle;

static const GuiPalette g_palettes[] = {
    {
        "Classic Amber",
        {250, 244, 233, 255},
        {222, 209, 186, 255},
        {250, 250, 250, 232},
        {239, 233, 221, 232},
        {140, 118, 88, 255},
        {26, 30, 35, 255},
        {72, 79, 88, 255},
        {182, 104, 38, 255},
        {204, 124, 53, 255},
        {241, 216, 177, 255},
        {178, 127, 84, 255},
        {52, 38, 24, 255},
        {255, 208, 69, 255},
        {39, 53, 70, 145},
        {244, 244, 238, 255},
        {96, 90, 86, 255},
        {52, 54, 60, 255},
        {220, 223, 229, 255}
    },
    {
        "Emerald Velvet",
        {231, 247, 240, 255},
        {176, 216, 199, 255},
        {246, 253, 250, 235},
        {222, 241, 232, 235},
        {66, 122, 95, 255},
        {14, 39, 33, 255},
        {44, 83, 70, 255},
        {42, 138, 92, 255},
        {58, 162, 112, 255},
        {229, 246, 234, 255},
        {107, 161, 131, 255},
        {34, 66, 52, 255},
        {121, 224, 169, 255},
        {27, 84, 58, 145},
        {248, 251, 246, 255},
        {93, 121, 110, 255},
        {24, 53, 43, 255},
        {187, 223, 208, 255}
    },
    {
        "Ocean Slate",
        {228, 239, 250, 255},
        {158, 186, 212, 255},
        {244, 249, 255, 235},
        {221, 232, 246, 235},
        {58, 97, 138, 255},
        {18, 33, 52, 255},
        {53, 76, 105, 255},
        {42, 116, 170, 255},
        {58, 136, 194, 255},
        {219, 234, 247, 255},
        {93, 132, 170, 255},
        {28, 52, 79, 255},
        {122, 193, 255, 255},
        {25, 47, 80, 145},
        {246, 250, 255, 255},
        {92, 114, 141, 255},
        {27, 42, 66, 255},
        {180, 207, 235, 255}
    }
};

static int g_active_theme = 0;
enum {
    PIECE_VARIANT_NORMAL = 0,
    PIECE_VARIANT_FLIPPED = 1
};
static const char* g_piece_texture_paths[2][6] = {
    {
        "assets/pieces/staunton/wp.png",
        "assets/pieces/staunton/wn.png",
        "assets/pieces/staunton/wb.png",
        "assets/pieces/staunton/wr.png",
        "assets/pieces/staunton/wq.png",
        "assets/pieces/staunton/wk.png"
    },
    {
        "assets/pieces/staunton/bp.png",
        "assets/pieces/staunton/bn.png",
        "assets/pieces/staunton/bb.png",
        "assets/pieces/staunton/br.png",
        "assets/pieces/staunton/bq.png",
        "assets/pieces/staunton/bk.png"
    }
};
static const char* g_piece_texture_paths_flipped[2][6] = {
    {
        "assets/pieces/staunton_flipped/wp.png",
        "assets/pieces/staunton_flipped/wn.png",
        "assets/pieces/staunton_flipped/wb.png",
        "assets/pieces/staunton_flipped/wr.png",
        "assets/pieces/staunton_flipped/wq.png",
        "assets/pieces/staunton_flipped/wk.png"
    },
    {
        "assets/pieces/staunton_flipped/bp.png",
        "assets/pieces/staunton_flipped/bn.png",
        "assets/pieces/staunton_flipped/bb.png",
        "assets/pieces/staunton_flipped/br.png",
        "assets/pieces/staunton_flipped/bq.png",
        "assets/pieces/staunton_flipped/bk.png"
    }
};
static Texture2D g_piece_textures[2][2][6];
static bool g_piece_texture_ready[2][2][6];
static bool g_piece_texture_init_attempted = false;
static RenderTexture2D g_board_surface;
static bool g_board_surface_ready = false;
static int g_board_surface_size = 0;
static bool g_view_initialized = false;
static bool g_board_rotating = false;
static Side g_board_input_side = SIDE_WHITE;
static float g_board_rotation_deg = 0.0f;

/* Clamps index into available palette range. */
static int clamp_theme_index(int index) {
    int max = (int)(sizeof(g_palettes) / sizeof(g_palettes[0])) - 1;
    if (index < 0) {
        return 0;
    }
    if (index > max) {
        return max;
    }
    return index;
}

/* Returns alpha-adjusted color copy. */
static Color with_alpha(Color color, float alpha) {
    float a = alpha;
    if (a < 0.0f) {
        a = 0.0f;
    }
    if (a > 1.0f) {
        a = 1.0f;
    }
    color.a = (unsigned char)(color.a * a);
    return color;
}

/* Returns piece style for white/black side from active palette. */
static PieceDrawStyle piece_style(Side side, float alpha) {
    const GuiPalette* palette = gui_palette();
    PieceDrawStyle style;

    if (side == SIDE_WHITE) {
        style.fill = with_alpha(palette->white_piece_fill, alpha);
        style.stroke = with_alpha(palette->white_piece_stroke, alpha);
    } else {
        style.fill = with_alpha(palette->black_piece_fill, alpha);
        style.stroke = with_alpha(palette->black_piece_stroke, alpha);
    }

    return style;
}

/* Clamps integer to byte range for color channel updates. */
static unsigned char clamp_u8(int value) {
    if (value < 0) {
        return 0;
    }
    if (value > 255) {
        return 255;
    }
    return (unsigned char)value;
}

/* Adjusts brightness and alpha of a color. */
static Color adjust_color(Color color, int delta, float alpha_scale) {
    Color out = color;
    int a = (int)((float)color.a * alpha_scale);

    out.r = clamp_u8((int)color.r + delta);
    out.g = clamp_u8((int)color.g + delta);
    out.b = clamp_u8((int)color.b + delta);
    out.a = clamp_u8(a);
    return out;
}

/* Draws a shared base pedestal for all piece types. */
static void draw_piece_foundation(Vector2 c, float s, Color fill, Color stroke, float alpha) {
    Color shadow = with_alpha(BLACK, 0.24f * alpha);
    Color rim_light = adjust_color(fill, 18, alpha);

    DrawEllipse((int)c.x, (int)(c.y + s * 0.36f), (int)(s * 0.32f), (int)(s * 0.10f), shadow);

    DrawEllipse((int)c.x, (int)(c.y + s * 0.27f), (int)(s * 0.34f), (int)(s * 0.11f), stroke);
    DrawEllipse((int)c.x, (int)(c.y + s * 0.27f), (int)(s * 0.30f), (int)(s * 0.08f), fill);
    DrawEllipse((int)(c.x - s * 0.05f), (int)(c.y + s * 0.25f), (int)(s * 0.18f), (int)(s * 0.04f), rim_light);
}

/* Draws a soft glossy highlight spot to fake 3D lighting. */
static void draw_piece_gloss(Vector2 c, float radius, Color fill, float alpha) {
    Color gloss = with_alpha(adjust_color(fill, 42, 1.0f), 0.45f * alpha);
    DrawCircleV((Vector2){c.x - radius * 0.34f, c.y - radius * 0.30f}, radius * 0.34f, gloss);
}

/* Attempts one-time loading of local piece textures for realistic rendering. */
static void ensure_piece_textures_loaded(void) {
    if (g_piece_texture_init_attempted) {
        return;
    }

    g_piece_texture_init_attempted = true;
    memset(g_piece_textures, 0, sizeof(g_piece_textures));
    memset(g_piece_texture_ready, 0, sizeof(g_piece_texture_ready));

    for (int side = 0; side < 2; ++side) {
        for (int piece = 0; piece < 6; ++piece) {
            const char* normal_path = g_piece_texture_paths[side][piece];
            const char* flipped_path = g_piece_texture_paths_flipped[side][piece];

            if (FileExists(normal_path)) {
                g_piece_textures[side][PIECE_VARIANT_NORMAL][piece] = LoadTexture(normal_path);
                g_piece_texture_ready[side][PIECE_VARIANT_NORMAL][piece] =
                    (g_piece_textures[side][PIECE_VARIANT_NORMAL][piece].id != 0);
            }

            if (FileExists(flipped_path)) {
                g_piece_textures[side][PIECE_VARIANT_FLIPPED][piece] = LoadTexture(flipped_path);
                g_piece_texture_ready[side][PIECE_VARIANT_FLIPPED][piece] =
                    (g_piece_textures[side][PIECE_VARIANT_FLIPPED][piece].id != 0);
            }
        }
    }
}

/* Draws a realistic piece texture if loaded, returns false when unavailable. */
static bool draw_piece_texture(PieceType piece,
                               Side side,
                               Vector2 center,
                               float size,
                               float alpha,
                               float rotation_deg,
                               bool use_flipped_variant,
                               float shadow_dir) {
    Texture2D tex;
    Rectangle src;
    Rectangle dst;
    Vector2 origin;
    float ratio;
    int variant = use_flipped_variant ? PIECE_VARIANT_FLIPPED : PIECE_VARIANT_NORMAL;
    float final_rotation = rotation_deg;

    if (side < SIDE_WHITE || side > SIDE_BLACK) {
        return false;
    }
    if (piece < PIECE_PAWN || piece > PIECE_KING) {
        return false;
    }

    ensure_piece_textures_loaded();
    if (!g_piece_texture_ready[side][variant][piece]) {
        if (variant == PIECE_VARIANT_FLIPPED && g_piece_texture_ready[side][PIECE_VARIANT_NORMAL][piece]) {
            variant = PIECE_VARIANT_NORMAL;
            final_rotation += 180.0f;
        } else {
            return false;
        }
    }

    tex = g_piece_textures[side][variant][piece];
    if (tex.width <= 0 || tex.height <= 0) {
        return false;
    }

    ratio = (float)tex.width / (float)tex.height;
    dst.height = size * 0.94f;
    dst.width = dst.height * ratio;
    if (dst.width > size * 0.94f) {
        dst.width = size * 0.94f;
        dst.height = dst.width / ratio;
    }
    dst.x = center.x;
    dst.y = center.y;

    origin = (Vector2){dst.width * 0.5f, dst.height * 0.5f};
    src = (Rectangle){0.0f, 0.0f, (float)tex.width, (float)tex.height};

    DrawEllipse((int)center.x,
                (int)(center.y + size * 0.34f * shadow_dir),
                (int)(size * 0.29f),
                (int)(size * 0.075f),
                with_alpha(BLACK, 0.16f * alpha));
    DrawTexturePro(tex, src, dst, origin, final_rotation, with_alpha(WHITE, alpha));
    return true;
}

/* Converts board square index to rectangle in pixel coordinates. */
static Rectangle square_rect(const GuiPlayLayout* layout, int square) {
    float file = (float)(square & 7);
    float rank = (float)(square >> 3);

    return (Rectangle){
        layout->board.x + file * layout->square_size,
        layout->board.y + (7.0f - rank) * layout->square_size,
        layout->square_size,
        layout->square_size
    };
}

/* Converts board square index to center point in pixel coordinates. */
static Vector2 square_center(const GuiPlayLayout* layout, int square) {
    Rectangle rect = square_rect(layout, square);
    return (Vector2){
        rect.x + rect.width * 0.5f,
        rect.y + rect.height * 0.5f
    };
}

/* Returns board-view target side based on mode and local player color. */
static Side board_target_side(const ChessApp* app) {
    if (app->mode == MODE_LOCAL) {
        return app->position.side_to_move;
    }
    if (app->mode == MODE_ONLINE) {
        return app->human_side;
    }
    return SIDE_WHITE;
}

/* Eases board rotation angle toward target side for smooth flips. */
static void update_board_rotation(const ChessApp* app) {
    float dt = GetFrameTime();
    Side target_side = board_target_side(app);
    float target_deg = (target_side == SIDE_WHITE) ? 0.0f : 180.0f;
    float lerp_factor;
    float diff;

    if (!g_view_initialized) {
        g_view_initialized = true;
        g_board_rotation_deg = target_deg;
        g_board_input_side = target_side;
        g_board_rotating = false;
        return;
    }

    diff = target_deg - g_board_rotation_deg;
    while (diff > 180.0f) {
        diff -= 360.0f;
    }
    while (diff < -180.0f) {
        diff += 360.0f;
    }

    lerp_factor = 1.0f - expf(-8.0f * dt);
    if (lerp_factor < 0.10f) {
        lerp_factor = 0.10f;
    }
    if (lerp_factor > 0.38f) {
        lerp_factor = 0.38f;
    }

    if (fabsf(diff) <= 0.35f) {
        g_board_rotation_deg = target_deg;
        g_board_input_side = target_side;
        g_board_rotating = false;
    } else {
        g_board_rotation_deg += diff * lerp_factor;
        while (g_board_rotation_deg >= 360.0f) {
            g_board_rotation_deg -= 360.0f;
        }
        while (g_board_rotation_deg < 0.0f) {
            g_board_rotation_deg += 360.0f;
        }

        g_board_rotating = true;
    }
}

/* Allocates or resizes board render surface to match current board pixel size. */
static bool ensure_board_surface(int size) {
    if (size <= 0) {
        return false;
    }

    if (g_board_surface_ready && g_board_surface_size != size) {
        UnloadRenderTexture(g_board_surface);
        memset(&g_board_surface, 0, sizeof(g_board_surface));
        g_board_surface_ready = false;
        g_board_surface_size = 0;
    }

    if (!g_board_surface_ready) {
        g_board_surface = LoadRenderTexture(size, size);
        if (g_board_surface.texture.id == 0) {
            memset(&g_board_surface, 0, sizeof(g_board_surface));
            return false;
        }
        SetTextureFilter(g_board_surface.texture, TEXTURE_FILTER_BILINEAR);
        g_board_surface_ready = true;
        g_board_surface_size = size;
    }

    return true;
}

/* Finds one attacker square delivering check to given king square, if any. */
static bool find_check_attacker_square(const Position* pos, Side checked_side, int king_square, int* out_square) {
    Side attacker_side = (checked_side == SIDE_WHITE) ? SIDE_BLACK : SIDE_WHITE;
    Bitboard king_bb;

    if (king_square < 0 || king_square >= BOARD_SQUARES || out_square == NULL) {
        return false;
    }

    king_bb = 1ULL << king_square;

    for (int square = 0; square < BOARD_SQUARES; ++square) {
        Bitboard bb = 1ULL << square;

        if ((pos->pieces[attacker_side][PIECE_PAWN] & bb) != 0ULL &&
            (engine_get_pawn_attacks(attacker_side, square) & king_bb) != 0ULL) {
            *out_square = square;
            return true;
        }
    }

    for (int square = 0; square < BOARD_SQUARES; ++square) {
        Bitboard bb = 1ULL << square;

        if ((pos->pieces[attacker_side][PIECE_KNIGHT] & bb) != 0ULL &&
            (engine_get_knight_attacks(square) & king_bb) != 0ULL) {
            *out_square = square;
            return true;
        }
    }

    for (int square = 0; square < BOARD_SQUARES; ++square) {
        Bitboard bb = 1ULL << square;

        if ((pos->pieces[attacker_side][PIECE_BISHOP] & bb) != 0ULL &&
            (engine_get_bishop_attacks(square, pos->all_occupied) & king_bb) != 0ULL) {
            *out_square = square;
            return true;
        }
    }

    for (int square = 0; square < BOARD_SQUARES; ++square) {
        Bitboard bb = 1ULL << square;

        if ((pos->pieces[attacker_side][PIECE_ROOK] & bb) != 0ULL &&
            (engine_get_rook_attacks(square, pos->all_occupied) & king_bb) != 0ULL) {
            *out_square = square;
            return true;
        }
    }

    for (int square = 0; square < BOARD_SQUARES; ++square) {
        Bitboard bb = 1ULL << square;

        if ((pos->pieces[attacker_side][PIECE_QUEEN] & bb) != 0ULL) {
            Bitboard queen_attacks = engine_get_bishop_attacks(square, pos->all_occupied) |
                                     engine_get_rook_attacks(square, pos->all_occupied);
            if ((queen_attacks & king_bb) != 0ULL) {
                *out_square = square;
                return true;
            }
        }
    }

    for (int square = 0; square < BOARD_SQUARES; ++square) {
        Bitboard bb = 1ULL << square;

        if ((pos->pieces[attacker_side][PIECE_KING] & bb) != 0ULL &&
            (engine_get_king_attacks(square) & king_bb) != 0ULL) {
            *out_square = square;
            return true;
        }
    }

    return false;
}

/* Returns true when square is a legal destination for current selection. */
static bool is_target_for_selected(const ChessApp* app, int square) {
    if (app->selected_square < 0) {
        return false;
    }

    for (int i = 0; i < app->legal_moves.count; ++i) {
        if (app->legal_moves.moves[i].from == app->selected_square &&
            app->legal_moves.moves[i].to == square) {
            return true;
        }
    }

    return false;
}

/* Portable bit count for captured-piece view. */
static int bit_count(Bitboard bb) {
    int count = 0;
    while (bb != 0ULL) {
        bb &= (bb - 1ULL);
        count++;
    }
    return count;
}

/* Initial material counts per piece type for capture tracking. */
static int initial_piece_count(PieceType piece) {
    if (piece == PIECE_PAWN) {
        return 8;
    }
    if (piece == PIECE_KNIGHT || piece == PIECE_BISHOP || piece == PIECE_ROOK) {
        return 2;
    }
    if (piece == PIECE_QUEEN) {
        return 1;
    }
    return 0;
}

/* Draws a rounded rectangle with a subtle border/shadow treatment. */
static void draw_card(Rectangle rect, Color fill, Color border) {
    DrawRectangleRounded((Rectangle){rect.x + 3.0f, rect.y + 4.0f, rect.width, rect.height},
                         0.09f,
                         8,
                         with_alpha(BLACK, 0.10f));
    DrawRectangleRounded(rect, 0.09f, 8, fill);
    DrawRectangleRoundedLinesEx(rect, 0.09f, 8, 1.2f, border);
}

/* Draws a piece with vector shapes so no sprite assets are required. */
static void draw_piece_shape(PieceType piece,
                             Side side,
                             Vector2 center,
                             float size,
                             float alpha,
                             float rotation_deg,
                             bool use_flipped_variant,
                             float shadow_dir) {
    PieceDrawStyle style = piece_style(side, alpha);
    Color fill = adjust_color(style.fill, 0, alpha);
    Color fill_light = adjust_color(style.fill, 24, alpha);
    Color fill_dark = adjust_color(style.fill, -22, alpha);
    Color stroke = adjust_color(style.stroke, 0, alpha);
    Color stroke_dark = adjust_color(style.stroke, -20, alpha);
    Color shade = with_alpha(BLACK, 0.16f * alpha);
    float s = size;
    bool compact = s < 30.0f;
    bool use_transform = fabsf(rotation_deg) > 0.05f;
    Vector2 c = center;
    Vector2 shadow_offset = {2.2f, 2.0f};

    if (draw_piece_texture(piece, side, center, size, alpha, rotation_deg, use_flipped_variant, shadow_dir)) {
        return;
    }

    if (use_transform) {
        rlPushMatrix();
        rlTranslatef(center.x, center.y, 0.0f);
        rlRotatef(rotation_deg, 0.0f, 0.0f, 1.0f);
        rlTranslatef(-center.x, -center.y, 0.0f);
    }

#define PIECE_RETURN()            \
    do {                          \
        if (use_transform) {      \
            rlPopMatrix();        \
        }                         \
        return;                   \
    } while (0)

    DrawEllipse((int)(c.x + shadow_offset.x),
                (int)(c.y + s * 0.35f + shadow_offset.y),
                (int)(s * 0.30f),
                (int)(s * 0.08f),
                with_alpha(BLACK, 0.16f * alpha));

    draw_piece_foundation(c, s, fill_dark, stroke, alpha);

    c.x += shadow_offset.x;
    c.y += shadow_offset.y;
    DrawCircleV(c, s * 0.13f, with_alpha(BLACK, 0.14f * alpha));
    c = center;

    if (piece == PIECE_PAWN) {
        DrawCircleV((Vector2){c.x, c.y - s * 0.21f}, s * 0.18f, stroke);
        DrawCircleV((Vector2){c.x, c.y - s * 0.21f}, s * 0.15f, fill);
        DrawRectangleRounded((Rectangle){c.x - s * 0.20f, c.y - s * 0.01f, s * 0.40f, s * 0.30f},
                             0.45f,
                             8,
                             stroke);
        DrawRectangleRounded((Rectangle){c.x - s * 0.16f, c.y + s * 0.01f, s * 0.32f, s * 0.26f},
                             0.45f,
                             8,
                             fill_dark);
        DrawEllipse((int)c.x, (int)(c.y + s * 0.11f), (int)(s * 0.10f), (int)(s * 0.16f), fill_light);
        DrawRectangleRounded((Rectangle){c.x - s * 0.28f, c.y + s * 0.18f, s * 0.56f, s * 0.10f},
                             0.35f,
                             8,
                             stroke_dark);
        DrawRectangleRounded((Rectangle){c.x - s * 0.24f, c.y + s * 0.20f, s * 0.48f, s * 0.06f},
                             0.35f, 8, fill);
        draw_piece_gloss((Vector2){c.x, c.y - s * 0.23f}, s * 0.26f, fill, alpha);
        PIECE_RETURN();
    }

    if (piece == PIECE_KNIGHT) {
        DrawTriangle((Vector2){c.x - s * 0.24f, c.y + s * 0.25f},
                     (Vector2){c.x + s * 0.22f, c.y + s * 0.25f},
                     (Vector2){c.x - s * 0.08f, c.y - s * 0.33f},
                     stroke);
        DrawTriangle((Vector2){c.x - s * 0.20f, c.y + s * 0.23f},
                     (Vector2){c.x + s * 0.18f, c.y + s * 0.23f},
                     (Vector2){c.x - s * 0.06f, c.y - s * 0.28f},
                     fill_dark);
        DrawTriangle((Vector2){c.x - s * 0.12f, c.y - s * 0.14f},
                     (Vector2){c.x + s * 0.09f, c.y - s * 0.02f},
                     (Vector2){c.x - s * 0.02f, c.y - s * 0.28f},
                     fill_light);
        DrawTriangle((Vector2){c.x - s * 0.12f, c.y - s * 0.34f},
                     (Vector2){c.x - s * 0.04f, c.y - s * 0.34f},
                     (Vector2){c.x - s * 0.08f, c.y - s * 0.44f},
                     stroke_dark);
        DrawCircleV((Vector2){c.x + s * 0.03f, c.y - s * 0.09f}, s * 0.042f, stroke_dark);
        DrawCircleV((Vector2){c.x + s * 0.03f, c.y - s * 0.09f}, s * 0.028f, fill);
        DrawCircleV((Vector2){c.x + s * 0.06f, c.y - s * 0.11f}, s * 0.008f, stroke);
        DrawLineEx((Vector2){c.x - s * 0.20f, c.y - s * 0.05f},
                   (Vector2){c.x - s * 0.03f, c.y + s * 0.16f},
                   fmaxf(1.0f, s * 0.03f),
                   shade);
        DrawRectangleRounded((Rectangle){c.x - s * 0.30f, c.y + s * 0.20f, s * 0.58f, s * 0.10f},
                             0.35f,
                             8,
                             stroke_dark);
        DrawRectangleRounded((Rectangle){c.x - s * 0.25f, c.y + s * 0.22f, s * 0.50f, s * 0.06f},
                             0.35f,
                             8,
                             fill);
        if (!compact) {
            draw_piece_gloss((Vector2){c.x - s * 0.02f, c.y - s * 0.10f}, s * 0.26f, fill, alpha);
        }
        PIECE_RETURN();
    }

    if (piece == PIECE_BISHOP) {
        DrawEllipse((int)c.x, (int)(c.y - s * 0.06f), (int)(s * 0.22f), (int)(s * 0.30f), stroke);
        DrawEllipse((int)c.x, (int)(c.y - s * 0.06f), (int)(s * 0.18f), (int)(s * 0.26f), fill_dark);
        DrawEllipse((int)(c.x - s * 0.03f), (int)(c.y - s * 0.10f), (int)(s * 0.08f), (int)(s * 0.16f), fill_light);
        DrawCircleV((Vector2){c.x, c.y - s * 0.34f}, s * 0.11f, stroke);
        DrawCircleV((Vector2){c.x, c.y - s * 0.34f}, s * 0.08f, fill);
        DrawLineEx((Vector2){c.x - s * 0.07f, c.y - s * 0.22f},
                   (Vector2){c.x + s * 0.08f, c.y - s * 0.04f},
                   fmaxf(1.0f, s * 0.04f),
                   stroke_dark);
        DrawRectangleRounded((Rectangle){c.x - s * 0.28f, c.y + s * 0.20f, s * 0.56f, s * 0.10f},
                             0.35f,
                             8,
                             stroke_dark);
        DrawRectangleRounded((Rectangle){c.x - s * 0.24f, c.y + s * 0.22f, s * 0.48f, s * 0.06f},
                             0.35f,
                             8,
                             fill);
        if (!compact) {
            draw_piece_gloss((Vector2){c.x, c.y - s * 0.15f}, s * 0.24f, fill, alpha);
        }
        PIECE_RETURN();
    }

    if (piece == PIECE_ROOK) {
        DrawRectangleRounded((Rectangle){c.x - s * 0.25f, c.y - s * 0.14f, s * 0.50f, s * 0.40f},
                             0.16f,
                             8,
                             stroke);
        DrawRectangleRounded((Rectangle){c.x - s * 0.21f, c.y - s * 0.11f, s * 0.42f, s * 0.34f},
                             0.16f,
                             8,
                             fill_dark);
        DrawRectangleRounded((Rectangle){c.x - s * 0.13f, c.y - s * 0.10f, s * 0.12f, s * 0.31f}, 0.16f, 6, fill_light);
        for (int i = -1; i <= 1; ++i) {
            DrawRectangleRounded((Rectangle){c.x + (float)i * s * 0.14f - s * 0.05f, c.y - s * 0.31f, s * 0.10f, s * 0.14f},
                                 0.2f,
                                 6,
                                 stroke_dark);
            DrawRectangleRounded((Rectangle){c.x + (float)i * s * 0.14f - s * 0.04f, c.y - s * 0.29f, s * 0.08f, s * 0.10f},
                                 0.2f,
                                 6,
                                 fill);
        }
        DrawRectangleRounded((Rectangle){c.x - s * 0.31f, c.y + s * 0.20f, s * 0.62f, s * 0.10f},
                             0.35f,
                             8,
                             stroke_dark);
        DrawRectangleRounded((Rectangle){c.x - s * 0.27f, c.y + s * 0.22f, s * 0.54f, s * 0.06f},
                             0.35f,
                             8,
                             fill);
        if (!compact) {
            draw_piece_gloss((Vector2){c.x, c.y - s * 0.08f}, s * 0.26f, fill, alpha);
        }
        PIECE_RETURN();
    }

    if (piece == PIECE_QUEEN) {
        DrawRectangleRounded((Rectangle){c.x - s * 0.23f, c.y - s * 0.07f, s * 0.46f, s * 0.35f},
                             0.30f,
                             8,
                             stroke);
        DrawRectangleRounded((Rectangle){c.x - s * 0.18f, c.y - s * 0.04f, s * 0.36f, s * 0.30f},
                             0.30f,
                             8,
                             fill_dark);
        DrawEllipse((int)(c.x - s * 0.03f), (int)(c.y + s * 0.02f), (int)(s * 0.08f), (int)(s * 0.17f), fill_light);
        DrawTriangle((Vector2){c.x - s * 0.22f, c.y - s * 0.08f},
                     (Vector2){c.x + s * 0.22f, c.y - s * 0.08f},
                     (Vector2){c.x, c.y - s * 0.35f},
                     stroke_dark);
        DrawTriangle((Vector2){c.x - s * 0.17f, c.y - s * 0.09f},
                     (Vector2){c.x + s * 0.17f, c.y - s * 0.09f},
                     (Vector2){c.x, c.y - s * 0.30f},
                     fill);
        {
            const float orb_x[5] = {-0.19f, -0.10f, 0.0f, 0.10f, 0.19f};
            const float orb_y[5] = {-0.29f, -0.35f, -0.39f, -0.35f, -0.29f};
            for (int i = 0; i < 5; ++i) {
                float r = (i == 2) ? s * 0.055f : s * 0.048f;
                DrawCircleV((Vector2){c.x + s * orb_x[i], c.y + s * orb_y[i]}, r, stroke_dark);
                DrawCircleV((Vector2){c.x + s * orb_x[i], c.y + s * orb_y[i]}, r * 0.62f, fill_light);
            }
        }
        DrawRectangleRounded((Rectangle){c.x - s * 0.32f, c.y + s * 0.20f, s * 0.64f, s * 0.10f},
                             0.35f,
                             8,
                             stroke_dark);
        DrawRectangleRounded((Rectangle){c.x - s * 0.28f, c.y + s * 0.22f, s * 0.56f, s * 0.06f},
                             0.35f,
                             8,
                             fill);
        if (!compact) {
            draw_piece_gloss((Vector2){c.x + s * 0.02f, c.y - s * 0.04f}, s * 0.28f, fill, alpha);
        }
        PIECE_RETURN();
    }

    if (piece == PIECE_KING) {
        DrawRectangleRounded((Rectangle){c.x - s * 0.20f, c.y - s * 0.06f, s * 0.40f, s * 0.34f},
                             0.30f,
                             8,
                             stroke);
        DrawRectangleRounded((Rectangle){c.x - s * 0.16f, c.y - s * 0.03f, s * 0.32f, s * 0.28f},
                             0.30f,
                             8,
                             fill_dark);
        DrawEllipse((int)(c.x - s * 0.02f), (int)(c.y + s * 0.02f), (int)(s * 0.08f), (int)(s * 0.14f), fill_light);
        DrawRectangleRounded((Rectangle){c.x - s * 0.18f, c.y - s * 0.18f, s * 0.36f, s * 0.07f},
                             0.20f, 6, stroke_dark);
        DrawRectangleRounded((Rectangle){c.x - s * 0.14f, c.y - s * 0.17f, s * 0.28f, s * 0.05f},
                             0.20f, 6, fill);
        DrawRectangleRounded((Rectangle){c.x - s * 0.05f, c.y - s * 0.36f, s * 0.10f, s * 0.18f},
                             0.25f,
                             6,
                             stroke_dark);
        DrawRectangleRounded((Rectangle){c.x - s * 0.03f, c.y - s * 0.34f, s * 0.06f, s * 0.15f},
                             0.25f,
                             6,
                             fill);
        DrawRectangleRounded((Rectangle){c.x - s * 0.14f, c.y - s * 0.30f, s * 0.28f, s * 0.08f},
                             0.25f,
                             6,
                             stroke_dark);
        DrawRectangleRounded((Rectangle){c.x - s * 0.12f, c.y - s * 0.29f, s * 0.24f, s * 0.06f},
                             0.25f,
                             6,
                             fill_light);
        DrawRectangleRounded((Rectangle){c.x - s * 0.32f, c.y + s * 0.20f, s * 0.64f, s * 0.10f},
                             0.35f,
                             8,
                             stroke_dark);
        DrawRectangleRounded((Rectangle){c.x - s * 0.28f, c.y + s * 0.22f, s * 0.56f, s * 0.06f},
                             0.35f,
                             8,
                             fill);
        if (!compact) {
            draw_piece_gloss((Vector2){c.x, c.y - s * 0.02f}, s * 0.27f, fill, alpha);
        }
    } else {
        /* Fallback silhouette for unknown piece values. */
        DrawCircleV(c, s * 0.18f, stroke);
        DrawCircleV(c, s * 0.14f, fill);
    }

    if (use_transform) {
        rlPopMatrix();
    }

#undef PIECE_RETURN
}

/* Returns outer coordinate frame rect and writes side-band thickness. */
static Rectangle coordinate_frame_rect(const GuiPlayLayout* layout, float* out_band) {
    float band = layout->square_size * 0.32f;

    if (band < 18.0f) {
        band = 18.0f;
    }
    if (band > 26.0f) {
        band = 26.0f;
    }

    if (out_band != NULL) {
        *out_band = band;
    }

    return (Rectangle){
        layout->board.x - band,
        layout->board.y - band,
        layout->board.width + band * 2.0f,
        layout->board.height + band * 2.0f
    };
}

/* Draws one shared frame around board + coordinates. */
static void draw_coordinate_frame(const GuiPlayLayout* layout) {
    const GuiPalette* palette = gui_palette();
    Rectangle frame = coordinate_frame_rect(layout, NULL);
    Rectangle inner = {frame.x + 3.0f, frame.y + 3.0f, frame.width - 6.0f, frame.height - 6.0f};

    DrawRectangleRounded((Rectangle){frame.x + 3.0f, frame.y + 4.0f, frame.width, frame.height},
                         0.06f,
                         8,
                         with_alpha(BLACK, 0.12f));
    DrawRectangleRounded(frame, 0.06f, 8, with_alpha(palette->panel, 0.90f));
    DrawRectangleRoundedLinesEx(frame, 0.06f, 8, 2.0f, with_alpha(palette->board_outline, 0.95f));
    DrawRectangleRoundedLinesEx(inner, 0.06f, 8, 1.0f, with_alpha(palette->accent, 0.38f));
}

/* Draws board coordinates within outer frame bands (no per-cell badges). */
static void draw_coordinates(const GuiPlayLayout* layout) {
    const GuiPalette* palette = gui_palette();
    float band;
    Rectangle frame = coordinate_frame_rect(layout, &band);
    int font_size = (int)(band * 0.62f);
    int text_h;
    int top_y;
    int bottom_y;

    if (font_size < 16) {
        font_size = 16;
    }
    if (font_size > 24) {
        font_size = 24;
    }

    text_h = gui_measure_text_height(font_size);
    top_y = (int)(frame.y + (band - (float)text_h) * 0.5f);
    bottom_y = (int)(frame.y + frame.height - band + (band - (float)text_h) * 0.5f);

    for (int file = 0; file < 8; ++file) {
        char text[2] = {(char)('a' + file), '\0'};
        float cx = layout->board.x + ((float)file + 0.5f) * layout->square_size;
        int text_w = gui_measure_text(text, font_size);
        int x = (int)(cx - (float)text_w * 0.5f);

        gui_draw_text(text, x, top_y, font_size, palette->text_primary);
        gui_draw_text(text, x, bottom_y, font_size, palette->text_primary);
    }

    for (int row = 0; row < 8; ++row) {
        char text[2] = {(char)('8' - row), '\0'};
        float cy = layout->board.y + ((float)row + 0.5f) * layout->square_size;
        int text_w = gui_measure_text(text, font_size);
        int y = (int)(cy - (float)text_h * 0.5f);
        int left_x = (int)(frame.x + (band - (float)text_w) * 0.5f);
        int right_x = (int)(frame.x + frame.width - band + (band - (float)text_w) * 0.5f);

        gui_draw_text(text, left_x, y, font_size, palette->text_primary);
        gui_draw_text(text, right_x, y, font_size, palette->text_primary);
    }
}

/* Draws captured pieces panel for one capturer side. */
static void draw_captured_group(const Position* pos, Rectangle rect, Side capturer) {
    const GuiPalette* palette = gui_palette();
    Side captured_side = (capturer == SIDE_WHITE) ? SIDE_BLACK : SIDE_WHITE;
    const char* title = (capturer == SIDE_WHITE) ? "White Captures" : "Black Captures";
    const PieceType order[] = {PIECE_QUEEN, PIECE_ROOK, PIECE_BISHOP, PIECE_KNIGHT, PIECE_PAWN};
    float icon_size = rect.height * 0.30f;
    float gap;
    float x = rect.x + 14.0f;
    float y = rect.y + 44.0f;

    if (icon_size < 28.0f) {
        icon_size = 28.0f;
    }
    if (icon_size > 40.0f) {
        icon_size = 40.0f;
    }
    gap = icon_size * 0.22f;

    draw_card(rect, palette->panel_alt, with_alpha(palette->panel_border, 0.9f));
    gui_draw_text(title, (int)rect.x + 12, (int)rect.y + 10, 22, palette->text_primary);

    for (int i = 0; i < (int)(sizeof(order) / sizeof(order[0])); ++i) {
        PieceType piece = order[i];
        int total = initial_piece_count(piece);
        int current = bit_count(pos->pieces[captured_side][piece]);
        int captured = total - current;

        for (int n = 0; n < captured; ++n) {
            if (x + icon_size > rect.x + rect.width - 14.0f) {
                x = rect.x + 14.0f;
                y += icon_size + gap;
            }

            draw_piece_shape(piece,
                             captured_side,
                             (Vector2){x + icon_size * 0.5f, y + icon_size * 0.5f},
                             icon_size,
                             0.95f,
                             0.0f,
                             false,
                             1.0f);
            x += icon_size + gap;
        }
    }
}

GuiPlayLayout gui_get_play_layout(void) {
    GuiPlayLayout layout;
    float sw = (float)GetScreenWidth();
    float sh = (float)GetScreenHeight();
    float min_dim = (sw < sh) ? sw : sh;
    float margin = min_dim * 0.022f;
    float coord_padding = min_dim * 0.047f;
    float sidebar_width;
    float board_width_space;
    float board_height_space;
    float board_size;
    int square_int;

    if (margin < 16.0f) {
        margin = 16.0f;
    }

    if (coord_padding < 26.0f) {
        coord_padding = 26.0f;
    }
    if (coord_padding > 40.0f) {
        coord_padding = 40.0f;
    }

    sidebar_width = sw * 0.26f;
    if (sidebar_width < 260.0f) {
        sidebar_width = 260.0f;
    }
    if (sidebar_width > 360.0f) {
        sidebar_width = 360.0f;
    }

    layout.sidebar = (Rectangle){
        sw - margin - sidebar_width,
        margin,
        sidebar_width,
        sh - margin * 2.0f
    };

    board_width_space = layout.sidebar.x - margin * 2.0f;
    board_height_space = sh - margin * 2.0f;
    board_width_space -= coord_padding * 2.0f;
    board_height_space -= coord_padding * 2.0f;

    board_size = (board_width_space < board_height_space) ? board_width_space : board_height_space;
    square_int = (int)(board_size / 8.0f);
    if (square_int < 48) {
        square_int = 48;
    }
    board_size = (float)(square_int * 8);

    layout.square_size = (float)square_int;
    layout.board = (Rectangle){
        margin + coord_padding + (board_width_space - board_size) * 0.5f,
        margin + coord_padding + (board_height_space - board_size) * 0.5f,
        board_size,
        board_size
    };

    return layout;
}

int gui_theme_count(void) {
    return (int)(sizeof(g_palettes) / sizeof(g_palettes[0]));
}

const char* gui_theme_name(int index) {
    int clamped = clamp_theme_index(index);
    return g_palettes[clamped].name;
}

int gui_get_active_theme(void) {
    return g_active_theme;
}

void gui_set_active_theme(int index) {
    g_active_theme = clamp_theme_index(index);
}

const GuiPalette* gui_palette(void) {
    return &g_palettes[g_active_theme];
}

void gui_draw_background(void) {
    const GuiPalette* palette = gui_palette();
    float sw = (float)GetScreenWidth();
    float sh = (float)GetScreenHeight();
    float orb = ((sw < sh) ? sw : sh) * 0.22f;

    DrawRectangleGradientV(0, 0, (int)sw, (int)sh, palette->bg_top, palette->bg_bottom);
    DrawCircleV((Vector2){sw * 0.10f, sh * 0.12f}, orb, with_alpha(palette->accent, 0.08f));
    DrawCircleV((Vector2){sw * 0.88f, sh * 0.86f}, orb * 1.1f, with_alpha(palette->accent_hover, 0.08f));
    DrawCircleV((Vector2){sw * 0.78f, sh * 0.20f}, orb * 0.65f, with_alpha(palette->panel_border, 0.07f));
}

int gui_square_from_mouse(Vector2 mouse) {
    GuiPlayLayout layout = gui_get_play_layout();
    int file;
    int rank_from_top;
    int rank;

    if (!CheckCollisionPointRec(mouse, layout.board)) {
        return -1;
    }

    file = (int)((mouse.x - layout.board.x) / layout.square_size);
    rank_from_top = (int)((mouse.y - layout.board.y) / layout.square_size);

    if (file < 0 || file > 7 || rank_from_top < 0 || rank_from_top > 7) {
        return -1;
    }

    if (g_board_input_side == SIDE_BLACK) {
        file = 7 - file;
        rank = rank_from_top;
    } else {
        rank = 7 - rank_from_top;
    }
    return (rank << 3) | file;
}

bool gui_board_is_rotating(void) {
    return g_board_rotating;
}

void gui_draw_board(const ChessApp* app) {
    const GuiPalette* palette = gui_palette();
    GuiPlayLayout layout = gui_get_play_layout();
    GuiPlayLayout board_surface_layout;
    Rectangle info_card = {
        layout.sidebar.x,
        layout.sidebar.y,
        layout.sidebar.width,
        layout.sidebar.height
    };
    int board_px = (int)layout.board.width;
    bool in_check;
    int checked_king_square = -1;
    int check_attacker_square = -1;
    float piece_size = layout.square_size * 0.88f;
    bool use_flipped_pieces;
    float piece_rotation;
    float piece_shadow_dir;

    update_board_rotation(app);
    draw_card(info_card, with_alpha(palette->panel, 0.92f), palette->panel_border);
    draw_coordinate_frame(&layout);
    use_flipped_pieces = (board_target_side(app) == SIDE_BLACK);
    piece_rotation = 0.0f;
    piece_shadow_dir = use_flipped_pieces ? -1.0f : 1.0f;

    if (!ensure_board_surface(board_px)) {
        return;
    }

    board_surface_layout = layout;
    board_surface_layout.board.x = 0.0f;
    board_surface_layout.board.y = 0.0f;

    in_check = engine_in_check(&app->position, app->position.side_to_move);
    if (in_check) {
        checked_king_square = engine_find_king_square(&app->position, app->position.side_to_move);
        find_check_attacker_square(&app->position,
                                   app->position.side_to_move,
                                   checked_king_square,
                                   &check_attacker_square);
    }

    BeginTextureMode(g_board_surface);
    ClearBackground(BLANK);

    for (int rank = 7; rank >= 0; --rank) {
        for (int file = 0; file < 8; ++file) {
            int square = (rank << 3) | file;
            Rectangle rect = square_rect(&board_surface_layout, square);
            bool light = ((rank + file) & 1) == 0;
            Color sq_color = light ? palette->board_light : palette->board_dark;

            if (square == app->last_move_from || square == app->last_move_to) {
                sq_color = ColorAlphaBlend(sq_color, with_alpha(palette->accent, 0.20f), WHITE);
            }

            DrawRectangleRec(rect, sq_color);

            if (square == app->selected_square) {
                DrawRectangleLinesEx(rect, 3.0f, palette->selection);
            } else if (is_target_for_selected(app, square)) {
                DrawCircleV((Vector2){rect.x + rect.width * 0.5f, rect.y + rect.height * 0.5f},
                            rect.width * 0.15f,
                            palette->legal_hint);
            }
        }
    }

    for (int square = 0; square < BOARD_SQUARES; ++square) {
        Side side;
        PieceType piece;
        Vector2 center;

        if (!position_piece_at(&app->position, square, &side, &piece)) {
            continue;
        }

        if (app->move_animating && square == app->move_anim_to) {
            continue;
        }

        center = square_center(&board_surface_layout, square);
        draw_piece_shape(piece,
                         side,
                         center,
                         piece_size,
                         1.0f,
                         piece_rotation,
                         use_flipped_pieces,
                         piece_shadow_dir);
    }

    if (app->move_animating) {
        Vector2 from = square_center(&board_surface_layout, app->move_anim_from);
        Vector2 to = square_center(&board_surface_layout, app->move_anim_to);
        float t = app->move_anim_progress;
        float eased;
        Vector2 current;

        if (t < 0.0f) {
            t = 0.0f;
        }
        if (t > 1.0f) {
            t = 1.0f;
        }

        eased = t * t * (3.0f - 2.0f * t);
        current.x = from.x + (to.x - from.x) * eased;
        current.y = from.y + (to.y - from.y) * eased;

        draw_piece_shape(app->move_anim_piece,
                         app->move_anim_side,
                         current,
                         piece_size,
                         1.0f,
                         piece_rotation,
                         use_flipped_pieces,
                         piece_shadow_dir);
    }

    if (in_check && checked_king_square >= 0) {
        Rectangle king_rect = square_rect(&board_surface_layout, checked_king_square);
        DrawRectangleLinesEx(king_rect, fmaxf(4.0f, layout.square_size * 0.08f), (Color){199, 36, 48, 255});
        DrawRectangleLinesEx((Rectangle){king_rect.x + 2.0f, king_rect.y + 2.0f, king_rect.width - 4.0f, king_rect.height - 4.0f},
                             fmaxf(2.0f, layout.square_size * 0.03f),
                             (Color){255, 201, 75, 245});

        if (check_attacker_square >= 0) {
            Rectangle attacker_rect = square_rect(&board_surface_layout, check_attacker_square);
            DrawRectangleLinesEx(attacker_rect, fmaxf(4.0f, layout.square_size * 0.07f), (Color){255, 205, 68, 255});
        }
    }

    EndTextureMode();

    {
        Rectangle src = {0.0f, 0.0f, (float)board_px, -(float)board_px};
        Rectangle dst = {
            layout.board.x + layout.board.width * 0.5f,
            layout.board.y + layout.board.height * 0.5f,
            layout.board.width,
            layout.board.height
        };
        Vector2 origin = {layout.board.width * 0.5f, layout.board.height * 0.5f};

        DrawTexturePro(g_board_surface.texture, src, dst, origin, g_board_rotation_deg, WHITE);
    }

    DrawRectangleRoundedLinesEx(layout.board, 0.02f, 8, 2.0f, palette->board_outline);
    draw_coordinates(&layout);

    {
        float capture_height = layout.sidebar.height * 0.26f;
        Rectangle top = {
            layout.sidebar.x + 12.0f,
            layout.sidebar.y + 70.0f,
            layout.sidebar.width - 24.0f,
            capture_height
        };
        Rectangle bottom = {
            layout.sidebar.x + 12.0f,
            layout.sidebar.y + layout.sidebar.height - capture_height - 14.0f,
            layout.sidebar.width - 24.0f,
            capture_height
        };

        if (capture_height < 138.0f) {
            capture_height = 138.0f;
            top.height = capture_height;
            bottom.y = layout.sidebar.y + layout.sidebar.height - capture_height - 14.0f;
            bottom.height = capture_height;
        }
        if (capture_height > 210.0f) {
            capture_height = 210.0f;
            top.height = capture_height;
            bottom.y = layout.sidebar.y + layout.sidebar.height - capture_height - 14.0f;
            bottom.height = capture_height;
        }

        draw_captured_group(&app->position, top, SIDE_WHITE);
        draw_captured_group(&app->position, bottom, SIDE_BLACK);
    }
}
