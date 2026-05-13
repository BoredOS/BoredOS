// Copyright (c) 2026 zeyadhost (https://github.com/zeyadhost)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syscall.h>
#include <stdint.h>

#define MAX_ENTRIES 1024
#define DU_KB 1024ULL
#define DU_MB (1024ULL * 1024ULL)
#define DU_GB (1024ULL * 1024ULL * 1024ULL)

static int opt_summarize = 0;
static int opt_all = 0;
static int opt_max_depth = -1;
static int opt_total = 0;
static int opt_bytes = 0;

static uint64_t grand_total = 0;

static void usage(void) {
    printf("Usage: du [options]..[file]\n");
    printf("Summarize disk usage of the set of FILEs, recursively for directories.\n\n");
    printf("Options:\n");
    printf("  -s, --summarize       display only a total for each argument\n");
    printf("  -a, --all             write counts for all files, not just directories\n");
    printf("  -d, --max-depth=N     print the total for a directory only if it is N or\n");
    printf("                        fewer levels below the command line argument\n");
    printf("  -c, --total           produce a grand total\n");
    printf("  -b, --bytes           print sizes in bytes\n");
    printf("  -H, --human-readable  print sizes in human readable format (default)\n");
    printf("      --help            display this help and exit\n");
}

static void print_size(uint64_t bytes, const char *path) {
    if (opt_bytes) {
        printf("%llu\t%s\n", (unsigned long long)bytes, path);
        return;
    }
    
    char size_str[32];
    uint64_t unit = 1;
    const char *suffix = "B";

    if (bytes >= DU_GB) {
        unit = DU_GB;
        suffix = "GB";
    } else if (bytes >= DU_MB) {
        unit = DU_MB;
        suffix = "MB";
    } else if (bytes >= DU_KB) {
        unit = DU_KB;
        suffix = "KB";
    }

    if (unit == 1) {
        snprintf(size_str, sizeof(size_str), "%llu%s", (unsigned long long)bytes, suffix);
    } else {
        // Round to one decimal place
        uint64_t whole = bytes / unit;
        uint64_t rem = bytes % unit;
        uint64_t tenth = (rem * 10ULL + unit / 2ULL) / unit;

        if (tenth >= 10ULL) {
            whole++;
            tenth = 0;
        }

        if (tenth == 0) {
            snprintf(size_str, sizeof(size_str), "%llu%s", (unsigned long long)whole, suffix);
        } else {
            snprintf(size_str, sizeof(size_str), "%llu.%llu%s", (unsigned long long)whole, (unsigned long long)tenth, suffix);
        }
    }
    printf("%s\t%s\n", size_str, path);
}

static void join_path(char *dest, size_t size, const char *p1, const char *p2) {
    if (strcmp(p1, "/") == 0) {
        snprintf(dest, size, "/%s", p2);
    } else if (p1[strlen(p1) - 1] == '/') {
        snprintf(dest, size, "%s%s", p1, p2);
    } else {
        snprintf(dest, size, "%s/%s", p1, p2);
    }
}

static uint64_t do_du(const char *path, int depth) {
    FAT32_FileInfo info;
    if (sys_get_file_info(path, &info) < 0) {
        printf("du: cannot access '%s'\n", path);
        return 0;
    }
    
    if (!info.is_directory) {
        if (opt_all || (depth == 0)) {
            if (opt_max_depth == -1 || depth <= opt_max_depth) {
                if (!opt_summarize || depth == 0) {
                    print_size(info.size, path);
                }
            }
        }
        return info.size;
    }
    
    uint64_t total_size = info.size;
    
    FAT32_FileInfo *entries = malloc(sizeof(FAT32_FileInfo) * MAX_ENTRIES);
    if (!entries) {
        printf("du: out of memory for '%s'\n", path);
        return total_size;
    }
    
    int count = sys_list(path, entries, MAX_ENTRIES);
    if (count < 0) {
        printf("du: cannot read directory '%s'\n", path);
        free(entries);
        return total_size;
    }
    
    // Recurse into subdirectories
    for (int i = 0; i < count; i++) {
        if (strcmp(entries[i].name, ".") == 0 || strcmp(entries[i].name, "..") == 0) {
            continue;
        }

        char child_path[1024];
        join_path(child_path, sizeof(child_path), path, entries[i].name);

        total_size += do_du(child_path, depth + 1);
    }

    free(entries);

    // Print directory size at this depth
    if (!opt_summarize) {
        if (opt_max_depth == -1 || depth <= opt_max_depth) {
            print_size(total_size, path);
        }
    } else if (depth == 0) {
        // With -s, only print the root of each requested path
        print_size(total_size, path);
    }

    return total_size;
}

int main(int argc, char **argv) {
    char **paths = malloc(sizeof(char*) * argc);
    int num_paths = 0;
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--summarize") == 0) {
            opt_summarize = 1;
        } else if (strcmp(argv[i], "-a") == 0 || strcmp(argv[i], "--all") == 0) {
            opt_all = 1;
        } else if (strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--total") == 0) {
            opt_total = 1;
        } else if (strcmp(argv[i], "-b") == 0 || strcmp(argv[i], "--bytes") == 0) {
            opt_bytes = 1;
        } else if (strcmp(argv[i], "-H") == 0 || strcmp(argv[i], "--human-readable") == 0) {
            // No-op: human-readable is the default
        } else if (strcmp(argv[i], "-d") == 0) {
            if (i + 1 < argc) {
                opt_max_depth = atoi(argv[++i]);
            } else {
                printf("du: option requires an argument -- '-d'\n");
                return 1;
            }
        } else if (strncmp(argv[i], "--max-depth=", 12) == 0) {
            opt_max_depth = atoi(argv[i] + 12);
        } else if (strcmp(argv[i], "--help") == 0) {
            usage();
            free(paths);
            return 0;
        } else if (argv[i][0] == '-') {
            printf("du: invalid option -- '%s'\n", argv[i]);
            usage();
            free(paths);
            return 1;
        } else {
            paths[num_paths++] = argv[i];
        }
    }
    
    if (num_paths == 0) {
        grand_total += do_du(".", 0);
    } else {
        for (int i = 0; i < num_paths; i++) {
            grand_total += do_du(paths[i], 0);
        }
    }
    
    if (opt_total) {
        print_size(grand_total, "total");
    }
    
    free(paths);
    return 0;
}
