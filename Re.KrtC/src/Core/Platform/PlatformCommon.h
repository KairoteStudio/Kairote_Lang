#ifndef PLATFORM_H
#define PLATFORM_H

#ifdef _WIN32
#define KRT_PLATFORM_WINDOWS 1
#define KRT_PLATFORM_LINUX 0
#elif defined(__linux__)
#define KRT_PLATFORM_WINDOWS 0
#define KRT_PLATFORM_LINUX 1
#else
#error "Unsupported platform"
#endif

#ifdef _WIN32
#define KRT_EXPORT __declspec(dllexport)
#define KRT_IMPORT __declspec(dllimport)
#define KRT_API __stdcall
#else

#define KRT_EXPORT
#define KRT_IMPORT
#define KRT_API
#endif

#ifdef _WIN32
#include <windows.h>
typedef HANDLE KrtHandleT;
typedef DWORD KrtDwordT;
typedef WORD KrtWordT;
#define KRT_INVALID_HANDLE INVALID_HANDLE_VALUE
#else
#include <unistd.h>
#include <sys/types.h>
typedef int KrtHandleT;
typedef unsigned int KrtDwordT;
typedef unsigned short KrtWordT;
#define KRT_INVALID_HANDLE -1
#endif

#ifdef _WIN32
#define KRT_STDIN_HANDLE GetStdHandle(STD_INPUT_HANDLE)
#define KRT_STDOUT_HANDLE GetStdHandle(STD_OUTPUT_HANDLE)
#define KRT_STDERR_HANDLE GetStdHandle(STD_ERROR_HANDLE)
#else
#define KRT_STDIN_HANDLE 0
#define KRT_STDOUT_HANDLE 1
#define KRT_STDERR_HANDLE 2
#endif

#ifdef _WIN32
#define KrtWriteConsole(handle, buffer, count, written)                                                                \
    do {                                                                                                               \
        DWORD _written;                                                                                                \
        WriteConsole(handle, buffer, count, &_written, NULL);                                                          \
        *(written) = _written;                                                                                         \
    } while (0)
#define KrtReadConsole(handle, buffer, count, read, ctrl)                                                              \
    do {                                                                                                               \
        DWORD _read;                                                                                                   \
        ReadConsole(handle, buffer, count, &_read, ctrl);                                                              \
        *(read) = _read;                                                                                               \
    } while (0)
#define KrtWriteFileWin32(handle, buffer, count, written, overlapped)                                                  \
    do {                                                                                                               \
        DWORD _written;                                                                                                \
        WriteFile(handle, buffer, count, &_written, overlapped);                                                       \
        *(written) = _written;                                                                                         \
    } while (0)
#define KrtReadFileWin32(handle, buffer, count, read, overlapped)                                                      \
    do {                                                                                                               \
        DWORD _read;                                                                                                   \
        ReadFile(handle, buffer, count, &_read, overlapped);                                                           \
        *(read) = _read;                                                                                               \
    } while (0)
#else
#include <sys/syscall.h>
#define KrtWriteConsole(handle, buffer, count, written) (*(written) = write(handle, buffer, count))
#define KrtReadConsole(handle, buffer, count, read_result, ctrl) (*(read_result) = read(handle, buffer, count))
#define KrtWriteFilePosix(handle, buffer, count, written, overlapped) (*(written) = write(handle, buffer, count))
#define KrtReadFilePosix(handle, buffer, count, read_result, overlapped) (*(read_result) = read(handle, buffer, count))
#endif

#ifdef _WIN32
#ifndef _WINDOWS_
#include <windows.h>
#endif
#define KrtMallocImpl(size) HeapAlloc(GetProcessHeap(), 0, size)
#define KrtFreeImpl(ptr) HeapFree(GetProcessHeap(), 0, ptr)
#define KrtReallocImpl(ptr, size) HeapReAlloc(GetProcessHeap(), 0, ptr, size)
#define KrtCallocImpl(num, size) HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (num) * (size))
#else
#include <unistd.h>
#include <sys/mman.h>

void* KrtMallocImpl(size_t size);
void KrtFreeImpl(void* ptr);
void* KrtReallocImpl(void* ptr, size_t size);
void* KrtCallocImpl(size_t num, size_t size);
#endif

