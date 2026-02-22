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

/* Converts side enum value to text label. */
static const char* side_to_text(Side side) {
    return (side == SIDE_WHITE) ? "White" : "Black";
}

/* Finds the legal move matching UI selection state. */
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

/* Draws a blocking confirmation dialog when user attempts to leave a running game. */
static void draw_leave_confirm_dialog(ChessApp* app) {
    const GuiPalette* palette = gui_palette();
    float sw = (float)GetScreenWidth();
    float sh = (float)GetScreenHeight();
    float panel_w = sw * 0.48f;
    float panel_h = (app->mode == MODE_ONLINE) ? 250.0f : 210.0f;
    Rectangle panel;
    Rectangle stay_btn;
    Rectangle menu_btn;
    Rectangle leave_btn;

    if (panel_w < 500.0f) {
        panel_w = 500.0f;
    }
    if (panel_w > 720.0f) {
        panel_w = 720.0f;
    }

    panel = (Rectangle){
        sw * 0.5f - panel_w * 0.5f,
        sh * 0.5f - panel_h * 0.5f,
        panel_w,
        panel_h
    };

    DrawRectangle(0, 0, (int)sw, (int)sh, Fade(BLACK, 0.50f));
    DrawRectangleRounded(panel, 0.08f, 8, Fade(palette->panel, 0.98f));
    DrawRectangleRoundedLinesEx(panel, 0.08f, 8, 1.4f, palette->panel_border);

    DrawText("Leave Current Game?", (int)panel.x + 20, (int)panel.y + 20, 34, palette->text_primary);

    if (app->mode == MODE_ONLINE && app->online_match_active) {
        DrawText("Menu: game stays active in background and can be resumed from lobby/menu.",
                 (int)panel.x + 20,
                 (int)panel.y + 74,
                 20,
                 palette->text_secondary);
        DrawText("Leave Match: notifies opponent and closes this online session.",
                 (int)panel.x + 20,
                 (int)panel.y + 102,
                 20,
                 palette->text_secondary);
    } else {
        DrawText("If you leave now, this match state will be closed.",
                 (int)panel.x + 20,
                 (int)panel.y + 84,
                 22,
                 palette->text_secondary);
    }

    stay_btn = (Rectangle){panel.x + 20.0f, panel.y + panel.height - 64.0f, 130.0f, 44.0f};
    if (gui_button(stay_btn, "Stay")) {
        app->leave_confirm_open = false;
    }

    if (app->mode == MODE_ONLINE && app->online_match_active) {
        menu_btn = (Rectangle){panel.x + 166.0f, panel.y + panel.height - 64.0f, 208.0f, 44.0f};
        leave_btn = (Rectangle){panel.x + panel.width - 172.0f, panel.y + panel.height - 64.0f, 152.0f, 44.0f};

        if (gui_button(menu_btn, "Menu (Keep Match)")) {
            app->screen = SCREEN_MENU;
            app->leave_confirm_open = false;
            app->has_selection = false;
            app->selected_square = -1;
            app->move_animating = false;
            snprintf(app->online_runtime_status,
                     sizeof(app->online_runtime_status),
                     "Match running in background. Resume any time.");
        }

        if (gui_button(leave_btn, "Leave Match")) {
            app_online_end_match(app, true);
            app->screen = SCREEN_MENU;
            app->has_selection = false;
            app->selected_square = -1;
            app->move_animating = false;
        }
    } else {
        leave_btn = (Rectangle){panel.x + panel.width - 172.0f, panel.y + panel.height - 64.0f, 152.0f, 44.0f};
        if (gui_button(leave_btn, "Leave")) {
            app->screen = SCREEN_MENU;
            app->leave_confirm_open = false;
            app->has_selection = false;
            app->selected_square = -1;
            app->ai_thinking = false;
            app->move_animating = false;
        }
    }
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
        app->leave_confirm_open = true;
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
        y += 32;

        if (app->mode == MODE_ONLINE) {
            DrawText(app->online_runtime_status, (int)middle.x + 12, y, 20, palette->text_secondary);
            y += 30;
        } else if (app->mode == MODE_SINGLE) {
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
        } else if (app->mode == MODE_ONLINE && app->online_match_active && app->network.connected && !app_is_human_turn(app)) {
            DrawText("Waiting for opponent...", (int)middle.x + 12, y, 22, palette->accent);
            y += 32;
        } else if (app->mode == MODE_ONLINE && !app->network.connected) {
            DrawText("Opponent disconnected.", (int)middle.x + 12, y, 22, (Color){176, 78, 29, 255});
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

    if (app->leave_confirm_open) {
        draw_leave_confirm_dialog(app);
        return;
    }

    if (app->game_over) {
        return;
    }

    {
        bool online_input_ok = (app->mode != MODE_ONLINE) || (app->online_match_active && app->network.connected);
        bool input_allowed = app_is_human_turn(app) &&
                             !app->move_animating &&
                             online_input_ok &&
                             !(app->mode == MODE_SINGLE && app->ai_thinking);

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
