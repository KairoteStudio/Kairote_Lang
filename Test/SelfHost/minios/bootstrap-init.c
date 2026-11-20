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


static int equal_files(const char *a, const char *b) {
    int x = open(a, MINIOS_O_RDONLY), y = open(b, MINIOS_O_RDONLY);
    if (x < 0 || y < 0) { printf("COMPARE-OPEN-FAIL %s %s %d %d\n",a,b,x,y); return 0; }
    printf("COMPARE-SIZES %s=%ld %s=%ld\n",a,lseek(x,0,MINIOS_SEEK_END),b,lseek(y,0,MINIOS_SEEK_END));
    lseek(x,0,MINIOS_SEEK_SET);lseek(y,0,MINIOS_SEEK_SET);
    char left[4096], right[4096]; unsigned long total = 0;
    for (;;) {
        long n = read(x,left,sizeof(left)), m = read(y,right,sizeof(right));
        if (n < 0 || m < 0 || n != m || (n && memcmp(left,right,n))) {
            printf("COMPARE-FAIL %s %s offset=%lu lengths=%ld,%ld\n",a,b,total,n,m);
            if(total==0 && n>=64 && m>=64){
                printf("HEADER-LEFT ");for(int i=0;i<64;i++)printf("%02x",(unsigned char)left[i]);printf("\n");
                printf("HEADER-RIGHT ");for(int i=0;i<64;i++)printf("%02x",(unsigned char)right[i]);printf("\n");
            }
            for(long i=0;i<n && i<m;i++)if(left[i]!=right[i]){printf("FIRST-DIFFERENCE %lu %02x %02x\n",total+i,(unsigned char)left[i],(unsigned char)right[i]);break;}
            close(x); close(y); return 0;
        }
        if (!n) break;
        total += n;
    }
    close(x); close(y);
    printf("BYTE-EQUAL %s %s bytes=%lu\n",a,b,total);
    return total > 0;
}
#ifndef KRT_BOOTSTRAP_INPUTS
static const char *krt_bootstrap_sources[]={"/src/Compiler-verified-input.krt",NULL};
static const char *krt_bootstrap_packed[]={"/src/Compiler-verified-input.krt",NULL};
#endif
static int prepare_sources(void) {
    for(int index=0;krt_bootstrap_sources[index];index++) {
        const char *target=krt_bootstrap_sources[index],*source=krt_bootstrap_packed[index];
        if(!strcmp(source,target)) continue;
        char path[256];
        if(strlen(target)>=sizeof(path))return 0;
        strcpy(path,target);
        for(char *part=path+1;*part;part++)if(*part=='/'){
            *part=0;int result=mkdir(path);*part='/';if(result<0&&result!=-17)return 0;
        }
        int in=open(source,MINIOS_O_RDONLY),out=open(target,MINIOS_O_CREAT|MINIOS_O_TRUNC|MINIOS_O_WRONLY);
        if(in<0||out<0)return 0;
        char buffer[4096];
        for(;;){long count=read(in,buffer,sizeof(buffer));if(count<0)return 0;if(!count)break;
            long offset=0;while(offset<count){long n=write(out,buffer+offset,count-offset);if(n<=0)return 0;offset+=n;}}
        close(in);close(out);
        printf("SOURCE-IDENTITY %s\n",target);
    }
    return 1;
}
static int compile(const char *compiler, const char *out) {
    const char *args[MINIOS_ARGS_COUNT+1];int count=0;
    args[count++]=compiler;args[count++]="--platform";args[count++]="minios-x64";args[count++]="-c";
    for(int index=0;krt_bootstrap_sources[index];index++){
        if((unsigned)count>=MINIOS_ARGS_COUNT-2)return -7;
        args[count++]=krt_bootstrap_sources[index];
    }
    args[count++]="-o";args[count++]=out;args[count]=NULL;
    return run(compiler,args);
}
static int link(const char *source, const char *out) {
    const char *args[] = {"/bin/ArkLink",source,"--target","elf","-o",out,NULL};
    return run("/bin/ArkLink",args);
}

static int capture(const char *compiler, const char *source, const char *log) {
    struct minios_args *args = calloc(1,sizeof(*args));
    if (!args) return -1;
    const char *argv[] = {compiler,"-c",source,"-o","/home/rejected.kro",NULL};
    args->magic=MINIOS_ARGS_MAGIC;
    size_t offset=0;
    while(argv[args->count]) {
        size_t n=strlen(argv[args->count])+1;
        args->offsets[args->count]=offset;
        memcpy(args->bytes+offset,argv[args->count],n);
        offset+=n;args->count++;
    }
    int fd=open(log,MINIOS_O_WRONLY|MINIOS_O_CREAT|MINIOS_O_TRUNC);
    if(fd<0){free(args);return fd;}
    int pid=fork();
    if(pid==0){
        dup2(fd,1);dup2(fd,2);close(fd);
        __syscall2(SYS_EXEC_ARGS,(long)compiler,(long)args);
        __syscall1(SYS_EXIT,127);
        for(;;){}
    }
    close(fd);free(args);
    if(pid<0)return pid;
    int status=-1;
    if(waitpid(pid,&status)<0)return -1;
    return status;
}
int main(void) {
    if(!prepare_sources()){puts("BOOTSTRAP-FAIL source-identity");shutdown(10);}
    puts("BOOTSTRAP-BEGIN stage2->3");
    if (compile("/bin/krtc","/home/stage3.kro")) { puts("BOOTSTRAP-FAIL stage3-compile");shutdown(1); }
    if (link("/home/stage3.kro","/home/stage3")) { puts("BOOTSTRAP-FAIL stage3-link");shutdown(2); }
    if (!equal_files("/src/stage2-final.kro","/home/stage3.kro") ||
        !equal_files("/bin/krtc","/home/stage3")) { puts("BOOTSTRAP-FAIL stage2-stage3");shutdown(3); }
    puts("BOOTSTRAP-BEGIN stage3->4");
    if (compile("/home/stage3","/home/stage4.kro")) { puts("BOOTSTRAP-FAIL stage4-compile");shutdown(4); }
    if (link("/home/stage4.kro","/home/stage4")) { puts("BOOTSTRAP-FAIL stage4-link");shutdown(5); }
    if (!equal_files("/home/stage3.kro","/home/stage4.kro") ||
        !equal_files("/home/stage3","/home/stage4")) { puts("BOOTSTRAP-FAIL stage3-stage4");shutdown(6); }

    const char *compilers[]={"/bin/krtc","/home/stage3","/home/stage4"};
    const char *errors[]={"/src/bad-name.krt","/src/bad-type.krt","/src/bad-parse.krt"};
    for(int test=0;test<3;test++) {
        char paths[3][40];
        for(int stage=0;stage<3;stage++) {
            snprintf(paths[stage],sizeof(paths[stage]),"/home/diag-%d-%d.txt",test,stage+2);
            int status=capture(compilers[stage],errors[test],paths[stage]);
            printf("DIAGNOSTIC test=%d stage=%d status=%d\n",test,stage+2,status);
            if(status!=1){puts("BOOTSTRAP-FAIL diagnostic-status");shutdown(7);}
        }
        if(!equal_files(paths[0],paths[1])||!equal_files(paths[1],paths[2])){
            puts("BOOTSTRAP-FAIL diagnostic-bytes");shutdown(8);
        }
    }
    puts("BOOTSTRAP-DIAGNOSTICS-PASS");
    puts("BOOTSTRAP-RAW-BYTES-PASS");
    shutdown(0);
}
