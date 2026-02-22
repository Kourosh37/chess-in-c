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

#ifdef __cplusplus
extern "C" {
#endif

/* Generic widgets. */
bool gui_button(Rectangle bounds, const char* label);
void gui_input_box(Rectangle bounds, char* buffer, int capacity, bool active);

/* Board rendering and coordinate conversion helpers. */
void gui_draw_board(const Position* pos, int selected_square, const MoveList* legal_moves);
int gui_square_from_mouse(Vector2 mouse);

/* Top-level screen render/update handlers. */
void gui_screen_menu(struct ChessApp* app);
void gui_screen_play(struct ChessApp* app);
void gui_screen_lobby(struct ChessApp* app);

#ifdef __cplusplus
}
#endif

#endif
