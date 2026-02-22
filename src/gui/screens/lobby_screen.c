#include "gui.h"

#include <stdio.h>
#include <string.h>

#include "game_state.h"

/* Renders and updates online lobby UI for host/join/resume flows. */
void gui_screen_lobby(struct ChessApp* app) {
    const GuiPalette* palette = gui_palette();
    float sw = (float)GetScreenWidth();
    float sh = (float)GetScreenHeight();
    float panel_w = sw * 0.82f;
    float panel_h = sh * 0.82f;
    Rectangle panel;
    Rectangle left_card;
    Rectangle right_card;
    Rectangle host_btn;
    Rectangle join_btn;
    Rectangle back_btn;
    Rectangle input_box;
    Rectangle copy_btn;
    Rectangle status_box;
    Rectangle session_box;

    if (panel_w < 900.0f) {
        panel_w = 900.0f;
    }
    if (panel_w > 1180.0f) {
        panel_w = 1180.0f;
    }
    if (panel_h < 560.0f) {
        panel_h = 560.0f;
    }
    if (panel_h > 790.0f) {
        panel_h = 790.0f;
    }

    panel = (Rectangle){
        sw * 0.5f - panel_w * 0.5f,
        sh * 0.5f - panel_h * 0.5f,
        panel_w,
        panel_h
    };

    left_card = (Rectangle){
        panel.x + 26.0f,
        panel.y + 120.0f,
        panel.width * 0.53f - 36.0f,
        panel.height - 146.0f
    };
    right_card = (Rectangle){
        panel.x + panel.width * 0.53f + 8.0f,
        panel.y + 120.0f,
        panel.width * 0.47f - 34.0f,
        panel.height - 146.0f
    };

    DrawRectangleRounded((Rectangle){panel.x + 6.0f, panel.y + 7.0f, panel.width, panel.height},
                         0.08f,
                         8,
                         Fade(BLACK, 0.16f));
    DrawRectangleRounded(panel, 0.08f, 8, Fade(palette->panel, 0.95f));
    DrawRectangleRoundedLinesEx(panel, 0.08f, 8, 1.4f, palette->panel_border);

    DrawText("Online Lobby", (int)panel.x + 30, (int)panel.y + 28, 48, palette->text_primary);
    DrawText("Direct P2P match: host shares code, guest joins, both can resume active session.",
             (int)panel.x + 34,
             (int)panel.y + 84,
             22,
             palette->text_secondary);

    back_btn = (Rectangle){panel.x + panel.width - 176.0f, panel.y + 28.0f, 146.0f, 50.0f};
    if (gui_button(back_btn, "Back")) {
        app->screen = SCREEN_MENU;
    }

    DrawRectangleRounded(left_card, 0.08f, 8, Fade(palette->panel_alt, 0.95f));
    DrawRectangleRoundedLinesEx(left_card, 0.08f, 8, 1.0f, palette->panel_border);
    DrawText("Host / Join", (int)left_card.x + 16, (int)left_card.y + 14, 30, palette->text_primary);

    host_btn = (Rectangle){left_card.x + 16.0f, left_card.y + 62.0f, 210.0f, 54.0f};
    join_btn = (Rectangle){left_card.x + 236.0f, left_card.y + 62.0f, 210.0f, 54.0f};

    if (gui_button(host_btn, "Host Game")) {
        app->human_side = SIDE_WHITE;

        if (network_client_host(&app->network, app->profile.username, app->lobby_code)) {
            app->mode = MODE_ONLINE;
            strncpy(app->online_match_code, app->lobby_code, INVITE_CODE_LEN);
            app->online_match_code[INVITE_CODE_LEN] = '\0';
            snprintf(app->lobby_status, sizeof(app->lobby_status), "Hosting. Share code with your opponent.");
        } else {
            snprintf(app->lobby_status, sizeof(app->lobby_status), "Failed to start host socket.");
        }
    }

    if (gui_button(join_btn, "Join Game")) {
        if (!matchmaker_is_valid_code(app->lobby_input)) {
            snprintf(app->lobby_status, sizeof(app->lobby_status), "Invite code format is invalid.");
        } else if (network_client_join(&app->network, app->profile.username, app->lobby_input)) {
            app->mode = MODE_ONLINE;
            app->human_side = SIDE_BLACK;
            strncpy(app->online_match_code, app->lobby_input, INVITE_CODE_LEN);
            app->online_match_code[INVITE_CODE_LEN] = '\0';
            snprintf(app->lobby_status, sizeof(app->lobby_status), "Join request sent. Waiting for host...");
        } else {
            snprintf(app->lobby_status, sizeof(app->lobby_status), "Could not send join request.");
        }
    }

    DrawText("Invite Code", (int)left_card.x + 16, (int)left_card.y + 136, 23, palette->text_primary);
    input_box = (Rectangle){left_card.x + 16.0f, left_card.y + 166.0f, 360.0f, 56.0f};
    if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
        app->lobby_input_active = CheckCollisionPointRec(GetMousePosition(), input_box);
    }
    gui_input_box(input_box, app->lobby_input, INVITE_CODE_LEN + 1, app->lobby_input_active);

    if (app->lobby_code[0] != '\0') {
        Rectangle host_code_box = {left_card.x + 16.0f, left_card.y + 242.0f, left_card.width - 32.0f, 72.0f};
        DrawRectangleRounded(host_code_box, 0.10f, 8, Fade(palette->panel, 0.95f));
        DrawRectangleRoundedLinesEx(host_code_box, 0.10f, 8, 1.0f, palette->panel_border);
        DrawText("Your Host Code", (int)host_code_box.x + 12, (int)host_code_box.y + 10, 21, palette->text_secondary);
        DrawText(app->lobby_code, (int)host_code_box.x + 14, (int)host_code_box.y + 36, 30, palette->accent);

        copy_btn = (Rectangle){host_code_box.x + host_code_box.width - 118.0f, host_code_box.y + 18.0f, 102.0f, 38.0f};
        if (gui_button(copy_btn, "Copy")) {
            SetClipboardText(app->lobby_code);
            snprintf(app->lobby_status, sizeof(app->lobby_status), "Host code copied to clipboard.");
        }
    }

    status_box = (Rectangle){left_card.x + 16.0f, left_card.y + left_card.height - 146.0f, left_card.width - 32.0f, 130.0f};
    DrawRectangleRounded(status_box, 0.08f, 8, Fade(palette->panel, 0.95f));
    DrawRectangleRoundedLinesEx(status_box, 0.08f, 8, 1.0f, palette->panel_border);
    DrawText("Status", (int)status_box.x + 12, (int)status_box.y + 10, 24, palette->text_primary);
    DrawText(app->lobby_status, (int)status_box.x + 12, (int)status_box.y + 46, 22, palette->text_primary);

    DrawRectangleRounded(right_card, 0.08f, 8, Fade(palette->panel_alt, 0.95f));
    DrawRectangleRoundedLinesEx(right_card, 0.08f, 8, 1.0f, palette->panel_border);
    DrawText("Active Online Session", (int)right_card.x + 16, (int)right_card.y + 14, 30, palette->text_primary);

    session_box = (Rectangle){right_card.x + 16.0f, right_card.y + 60.0f, right_card.width - 32.0f, right_card.height - 76.0f};
    DrawRectangleRounded(session_box, 0.08f, 8, Fade(palette->panel, 0.95f));
    DrawRectangleRoundedLinesEx(session_box, 0.08f, 8, 1.0f, palette->panel_border);

    if (app->online_match_active) {
        DrawText("Match: Online Game", (int)session_box.x + 14, (int)session_box.y + 14, 24, palette->text_primary);

        if (app->online_match_code[0] != '\0') {
            char code_line[64];
            snprintf(code_line, sizeof(code_line), "Code: %s", app->online_match_code);
            DrawText(code_line, (int)session_box.x + 14, (int)session_box.y + 50, 22, palette->text_secondary);
        }

        DrawText(app->online_runtime_status, (int)session_box.x + 14, (int)session_box.y + 82, 21, palette->text_secondary);

        if (app->network.connected) {
            Rectangle resume_btn = {session_box.x + 14.0f, session_box.y + 126.0f, session_box.width - 28.0f, 52.0f};
            if (gui_button(resume_btn, "Resume Match")) {
                app->screen = SCREEN_PLAY;
                app->mode = MODE_ONLINE;
            }
        } else if (!app->network.is_host && app->online_match_code[0] != '\0') {
            Rectangle rejoin_btn = {session_box.x + 14.0f, session_box.y + 126.0f, session_box.width - 28.0f, 52.0f};
            if (gui_button(rejoin_btn, "Rejoin Last Match")) {
                if (network_client_join(&app->network, app->profile.username, app->online_match_code)) {
                    app->mode = MODE_ONLINE;
                    app->human_side = SIDE_BLACK;
                    snprintf(app->lobby_status, sizeof(app->lobby_status), "Rejoin request sent. Waiting for host...");
                } else {
                    snprintf(app->lobby_status, sizeof(app->lobby_status), "Rejoin failed. Check host availability.");
                }
            }
        }

        {
            Rectangle end_btn = {session_box.x + 14.0f, session_box.y + session_box.height - 64.0f, session_box.width - 28.0f, 50.0f};
            if (gui_button(end_btn, "Leave Match")) {
                app_online_end_match(app, true);
                snprintf(app->lobby_status, sizeof(app->lobby_status), "Online match closed.");
            }
        }
    } else {
        DrawText("No active match in memory.", (int)session_box.x + 14, (int)session_box.y + 14, 24, palette->text_primary);
        DrawText("When you leave a live online game to menu,", (int)session_box.x + 14, (int)session_box.y + 52, 20, palette->text_secondary);
        DrawText("it appears here and you can resume/rejoin.", (int)session_box.x + 14, (int)session_box.y + 78, 20, palette->text_secondary);
    }
}
