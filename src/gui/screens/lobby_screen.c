#include "gui.h"

#include <stdio.h>

#include "game_state.h"

/* Renders and updates online lobby for direct P2P host/join flow. */
void gui_screen_lobby(struct ChessApp* app) {
    DrawText("Online Lobby (Direct P2P)", 70, 38, 38, (Color){25, 52, 89, 255});
    DrawText("No standalone server: host client accepts one peer directly.",
             72,
             82,
             18,
             (Color){65, 70, 78, 255});

    {
        Rectangle host_btn = {70, 150, 240, 52};
        Rectangle join_btn = {330, 150, 240, 52};
        Rectangle back_btn = {590, 150, 180, 52};

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
    }

    {
        Rectangle input_box = {70, 250, 340, 52};

        if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
            app->lobby_input_active = CheckCollisionPointRec(GetMousePosition(), input_box);
        }

        gui_input_box(input_box, app->lobby_input, INVITE_CODE_LEN + 1, app->lobby_input_active);
        DrawText("Invite code", 70, 222, 20, (Color){25, 25, 25, 255});
    }

    if (app->lobby_code[0] != '\0') {
        char code_line[64];
        snprintf(code_line, sizeof(code_line), "Your code: %s", app->lobby_code);
        DrawText(code_line, 430, 262, 28, (Color){16, 75, 51, 255});
    }

    DrawRectangleRounded((Rectangle){70, 340, 700, 120}, 0.08f, 8, (Color){237, 240, 245, 255});
    DrawRectangleLinesEx((Rectangle){70, 340, 700, 120}, 1.0f, (Color){189, 195, 201, 255});

    DrawText("Status", 90, 356, 22, (Color){35, 35, 35, 255});
    DrawText(app->lobby_status, 90, 392, 22, (Color){30, 30, 30, 255});

    DrawText("Tip: this mode works on LAN directly; internet play may need port forwarding.",
             70,
             500,
             18,
             (Color){80, 80, 80, 255});

    if (app->network.connected) {
        DrawText("Connection established. Starting match...", 70, 535, 22, (Color){17, 130, 59, 255});
    }
}
