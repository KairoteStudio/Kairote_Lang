#ifndef KRT_PATH_H
#define KRT_PATH_H

#include <stddef.h>
#include <stdbool.h>

double KrtTimeNowSeconds(void);
int KrtPathExists(const char* path);
int KrtCreateDirectory(const char* path);
char* KrtGetDirectory(const char* path);
char* KrtGetFilename(const char* path);
char* KrtGetExtension(const char* path);
char* KrtJoinPath(const char* base, const char* relative);
char* KrtResolvePath(const char* path);
char* KrtGetCurrentDirectory(void);
char* KrtReadFile(const char* filename, size_t* length);
int KrtWriteFile(const char* filename, const char* content, size_t length);
int KrtGetExecutableDirectory(char* result, size_t size);

#endif
