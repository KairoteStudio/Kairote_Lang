#ifndef KRT_PATH_H
#define KRT_PATH_H

#include <stddef.h>
#include <stdbool.h>

double KrtTimeNowSeconds(void);
int KrtPathExists(const char* path);
int KrtCreateDirectory(const char* path);
char* KrtGetDirectory(const char* path);
int KrtPathGetFilename(const char* path, char* filename, size_t size);
int KrtPathRemoveExtension(const char* path, char* result, size_t size);
int KrtPathJoin(char* result, size_t size, const char* path1, const char* path2);
char* KrtGetCurrentDirectory(void);
int KrtGetExecutableDirectory(char* result, size_t size);
int KrtEnsureDirectoryRecursive(const char* path);

#endif
