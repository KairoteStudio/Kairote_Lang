#include "ConsoleUtils.h"
#include <stdio.h>
#ifdef _WIN32
#include <windows.h>
#include <io.h>
#else
#include <unistd.h>
#endif

static int g_color_enabled = 0;

int KrtConsoleSupportsColor(void) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    if (!_isatty(_fileno(stdout))) {
        return 0;
    }
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut == INVALID_HANDLE_VALUE) {
        return 0;
    }
    DWORD mode = 0;
    if (!GetConsoleMode(hOut, &mode)) {
        return 0;
    }
    if (!(mode & ENABLE_VIRTUAL_TERMINAL_PROCESSING)) {
        if (!SetConsoleMode(hOut, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING)) {
            return 0;
        }
    }
    return 1;
#else
    return isatty(fileno(stdout));
#endif
}

void KrtConsoleSetColorEnabled(int enabled) {
    g_color_enabled = enabled;
}

const char* KrtColor(const char* code) {
    return g_color_enabled ? code : "";
}
