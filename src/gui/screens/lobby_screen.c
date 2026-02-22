#include "gui.h"

#include <stdio.h>
#include <string.h>

#include "game_state.h"

/* Draws a rounded status/info block used in join/host subviews. */
static void draw_status_box(Rectangle rect, const char* title, const char* text) {
    const GuiPalette* palette = gui_palette();

    DrawRectangleRounded(rect, 0.10f, 8, Fade(palette->panel, 0.95f));
    DrawRectangleRoundedLinesEx(rect, 0.10f, 8, 1.0f, palette->panel_border);
    gui_draw_text(title, (int)rect.x + 14, (int)rect.y + 10, 24, palette->text_primary);
    gui_draw_text(text, (int)rect.x + 14, (int)rect.y + 44, 21, palette->text_secondary);
}

/* Closes temporary host/join room state and returns to lobby main choices. */
static void lobby_return_home(ChessApp* app, bool notify_peer) {
    if (notify_peer) {
        network_client_send_leave(&app->network);
    }

    app->network.connected = false;
    app->network.peer_addr_len = 0;
    app->network.is_host = false;
    app->online_match_active = false;
    app->online_local_ready = false;
    app->online_peer_ready = false;
    app->lobby_code[0] = '\0';
    app->online_match_code[0] = '\0';
    app->lobby_input_active = false;
    app->lobby_copy_feedback = false;
    app->lobby_copy_feedback_timer = 0.0f;
    app->lobby_view = LOBBY_VIEW_HOME;
    snprintf(app->lobby_status, sizeof(app->lobby_status), "Choose Host Game or Join Game.");
}

