// Copyright (c) 2023-2026 Chris (boreddevnl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
#include "../libc/syscall.h"
#include "../libc/stdlib.h"
#include "../libc/string.h"
#include "../libc/stdio.h"

static int sc_strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

int main(int argc, char **argv) {
    const char *devname = NULL;
    const char *label   = "BOREDOS";
    int fat_type = 32;

    for (int i = 1; i < argc; i++) {
        if (sc_strcmp(argv[i], "-F") == 0 && i + 1 < argc) {
            fat_type = 0;
            const char *s = argv[++i];
            while (*s >= '0' && *s <= '9') fat_type = fat_type * 10 + (*s++ - '0');
        } else if (sc_strcmp(argv[i], "-n") == 0 && i + 1 < argc) {
            label = argv[++i];
        } else if (sc_strcmp(argv[i], "-h") == 0 || sc_strcmp(argv[i], "--help") == 0) {
            printf("mkfs.fat [OPTIONS] /dev/DEVICE\n");
            printf("  -F 32       FAT type (32 only)\n");
            printf("  -n LABEL    Volume label (max 11 chars, default: BOREDOS)\n");
            return 0;
        } else if (argv[i][0] != '-') {
            devname = argv[i];
            if (devname[0]=='/' && devname[1]=='d' && devname[2]=='e' && devname[3]=='v' && devname[4]=='/')
                devname += 5;
        }
    }

    if (!devname) {
        printf("Usage: mkfs.fat -F 32 [-n LABEL] /dev/DEVICE\n");
        return 1;
    }
    if (fat_type != 32) {
        printf("[ERROR] Only FAT32 (-F 32) is supported.\n");
        return 1;
    }

    disk_info_t d;
    int found = 0;
    int n = sys_disk_get_count();
    for (int i = 0; i < n; i++) {
        if (sys_disk_get_info(i, &d) != 0) continue;
        if (sc_strcmp(d.devname, devname) == 0) { found = 1; break; }
    }
    if (!found) { printf("[ERROR] Device not found: /dev/%s\n", devname); return 1; }
    if (!d.is_partition) { printf("[ERROR] /dev/%s is a whole disk, not a partition.\n", devname); return 1; }
    if (d.total_sectors < 65536) {
        printf("[ERROR] Partition too small (< 32 MB) for FAT32.\n");
        return 1;
    }

    printf("Formatting /dev/%s as FAT32 (label: %s)...\n", devname, label);
    int ret = sys_disk_mkfs_fat32(devname, label);
    if (ret != 0) { printf("[ERROR] Format failed.\n"); return 1; }
    printf("Done.\n");
    return 0;
}
