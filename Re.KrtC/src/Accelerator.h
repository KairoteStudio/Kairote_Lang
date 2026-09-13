#ifndef KRT_ACCELERATOR_H
#define KRT_ACCELERATOR_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

size_t accelerator_strlen(const char* str);
int accelerator_strcmp(const char* str1, const char* str2);

double accelerator_add_double(double a, double b);
double accelerator_sub_double(double a, double b);
double accelerator_mul_double(double a, double b);
double accelerator_div_double(double a, double b);

void* accelerator_memcpy(void* dest, const void* src, size_t n);
void* accelerator_memset(void* s, int c, size_t n);

bool accelerator_is_alpha(char c);
bool accelerator_is_digit(char c);
bool accelerator_is_octal_digit(char c);
bool accelerator_is_hex_digit(char c);
int accelerator_hex_to_int(char c);
int encode_utf8(char* buffer, int codepoint, int max_length);
bool accelerator_is_whitespace(char c);

uint32_t accelerator_hash_string(const char* str);

#endif
