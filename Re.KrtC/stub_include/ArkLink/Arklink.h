#ifndef ARKLINK_ARKLINK_H
#define ARKLINK_ARKLINK_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    ARK_LINK_FLAG_VERBOSE = 1u << 0,
    ARK_LINK_FLAG_QUIET = 1u << 1,
};

typedef enum ArkLinkTarget {
    ARK_LINK_TARGET_PE,
    ARK_LINK_TARGET_ELF,
} ArkLinkTarget;

typedef enum ArkLinkOutputKind {
    ARK_LINK_OUTPUT_EXECUTABLE,
    ARK_LINK_OUTPUT_SHARED_LIBRARY,
    ARK_LINK_OUTPUT_STATIC_LIBRARY,
} ArkLinkOutputKind;

typedef enum ArkLinkResult {
    ARK_LINK_OK = 0,
    ARK_LINK_ERR_INVALID_ARGUMENT,
    ARK_LINK_ERR_IO,
    ARK_LINK_ERR_FORMAT,
    ARK_LINK_ERR_UNRESOLVED_SYMBOL,
    ARK_LINK_ERR_DUPLICATE_SYMBOL,
    ARK_LINK_ERR_BACKEND,
    ARK_LINK_ERR_INTERNAL,
    ARK_LINK_ERR_UNSUPPORTED,
    ARK_LINK_ERR_NOT_FOUND,
    ARK_LINK_ERR_MEMORY,
} ArkLinkResult;

typedef enum ArkLogLevel {
    ARK_LOG_ERROR,
    ARK_LOG_WARN,
    ARK_LOG_INFO,
    ARK_LOG_DEBUG,
} ArkLogLevel;

typedef enum ArkSubsystem {
    ARK_SUBSYSTEM_CONSOLE = 0,
    ARK_SUBSYSTEM_WINDOWS = 1,
} ArkSubsystem;

typedef void (*ArkLinkLogger)(ArkLogLevel level, const char* message, void* user_data);

typedef struct ArkLinkSession ArkLinkSession;

ArkLinkSession* arklink_session_create(void);
void arklink_session_destroy(ArkLinkSession* session);
ArkLinkResult arklink_session_set_target(ArkLinkSession* session, ArkLinkTarget target);
ArkLinkResult arklink_session_set_output(ArkLinkSession* session, const char* output_path);
ArkLinkResult arklink_session_set_output_kind(ArkLinkSession* session, ArkLinkOutputKind kind);
ArkLinkResult arklink_session_set_entry_point(ArkLinkSession* session, const char* entry_point);
ArkLinkResult arklink_session_set_subsystem(ArkLinkSession* session, ArkSubsystem subsystem);
ArkLinkResult arklink_session_set_image_base(ArkLinkSession* session, uint64_t image_base);
ArkLinkResult arklink_session_set_stack_size(ArkLinkSession* session, uint64_t stack_size);
ArkLinkResult arklink_session_add_input(ArkLinkSession* session, const char* path);
ArkLinkResult arklink_session_set_logger(ArkLinkSession* session, ArkLinkLogger logger, void* user_data);
ArkLinkResult arklink_session_link(ArkLinkSession* session);
const char* arklink_session_get_error(ArkLinkSession* session);

#ifdef __cplusplus
}
#endif

#endif
