#ifndef KRT_CONSOLE_UTILS_H
#define KRT_CONSOLE_UTILS_H

#define KRT_COL_RESET "\033[0m"
#define KRT_COL_BOLD "\033[1m"
#define KRT_COL_GREEN "\033[32m"
#define KRT_COL_RED "\033[31m"
#define KRT_COL_CYAN "\033[36m"
#define KRT_COL_YELLOW "\033[33m"
#define KRT_COL_GRAY "\033[90m"
#define KRT_COL_MAGENTA "\033[35m"
#define KRT_COL_BLUE "\033[34m"

#define ANSI_CURSOR_UP "\033[F"
#define ANSI_CLEAR_LINE "\033[K"
#define ANSI_SAVE_CURSOR "\033[s"
#define ANSI_RESTORE_CURSOR "\033[u"

int KrtConsoleSupportsColor(void);
void KrtConsoleSetColorEnabled(int enabled);
const char* KrtColor(const char* code);

#endif
