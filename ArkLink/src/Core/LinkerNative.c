#include "ArkLink/LinkerNative.h"
#include "ArkLink/Arklink.h"
#include "ArkLink/Loader.h"
#include "ArkLink/Resolver.h"
#include "ArkLink/BackendPe.h"
#include "ArkLink/BackendElf.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <elf.h>

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

static void log_message(struct ArkLinkSession* session, ArkLogLevel level, const char* message) {
    if (session && session->logger) {
        session->logger(level, message, session->logger_user_data);
    }
}

ArkLinkResult arklink_session_link_native(ArkLinkSession* session) {
    if (!session || session->input_count == 0) {
        return ARK_LINK_ERR_INVALID_ARGUMENT;
    }

    log_message(session, ARK_LOG_INFO, "Loading input files...");

    ArkLinkUnit** units = (ArkLinkUnit**)calloc(session->input_count, sizeof(ArkLinkUnit*));
    if (!units) {
        strncpy(session->error_buffer, "Failed to allocate memory for units", sizeof(session->error_buffer) - 1);
        return ARK_LINK_ERR_MEMORY;
    }

    ArkLoaderOptions loader_opts = {0};
    ArkLoaderDiagnostics diag = {0};

    ArkLinkContext* ctx = ark_context_create(session->target);
    if (!ctx) {
        free(units);
        strncpy(session->error_buffer, "Failed to create context", sizeof(session->error_buffer) - 1);
        return ARK_LINK_ERR_MEMORY;
    }

    for (size_t i = 0; i < session->input_count; i++) {
        ArkLinkResult result = ark_loader_load_unit(ctx, session->inputs[i], &loader_opts, &units[i], &diag);
        if (result != ARK_LINK_OK) {
            snprintf(session->error_buffer, sizeof(session->error_buffer),
                     "Failed to load %s: %s", session->inputs[i], diag.message ? diag.message : "Unknown error");

            for (size_t j = 0; j < i; j++) {
                if (units[j]) ark_link_unit_destroy(units[j]);
            }
            free(units);
            ark_context_destroy(ctx);
            return result;
        }


    }

    log_message(session, ARK_LOG_INFO, "Resolving symbols...");

    ArkResolverPlan plan = {0};
    ArkLinkResult result = ark_resolver_resolve(ctx, units, session->input_count, &plan);
    if (result != ARK_LINK_OK) {
        strncpy(session->error_buffer, "Symbol resolution failed", sizeof(session->error_buffer) - 1);

        for (size_t i = 0; i < session->input_count; i++) {
            if (units[i]) ark_link_unit_destroy(units[i]);
        }
        free(units);
        ark_context_destroy(ctx);
        return result;
    }

    if (plan.backend_input) {
        if (session->image_base) {
            plan.backend_input->image_base = session->image_base;
        } else if (session->target == ARK_LINK_TARGET_PE) {
            plan.backend_input->image_base = 0x140000000ULL;
        } else {
            plan.backend_input->image_base = 0x400000ULL;
        }
    }

    log_message(session, ARK_LOG_INFO, "Linking with backend...");

    ArkBackendOutput output = {0};

    switch (session->target) {
        case ARK_LINK_TARGET_PE:
            result = ark_backend_pe_link(ctx, plan.backend_input, &output);
            break;
        case ARK_LINK_TARGET_ELF:
            result = ark_backend_elf_link(ctx, plan.backend_input, &output);
            break;
        default:
            result = ARK_LINK_ERR_UNSUPPORTED;
            break;
    }

    if (result != ARK_LINK_OK) {
        strncpy(session->error_buffer, "Backend linking failed", sizeof(session->error_buffer) - 1);
        ark_resolver_plan_destroy(ctx, &plan);
        for (size_t i = 0; i < session->input_count; i++) {
            if (units[i]) ark_link_unit_destroy(units[i]);
        }
        free(units);
        ark_context_destroy(ctx);
        return result;
    }

    log_message(session, ARK_LOG_INFO, "Writing output file...");

    FILE* out_file = fopen(session->output_path, "wb");
    if (!out_file) {
        snprintf(session->error_buffer, sizeof(session->error_buffer),
                 "Failed to open output file: %s", session->output_path);
        free(output.data);
        free(output.section_maps);
        ark_resolver_plan_destroy(ctx, &plan);
        for (size_t i = 0; i < session->input_count; i++) {
            if (units[i]) ark_link_unit_destroy(units[i]);
        }
        free(units);
        ark_context_destroy(ctx);
        return ARK_LINK_ERR_IO;
    }

    size_t written = fwrite(output.data, 1, output.size, out_file);
    fclose(out_file);
    if (written != output.size) {
        strncpy(session->error_buffer, "Failed to write output file", sizeof(session->error_buffer) - 1);
        free(output.data);
        free(output.section_maps);
        ark_resolver_plan_destroy(ctx, &plan);
        for (size_t i = 0; i < session->input_count; i++) {
            if (units[i]) ark_link_unit_destroy(units[i]);
        }
        free(units);
        ark_context_destroy(ctx);
        return ARK_LINK_ERR_IO;
    }

    free(output.data);
    free(output.section_maps);
    ark_resolver_plan_destroy(ctx, &plan);

    for (size_t i = 0; i < session->input_count; i++) {
        if (units[i]) ark_link_unit_destroy(units[i]);
    }
    free(units);
    ark_context_destroy(ctx);

    log_message(session, ARK_LOG_INFO, "Link completed successfully");

    return ARK_LINK_OK;
}