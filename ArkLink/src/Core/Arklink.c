#include "ArkLink/Arklink.h"
#include "ArkLink/Loader.h"
#include "ArkLink/LinkerNative.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>

struct ArkLinkSession {
    ArkLinkTarget target;
    ArkLinkOutputKind output_kind;
    char* output_path;
    char* entry_point;
    ArkSubsystem subsystem;
    uint64_t image_base;
    uint64_t stack_size;
    char** inputs;
    size_t input_count;
    ArkLinkLogger logger;
    void* logger_user_data;
    char error_buffer[1024];
};

ArkLinkSession* arklink_session_create(void) {
    ArkLinkSession* session = (ArkLinkSession*)calloc(1, sizeof(ArkLinkSession));
    if (!session) {
        return NULL;
    }

    session->target = ARK_LINK_TARGET_PE;
    session->output_kind = ARK_LINK_OUTPUT_EXECUTABLE;
    session->subsystem = ARK_SUBSYSTEM_CONSOLE;
    session->image_base = 0x140000000;
    session->stack_size = 0x100000;

    return session;
}

void arklink_session_destroy(ArkLinkSession* session) {
    if (!session) {
        return;
    }

    free(session->output_path);
    free(session->entry_point);

    for (size_t i = 0; i < session->input_count; i++) {
        free(session->inputs[i]);
    }
    free(session->inputs);

    free(session);
}

ArkLinkResult arklink_session_set_target(ArkLinkSession* session, ArkLinkTarget target) {
    if (!session) {
        return ARK_LINK_ERR_INVALID_ARGUMENT;
    }
    session->target = target;
    return ARK_LINK_OK;
}

ArkLinkResult arklink_session_set_output(ArkLinkSession* session, const char* output_path) {
    if (!session || !output_path) {
        return ARK_LINK_ERR_INVALID_ARGUMENT;
    }

    char* new_path = strdup(output_path);
    if (!new_path) {
        return ARK_LINK_ERR_MEMORY;
    }

    free(session->output_path);
    session->output_path = new_path;
    return ARK_LINK_OK;
}

ArkLinkResult arklink_session_set_output_kind(ArkLinkSession* session, ArkLinkOutputKind kind) {
    if (!session) {
        return ARK_LINK_ERR_INVALID_ARGUMENT;
    }
    session->output_kind = kind;
    return ARK_LINK_OK;
}

ArkLinkResult arklink_session_set_entry_point(ArkLinkSession* session, const char* entry_point) {
    if (!session || !entry_point) {
        return ARK_LINK_ERR_INVALID_ARGUMENT;
    }

    char* new_entry = strdup(entry_point);
    if (!new_entry) {
        return ARK_LINK_ERR_MEMORY;
    }

    free(session->entry_point);
    session->entry_point = new_entry;
    return ARK_LINK_OK;
}

ArkLinkResult arklink_session_set_subsystem(ArkLinkSession* session, ArkSubsystem subsystem) {
    if (!session) {
        return ARK_LINK_ERR_INVALID_ARGUMENT;
    }
    session->subsystem = subsystem;
    return ARK_LINK_OK;
}

ArkLinkResult arklink_session_set_image_base(ArkLinkSession* session, uint64_t image_base) {
    if (!session) {
        return ARK_LINK_ERR_INVALID_ARGUMENT;
    }
    session->image_base = image_base;
    return ARK_LINK_OK;
}

ArkLinkResult arklink_session_set_stack_size(ArkLinkSession* session, uint64_t stack_size) {
    if (!session) {
        return ARK_LINK_ERR_INVALID_ARGUMENT;
    }
    session->stack_size = stack_size;
    return ARK_LINK_OK;
}

ArkLinkResult arklink_session_add_input(ArkLinkSession* session, const char* path) {
    if (!session || !path) {
        return ARK_LINK_ERR_INVALID_ARGUMENT;
    }

    char** new_inputs = (char**)realloc(session->inputs, (session->input_count + 1) * sizeof(char*));
    if (!new_inputs) {
        return ARK_LINK_ERR_MEMORY;
    }

    session->inputs = new_inputs;
    session->inputs[session->input_count] = strdup(path);
    if (!session->inputs[session->input_count]) {
        return ARK_LINK_ERR_MEMORY;
    }

    session->input_count++;
    return ARK_LINK_OK;
}

ArkLinkResult arklink_session_set_logger(ArkLinkSession* session, ArkLinkLogger logger, void* user_data) {
    if (!session) {
        return ARK_LINK_ERR_INVALID_ARGUMENT;
    }
    session->logger = logger;
    session->logger_user_data = user_data;
    return ARK_LINK_OK;
}

const char* arklink_session_get_error(ArkLinkSession* session) {
    if (!session) {
        return "Invalid session";
    }
    return session->error_buffer[0] ? session->error_buffer : "Unknown error";
}

static void log_message(ArkLinkSession* session, ArkLogLevel level, const char* message) {
    if (session && session->logger) {
        session->logger(level, message, session->logger_user_data);
    }
}

ArkLinkResult arklink_session_link(ArkLinkSession* session) {
    if (!session) {
        return ARK_LINK_ERR_INVALID_ARGUMENT;
    }

    if (session->input_count == 0) {
        strncpy(session->error_buffer, "No input files specified", sizeof(session->error_buffer) - 1);
        return ARK_LINK_ERR_INVALID_ARGUMENT;
    }

    if (!session->output_path) {
        strncpy(session->error_buffer, "No output path specified", sizeof(session->error_buffer) - 1);
        return ARK_LINK_ERR_INVALID_ARGUMENT;
    }

    log_message(session, ARK_LOG_INFO, "Starting link process...");

    switch (session->target) {
    case ARK_LINK_TARGET_PE:
    case ARK_LINK_TARGET_ELF:
        return arklink_session_link_native(session);
    default:
        snprintf(session->error_buffer, sizeof(session->error_buffer), "Unsupported target: %d", session->target);
        return ARK_LINK_ERR_UNSUPPORTED;
    }
}
