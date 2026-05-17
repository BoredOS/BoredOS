// Copyright (c) 2023-2026 Christiaan (chris@boreddev.nl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
#include "../libc/syscall.h"
#include "../libc/stdlib.h"
#include "../libc/string.h"
#include "../libc/stdio.h"

#define MAX_PARTS 4
#define SECTOR_SIZE_BYTES 512ULL
#define ONE_MB (1024ULL * 1024ULL)
#define ONE_GB (1024ULL * 1024ULL * 1024ULL)

static void print_usage(void) {
    printf("fdisk [OPTIONS] /dev/DEVICE\n");
    printf("  -p, --print     Print partition table and exit\n");
    printf("  -s, --script    Non-interactive auto-partition\n");
    printf("      --mbr       Use MBR instead of GPT\n");
    printf("      --uefi      Include ESP (default with GPT)\n");
    printf("      --esp-size N  ESP size (b/mb/gb, default: 512mb)\n");
    printf("  -h, --help\n");
}

static int sc_strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

static int sc_atoi(const char *s) {
    int n = 0;
    while (*s >= '0' && *s <= '9') n = n * 10 + (*s++ - '0');
    return n;
}

static char sc_tolower(char ch) {
    if (ch >= 'A' && ch <= 'Z') return (char)(ch + 32);
    return ch;
}

static uint64_t sc_parse_size_bytes(const char *s) {
    uint64_t n = 0;
    int has_digit = 0;

    while (*s == ' ' || *s == '\t') s++;
    if (*s == '+') s++;

    while (*s >= '0' && *s <= '9') {
        has_digit = 1;
        n = n * 10ULL + (uint64_t)(*s - '0');
        s++;
    }

    if (!has_digit) return 0;

    while (*s == ' ' || *s == '\t') s++;

    if (*s == '\0') return n * ONE_MB;

    {
        char c1 = sc_tolower(*s);
        char c2 = sc_tolower(*(s + 1));

        if (c1 == 'g' && c2 == 'b') return n * ONE_GB;
        if (c1 == 'm' && c2 == 'b') return n * ONE_MB;
        if (c1 == 'b') return n;
        if (c1 == 'g' && c2 == '\0') return n * ONE_GB;
        if (c1 == 'm' && c2 == '\0') return n * ONE_MB;
    }

    return n * ONE_MB;
}

static uint32_t sc_bytes_to_sectors_ceil(uint64_t bytes) {
    if (bytes == 0) return 0;
    return (uint32_t)((bytes + SECTOR_SIZE_BYTES - 1ULL) / SECTOR_SIZE_BYTES);
}

static void sc_format_size(uint64_t bytes, char *out, size_t out_len) {
    if (bytes >= ONE_GB) {
        unsigned long long whole = (unsigned long long)(bytes / ONE_GB);
        uint64_t rem = bytes % ONE_GB;
        unsigned long long frac = (unsigned long long)((rem * 100ULL + (ONE_GB / 2)) / ONE_GB);
        if (frac >= 100ULL) { whole++; frac = 0; }
        snprintf(out, out_len, "%llu.%02llugb", whole, frac);
    } else if (bytes >= ONE_MB) {
        unsigned long long whole = (unsigned long long)(bytes / ONE_MB);
        uint64_t rem = bytes % ONE_MB;
        unsigned long long frac = (unsigned long long)((rem * 100ULL + (ONE_MB / 2)) / ONE_MB);
        if (frac >= 100ULL) { whole++; frac = 0; }
        snprintf(out, out_len, "%llu.%02llumb", whole, frac);
    } else {
        snprintf(out, out_len, "%llub", (unsigned long long)bytes);
    }
}

static void read_line_blocking(char *buf, int max_len) {
    int len = 0;
    while (1) {
        struct pollfd pfd = { .fd = 0, .events = POLLIN, .revents = 0 };
        sys_poll(&pfd, 1, -1);
        char ch;
        if (sys_tty_read_in(&ch, 1) <= 0) continue;
        if (ch == '\r' || ch == '\n') {
            sys_write(1, "\n", 1);
            break;
        }
        if (ch == '\b' || ch == 127) {
            if (len > 0) {
                len--;
                sys_write(1, "\b \b", 3);
            }
            continue;
        }
        if (len < max_len - 1) {
            buf[len++] = ch;
            sys_write(1, &ch, 1);
        }
    }
    buf[len] = '\0';
}

