#include "gui.h"

#include "engine.h"

/* Board layout constants (screen-space pixels). */
#define BOARD_PIXELS 640
#define BOARD_ORIGIN_X 80
#define BOARD_ORIGIN_Y 60
#define SQUARE_PIXELS (BOARD_PIXELS / 8)

/* Returns board rectangle in screen coordinates. */
static Rectangle board_rect(void) {
    Rectangle rect;

    rect.x = (float)BOARD_ORIGIN_X;
    rect.y = (float)BOARD_ORIGIN_Y;
    rect.width = (float)BOARD_PIXELS;
    rect.height = (float)BOARD_PIXELS;
    return rect;
}

/* Converts mouse pixel position to board square index (0..63), or -1 if outside. */
int gui_square_from_mouse(Vector2 mouse) {
    Rectangle rect = board_rect();
    int file;
    int rank_from_top;
    int rank;

    if (!CheckCollisionPointRec(mouse, rect)) {
        return -1;
    }

    file = ((int)mouse.x - BOARD_ORIGIN_X) / SQUARE_PIXELS;
    rank_from_top = ((int)mouse.y - BOARD_ORIGIN_Y) / SQUARE_PIXELS;

    if (file < 0 || file > 7 || rank_from_top < 0 || rank_from_top > 7) {
        return -1;
    }

    rank = 7 - rank_from_top;
    return (rank << 3) | file;
}

/* Draws board coordinate labels around the chessboard. */
static void draw_coordinates(void) {
    for (int file = 0; file < 8; ++file) {
        char text[2] = {(char)('a' + file), '\0'};
        DrawText(text,
                 BOARD_ORIGIN_X + file * SQUARE_PIXELS + SQUARE_PIXELS - 14,
                 BOARD_ORIGIN_Y + BOARD_PIXELS + 6,
                 14,
                 DARKGRAY);
    }

    for (int rank = 0; rank < 8; ++rank) {
        char text[2] = {(char)('1' + rank), '\0'};
        DrawText(text,
                 BOARD_ORIGIN_X - 18,
                 BOARD_ORIGIN_Y + (7 - rank) * SQUARE_PIXELS + 6,
                 14,
                 DARKGRAY);
    }
}

/* True when square is a legal destination for the selected source square. */
static bool is_target_for_selected(const MoveList* legal_moves, int selected_square, int square) {
    if (selected_square < 0) {
        return false;
    }

    for (int i = 0; i < legal_moves->count; ++i) {
        if (legal_moves->moves[i].from == selected_square && legal_moves->moves[i].to == square) {
            return true;
        }
    }

    return false;
}

/* Draws chessboard, highlights, and piece symbols. */
void gui_draw_board(const Position* pos, int selected_square, const MoveList* legal_moves) {
    Color light_square = (Color){238, 219, 180, 255};
    Color dark_square = (Color){181, 136, 99, 255};

    for (int rank = 7; rank >= 0; --rank) {
        for (int file = 0; file < 8; ++file) {
            int square = (rank << 3) | file;
            int draw_x = BOARD_ORIGIN_X + file * SQUARE_PIXELS;
            int draw_y = BOARD_ORIGIN_Y + (7 - rank) * SQUARE_PIXELS;
            bool light = ((rank + file) & 1) == 0;
            Color square_color = light ? light_square : dark_square;

            DrawRectangle(draw_x, draw_y, SQUARE_PIXELS, SQUARE_PIXELS, square_color);

            if (square == selected_square) {
                DrawRectangleLinesEx(
                    (Rectangle){(float)draw_x, (float)draw_y, (float)SQUARE_PIXELS, (float)SQUARE_PIXELS},
                    3.0f,
                    GOLD);
            } else if (is_target_for_selected(legal_moves, selected_square, square)) {
                DrawCircle(draw_x + SQUARE_PIXELS / 2,
                           draw_y + SQUARE_PIXELS / 2,
                           10.0f,
                           (Color){30, 30, 30, 130});
            }

            {
                Side side;
                PieceType piece;
                if (position_piece_at(pos, square, &side, &piece)) {
                    char symbol[2] = {piece_to_char(side, piece), '\0'};
                    Color piece_color = (side == SIDE_WHITE) ? (Color){245, 245, 245, 255} : (Color){20, 20, 20, 255};
                    DrawText(symbol, draw_x + SQUARE_PIXELS / 2 - 10, draw_y + SQUARE_PIXELS / 2 - 18, 36, piece_color);
                }
            }
        }
    }

    DrawRectangleLinesEx(board_rect(), 2.0f, BLACK);
    draw_coordinates();
}
