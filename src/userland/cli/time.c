// Copyright (c) 2023-2026 Chris (boreddevnl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.

#include "stdlib.h"
#include "syscall.h"

#define CMDLINE_MAX 512

static int sc_strcmp(const char *a, const char *b) {
    while (*a && *a == *b) {
        a++;
        b++;
    }

    return (unsigned char)*a - (unsigned char)*b;
}

static int has_slash(const char *s) {
    while (s && *s) {
        if (*s == '/')
            return 1;
        s++;
    }

    return 0;
}

static int ends_with_elf(const char *s) {
    int len;

    if (!s)
        return 0;

    len = strlen(s);

    if (len < 4)
        return 0;

    return sc_strcmp(s + len - 4, ".elf") == 0;
}

static void print_usage(void) {
    printf("Usage: time <command> [args...]\n");
    printf("\n");
    printf("Examples:\n");
    printf("  time ls\n");
    printf("  time hexdump file.txt\n");
    printf("  time /bin/hexdump.elf file.txt\n");
}

// Read the system uptime in milliseconds by reading /proc/uptime and parsing the first number (seconds).
static unsigned long long read_uptime_ms(void) {
    char buf[64];
    int fd;
    int bytes;
    int seconds;

    fd = sys_open("/proc/uptime", "r");

    if (fd < 0)
        return 0;

    bytes = sys_read(fd, buf, sizeof(buf) - 1);

    sys_close(fd);

    if (bytes <= 0)
        return 0;

    buf[bytes] = 0;

    seconds = atoi(buf);

    return (unsigned long long)seconds * 1000ULL;
}

// Build the command line for execution
// If the first argument contains a slash, use it as is. Otherwise, prepend "/bin/" and append ".elf" if it doesn't already end with ".elf".
static void build_command_line(int argc, char **argv, char *out) {
    int i;

    out[0] = 0;

    if (has_slash(argv[1])) {
        strcat(out, argv[1]);
    } else {
        strcat(out, "/bin/");
        strcat(out, argv[1]);

        if (!ends_with_elf(argv[1])) {
            strcat(out, ".elf");
        }
    }

    for (i = 2; i < argc; i++) {
        strcat(out, " ");
        strcat(out, argv[i]);
    }
}

int main(int argc, char **argv) {
    char cmdline[CMDLINE_MAX];

    unsigned long long start;
    unsigned long long end;
    unsigned long long elapsed;

    int ret;

    if (argc < 2) {
        print_usage();
        return 1;
    }

    if (sc_strcmp(argv[1], "-h") == 0 ||
        sc_strcmp(argv[1], "--help") == 0) {
        print_usage();
        return 0;
    }

    build_command_line(argc, argv, cmdline);

    start = read_uptime_ms();

    ret = system(cmdline);

    end = read_uptime_ms();

    if (end >= start)
        elapsed = end - start;
    else
        elapsed = 0;

    printf("\n");
    printf("Command: %s\n", cmdline);
    printf("Exit code: %d\n", ret);
    printf("Elapsed: %llu ms\n", elapsed);

    return ret;
}
