#ifndef _DLFCN_H
#define _DLFCN_H
#define RTLD_LAZY 0x00001
#define RTLD_NOW 0x00002
void *dlopen(const char *filename, int flag);
char *dlerror(void);
void *dlsym(void *handle, const char *symbol);
int dlclose(void *handle);
#endif
