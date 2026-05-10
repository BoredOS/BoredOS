// Copyright (c) 2023-2026 Chris (boreddevnl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
#ifndef KUTILS_H
#define KUTILS_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

// Kernel string utilities
void k_memset(void *dest, int val, size_t len);
void k_memcpy(void *dest, const void *src, size_t len);
int k_memcmp (const void *str1, const void *str2, size_t count);
size_t k_strlen(const char *str);
int k_strcmp(const char *s1, const char *s2);
int k_strncmp(const char *s1, const char *s2, size_t n);
void k_strcpy(char *dest, const char *src);
int k_atoi(const char *str);
void k_itoa(int n, char *buf);
void k_itoa_hex(uint64_t n, char *buf);

// Kernel timing utilities
void k_delay(int iterations);
void k_sleep(int ms);
void k_reboot(void);
void k_shutdown(void);
void k_beep(int freq, int ms);
void k_beep_process(void);
char *k_strstr(const char *haystack, const char *needle);

#endif
