// Copyright (c) 2026 zeyadhost (https://github.com/zeyadhost)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <syscall.h>

#define LSBLK_MAX_DISKS 32
#define LSBLK_SECTOR_SIZE 512ULL
#define LSBLK_KB 1024ULL
#define LSBLK_MB (1024ULL * 1024ULL)
#define LSBLK_GB (1024ULL * 1024ULL * 1024ULL)

static int streq(const char *a, const char *b) {
    return strcmp(a, b) == 0;
}

static int starts_with(const char *s, const char *prefix) {
    while (*prefix) {
        if (*s++ != *prefix++) return 0;
    }
    return 1;
}

static const char *display_label(const disk_info_t *d) {
    if (!d->label[0]) return "";
    if (streq(d->label, "Unknown Partition")) return "";
    if (streq(d->label, "FAT32 Partition")) return "";
    if (streq(d->label, "EFI System Partition")) return "EFI";
    return d->label;
}

static const char *device_name_arg(const char *arg) {
    if (starts_with(arg, "/dev/")) return arg + 5;
    return arg;
}

static void format_size(uint64_t bytes, char *out, size_t out_len, int compact) {
    const char *sep = compact ? "" : " ";
    uint64_t unit = 1;
    const char *suffix = "B";

    if (bytes >= LSBLK_GB) {
        unit = LSBLK_GB;
        suffix = "GB";
    } else if (bytes >= LSBLK_MB) {
        unit = LSBLK_MB;
        suffix = "MB";
    } else if (bytes >= LSBLK_KB) {
        unit = LSBLK_KB;
        suffix = "KB";
    }

    if (unit == 1) {
        snprintf(out, out_len, "%llu%s%s", (unsigned long long)bytes, sep, suffix);
        return;
    }

    uint64_t whole = bytes / unit;
    uint64_t rem = bytes % unit;
    uint64_t tenth = (rem * 10ULL + unit / 2ULL) / unit;

    if (tenth >= 10ULL) {
        whole++;
        tenth = 0;
    }

    if (tenth == 0) {
        snprintf(out, out_len, "%llu%s%s", (unsigned long long)whole, sep, suffix);
    } else {
        snprintf(out, out_len, "%llu.%llu%s%s", (unsigned long long)whole, (unsigned long long)tenth, sep, suffix);
    }
}

static uint64_t disk_size_bytes(const disk_info_t *d) {
    return (uint64_t)d->total_sectors * LSBLK_SECTOR_SIZE;
}

static int is_child_partition(const disk_info_t *disk, const disk_info_t *part) {
    size_t len;

    if (disk->is_partition || !part->is_partition) return 0;

    len = strlen(disk->devname);
    if (strncmp(part->devname, disk->devname, len) != 0) return 0;

    return part->devname[len] >= '0' && part->devname[len] <= '9';
}

static int child_count(const disk_info_t *disk, disk_info_t *items, int count) {
    int children = 0;

    for (int i = 0; i < count; i++) {
        if (is_child_partition(disk, &items[i])) children++;
    }

    return children;
}

static void print_tree_device(const disk_info_t *d, const char *branch) {
    char size[24];
    const char *type = d->is_partition ? "part" : "disk";

    format_size(disk_size_bytes(d), size, sizeof(size), 0);

    if (d->is_partition) {
        const char *label = display_label(d);
        if (branch[0]) printf("%s %-8s %8s  %s", branch, d->devname, size, type);
        else printf("/dev/%-8s %8s  %s", d->devname, size, type);
        if (d->is_fat32) printf("  FAT32");
        if (label[0]) printf("  %s", label);
        if (d->is_esp) printf("  [ESP]");
        printf("\n");
    } else {
        printf("/dev/%-8s %8s  %s\n", d->devname, size, type);
    }
}

static void print_tree_disk(const disk_info_t *disk, disk_info_t *items, int count) {
    int children = child_count(disk, items, count);
    int seen = 0;

    print_tree_device(disk, "");

    for (int i = 0; i < count; i++) {
        if (!is_child_partition(disk, &items[i])) continue;
        seen++;
        print_tree_device(&items[i], seen == children ? "└─" : "├─");
    }
}

static void print_raw_device(const disk_info_t *d) {
    char size[24];

    format_size(disk_size_bytes(d), size, sizeof(size), 1);
    printf("/dev/%s %s %s", d->devname, size, d->is_partition ? "part" : "disk");

    if (d->is_partition) {
        const char *label = display_label(d);
        if (d->is_fat32) printf(" FAT32");
        if (label[0]) printf(" %s", label);
        if (d->is_esp) printf(" ESP");
    }

    printf("\n");
}

static void print_raw_disk(const disk_info_t *disk, disk_info_t *items, int count) {
    print_raw_device(disk);

    for (int i = 0; i < count; i++) {
        if (is_child_partition(disk, &items[i])) print_raw_device(&items[i]);
    }
}

