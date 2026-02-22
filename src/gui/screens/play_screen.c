#include "gui.h"

#include <stdio.h>

#include "game_state.h"

/* True when at least one legal move starts from square. */
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

/* Returns a readable label for side enum value. */
static const char* side_to_text(Side side) {
    return (side == SIDE_WHITE) ? "White" : "Black";
}

/* Finds the legal move matching selection and promotion preference. */
static bool find_selected_move(const ChessApp* app, int from, int to, uint8_t promotion_piece, Move* out_move) {
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

void gui_screen_play(struct ChessApp* app) {
    const GuiPalette* palette = gui_palette();
    GuiPlayLayout layout = gui_get_play_layout();
    float capture_height = layout.sidebar.height * 0.26f;
    float middle_y = layout.sidebar.y + 70.0f + capture_height + 14.0f;
    float middle_h = layout.sidebar.y + layout.sidebar.height - capture_height - 14.0f - middle_y - 10.0f;
    Rectangle back_btn = {
        layout.sidebar.x + 12.0f,
        layout.sidebar.y + 12.0f,
        layout.sidebar.width - 24.0f,
        48.0f
    };
    Rectangle middle = {
        layout.sidebar.x + 12.0f,
        middle_y,
        layout.sidebar.width - 24.0f,
        middle_h
    };

    gui_draw_board(app);

    if (gui_button(back_btn, "Menu")) {
        app->screen = SCREEN_MENU;
        app->has_selection = false;
        app->selected_square = -1;
        app->ai_thinking = false;
        app->move_animating = false;
        return;
    }

    DrawRectangleRounded(middle, 0.09f, 8, Fade(palette->panel_alt, 0.95f));
    DrawRectangleRoundedLinesEx(middle, 0.09f, 8, 1.0f, palette->panel_border);

    {
        int y = (int)middle.y + 14;
        char line[128];

        snprintf(line, sizeof(line), "Turn: %s", side_to_text(app->position.side_to_move));
        DrawText(line, (int)middle.x + 12, y, 24, palette->text_primary);
        y += 34;

        if (app->mode == MODE_SINGLE) {
            DrawText("Mode: Single Player", (int)middle.x + 12, y, 21, palette->text_secondary);
        } else if (app->mode == MODE_LOCAL) {
            DrawText("Mode: Local Multiplayer", (int)middle.x + 12, y, 21, palette->text_secondary);
        } else {
            DrawText("Mode: Online P2P", (int)middle.x + 12, y, 21, palette->text_secondary);
        }
        y += 34;

        if (app->mode == MODE_SINGLE) {
            char ai_line[96];
            snprintf(ai_line, sizeof(ai_line), "AI: depth %d  randomness %d",
                     app->ai_limits.depth,
                     app->ai_limits.randomness);
            DrawText(ai_line, (int)middle.x + 12, y, 20, palette->text_secondary);
            y += 30;
        }

        if (app->mode == MODE_SINGLE && app->ai_thinking) {
            DrawText("AI is thinking...", (int)middle.x + 12, y, 22, palette->accent);
            y += 32;
        } else if (app->mode == MODE_ONLINE && !app_is_human_turn(app)) {
            DrawText("Waiting for opponent...", (int)middle.x + 12, y, 22, palette->accent);
            y += 32;
        }

        if (app->mode == MODE_SINGLE && app->last_ai_result.depth_reached > 0) {
            char info[128];
            snprintf(info,
                     sizeof(info),
                     "Last AI: depth %d | score %d | nodes %llu",
                     app->last_ai_result.depth_reached,
                     app->last_ai_result.score,
                     (unsigned long long)app->last_ai_result.nodes);
            DrawText(info, (int)middle.x + 12, y, 18, palette->text_secondary);
            y += 28;
        }

        if (app->game_over) {
            Side loser = app->position.side_to_move;
            bool mate = engine_in_check(&app->position, loser);

            if (mate) {
                Side winner = (loser == SIDE_WHITE) ? SIDE_BLACK : SIDE_WHITE;
                snprintf(line, sizeof(line), "Checkmate! %s wins.", side_to_text(winner));
                DrawText(line, (int)middle.x + 12, y + 6, 25, (Color){184, 36, 42, 255});
            } else {
                DrawText("Draw (stalemate).", (int)middle.x + 12, y + 6, 25, palette->text_primary);
            }
        }
    }

    if (app->game_over) {
        return;
    }

    {
        bool input_allowed = app_is_human_turn(app) &&
                             !app->move_animating &&
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
