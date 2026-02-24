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
#include <stdio.h>

#include "platform_dialog.h"

/* GUI subsystem entry point required by MSVC /SUBSYSTEM:WINDOWS builds. */
int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR cmd_line, int show_cmd) {
    (void)instance;
    (void)previous;
    (void)cmd_line;
    (void)show_cmd;

#if defined(_MSC_VER)
    __try {
        return chess_entry();
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        char message[256];
        unsigned long code = (unsigned long)GetExceptionCode();
        snprintf(message,
                 sizeof(message),
                 "Chess crashed during startup (0x%08lX).\n"
                 "Please reinstall the latest release package.",
                 code);
        platform_show_error_dialog("Chess Startup Error", message);
        return (int)code;
    }
#else
    return chess_entry();
#endif
}
#endif