static void print_partition_table(const char *devname) {
    int n = sys_disk_get_count();
    int found = 0;
    printf("Partition table for /dev/%s:\n", devname);
    printf("%-10s %-12s %-12s %-10s %-6s %s\n",
           "Device", "Start", "End", "Size", "ESP", "FAT32");
    for (int i = 0; i < n; i++) {
        disk_info_t d;
        if (sys_disk_get_info(i, &d) != 0) continue;
        if (!d.is_partition) continue;
        char parent[16];
        int len = 0;
        while (devname[len]) len++;
        int match = 1;
        for (int j = 0; j < len; j++) {
            if (d.devname[j] != devname[j]) { match = 0; break; }
        }
        if (!match) continue;
        {
            char start_buf[24];
            char end_buf[24];
            char size_buf[24];
            uint64_t start_bytes = (uint64_t)d.lba_offset * SECTOR_SIZE_BYTES;
            uint64_t end_bytes = (uint64_t)(d.lba_offset + d.total_sectors - 1) * SECTOR_SIZE_BYTES;
            uint64_t size_bytes = (uint64_t)d.total_sectors * SECTOR_SIZE_BYTES;
            sc_format_size(start_bytes, start_buf, sizeof(start_buf));
            sc_format_size(end_bytes, end_buf, sizeof(end_buf));
            sc_format_size(size_bytes, size_buf, sizeof(size_buf));
            printf("/dev/%-5s %-12s %-12s %-10s %-6s %s\n",
                   d.devname,
                   start_buf,
                   end_buf,
                   size_buf,
                   d.is_esp ? "yes" : "no",
                   d.is_fat32 ? "yes" : "no");
        }
        found++;
    }
    if (!found) printf("  (no partitions)\n");
}

