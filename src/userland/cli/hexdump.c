// Copyright (c) 2026 Lluciocc (https://github.com/lluciocc)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
// BOREDOS_APP_DESC: Display file contents in hexadecimal.

#include "syscall.h"
#include "stdlib.h"
#include "string.h"
#include "stdio.h"

#define BYTES_PER_LINE 16

static int sc_strcmp(const char *a, const char *b) {
    while (*a && *a == *b) {
        a++;
        b++;
    }

    return (unsigned char)*a - (unsigned char)*b;
}

static void print_usage(void) {
    printf("Usage: hexdump <file>\n");
    printf("\n");
    printf("Display file contents in hexadecimal.\n");
}

static void print_hex_byte(unsigned char b) {
    const char *hex = "0123456789ABCDEF";

    putchar(hex[(b >> 4) & 0xF]);
    putchar(hex[b & 0xF]);
}

static void print_hex32(unsigned int v) {
    const char *hex = "0123456789ABCDEF";

    for (int i = 7; i >= 0; i--) {
        putchar(hex[(v >> (i * 4)) & 0xF]);
    }
}

int main(int argc, char **argv) {
    int fd;
    int offset = 0;

    unsigned char buf[BYTES_PER_LINE];

    if (argc < 2) {
        print_usage();
        return 1;
    }

    if (sc_strcmp(argv[1], "-h") == 0 ||
        sc_strcmp(argv[1], "--help") == 0) {
        print_usage();
        return 0;
    }

    fd = sys_open(argv[1], "r");

    if (fd < 0) {
        printf("hexdump: cannot open '%s'\n", argv[1]);
        return 1;
    }

    while (1) {
        int bytes = sys_read(fd, buf, BYTES_PER_LINE);

        if (bytes <= 0)
            break;

        // Offset
        print_hex32(offset);
        printf("  ");

        // Hex bytes
        for (int i = 0; i < BYTES_PER_LINE; i++) {
            if (i < bytes) {
                print_hex_byte(buf[i]);
            } else {
                printf("  ");
            }

            printf(" ");

            if (i == 7)
                printf(" ");
        }

        printf(" |");

        // ASCII preview
        for (int i = 0; i < bytes; i++) {
            unsigned char c = buf[i];

            if (c >= 32 && c <= 126)
                putchar(c);
            else
                putchar('.');
        }

        printf("|\n");

        offset += bytes;
    }

    sys_close(fd);

    return 0;
}