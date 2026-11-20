#include <minios/minios.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int run(const char *path, const char **argv) {
    struct minios_args *args = calloc(1, sizeof(*args));
    if (!args) return -1;
    args->magic = MINIOS_ARGS_MAGIC;
    size_t bytes = 0;
    while (argv[args->count]) {
        if (args->count >= MINIOS_ARGS_COUNT) { free(args); return -2; }
        size_t size = strlen(argv[args->count]) + 1;
        if (bytes + size > MINIOS_ARGS_BYTES) { free(args); return -3; }
        args->offsets[args->count] = bytes;
        memcpy(args->bytes + bytes, argv[args->count], size);
        bytes += size;
        args->count++;
    }
    printf("PORT-RUN %s\n", path);
    int process = spawn_argv(path, args);
    free(args);
    if (process < 0) { printf("PORT-SPAWN-ERROR %d\n", process); return process; }
    int status = -1;
    int waited = waitpid(process, &status);
    printf("PORT-RESULT %s wait=%d status=%d\n", path, waited, status);
    return waited < 0 ? waited : status;
}

int main(void){const char*args[]={"/bin/krtc","space argument","","中文",NULL};int code=run("/bin/krtc",args);if(code!=42){puts("SYS-RUNTIME-FAIL");shutdown(1);}puts("SYS-RUNTIME-PASS");shutdown(0);}
