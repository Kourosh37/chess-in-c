#include "main_loop.h"

/* Shared entry to keep behavior identical across main/WinMain frontends. */
static int chess_entry(void) {
    return run_main_loop();
}

/* Standard C entry point. */
int main(void) {
    return chess_entry();
}

#ifdef _WIN32
#include <windows.h>

/* GUI subsystem entry point required by MSVC /SUBSYSTEM:WINDOWS builds. */
int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR cmd_line, int show_cmd) {
    (void)instance;
    (void)previous;
    (void)cmd_line;
    (void)show_cmd;
    return chess_entry();
}
#endif