static void json_string(const char *s) {
    putchar('"');

    while (*s) {
        if (*s == '"' || *s == '\\') {
            putchar('\\');
            putchar(*s);
        } else if (*s == '\n') {
            printf("\\n");
        } else {
            putchar(*s);
        }
        s++;
    }

    putchar('"');
}

static void json_device_fields(const disk_info_t *d) {
    char size[24];
    char name[24];

    format_size(disk_size_bytes(d), size, sizeof(size), 0);
    snprintf(name, sizeof(name), "/dev/%s", d->devname);

    printf("\"name\":");
    json_string(name);
    printf(",\"size\":");
    json_string(size);
    printf(",\"type\":");
    json_string(d->is_partition ? "part" : "disk");
    printf(",\"fstype\":");
    json_string(d->is_fat32 ? "FAT32" : "");
    printf(",\"label\":");
    json_string(display_label(d));
    printf(",\"flags\":[");
    if (d->is_esp) json_string("ESP");
    printf("]");
}

static void print_json_partition(const disk_info_t *d) {
    printf("{");
    json_device_fields(d);
    printf("}");
}

static void print_json_disk(const disk_info_t *disk, disk_info_t *items, int count) {
    int seen = 0;

    printf("{");
    json_device_fields(disk);
    printf(",\"children\":[");

    for (int i = 0; i < count; i++) {
        if (!is_child_partition(disk, &items[i])) continue;
        if (seen > 0) printf(",");
        print_json_partition(&items[i]);
        seen++;
    }

    printf("]}");
}

static int load_disks(disk_info_t *items, int max) {
    int total = sys_disk_get_count();
    int count = 0;

    for (int i = 0; i < total && count < max; i++) {
        if (sys_disk_get_info(i, &items[count]) == 0) count++;
    }

    return count;
}

static void usage(void) {
    printf("Usage: lsblk [-r] [--json] [/dev/DEVICE]\n");
}

int main(int argc, char **argv) {
    disk_info_t items[LSBLK_MAX_DISKS];
    const char *filter = NULL;
    int raw = 0;
    int json = 0;
    int count;
    int printed = 0;

    for (int i = 1; i < argc; i++) {
        if (streq(argv[i], "-r")) {
            raw = 1;
        } else if (streq(argv[i], "--json")) {
            json = 1;
        } else if (streq(argv[i], "-h") || streq(argv[i], "--help")) {
            usage();
            return 0;
        } else if (argv[i][0] == '-') {
            printf("lsblk: unknown option: %s\n", argv[i]);
            usage();
            return 1;
        } else if (!filter) {
            filter = device_name_arg(argv[i]);
        } else {
            printf("lsblk: only one device filter is supported\n");
            return 1;
        }
    }

    if (raw && json) {
        printf("lsblk: -r and --json cannot be used together\n");
        return 1;
    }

    count = load_disks(items, LSBLK_MAX_DISKS);

    if (json) {
        printf("{\"devices\":[");

        for (int i = 0; i < count; i++) {
            if (items[i].is_partition) continue;
            if (filter && !streq(items[i].devname, filter)) continue;
            if (printed > 0) printf(",");
            print_json_disk(&items[i], items, count);
            printed++;
        }

        if (filter && printed == 0) {
            for (int i = 0; i < count; i++) {
                if (!items[i].is_partition || !streq(items[i].devname, filter)) continue;
                print_json_partition(&items[i]);
                printed++;
                break;
            }
        }

        printf("]}\n");
    } else if (raw) {
        for (int i = 0; i < count; i++) {
            if (items[i].is_partition) continue;
            if (filter && !streq(items[i].devname, filter)) continue;
            print_raw_disk(&items[i], items, count);
            printed++;
        }

        if (filter && printed == 0) {
            for (int i = 0; i < count; i++) {
                if (!items[i].is_partition || !streq(items[i].devname, filter)) continue;
                print_raw_device(&items[i]);
                printed++;
                break;
            }
        }
    } else {
        for (int i = 0; i < count; i++) {
            if (items[i].is_partition) continue;
            if (filter && !streq(items[i].devname, filter)) continue;
            print_tree_disk(&items[i], items, count);
            printed++;
        }

        if (filter && printed == 0) {
            for (int i = 0; i < count; i++) {
                if (!items[i].is_partition || !streq(items[i].devname, filter)) continue;
                print_tree_device(&items[i], "");
                printed++;
                break;
            }
        }
    }

    if (printed == 0 && !json) {
        if (filter) printf("lsblk: /dev/%s not found\n", filter);
        else printf("lsblk: no block devices found\n");
        return 1;
    }

    if (printed == 0 && filter) return 1;
    return 0;
}
