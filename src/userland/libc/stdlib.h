#ifndef STDLIB_H
#define STDLIB_H

#include <stddef.h>
#include <stdint.h>
#include <wchar.h>
extern char **environ;

typedef struct { int quot; int rem; } div_t;
typedef struct { long quot; long rem; } ldiv_t;
typedef struct { long long quot; long long rem; } lldiv_t;

void* malloc(size_t size);
void *aligned_alloc(size_t alignment, size_t size);
void free(void* ptr);
void* calloc(size_t nmemb, size_t size);
void* realloc(void* ptr, size_t size);
void *memset(void *s, int c, size_t n);
void *memcpy(void *dest, const void *src, size_t n);

#include "string.h"

// Math/Utility functions
int atoi(const char *nptr);
long atol(const char *nptr);
long long atoll(const char *nptr);
double atof(const char *nptr);
void itoa(int n, char *buf);
int abs(int x);
long labs(long x);
long long llabs(long long x);
double strtod(const char *nptr, char **endptr);
float strtof(const char *nptr, char **endptr);
long double strtold(const char *nptr, char **endptr);
long strtol(const char *nptr, char **endptr, int base);
long long strtoll(const char *nptr, char **endptr, int base);
unsigned long strtoul(const char *nptr, char **endptr, int base);
unsigned long long strtoull(const char *nptr, char **endptr, int base);
int rand(void);
void srand(unsigned int seed);
div_t div(int numer, int denom);
ldiv_t ldiv(long numer, long denom);
lldiv_t lldiv(long long numer, long long denom);
int mblen(const char *s, size_t n);
int mbtowc(wchar_t *pwc, const char *s, size_t n);
int wctomb(char *s, wchar_t wc);
size_t mbstowcs(wchar_t *dest, const char *src, size_t n);
size_t wcstombs(char *dest, const wchar_t *src, size_t n);
void qsort(void *base, size_t nmemb, size_t size, int (*compar)(const void *, const void *));
void *bsearch(const void *key, const void *base, size_t nmemb, size_t size, int (*compar)(const void *, const void *));

// IO functions
void puts(const char *s);
void printf(const char *fmt, ...);

// Runtime stubs
int system(const char *command);
char *getenv(const char *name);
void abort(void);
void _Exit(int status);
int at_quick_exit(void (*func)(void));
void quick_exit(int status);

// System/Process functions
int chdir(const char *path);
char* getcwd(char *buf, int size);
char *realpath(const char *path, char *resolved_path);
int access(const char *pathname, int mode);
void sleep(int ms);
int atexit(void (*func)(void));
void exit(int status);
void _exit(int status);

#endif
