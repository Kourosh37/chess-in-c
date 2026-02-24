#include "platform_dialog.h"

#include <stdio.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

void platform_show_error_dialog(const char* title, const char* message) {
    const char* t = (title != NULL && title[0] != '\0') ? title : "Chess Error";
    const char* m = (message != NULL && message[0] != '\0') ? message : "Unknown startup failure.";

#ifdef _WIN32
    MessageBoxA(NULL, m, t, MB_OK | MB_ICONERROR | MB_TASKMODAL);
#else
    fprintf(stderr, "%s: %s\n", t, m);
#endif
}
