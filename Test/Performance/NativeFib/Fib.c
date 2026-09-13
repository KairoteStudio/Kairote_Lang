#include <stdint.h>

#ifndef FIB_NAME
#define FIB_NAME fib_c
#endif

/* Separate translation unit, runtime input, no memoization or constant folding. */
__attribute__((noinline)) int32_t FIB_NAME(int32_t n) {
    if (n <= 1) {
        return n;
    }
    return FIB_NAME(n - 1) + FIB_NAME(n - 2);
}
