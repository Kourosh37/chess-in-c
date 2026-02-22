#include "gui.h"

#include <stdio.h>

#include "game_state.h"

/* Renders and updates online lobby UI for direct host/join flow. */
void gui_screen_lobby(struct ChessApp* app) {
    const GuiPalette* palette = gui_palette();
    float sw = (float)GetScreenWidth();
    float sh = (float)GetScreenHeight();
    float panel_w = sw * 0.74f;
    float panel_h = sh * 0.76f;
    Rectangle panel;
    Rectangle host_btn;
    Rectangle join_btn;
    Rectangle back_btn;
    Rectangle input_box;
    Rectangle status_box;

    if (panel_w < 720.0f) {
        panel_w = 720.0f;
    }
    if (panel_w > 980.0f) {
        panel_w = 980.0f;
    }
    if (panel_h < 520.0f) {
        panel_h = 520.0f;
    }
    if (panel_h > 720.0f) {
        panel_h = 720.0f;
    }

    panel = (Rectangle){
        sw * 0.5f - panel_w * 0.5f,
        sh * 0.5f - panel_h * 0.5f,
        panel_w,
        panel_h
    };

    DrawRectangleRounded((Rectangle){panel.x + 5.0f, panel.y + 6.0f, panel.width, panel.height},
                         0.08f,
                         8,
                         Fade(BLACK, 0.15f));
    DrawRectangleRounded(panel, 0.08f, 8, Fade(palette->panel, 0.94f));
    DrawRectangleRoundedLinesEx(panel, 0.08f, 8, 1.4f, palette->panel_border);

    DrawText("Online Lobby", (int)panel.x + 36, (int)panel.y + 30, 46, palette->text_primary);
    DrawText("Direct peer-to-peer mode. Host shares invite code, guest joins directly.",
             (int)panel.x + 38,
             (int)panel.y + 86,
             22,
             palette->text_secondary);

    host_btn = (Rectangle){panel.x + 38.0f, panel.y + 142.0f, 220.0f, 54.0f};
    join_btn = (Rectangle){panel.x + 274.0f, panel.y + 142.0f, 220.0f, 54.0f};
    back_btn = (Rectangle){panel.x + panel.width - 210.0f, panel.y + 142.0f, 170.0f, 54.0f};

    if (gui_button(host_btn, "Host Game")) {
        app->human_side = SIDE_WHITE;

        if (network_client_host(&app->network, app->profile.username, app->lobby_code)) {
            app->mode = MODE_ONLINE;
            snprintf(app->lobby_status, sizeof(app->lobby_status), "Hosting. Share code: %s", app->lobby_code);
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
            snprintf(app->lobby_status, sizeof(app->lobby_status), "Join request sent. Waiting for host...");
        } else {
            snprintf(app->lobby_status, sizeof(app->lobby_status), "Could not send join request.");
        }
    }

    if (gui_button(back_btn, "Back")) {
        app->screen = SCREEN_MENU;
    }

    DrawText("Invite Code", (int)panel.x + 38, (int)panel.y + 228, 23, palette->text_primary);

    input_box = (Rectangle){panel.x + 38.0f, panel.y + 258.0f, 356.0f, 56.0f};
    if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
        app->lobby_input_active = CheckCollisionPointRec(GetMousePosition(), input_box);
    }
    gui_input_box(input_box, app->lobby_input, INVITE_CODE_LEN + 1, app->lobby_input_active);

    if (app->lobby_code[0] != '\0') {
        char code_line[64];
        snprintf(code_line, sizeof(code_line), "Your host code: %s", app->lobby_code);
        DrawText(code_line, (int)panel.x + 418, (int)panel.y + 270, 30, palette->accent);
    }

    status_box = (Rectangle){panel.x + 38.0f, panel.y + 344.0f, panel.width - 76.0f, 154.0f};
    DrawRectangleRounded(status_box, 0.07f, 8, Fade(palette->panel_alt, 0.95f));
    DrawRectangleRoundedLinesEx(status_box, 0.07f, 8, 1.0f, palette->panel_border);
    DrawText("Status", (int)status_box.x + 14, (int)status_box.y + 14, 24, palette->text_primary);
    DrawText(app->lobby_status, (int)status_box.x + 14, (int)status_box.y + 52, 24, palette->text_primary);

    DrawText("LAN works out of the box. Internet sessions may require port forwarding/NAT setup.",
             (int)panel.x + 38,
             (int)panel.y + panel.height - 66,
             20,
             palette->text_secondary);

    if (app->network.connected) {
        DrawText("Connection established. Starting match...", (int)panel.x + 38, (int)panel.y + panel.height - 38, 22, palette->accent);
    }
}
