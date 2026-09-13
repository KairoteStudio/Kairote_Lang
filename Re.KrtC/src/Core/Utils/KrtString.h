#ifndef KRT_STRING_H
#define KRT_STRING_H

#include <stddef.h>
#include <string.h>
#include <stdio.h>

#define KRT_STRCPY_S(dest, dest_size, src)                                                                             \
    do {                                                                                                               \
        if ((dest) != NULL && (dest_size) > 0 && (src) != NULL) {                                                      \
            size_t src_len = strlen(src);                                                                              \
            size_t copy_len = (src_len < (dest_size) - 1) ? src_len : (dest_size) - 1;                                 \
            memcpy(dest, src, copy_len);                                                                               \
            (dest)[copy_len] = '\0';                                                                                   \
        }                                                                                                              \
    } while (0)

#define KRT_STRCAT_S(dest, dest_size, src)                                                                             \
    do {                                                                                                               \
        if ((dest) != NULL && (dest_size) > 0 && (src) != NULL) {                                                      \
            size_t dest_len = strlen(dest);                                                                            \
            size_t src_len = strlen(src);                                                                              \
            size_t remaining = (dest_size) - dest_len - 1;                                                             \
            if (remaining > 0) {                                                                                       \
                size_t copy_len = (src_len < remaining) ? src_len : remaining;                                         \
                memcpy((dest) + dest_len, src, copy_len);                                                              \
                (dest)[dest_len + copy_len] = '\0';                                                                    \
            }                                                                                                          \
        }                                                                                                              \
    } while (0)

#define KRT_SPRINTF_S(buffer, buffer_size, format, ...) snprintf(buffer, buffer_size, format, __VA_ARGS__)

size_t KrtStrlcpy(char* dest, const char* src, size_t dest_size);
size_t KrtStrlcat(char* dest, const char* src, size_t dest_size);

size_t KrtCalculateConcatenatedSize(const char* base, const char** parts, int part_count);

#endif
