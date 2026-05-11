// Copyright (c) 2023-2026 Chris (boreddevnl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
#ifndef KUTILS_H
#define KUTILS_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

// Kernel string utilities
void *memmove(void *dest, const void *src, uint64_t n);
void memset(void *dest, int val, size_t len);
void memcpy(void *dest, const void *src, size_t len);
int memcmp (const void *str1, const void *str2, size_t count);
size_t strlen(const char *str);
int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, size_t n);
void strcpy(char *dest, const char *src);
int atoi(const char *str);
void itoa(int n, char *buf);
void itoa_hex(uint64_t n, char *buf);

// Kernel timing utilities
void k_delay(int iterations);
void k_sleep(int ms);
void k_reboot(void);
void k_shutdown(void);
void k_beep(int freq, int ms);
void k_beep_process(void);
char *k_strstr(const char *haystack, const char *needle);

#endif
