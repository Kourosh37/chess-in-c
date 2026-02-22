#ifndef GUI_H
#define GUI_H

/*
 * GUI layer contracts for reusable widgets, board rendering, and screen
 * handlers. Implemented with Raylib.
 */

#include <stdbool.h>
#include <raylib.h>
#include "types.h"

struct ChessApp;

typedef struct GuiPalette {
    const char* name;
    Color bg_top;
    Color bg_bottom;
    Color panel;
    Color panel_alt;
    Color panel_border;
    Color text_primary;
    Color text_secondary;
    Color accent;
    Color accent_hover;
    Color board_light;
    Color board_dark;
    Color board_outline;
    Color selection;
    Color legal_hint;
    Color white_piece_fill;
    Color white_piece_stroke;
    Color black_piece_fill;
    Color black_piece_stroke;
} GuiPalette;

typedef struct GuiPlayLayout {
    Rectangle board;
    Rectangle sidebar;
    float square_size;
} GuiPlayLayout;

#ifdef __cplusplus
extern "C" {
#endif

/* Generic widgets. */
bool gui_button(Rectangle bounds, const char* label);
bool gui_slider_float(Rectangle bounds, float* value, float min_value, float max_value);
void gui_input_box(Rectangle bounds, char* buffer, int capacity, bool active);
void gui_widgets_begin_frame(void);
void gui_draw_input_overlays(void);
bool gui_font_init(void);
void gui_font_shutdown(void);
void gui_draw_text(const char* text, int pos_x, int pos_y, int font_size, Color color);
int gui_measure_text(const char* text, int font_size);

/* Board rendering and coordinate conversion helpers. */
void gui_draw_board(const struct ChessApp* app);
int gui_square_from_mouse(Vector2 mouse);
GuiPlayLayout gui_get_play_layout(void);
bool gui_board_is_rotating(void);

/* Theme and style accessors. */
int gui_theme_count(void);
const char* gui_theme_name(int index);
int gui_get_active_theme(void);
void gui_set_active_theme(int index);
const GuiPalette* gui_palette(void);
void gui_draw_background(void);

/* Top-level screen render/update handlers. */
void gui_screen_menu(struct ChessApp* app);
void gui_screen_play(struct ChessApp* app);
void gui_screen_lobby(struct ChessApp* app);
void gui_screen_settings(struct ChessApp* app);

#ifdef __cplusplus
}
#endif

#endif