#ifdef _WIN32
#include <process.h>
typedef HANDLE KrtThreadT;
typedef DWORD KrtThreadIdT;
#define KrtThreadCreate(handle, routine, arg) (*(handle) = (HANDLE)_beginthreadex(NULL, 0, routine, arg, 0, NULL))
#define KrtThreadWait(handle) WaitForSingleObject(handle, INFINITE)
#define KrtThreadClose(handle) CloseHandle(handle)
#define KrtMutexInit(mutex) InitializeCriticalSection(mutex)
#define KrtMutexLock(mutex) EnterCriticalSection(mutex)
#define KrtMutexUnlock(mutex) LeaveCriticalSection(mutex)
#define KrtMutexDestroy(mutex) DeleteCriticalSection(mutex)
typedef CRITICAL_SECTION KrtMutexT;
#else
#include <pthread.h>
typedef pthread_t KrtThreadT;
typedef pthread_t KrtThreadIdT;
#define KrtThreadCreate(handle, routine, arg) pthread_create(handle, NULL, routine, arg)
#define KrtThreadWait(handle) pthread_join(handle, NULL)
#define KrtThreadClose(handle)
#define KrtMutexInit(mutex) pthread_mutex_init(mutex, NULL)
#define KrtMutexLock(mutex) pthread_mutex_lock(mutex)
#define KrtMutexUnlock(mutex) pthread_mutex_unlock(mutex)
#define KrtMutexDestroy(mutex) pthread_mutex_destroy(mutex)
typedef pthread_mutex_t KrtMutexT;
#endif

#ifdef _WIN32
#include <time.h>
#define KrtTimeMs() GetTickCount64()
#define KrtTimeUs() (GetTickCount64() * 1000)
#define KrtSleepMs(ms) Sleep(ms)
#else
#include <sys/time.h>
#include <unistd.h>
static inline unsigned long long KrtTimeMs(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (unsigned long long)(tv.tv_sec) * 1000 + (unsigned long long)(tv.tv_usec) / 1000;
}
#define KrtTimeUs() (KrtTimeMs() * 1000)
#define KrtSleepMs(ms) usleep((ms) * 1000)
#endif

#ifdef _WIN32
#define KrtFileExists(path) (GetFileAttributes(path) != INVALID_FILE_ATTRIBUTES)
#define KrtDirectoryExists(path)                                                                                       \
    (GetFileAttributes(path) != INVALID_FILE_ATTRIBUTES && GetFileAttributes(path) & FILE_ATTRIBUTE_DIRECTORY)
#define KrtCreateDirectory(path) CreateDirectory(path, NULL)
#define KrtRemoveDirectory(path) RemoveDirectory(path)
#define KrtDeleteFile(path) DeleteFile(path)
#define KRT_PATH_SEPARATOR '\\'
#define KRT_PATH_SEPARATOR_STR "\\"
#else
#include <sys/stat.h>
#include <unistd.h>
static inline int KrtFileExists(const char* path) {
    struct stat st;
    return (stat(path, &st) == 0) && S_ISREG(st.st_mode);
}
static inline int KrtDirectoryExists(const char* path) {
    struct stat st;
    return (stat(path, &st) == 0) && S_ISDIR(st.st_mode);
}
#define KrtCreateDirectory(path) mkdir(path, 0755)
#define KrtRemoveDirectory(path) rmdir(path)
#define KrtDeleteFile(path) unlink(path)
#define KRT_PATH_SEPARATOR '/'
#define KRT_PATH_SEPARATOR_STR "/"
#endif

#ifdef _WIN32
#define KrtLoadLibrary(name) LoadLibrary(name)
#define KrtFreeLibrary(handle) FreeLibrary(handle)
#define KrtGetProcAddress(handle, name) GetProcAddress(handle, name)
typedef HMODULE KrtLibHandleT;
#else
#include <dlfcn.h>
#define KrtLoadLibrary(name) dlopen(name, RTLD_LAZY)
#define KrtFreeLibrary(handle) dlclose(handle)
#define KrtGetProcAddress(handle, name) dlsym(handle, name)
typedef void* KrtLibHandleT;
#endif

