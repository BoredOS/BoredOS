// Copyright (c) 2023-2026 Chris (boreddevnl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
#include "kutils.h"
#include "wm.h"
#include "io.h"

#include "../drivers/acpi.h"

void memset(void *dest, int val, size_t len) {
    unsigned char *ptr = (unsigned char *)dest;
    while (len-- > 0) *ptr++ = (unsigned char)val;
}

void memcpy(void *dest, const void *src, size_t len) {
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;
    while (len-- > 0) *d++ = *s++;
}

int memcmp(const void *str1, const void *str2, size_t count) {
    register const unsigned char *s1 = (const unsigned char*)str1;
    register const unsigned char *s2 = (const unsigned char*)str2;

    while (count-- > 0) {
        if (*s1++ != *s2++)
        return s1[-1] < s2[-1] ? -1 : 1;
    }
    return 0;
}

void *memmove(void *dest, const void *src, uint64_t n) {
    uint8_t *pdest = (uint8_t *)dest;
    const uint8_t *psrc = (const uint8_t *)src;

    if (src > dest) {
        for (uint64_t i = 0; i < n; i++) {
            pdest[i] = psrc[i];
        }
    } else if (src < dest) {
        for (uint64_t i = n; i > 0; i--) {
            pdest[i-1] = psrc[i-1];
        }
    }

    return dest;
}

size_t strlen(const char *str) {
    size_t len = 0;
    while (str[len]) len++;
    return len;
}

int strcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

int strncmp(const char *s1, const char *s2, size_t n) {
    while (n && *s1 && (*s1 == *s2)) {
        s1++;
        s2++;
        n--;
    }
    if (n == 0) return 0;
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

void strcpy(char *dest, const char *src) {
    while (*src) *dest++ = *src++;
    *dest = 0;
}

int atoi(const char *str) {
    int res = 0;
    int sign = 1;
    if (*str == '-') { sign = -1; str++; }
    while (*str >= '0' && *str <= '9') {
        res = res * 10 + (*str - '0');
        str++;
    }
    return res * sign;
}

void itoa(int n, char *buf) {
    if (n == 0) {
        buf[0] = '0'; buf[1] = 0; return;
    }
    int i = 0;
    int sign = n < 0;
    if (sign) n = -n;
    while (n > 0) {
        buf[i++] = (n % 10) + '0';
        n /= 10;
    }
    if (sign) buf[i++] = '-';
    buf[i] = 0;
    for (int j = 0; j < i / 2; j++) {
        char t = buf[j];
        buf[j] = buf[i - 1 - j];
        buf[i - 1 - j] = t;
    }
}

void itoa_hex(uint64_t n, char *buf) {
    const char *digits = "0123456789ABCDEF";
    if (n == 0) {
        buf[0] = '0';
        buf[1] = 0;
        return;
    }
    int i = 0;
    while (n > 0) {
        buf[i++] = digits[n & 0xF];
        n >>= 4;
    }
    buf[i] = 0;
    for (int j = 0; j < i / 2; j++) {
        char t = buf[j];
        buf[j] = buf[i - 1 - j];
        buf[i - 1 - j] = t;
    }
}

void k_delay(int iterations) {
    for (volatile int i = 0; i < iterations; i++) {
        __asm__ __volatile__("nop");
    }
}

void k_sleep(int ms) {
    // Timer is ~60Hz, so 1 tick = 16.66ms
    uint32_t ticks = ms / 16;
    if (ticks == 0 && ms > 0) ticks = 1;
    
    uint32_t target = wm_get_ticks() + ticks;
    while (wm_get_ticks() < target) {
        __asm__ __volatile__("hlt");
    }
}

void k_reboot(void) {
    outb(0x64, 0xFE);
}

void k_shutdown(void) {
    acpi_shutdown();
}

volatile uint64_t beep_end_tick = 0;
bool beep_active = false;

void k_beep(int freq, int ms) {
    if (freq <= 0) {
        outb(0x61, inb(0x61) & 0xFC);
        beep_active = false;
        return;
    }
    int div = 1193180 / freq;
    outb(0x43, 0xB6);
    outb(0x42, div & 0xFF);
    outb(0x42, (div >> 8) & 0xFF);
    outb(0x61, inb(0x61) | 0x03);
    
    uint32_t ticks = ms / 16;
    if (ticks == 0 && ms > 0) ticks = 1;
    extern volatile uint64_t kernel_ticks;
    beep_end_tick = kernel_ticks + ticks;
    beep_active = true;
}

void k_beep_process(void) {
    if (beep_active) {
        extern volatile uint64_t kernel_ticks;
        if (kernel_ticks >= beep_end_tick) {
            outb(0x61, inb(0x61) & 0xFC);
            beep_active = false;
        }
    }
}

char *k_strstr(const char *haystack, const char *needle) {
    if (!*needle) return (char *)haystack;
    for (; *haystack; haystack++) {
        const char *h = haystack;
        const char *n = needle;
        while (*h && *n && *h == *n) {
            h++;
            n++;
        }
        if (!*n) return (char *)haystack;
    }
    return NULL;
}

