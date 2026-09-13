#include "Core/Memory/Arena.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static double now(void) {
    struct timespec time;
    assert(!clock_gettime(CLOCK_MONOTONIC, &time));
    return time.tv_sec + time.tv_nsec * 1e-9;
}

int main(int argc, char** argv) {
    assert(argc == 2);
    double start = now();
    if (!strcmp(argv[1], "free")) {
        void* pointers[30000];
        for (int i = 0; i < 30000; i++) {
            pointers[i] = KRT_MALLOC(32);
            assert(pointers[i]);
        }
        for (int i = 0; i < 30000; i++) {
            KRT_FREE(pointers[i]);
        }
    } else {
        KrtArena* arena = KrtArenaCreate(4096);
        assert(arena);
        for (int pass = 0; pass < 3; pass++) {
            for (int i = 0; i < 100000; i++) {
                void* ptr = KrtArenaAlloc(arena, 64);
                assert(ptr);
                memset(ptr, i, 64);
            }
            KrtArenaReset(arena);
        }
        KrtArenaDestroy(arena);
    }
    printf("%.6f\n", (now() - start) * 1000);
    return 0;
}
