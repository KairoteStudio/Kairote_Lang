#include "ConsoleUtils.h"
#include <stdio.h>
#ifdef _WIN32
#include <windows.h>
#include <io.h>
#else
#include <unistd.h>
#endif

/* Process-wide colour switch.  Set once during start-up before any worker
 * thread is spawned; readers only need the value to be visible, which the
 * compiler's existing thread-start synchronization provides. */
static int g_color_enabled = 0;

int KrtConsoleSupportsColor(void) {
#ifdef _WIN32
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
    int supports = (mode & ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0;
    if (!supports && SetConsoleMode(hOut, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING)) {
        supports = 1;
    }
    /* Probing must not leave the console in a different mode. */
    SetConsoleMode(hOut, mode);
    return supports;
#else
    return isatty(fileno(stdout)) ? 1 : 0;
#endif
}

void KrtConsoleSetColorEnabled(int enabled) {
    g_color_enabled = enabled;
}

const char* KrtColor(const char* code) {
    return g_color_enabled ? code : "";
}
