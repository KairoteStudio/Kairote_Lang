#define _GNU_SOURCE
#include <stdint.h>
#include <time.h>
#include <unistd.h>
#include <sys/syscall.h>

extern uint64_t Kernel(uint64_t a, uint64_t b);

int main(void) {
    struct timespec start, end;
    if (syscall(SYS_clock_gettime, CLOCK_MONOTONIC, &start) != 0) {
        return 90;
    }
    uint64_t result = Kernel(BENCH_A, BENCH_B);
    if (syscall(SYS_clock_gettime, CLOCK_MONOTONIC, &end) != 0) {
        return 90;
    }
    int64_t report[2] = {(end.tv_sec - start.tv_sec) * INT64_C(1000000000) + end.tv_nsec - start.tv_nsec,
                         (int64_t)result};
    if (syscall(SYS_write, 2, report, sizeof(report)) != sizeof(report)) {
        return 91;
    }
    return result == EXPECTED ? 0 : 1;
}
