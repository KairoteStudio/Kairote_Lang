#define _GNU_SOURCE
#include <stdint.h>
#include <stdlib.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#ifndef FIB_INPUT
#define FIB_INPUT 35
#endif

int32_t Fib(int32_t n);

static int64_t now(void) {
    struct timespec stamp;
    if (syscall(SYS_clock_gettime, CLOCK_MONOTONIC, &stamp)) {
        return -1;
    }
    return (int64_t)stamp.tv_sec * 1000000000 + stamp.tv_nsec;
}

int main(int argc, char** argv) {
    int32_t n = FIB_INPUT;
    if (argc > 2) {
        return 2;
    }
    if (argc == 2) {
        char* end;
        long value = strtol(argv[1], &end, 10);
        if (!*argv[1] || *end || value < -100 || value > 40) {
            return 2;
        }
        n = (int32_t)value;
    }
    int64_t started = now();
    int32_t value = Fib(n);
    int64_t stopped = now();
    if (started < 0 || stopped < started) {
        return 90;
    }
    int64_t report[] = {stopped - started, value};
    return write(STDERR_FILENO, report, sizeof(report)) == sizeof(report) ? 0 : 91;
}
