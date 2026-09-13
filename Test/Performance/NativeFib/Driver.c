#define _GNU_SOURCE
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int32_t fib_c(int32_t n);
int32_t fib_c_o2(int32_t n);
int32_t fib_asm(int32_t n);

static uint64_t nanoseconds(struct timespec time) {
    return (uint64_t)time.tv_sec * 1000000000 + time.tv_nsec;
}

int main(int argc, char** argv) {
    if (argc != 3) {
        return 2;
    }
    char* end;
    long n = strtol(argv[2], &end, 10);
    if (*end || n < 0 || n > 40) {
        return 2;
    }
    int32_t (*fib)(int32_t) = NULL;
    if (!strcmp(argv[1], "asm")) {
        fib = fib_asm;
    }
    if (!strcmp(argv[1], "c_recursive")) {
        fib = fib_c;
    }
    if (!strcmp(argv[1], "c_o2")) {
        fib = fib_c_o2;
    }
    if (!fib) {
        return 2;
    }
    struct timespec start, finish;
    if (clock_gettime(CLOCK_MONOTONIC_RAW, &start)) {
        return 3;
    }
    int32_t value = fib((int32_t)n);
    if (clock_gettime(CLOCK_MONOTONIC_RAW, &finish)) {
        return 3;
    }
    printf("{\"result\":%" PRId32 ",\"duration_ns\":%" PRIu64 "}\n", value, nanoseconds(finish) - nanoseconds(start));
    return 0;
}
