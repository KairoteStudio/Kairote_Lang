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

int main(void) {
    const char *version[] = {"/bin/krtc", "--version", NULL};
    if (run("/bin/krtc", version) != 0) { puts("PORT-FAIL version"); shutdown(1); }
    const char *compile[] = {"/bin/krtc", "-c", "/src/answer.krt", "-o", "/home/answer.kro", NULL};
    if (run("/bin/krtc", compile) != 0) { puts("PORT-FAIL compile"); shutdown(2); }
    const char *link[] = {"/bin/ArkLink", "/home/answer.kro", "--target", "elf", "-o", "/home/answer", NULL};
    if (run("/bin/ArkLink", link) != 0) { puts("PORT-FAIL link"); shutdown(3); }
    const char *answer[] = {"/home/answer", "space argument", "", "中文", NULL};
    if (run("/home/answer", answer) != 42) { puts("PORT-FAIL answer"); shutdown(4); }
    const char *unsupported_name[] = {"/bin/krtc", "-c", "/src/answer.krt", "-o", "/home/space name.kro", NULL};
    if (run("/bin/krtc", unsupported_name) == 0) { puts("PORT-FAIL unsupported-name-succeeded"); shutdown(9); }
    puts("PORT-CAPABILITY ramfs-space-name-rejected");
    const char *automatic[] = {"/bin/krtc", "/home/answer.kro", "--linker", "/bin/ArkLink", "-o", "/home/linked-answer", NULL};
    if (run("/bin/krtc", automatic) != 0) { puts("PORT-FAIL automatic-link-argv"); shutdown(6); }
    const char *linked[] = {"/home/linked-answer", "space argument", "", "中文", NULL};
    if (run("/home/linked-answer", linked) != 42) { puts("PORT-FAIL automatic-linked-answer"); shutdown(7); }
    const char *many[513]; many[0] = "/bin/krtc";
    for (int index=1; index<512; index++) many[index]="--version";
    many[512]=NULL;
    if (run("/bin/krtc", many) != 0) { puts("PORT-FAIL 512-arguments"); shutdown(8); }
    const char *bad[] = {"/bin/krtc", "-c", "/src/bad.krt", "-o", "/home/bad.kro", NULL};
    if (run("/bin/krtc", bad) == 0) { puts("PORT-FAIL accepted-invalid-source"); shutdown(5); }
    const char *vm_compile[] = {"/bin/krtc", "--platform", "minios-x64", "target", "vm", "/src/vm.krt", "-o", "/home/program.vm", NULL};
    if (run("/bin/krtc", vm_compile) != 0) { puts("PORT-FAIL vm-compile"); shutdown(10); }
    const char *vm_run[] = {"/bin/krtc", "run-vm", "/home/program.vm", NULL};
    if (run("/bin/krtc", vm_run) != 42) { puts("PORT-FAIL vm-service-abi"); shutdown(11); }
    puts("VM-SERVICE-PASS");
    puts("PORT-SMOKE-PASS");
    shutdown(0);
}