#ifdef _WIN32
#define KrtAtomicInc(ptr) InterlockedIncrement(ptr)
#define KrtAtomicDec(ptr) InterlockedDecrement(ptr)
#define KrtAtomicAdd(ptr, val) InterlockedExchangeAdd(ptr, val)
#define KrtAtomicXchg(ptr, val) InterlockedExchange(ptr, val)
#define KrtAtomicCmpXchg(ptr, cmp, xchg) InterlockedCompareExchange(ptr, xchg, cmp)
#else
#include <stdatomic.h>
#define KrtAtomicInc(ptr) __atomic_add_fetch(ptr, 1, __ATOMIC_SEQ_CST)
#define KrtAtomicDec(ptr) __atomic_sub_fetch(ptr, 1, __ATOMIC_SEQ_CST)
#define KrtAtomicAdd(ptr, val) __atomic_add_fetch(ptr, val, __ATOMIC_SEQ_CST)
#define KrtAtomicXchg(ptr, val) __atomic_exchange_n(ptr, val, __ATOMIC_SEQ_CST)
#define KrtAtomicCmpXchg(ptr, cmp, xchg)                                                                               \
    __atomic_compare_exchange_n(ptr, cmp, xchg, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)
#endif

#ifdef _WIN32
#define KrtGetLastError() GetLastError()
#define KrtSetLastError(err) SetLastError(err)
#define KrtError_SUCCESS 0
#define KrtError_INVALID_PARAMETER ERROR_INVALID_PARAMETER
#define KrtError_FILE_NOT_FOUND ERROR_FILE_NOT_FOUND
#define KrtError_ACCESS_DENIED ERROR_ACCESS_DENIED
#define KrtError_OUT_OF_MEMORY ERROR_NOT_ENOUGH_MEMORY
#else
#include <errno.h>
#define KrtGetLastError() errno
#define KrtSetLastError(err) errno = err
#define KrtError_SUCCESS 0
#define KrtError_INVALID_PARAMETER EINVAL
#define KrtError_FILE_NOT_FOUND ENOENT
#define KrtError_ACCESS_DENIED EACCES
#define KrtError_OUT_OF_MEMORY ENOMEM
#endif

#ifdef _WIN32
#define KRT_INLINE __forceinline
#define KRT_NOINLINE __declspec(noinline)
#define KRT_PACKED __pragma(pack(push, 1))
#define KRT_UNPACKED __pragma(pack(pop))
#define KRT_ALIGNED(n) __declspec(align(n))
#else
#define KRT_INLINE static inline
#define KRT_NOINLINE __attribute__((noinline))
#define KRT_PACKED __attribute__((packed))
#define KRT_UNPACKED
#define KRT_ALIGNED(n) __attribute__((aligned(n)))
#endif

#ifdef _WIN32
#define KRT_LINKER_OUTPUT_FLAG "-o"
#define KRT_LINKER_CONSOLE_FLAG "-subsystem:console"
#define KRT_LINKER_ENTRY_FLAG "-entry:main"
#define KRT_LINKER_LIB_PATH_FLAG "-libpath:"
#define KRT_LINKER_RUNTIME_LIBS "kernel32.lib user32.lib msvcrt.lib"
#define KRT_LINKER_CMD "ld.exe"
#else
#define KRT_LINKER_OUTPUT_FLAG "-o"
#define KRT_LINKER_CONSOLE_FLAG
#define KRT_LINKER_ENTRY_FLAG "-e"
#define KRT_LINKER_LIB_PATH_FLAG "-L"
#define KRT_LINKER_RUNTIME_LIBS "-lc -lm -lpthread"
#define KRT_LINKER_CMD "ld"
#endif

#ifdef _WIN32
#define KRT_ASM_FORMAT "win64"
#define KRT_ASM_OUTPUT_FLAG "-f"
#define KRT_ASM_CMD "nasm.exe"
#else
#define KRT_ASM_FORMAT "elf64"
#define KRT_ASM_OUTPUT_FLAG "-f"
#define KRT_ASM_CMD "nasm"
#endif

#endif
