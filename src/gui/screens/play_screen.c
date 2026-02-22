#include "gui.h"

#include <stdio.h>

#include "game_state.h"

/* True when at least one legal move starts from the square. */
static bool has_move_from(const ChessApp* app, int square) {
    for (int i = 0; i < app->legal_moves.count; ++i) {
        if (app->legal_moves.moves[i].from == square) {
            return true;
        }
    }
    return false;
}

/* True when square contains a piece belonging to side-to-move. */
static bool square_has_turn_piece(const ChessApp* app, int square) {
    Side side;
    return position_piece_at(&app->position, square, &side, NULL) && side == app->position.side_to_move;
}

/* Converts side enum to UI label text. */
static const char* side_to_text(Side side) {
    return (side == SIDE_WHITE) ? "White" : "Black";
}

/* Finds a legal move by UI selection parameters. */
static bool find_selected_move(const ChessApp* app,
                               int from,
                               int to,
                               uint8_t promotion_piece,
                               Move* out_move) {
    for (int i = 0; i < app->legal_moves.count; ++i) {
        Move move = app->legal_moves.moves[i];

        if (move.from != from || move.to != to) {
            continue;
        }

        if ((move.flags & MOVE_FLAG_PROMOTION) != 0U) {
            uint8_t wanted = (promotion_piece == PIECE_NONE) ? PIECE_QUEEN : promotion_piece;
            if (move.promotion != wanted) {
                continue;
            }
        }

        *out_move = move;
        return true;
    }

    return false;
}

/* Renders and updates gameplay screen, including piece interaction flow. */
void gui_screen_play(struct ChessApp* app) {
    gui_draw_board(&app->position, app->selected_square, &app->legal_moves);

    {
        Rectangle back_btn = {760, 60, 150, 48};
        if (gui_button(back_btn, "Menu")) {
            app->screen = SCREEN_MENU;
            app->has_selection = false;
            app->selected_square = -1;
            app->ai_thinking = false;
            return;
        }
    }

    {
        char turn_line[96];
        snprintf(turn_line, sizeof(turn_line), "Turn: %s", side_to_text(app->position.side_to_move));
        DrawText(turn_line, 760, 130, 26, (Color){20, 20, 20, 255});
    }

    if (app->mode == MODE_SINGLE && app->ai_thinking) {
        DrawText("AI is thinking...", 760, 168, 22, (Color){155, 82, 5, 255});
    } else if (app->mode == MODE_ONLINE && !app_is_human_turn(app)) {
        DrawText("Waiting for opponent move...", 760, 168, 22, (Color){46, 82, 125, 255});
    }

    if (app->game_over) {
        Side loser = app->position.side_to_move;
        bool mate = engine_in_check(&app->position, loser);

        if (mate) {
            Side winner = (loser == SIDE_WHITE) ? SIDE_BLACK : SIDE_WHITE;
            char end_text[96];
            snprintf(end_text, sizeof(end_text), "Checkmate! %s wins.", side_to_text(winner));
            DrawText(end_text, 760, 218, 24, (Color){166, 20, 23, 255});
        } else {
            DrawText("Draw (Stalemate)", 760, 218, 24, (Color){90, 90, 90, 255});
        }

        return;
    }

    {
        bool input_allowed = app_is_human_turn(app) &&
                             !(app->mode == MODE_SINGLE && app->ai_thinking) &&
                             !(app->mode == MODE_ONLINE && !app->network.connected);

        if (!input_allowed) {
            return;
        }
    }

    if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
        int square = gui_square_from_mouse(GetMousePosition());

        if (square < 0) {
            return;
        }

        if (!app->has_selection) {
            if (square_has_turn_piece(app, square) && has_move_from(app, square)) {
                app->has_selection = true;
                app->selected_square = square;
            }
            return;
        }

        {
            int from = app->selected_square;
            int to = square;

            if (from == to) {
                app->has_selection = false;
                app->selected_square = -1;
                return;
            }

            {
                Move selected_move;
                if (find_selected_move(app, from, to, PIECE_QUEEN, &selected_move) &&
                    app_apply_move(app, selected_move)) {
                    app->has_selection = false;
                    app->selected_square = -1;

                    if (app->mode == MODE_ONLINE && app->network.connected) {
                        network_client_send_move(&app->network, selected_move);
                    }
                } else if (square_has_turn_piece(app, square) && has_move_from(app, square)) {
                    app->selected_square = square;
                } else {
                    app->has_selection = false;
                    app->selected_square = -1;
                }
            }
        }
    }
}
