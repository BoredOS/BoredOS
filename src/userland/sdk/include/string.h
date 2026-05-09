#ifndef BOREDOS_LIBC_STRING_H
#define BOREDOS_LIBC_STRING_H

#include <stddef.h>

void *memmove(void *dest, const void *src, size_t n);
int memcmp(const void *s1, const void *s2, size_t n);
void *memcpy(void *dest, const void *src, size_t n);
void *memset(void *s, int c, size_t n);
void *memchr(const void *s, int c, size_t n);
char *strchr(const char *s, int c);
char *strrchr(const char *s, int c);
char *strpbrk(const char *s, const char *accept);
char *strstr(const char *haystack, const char *needle);
size_t strspn(const char *s, const char *accept);
size_t strcspn(const char *s, const char *reject);
size_t strlen(const char *s);
int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, size_t n);
int strncasecmp(const char *s1, const char *s2, size_t n);
int strcasecmp(const char *s1, const char *s2);
int strcoll(const char *s1, const char *s2);
char* strcpy(char *dest, const char *src);
char* strcat(char *dest, const char *src);
char *strdup(const char *s);
char *strerror(int errnum);

#endif
