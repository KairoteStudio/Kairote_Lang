#include "ArkLink/Arklink.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_usage(const char* program) {
    printf("Usage: %s <input.kro> [-o output.exe]\n", program);
    printf("\nOptions:\n");
    printf("  -o <path>  Specify output path (default: a.out)\n");
    printf("  -h         Show this help\n");
}

static void logger_callback(ArkLogLevel level, const char* message, void* user_data) {
    (void)user_data;

    const char* level_str = "INFO";
    switch (level) {
    case ARK_LOG_ERROR:
        level_str = "ERROR";
        break;
    case ARK_LOG_WARN:
        level_str = "WARN";
        break;
    case ARK_LOG_INFO:
        level_str = "INFO";
        break;
    case ARK_LOG_DEBUG:
        level_str = "DEBUG";
        break;
    }

    printf("[%s] %s\n", level_str, message);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    const char* input_path = NULL;
    const char* output_path = "a.out";

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            output_path = argv[++i];
        } else if (argv[i][0] != '-') {
            input_path = argv[i];
        }
    }

    if (!input_path) {
        fprintf(stderr, "No input file specified\n");
        print_usage(argv[0]);
        return 1;
    }

    ArkLinkSession* session = arklink_session_create();
    if (!session) {
        fprintf(stderr, "Failed to create linker session\n");
        return 1;
    }

    arklink_session_set_logger(session, logger_callback, NULL);

    ArkLinkResult result = arklink_session_set_output(session, output_path);
    if (result != ARK_LINK_OK) {
        fprintf(stderr, "%s\n", arklink_session_get_error(session));
        arklink_session_destroy(session);
        return 1;
    }

    result = arklink_session_add_input(session, input_path);
    if (result != ARK_LINK_OK) {
        fprintf(stderr, "%s\n", arklink_session_get_error(session));
        arklink_session_destroy(session);
        return 1;
    }

    result = arklink_session_link(session);
    if (result != ARK_LINK_OK) {
        fprintf(stderr, "%s\n", arklink_session_get_error(session));
        arklink_session_destroy(session);
        return 1;
    }

    printf("Successfully linked: %s\n", output_path);

    arklink_session_destroy(session);
    return 0;
}