int main(int argc, char **argv) {
    int opt_print   = 0;
    int opt_script  = 0;
    int opt_mbr     = 0;
    int opt_uefi    = 1;
    uint64_t esp_size_bytes = 512ULL * ONE_MB;
    const char *devname = NULL;

    for (int i = 1; i < argc; i++) {
        if (sc_strcmp(argv[i], "-p") == 0 || sc_strcmp(argv[i], "--print") == 0)
            opt_print = 1;
        else if (sc_strcmp(argv[i], "-s") == 0 || sc_strcmp(argv[i], "--script") == 0)
            opt_script = 1;
        else if (sc_strcmp(argv[i], "--mbr") == 0) { opt_mbr = 1; opt_uefi = 0; }
        else if (sc_strcmp(argv[i], "--uefi") == 0) opt_uefi = 1;
        else if (sc_strcmp(argv[i], "--esp-size") == 0 && i + 1 < argc) {
            uint64_t parsed = sc_parse_size_bytes(argv[++i]);
            if (parsed > 0) esp_size_bytes = parsed;
        }
        else if (sc_strcmp(argv[i], "-h") == 0 || sc_strcmp(argv[i], "--help") == 0) {
            print_usage();
            return 0;
        } else if (argv[i][0] != '-') {
            devname = argv[i];
            if (devname[0]=='/' && devname[1]=='d' && devname[2]=='e' && devname[3]=='v' && devname[4]=='/')
                devname += 5;
        }
    }

    if (!devname) { print_usage(); return 1; }

    if (opt_print) {
        print_partition_table(devname);
        return 0;
    }

    if (!opt_script) {
        disk_info_t disk;
        int found = -1;
        int n = sys_disk_get_count();
        for (int i = 0; i < n; i++) {
            disk_info_t d;
            if (sys_disk_get_info(i, &d) != 0) continue;
            if (!d.is_partition && sc_strcmp(d.devname, devname) == 0) {
                disk = d;
                found = i;
                break;
            }
        }
        if (found < 0) { printf("[ERROR] Device not found: /dev/%s\n", devname); return 1; }

        {
            char disk_size[24];
            sc_format_size((uint64_t)disk.total_sectors * SECTOR_SIZE_BYTES, disk_size, sizeof(disk_size));
            printf("fdisk /dev/%s  (%s)\n", devname, disk_size);
        }
        printf("Commands: p (print), n (new), d NUM (delete), w (write), q (quit)\n");
        printf("Sizes accept b/mb/gb (default unit: mb).\n");

        partition_spec_t parts[MAX_PARTS];
        int part_count = 0;
        char line[128];

        while (1) {
            printf("fdisk> ");
            read_line_blocking(line, 128);
            int len = 0;
            while(line[len]) len++;
            if (len == 0) continue;

            if (line[0] == 'q') break;
            if (line[0] == 'p') {
                for (int i = 0; i < part_count; i++) {
                    char start_buf[24];
                    char size_buf[24];
                    sc_format_size((uint64_t)parts[i].lba_start * SECTOR_SIZE_BYTES,
                                   start_buf, sizeof(start_buf));
                    sc_format_size((uint64_t)parts[i].sector_count * SECTOR_SIZE_BYTES,
                                   size_buf, sizeof(size_buf));
                    printf("  %d: start=%s size=%s esp=%s\n", i + 1,
                           start_buf, size_buf,
                           (parts[i].flags & PART_FLAG_ESP) ? "yes" : "no");
                }
                continue;
            }
            if (line[0] == 'n') {
                if (part_count >= MAX_PARTS) { printf("Max partitions reached\n"); continue; }
                uint32_t start = 2048;
                if (part_count > 0)
                    start = parts[part_count-1].lba_start + parts[part_count-1].sector_count;
                if (start % 2048) start = ((start + 2047) / 2048) * 2048;
                {
                    char start_buf[24];
                    sc_format_size((uint64_t)start * SECTOR_SIZE_BYTES, start_buf, sizeof(start_buf));
                    printf("First offset [%s]: ", start_buf);
                }
                read_line_blocking(line, 64);
                len = 0; while(line[len]) len++;
                if (len > 0) start = sc_bytes_to_sectors_ceil(sc_parse_size_bytes(line));
                if (start % 2048) start = ((start + 2047) / 2048) * 2048;

                uint32_t last_usable = opt_mbr ? (disk.total_sectors - 1) : (disk.total_sectors - 34);
                uint32_t remaining = (last_usable >= start) ? (last_usable - start + 1) : 0;
                {
                    char rem_buf[24];
                    sc_format_size((uint64_t)remaining * SECTOR_SIZE_BYTES, rem_buf, sizeof(rem_buf));
                    printf("Size [%s]: ", rem_buf);
                }
                read_line_blocking(line, 64);
                len = 0; while(line[len]) len++;
                uint32_t size = remaining;
                if (len > 0) size = sc_bytes_to_sectors_ceil(sc_parse_size_bytes(line));

                parts[part_count].lba_start    = start;
                parts[part_count].sector_count = size;
                parts[part_count].part_type    = 0;
                parts[part_count].flags        = 0;
                parts[part_count].label[0]     = 0;
                part_count++;
                printf("Partition %d added\n", part_count);
                continue;
            }
            if (line[0] == 'd') {
                int idx = sc_atoi(line + 1) - 1;
                if (idx < 0 || idx >= part_count) { printf("Invalid partition\n"); continue; }
                for (int i = idx; i < part_count - 1; i++) parts[i] = parts[i+1];
                part_count--;
                printf("Partition %d deleted\n", idx + 1);
                continue;
            }
            if (line[0] == 'w') {
                int ret;
                if (opt_mbr)
                    ret = sys_disk_write_mbr(devname, parts, part_count);
                else
                    ret = sys_disk_write_gpt(devname, parts, part_count);
                if (ret == 0) printf("Partition table written.\n");
                else printf("[ERROR] Failed to write partition table.\n");
                break;
            }
        }
        return 0;
    }

    disk_info_t disk;
    int found = 0;
    {
        int n = sys_disk_get_count();
        for (int i = 0; i < n; i++) {
            disk_info_t d;
            if (sys_disk_get_info(i, &d) != 0) continue;
            if (!d.is_partition && sc_strcmp(d.devname, devname) == 0) {
                disk = d;
                found = 1;
                break;
            }
        }
    }
    if (!found) { printf("[ERROR] Device not found: /dev/%s\n", devname); return 1; }

    partition_spec_t parts[2];
    int count = 0;
    int ret;

    if (!opt_mbr && opt_uefi) {
        uint32_t esp_sectors = sc_bytes_to_sectors_ceil(esp_size_bytes);
        if (esp_sectors % 2048) esp_sectors = ((esp_sectors + 2047) / 2048) * 2048;
        parts[0].lba_start    = 2048;
        parts[0].sector_count = esp_sectors;
        parts[0].part_type    = 0;
        parts[0].flags        = PART_FLAG_ESP;
        parts[0].label[0]='E'; parts[0].label[1]='F'; parts[0].label[2]='I';
        parts[0].label[3]=' '; parts[0].label[4]='S'; parts[0].label[5]='y';
        parts[0].label[6]='s'; parts[0].label[7]='t'; parts[0].label[8]='e';
        parts[0].label[9]='m'; parts[0].label[10]=0;

        uint32_t root_start = 2048 + esp_sectors;
        if (root_start % 2048) root_start = ((root_start + 2047) / 2048) * 2048;
        parts[1].lba_start    = root_start;
        parts[1].sector_count = disk.total_sectors - root_start - 34;
        parts[1].part_type    = 0;
        parts[1].flags        = 0;
        parts[1].label[0]='B'; parts[1].label[1]='o'; parts[1].label[2]='r';
        parts[1].label[3]='e'; parts[1].label[4]='d'; parts[1].label[5]='O';
        parts[1].label[6]='S'; parts[1].label[7]=0;
        count = 2;
        ret = sys_disk_write_gpt(devname, parts, count);
    } else {
        parts[0].lba_start    = 2048;
        parts[0].sector_count = disk.total_sectors - 2048;
        parts[0].part_type    = 0;
        parts[0].flags        = 0;
        parts[0].label[0]='B'; parts[0].label[1]='o'; parts[0].label[2]='r';
        parts[0].label[3]='e'; parts[0].label[4]='d'; parts[0].label[5]='O';
        parts[0].label[6]='S'; parts[0].label[7]=0;
        count = 1;
        ret = sys_disk_write_mbr(devname, parts, count);
    }

    if (ret != 0) { printf("[ERROR] Partition write failed.\n"); return 1; }
    printf("Partition table written to /dev/%s.\n", devname);
    
    sys_disk_rescan(devname);
    
    return 0;
}
