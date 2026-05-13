// Copyright (c) 2026 Lluciocc (https://github.com/lluciocc)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
// BOREDOS_APP_DESC: List running processes.

#include "../libc/syscall.h"
#include "../libc/stdlib.h"
#include "../libc/string.h"
#include "../libc/stdio.h"

#define MAX_PROC_ENTRIES 64

static int sc_strcmp(const char *a, const char *b) {
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

static int is_numeric(const char *s) {
    int i = 0;

    if (!s || !s[0])
        return 0;

    while (s[i]) {
        if (s[i] < '0' || s[i] > '9')
            return 0;
        i++;
    }

    return 1;
}

static void print_spaces(int count) {
    for (int i = 0; i < count; i++)
        printf(" ");
}

static void print_padded(const char *s, int width) {
    int len;

    if (!s)
        s = "";

    printf("%s", s);

    len = (int)strlen(s);

    if (len < width)
        print_spaces(width - len);
    else
        printf(" ");
}

static int find_value(const char *buf, const char *key) {
    char *p = (char*)buf;
    int key_len = strlen(key);

    while (*p) {
        if (memcmp(p, key, key_len) == 0 && p[key_len] == ':') {
            p += key_len + 1;

            while (*p == ' ' || *p == '\t')
                p++;

            return atoi(p);
        }

        while (*p && *p != '\n')
            p++;

        if (*p == '\n')
            p++;
    }

    return 0;
}

static void find_string(const char *buf,
                        const char *key,
                        char *out,
                        int max_len) {
    char *p = (char*)buf;
    int key_len = strlen(key);

    out[0] = 0;

    while (*p) {
        if (memcmp(p, key, key_len) == 0 && p[key_len] == ':') {
            int i = 0;

            p += key_len + 1;

            while (*p == ' ' || *p == '\t')
                p++;

            while (*p &&
                   *p != '\n' &&
                   *p != '\r' &&
                   i < max_len - 1) {
                out[i++] = *p++;
            }

            out[i] = 0;
            return;
        }

        while (*p && *p != '\n')
            p++;

        if (*p == '\n')
            p++;
    }
}

static int read_file_to_buf(const char *path,
                            char *buf,
                            int max_len) {
    int fd;
    int bytes;

    fd = sys_open(path, "r");

    if (fd < 0)
        return -1;

    bytes = sys_read(fd, buf, max_len - 1);

    sys_close(fd);

    if (bytes < 0)
        return -1;

    buf[bytes] = 0;

    return bytes;
}

static void format_mem(int kb, char *out) {
    char tmp[32];

    if (kb < 1024) {
        itoa(kb, tmp);

        strcpy(out, tmp);
        strcat(out, " KB");
    } else {
        int mb = kb / 1024;
        int frac = ((kb % 1024) * 10) / 1024;

        itoa(mb, tmp);

        strcpy(out, tmp);
        strcat(out, ".");

        itoa(frac, tmp);
        strcat(out, tmp);
        strcat(out, " MiB");
    }
}

static void print_usage(void) {
    printf("Usage: ps [options]\n");
    printf("\n");
    printf("Options:\n");
    printf("  -a       Show all processes\n");
    printf("  -i       Include idle tasks\n");
    printf("  -m       Show memory usage\n");
    printf("  -t       Show scheduler ticks\n");
    printf("  -p PID   Show only one process\n");
    printf("  -h       Show this help\n");
}

static void print_header(int show_mem,
                         int show_ticks,
                         int show_idle) {
    print_padded("PID", 8);
    print_padded("NAME", 22);

    if (show_mem)
        print_padded("MEMORY", 14);

    if (show_ticks)
        print_padded("TICKS", 12);

    if (show_idle)
        print_padded("IDLE", 8);

    printf("\n");
}

int main(int argc, char **argv) {
    int show_mem = 0;
    int show_ticks = 0;
    int include_idle = 0;
    int show_idle_col = 0;
    int filter_pid = -1;

    FAT32_FileInfo entries[MAX_PROC_ENTRIES];

    for (int i = 1; i < argc; i++) {
        if (sc_strcmp(argv[i], "-m") == 0) {
            show_mem = 1;
        } else if (sc_strcmp(argv[i], "-t") == 0) {
            show_ticks = 1;
        } else if (sc_strcmp(argv[i], "-a") == 0) {
            include_idle = 1;
            show_idle_col = 1;
        } else if (sc_strcmp(argv[i], "-i") == 0) {
            include_idle = 1;
            show_idle_col = 1;
        } else if (sc_strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
            filter_pid = atoi(argv[++i]);
        } else if (sc_strcmp(argv[i], "-h") == 0) {
            print_usage();
            return 0;
        } else {
            print_usage();
            return 1;
        }
    }

    int count = sys_list("/proc", entries, MAX_PROC_ENTRIES);

    if (count < 0) {
        printf("ps: failed to read /proc\n");
        return 1;
    }

    print_header(show_mem, show_ticks, show_idle_col);

    for (int i = 0; i < count; i++) {
        char path[96];
        char buf[512];
        char name[64];
        char mem_str[32];
        char tmp[32];

        int pid;
        int memory_kb;
        int ticks;
        int idle;

        if (!entries[i].is_directory)
            continue;

        if (!is_numeric(entries[i].name))
            continue;

        pid = atoi(entries[i].name);

        if (filter_pid >= 0 && pid != filter_pid)
            continue;

        strcpy(path, "/proc/");
        strcat(path, entries[i].name);
        strcat(path, "/status");

        if (read_file_to_buf(path, buf, sizeof(buf)) <= 0)
            continue;

        find_string(buf, "Name", name, sizeof(name));

        memory_kb = find_value(buf, "Memory");
        ticks = find_value(buf, "Ticks");
        idle = find_value(buf, "Idle");

        if (idle && !include_idle)
            continue;

        itoa(pid, tmp);
        print_padded(tmp, 8);

        if (!name[0])
            strcpy(name, "Unknown");

        print_padded(name, 22);

        if (show_mem) {
            format_mem(memory_kb, mem_str);
            print_padded(mem_str, 14);
        }

        if (show_ticks) {
            itoa(ticks, tmp);
            print_padded(tmp, 12);
        }

        if (show_idle_col) {
            print_padded(idle ? "yes" : "no", 8);
        }

        printf("\n");
    }

    return 0;
}