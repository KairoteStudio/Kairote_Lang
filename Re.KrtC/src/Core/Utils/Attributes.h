#ifndef KRT_ATTRIBUTES_H
#define KRT_ATTRIBUTES_H

#if defined(__GNUC__) || defined(__clang__)
#define KRT_PRINTF_FORMAT(format_index, first_argument) __attribute__((format(printf, format_index, first_argument)))
#else
#define KRT_PRINTF_FORMAT(format_index, first_argument)
#endif

#endif