/* Renders and updates online lobby using a simplified host/join flow. */
void gui_screen_lobby(struct ChessApp* app) {
    const GuiPalette* palette = gui_palette();
    float sw = (float)GetScreenWidth();
    float sh = (float)GetScreenHeight();
    float panel_w = sw * 0.68f;
    float panel_h = sh * 0.74f;
    Rectangle panel;
    Rectangle back_btn;
    Rectangle card;

    if (panel_w < 760.0f) {
        panel_w = 760.0f;
    }
    if (panel_w > 980.0f) {
        panel_w = 980.0f;
    }
    if (panel_h < 620.0f) {
        panel_h = 620.0f;
    }
    if (panel_h > 700.0f) {
        panel_h = 700.0f;
    }

    if (app->lobby_copy_feedback_timer > 0.0f) {
        app->lobby_copy_feedback_timer -= GetFrameTime();
        if (app->lobby_copy_feedback_timer <= 0.0f) {
            app->lobby_copy_feedback_timer = 0.0f;
            app->lobby_copy_feedback = false;
        }
    }

    panel = (Rectangle){
        sw * 0.5f - panel_w * 0.5f,
        sh * 0.5f - panel_h * 0.5f,
        panel_w,
        panel_h
    };
    card = (Rectangle){
        panel.x + 28.0f,
        panel.y + 108.0f,
        panel.width - 56.0f,
        panel.height - 136.0f
    };

    DrawRectangleRounded((Rectangle){panel.x + 5.0f, panel.y + 6.0f, panel.width, panel.height},
                         0.08f,
                         8,
                         Fade(BLACK, 0.16f));
    DrawRectangleRounded(panel, 0.08f, 8, Fade(palette->panel, 0.95f));
    DrawRectangleRoundedLinesEx(panel, 0.08f, 8, 1.4f, palette->panel_border);

    gui_draw_text("Online", (int)panel.x + 30, (int)panel.y + 30, 48, palette->text_primary);
    back_btn = (Rectangle){panel.x + panel.width - 176.0f, panel.y + 28.0f, 146.0f, 50.0f};
    if (gui_button(back_btn, "Back")) {
        if (!app->online_match_active &&
            (app->lobby_view != LOBBY_VIEW_HOME || app->network.connected || app->network.is_host)) {
            lobby_return_home(app, app->network.connected);
        }
        app->screen = SCREEN_MENU;
        return;
    }

    DrawRectangleRounded(card, 0.08f, 8, Fade(palette->panel_alt, 0.95f));
    DrawRectangleRoundedLinesEx(card, 0.08f, 8, 1.0f, palette->panel_border);

    if (app->lobby_view == LOBBY_VIEW_ACTIVE) {
        Rectangle action_btn = {card.x + 36.0f, card.y + 154.0f, card.width - 72.0f, 60.0f};
        Rectangle lobby_btn = {card.x + 36.0f, card.y + 224.0f, card.width - 72.0f, 52.0f};
        Rectangle status_box = {card.x + 36.0f, card.y + 292.0f, card.width - 72.0f, card.height - 336.0f};

        gui_draw_text("Active Games", (int)card.x + 36, (int)card.y + 40, 34, palette->text_primary);

        if (app->online_match_active) {
            gui_draw_text("1 active online match", (int)card.x + 36, (int)card.y + 96, 24, palette->text_secondary);

            if (gui_button(action_btn, "Join Active Match")) {
                app->mode = MODE_ONLINE;
                app->screen = SCREEN_PLAY;
                snprintf(app->online_runtime_status,
                         sizeof(app->online_runtime_status),
                         "Resumed active match.");
                return;
            }
        } else {
            gui_draw_text("No active games.", (int)card.x + 36, (int)card.y + 96, 24, palette->text_secondary);
            DrawRectangleRounded(action_btn, 0.20f, 10, Fade(palette->panel, 0.85f));
            DrawRectangleRoundedLinesEx(action_btn, 0.20f, 10, 1.0f, palette->panel_border);
            gui_draw_text("Join Active Match",
                          (int)action_btn.x + 24,
                          (int)action_btn.y + 18,
                          24,
                          palette->text_secondary);
        }

        if (gui_button(lobby_btn, "Open Online Lobby")) {
            app->lobby_view = LOBBY_VIEW_HOME;
            return;
        }

        draw_status_box(status_box, "Status", app->lobby_status);
        return;
    }

    if (app->lobby_view == LOBBY_VIEW_HOME) {
        Rectangle join_btn = {card.x + 36.0f, card.y + 116.0f, card.width - 72.0f, 64.0f};
        Rectangle host_btn = {card.x + 36.0f, card.y + 196.0f, card.width - 72.0f, 64.0f};
        Rectangle status_box = {card.x + 36.0f, card.y + card.height - 150.0f, card.width - 72.0f, 108.0f};

        gui_draw_text("Choose one option", (int)card.x + 36, (int)card.y + 38, 34, palette->text_primary);

        if (gui_button(join_btn, "Join Game")) {
            app->lobby_view = LOBBY_VIEW_JOIN;
            app->lobby_input_active = true;
            app->online_local_ready = false;
            app->online_peer_ready = false;
            snprintf(app->lobby_status, sizeof(app->lobby_status), "Enter invite code and press Join.");
        }

        if (gui_button(host_btn, "Host Game")) {
            app->mode = MODE_ONLINE;
            app->online_match_active = false;
            app->online_local_ready = false;
            app->online_peer_ready = false;
            app->lobby_input_active = false;
            app->lobby_copy_feedback = false;
            app->lobby_copy_feedback_timer = 0.0f;

            if (network_client_host(&app->network, app->profile.username, app->lobby_code)) {
                app->human_side = app->network.host_side;
                app->lobby_view = LOBBY_VIEW_HOST;
                strncpy(app->online_match_code, app->lobby_code, INVITE_CODE_LEN);
                app->online_match_code[INVITE_CODE_LEN] = '\0';
                snprintf(app->online_runtime_status,
                         sizeof(app->online_runtime_status),
                         "Room created. Share code with opponent.");
                snprintf(app->lobby_status,
                         sizeof(app->lobby_status),
                         "Waiting for player to join room.");
            } else {
                snprintf(app->lobby_status, sizeof(app->lobby_status), "Failed to create host room.");
            }
        }

        draw_status_box(status_box, "Status", app->lobby_status);
        return;
    }

    if (app->lobby_view == LOBBY_VIEW_JOIN) {
        Rectangle input_box = {card.x + 36.0f, card.y + 88.0f, card.width - 72.0f, 56.0f};
        Rectangle join_btn = {card.x + 36.0f, card.y + 154.0f, card.width - 72.0f, 54.0f};
        Rectangle ready_btn = {card.x + 36.0f, card.y + 218.0f, card.width - 72.0f, 52.0f};
        Rectangle mode_btn = {card.x + 36.0f, card.y + card.height - 66.0f, 186.0f, 44.0f};
        Rectangle status_box = {
            card.x + 36.0f,
            card.y + 280.0f,
            card.width - 72.0f,
            mode_btn.y - (card.y + 280.0f) - 8.0f
        };

        gui_draw_text("Join Game", (int)card.x + 36, (int)card.y + 36, 34, palette->text_primary);

        if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
            app->lobby_input_active = CheckCollisionPointRec(GetMousePosition(), input_box);
        }
        gui_input_box(input_box, app->lobby_input, INVITE_CODE_LEN + 1, app->lobby_input_active);

        if (gui_button(join_btn, app->network.connected ? "Connected" : "Join")) {
            if (app->network.connected) {
                snprintf(app->lobby_status, sizeof(app->lobby_status), "Connected to host.");
            } else if (!matchmaker_is_valid_code(app->lobby_input)) {
                snprintf(app->lobby_status, sizeof(app->lobby_status), "Invite code is invalid.");
            } else {
                if (network_client_join(&app->network, app->profile.username, app->lobby_input)) {
                    app->mode = MODE_ONLINE;
                    app->human_side = SIDE_BLACK;
                    app->online_match_active = false;
                    app->online_local_ready = false;
                    app->online_peer_ready = false;
                    strncpy(app->online_match_code, app->lobby_input, INVITE_CODE_LEN);
                    app->online_match_code[INVITE_CODE_LEN] = '\0';
                    snprintf(app->online_runtime_status,
                             sizeof(app->online_runtime_status),
                             "Join request sent.");
                    snprintf(app->lobby_status,
                             sizeof(app->lobby_status),
                             "Waiting for host connection.");
                } else {
                    snprintf(app->lobby_status, sizeof(app->lobby_status), "Could not send join request.");
                }
            }
        }

        if (app->network.connected && !app->online_match_active) {
            const char* ready_label = app->online_local_ready ? "Ready (On)" : "Ready";

            if (gui_button(ready_btn, ready_label)) {
                bool next_ready = !app->online_local_ready;
                if (network_client_send_ready(&app->network, next_ready)) {
                    app->online_local_ready = next_ready;
                    snprintf(app->lobby_status,
                             sizeof(app->lobby_status),
                             next_ready ? "You are Ready. Waiting for host to start."
                                        : "You are not ready.");
                } else {
                    snprintf(app->lobby_status,
                             sizeof(app->lobby_status),
                             "Failed to update ready status.");
                }
            }
        }

        if (gui_button(mode_btn, "Change Mode")) {
            lobby_return_home(app, app->network.connected);
            return;
        }

        draw_status_box(status_box, "Status", app->lobby_status);
        return;
    }

    {
        int members = app->network.connected ? 2 : 1;
        Rectangle code_box = {card.x + 36.0f, card.y + 78.0f, card.width - 72.0f, 74.0f};
        Rectangle room_box = {card.x + 36.0f, card.y + 162.0f, card.width - 72.0f, 92.0f};
        Rectangle start_btn = {card.x + 36.0f, card.y + 264.0f, card.width - 72.0f, 52.0f};
        Rectangle mode_btn = {card.x + 36.0f, card.y + card.height - 66.0f, 186.0f, 44.0f};
        Rectangle status_box = {
            card.x + 36.0f,
            start_btn.y + start_btn.height + 10.0f,
            card.width - 72.0f,
            mode_btn.y - (start_btn.y + start_btn.height + 10.0f) - 8.0f
        };
        Rectangle copy_btn = {code_box.x + code_box.width - 130.0f, code_box.y + 24.0f, 112.0f, 40.0f};
        char members_line[64];
        const char* copy_label = app->lobby_copy_feedback ? "Copied" : "Copy";

        gui_draw_text("Host Game", (int)card.x + 36, (int)card.y + 30, 34, palette->text_primary);

        DrawRectangleRounded(code_box, 0.10f, 8, Fade(palette->panel, 0.95f));
        DrawRectangleRoundedLinesEx(code_box, 0.10f, 8, 1.0f, palette->panel_border);
        gui_draw_text("Invite Code", (int)code_box.x + 14, (int)code_box.y + 8, 22, palette->text_secondary);
        gui_draw_text(app->lobby_code, (int)code_box.x + 14, (int)code_box.y + 36, 31, palette->accent);

        if (gui_button(copy_btn, copy_label)) {
            SetClipboardText(app->lobby_code);
            app->lobby_copy_feedback = true;
            app->lobby_copy_feedback_timer = 1.6f;
        }

        DrawRectangleRounded(room_box, 0.10f, 8, Fade(palette->panel, 0.95f));
        DrawRectangleRoundedLinesEx(room_box, 0.10f, 8, 1.0f, palette->panel_border);
        gui_draw_text("Room", (int)room_box.x + 14, (int)room_box.y + 8, 24, palette->text_primary);
        snprintf(members_line, sizeof(members_line), "Players: %d / 2", members);
        gui_draw_text(members_line, (int)room_box.x + 14, (int)room_box.y + 36, 22, palette->text_secondary);
        gui_draw_text(app->online_peer_ready ? "Opponent: Ready" : "Opponent: Not Ready",
                      (int)room_box.x + 14,
                      (int)room_box.y + 62,
                      22,
                      app->online_peer_ready ? palette->accent : palette->text_secondary);

        if (gui_button(start_btn, "Start Game")) {
            if (!app->network.connected) {
                snprintf(app->lobby_status, sizeof(app->lobby_status), "Need 2 players in room first.");
            } else if (!app->online_peer_ready) {
                snprintf(app->lobby_status, sizeof(app->lobby_status), "Opponent must press Ready first.");
            } else if (!network_client_send_start(&app->network)) {
                snprintf(app->lobby_status, sizeof(app->lobby_status), "Could not send start packet.");
            } else {
                app->online_match_active = true;
                app->online_local_ready = false;
                app->online_peer_ready = false;
                app->network.invite_code[0] = '\0';
                app->lobby_code[0] = '\0';
                app->online_match_code[0] = '\0';
                snprintf(app->online_runtime_status, sizeof(app->online_runtime_status), "Match started.");
                app_start_game(app, MODE_ONLINE);
                return;
            }
        }

        if (gui_button(mode_btn, "Change Mode")) {
            lobby_return_home(app, app->network.connected);
            return;
        }

        draw_status_box(status_box, "Status", app->lobby_status);
    }
}
