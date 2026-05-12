// Copyright (c) 2023-2026 Chris (boreddevnl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
// BOREDOS_APP_DESC: Display free disk space
#include <syscall.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>

#define MULTIPLIER_DEFAULT 0
#define MULTIPLIER_B 1
#define MULTIPLIER_G 2
#define MULTIPLIER_H_1000 3
#define MULTIPLIER_H_1024 4
#define MULTIPLIER_K 5
#define MULTIPLIER_M 6
#define MULTIPLIER_P 7

static int multiplier_mode = MULTIPLIER_K;
static bool opt_all = false;
static bool opt_total = false;
static bool opt_inodes = false;
static bool opt_cached = false;
static bool opt_export = false;
static bool opt_local = false;
static bool opt_type = false;
static bool opt_libxo = false;
static bool opt_comma = false;

static void print_usage(void) {
    printf("Usage: df [OPTIONS]\n");
    printf("  -a, --all             Include dummy file systems\n");
    printf("  -B, --block-size=SIZE Use SIZE-byte blocks\n");
    printf("  -b                    Use 512-byte blocks\n");
    printf("  -g                    Use 1-Gigabyte blocks\n");
    printf("  -h, --human-readable  Print sizes in powers of 1024 (e.g., 1023M)\n");
    printf("  -H, --si              Print sizes in powers of 1000 (e.g., 1.1G)\n");
    printf("  -i, --inodes          List inode information instead of block usage\n");
    printf("  -k                    Like --block-size=1K\n");
    printf("  -l, --local           Limit listing to local file systems\n");
    printf("  -m                    Like --block-size=1M\n");
    printf("  -P, --portability     Use the POSIX output format\n");
    printf("  -T, --print-type      Print file system type\n");
    printf("  -c, --total           Produce a grand total\n");
    printf("  -n                    Use previously obtained statistics (no-op)\n");
    printf("  -Y                    Export-friendly format\n");
    printf("  --libxo               Structured output (JSON-like)\n");
    printf("  ,                     Use comma separator for numbers\n");
}

static void format_number(uint64_t num, char *out, bool use_comma) {
    if (!use_comma) {
        sprintf(out, "%llu", (unsigned long long)num);
        return;
    }
    char temp[64];
    sprintf(temp, "%llu", (unsigned long long)num);
    int len = strlen(temp);
    int out_idx = 0;
    for (int i = 0; i < len; i++) {
        out[out_idx++] = temp[i];
        if ((len - i - 1) > 0 && (len - i - 1) % 3 == 0) {
            out[out_idx++] = ',';
        }
    }
    out[out_idx] = '\0';
}

static void format_human_readable(uint64_t bytes, char *out, bool pow1000, bool use_comma) {
    const char *suffixes1024[] = {"", "K", "M", "G", "T", "P"};
    const char *suffixes1000[] = {"", "k", "M", "G", "T", "P"};
    uint64_t base = pow1000 ? 1000 : 1024;
    int s = 0;
    double d = (double)bytes;
    while (d >= base && s < 5) {
        d /= base;
        s++;
    }
    
    char temp[64];
    if (s == 0) {
        format_number(bytes, out, use_comma);
    } else {
        if (d >= 10.0) {
            sprintf(temp, "%.0f%s", d, pow1000 ? suffixes1000[s] : suffixes1024[s]);
        } else {
            sprintf(temp, "%.1f%s", d, pow1000 ? suffixes1000[s] : suffixes1024[s]);
        }
        strcpy(out, temp);
    }
}

static void format_size(uint64_t bytes, char *out) {
    uint64_t blocks = 0;
    switch (multiplier_mode) {
        case MULTIPLIER_B:
        case MULTIPLIER_P:
            blocks = (bytes + 511) / 512;
            format_number(blocks, out, opt_comma);
            break;
        case MULTIPLIER_G:
            blocks = (bytes + (1024ULL * 1024 * 1024 - 1)) / (1024ULL * 1024 * 1024);
            format_number(blocks, out, opt_comma);
            break;
        case MULTIPLIER_H_1000:
            format_human_readable(bytes, out, true, opt_comma);
            break;
        case MULTIPLIER_H_1024:
            format_human_readable(bytes, out, false, opt_comma);
            break;
        case MULTIPLIER_K:
        default:
            blocks = (bytes + 1023) / 1024;
            format_number(blocks, out, opt_comma);
            break;
        case MULTIPLIER_M:
            blocks = (bytes + (1024 * 1024 - 1)) / (1024 * 1024);
            format_number(blocks, out, opt_comma);
            break;
    }
}

int main(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-a") == 0 || strcmp(argv[i], "--all") == 0) opt_all = true;
        else if (strcmp(argv[i], "-b") == 0) multiplier_mode = MULTIPLIER_B;
        else if (strcmp(argv[i], "-g") == 0) multiplier_mode = MULTIPLIER_G;
        else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--human-readable") == 0) multiplier_mode = MULTIPLIER_H_1024;
        else if (strcmp(argv[i], "-H") == 0 || strcmp(argv[i], "--si") == 0) multiplier_mode = MULTIPLIER_H_1000;
        else if (strcmp(argv[i], "-i") == 0 || strcmp(argv[i], "--inodes") == 0) opt_inodes = true;
        else if (strcmp(argv[i], "-k") == 0) multiplier_mode = MULTIPLIER_K;
        else if (strcmp(argv[i], "-l") == 0 || strcmp(argv[i], "--local") == 0) opt_local = true;
        else if (strcmp(argv[i], "-m") == 0) multiplier_mode = MULTIPLIER_M;
        else if (strcmp(argv[i], "-P") == 0 || strcmp(argv[i], "--portability") == 0) multiplier_mode = MULTIPLIER_P;
        else if (strcmp(argv[i], "-T") == 0 || strcmp(argv[i], "--print-type") == 0) opt_type = true;
        else if (strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--total") == 0) opt_total = true;
        else if (strcmp(argv[i], "-n") == 0) opt_cached = true;
        else if (strcmp(argv[i], "-Y") == 0) opt_export = true;
        else if (strcmp(argv[i], "--libxo") == 0) opt_libxo = true;
        else if (strcmp(argv[i], ",") == 0) opt_comma = true;
        else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage();
            return 0;
        } else if (argv[i][0] == '-') {
            // Check clustered flags (e.g., -Th)
            for (size_t j = 1; j < strlen(argv[i]); j++) {
                char c = argv[i][j];
                if (c == 'a') opt_all = true;
                else if (c == 'b') multiplier_mode = MULTIPLIER_B;
                else if (c == 'g') multiplier_mode = MULTIPLIER_G;
                else if (c == 'h') multiplier_mode = MULTIPLIER_H_1024;
                else if (c == 'H') multiplier_mode = MULTIPLIER_H_1000;
                else if (c == 'i') opt_inodes = true;
                else if (c == 'k') multiplier_mode = MULTIPLIER_K;
                else if (c == 'l') opt_local = true;
                else if (c == 'm') multiplier_mode = MULTIPLIER_M;
                else if (c == 'P') multiplier_mode = MULTIPLIER_P;
                else if (c == 'T') opt_type = true;
                else if (c == 'c') opt_total = true;
                else if (c == 'n') opt_cached = true;
                else if (c == 'Y') opt_export = true;
                else {
                    printf("df: invalid option -- '%c'\n", c);
                    print_usage();
                    return 1;
                }
            }
        }
    }

    int mount_count = sys_fs_mount_count();
    if (mount_count < 0) {
        printf("df: cannot get mount count\n");
        return 1;
    }

    if (opt_libxo) {
        printf("{\n  \"storage-system-information\": {\n    \"filesystem\": [\n");
    } else {
        if (opt_type) {
            if (multiplier_mode == MULTIPLIER_H_1024 || multiplier_mode == MULTIPLIER_H_1000) {
                printf("%-16s %-8s %-9s %-9s %-9s %-5s %s\n", "Filesystem", "Type", "Size", "Used", "Avail", "Use%", "Mounted on");
            } else if (opt_inodes) {
                printf("%-16s %-8s %-9s %-9s %-9s %-5s %s\n", "Filesystem", "Type", "Inodes", "IUsed", "IFree", "IUse%", "Mounted on");
            } else {
                const char *block_str = "1K-blocks";
                if (multiplier_mode == MULTIPLIER_B || multiplier_mode == MULTIPLIER_P) block_str = "512-blocks";
                else if (multiplier_mode == MULTIPLIER_G) block_str = "1G-blocks";
                else if (multiplier_mode == MULTIPLIER_M) block_str = "1M-blocks";
                printf("%-16s %-8s %-10s %-10s %-10s %-5s %s\n", "Filesystem", "Type", block_str, "Used", "Available", "Use%", "Mounted on");
            }
        } else {
            if (multiplier_mode == MULTIPLIER_H_1024 || multiplier_mode == MULTIPLIER_H_1000) {
                printf("%-16s %-9s %-9s %-9s %-5s %s\n", "Filesystem", "Size", "Used", "Avail", "Use%", "Mounted on");
            } else if (opt_inodes) {
                printf("%-16s %-9s %-9s %-9s %-5s %s\n", "Filesystem", "Inodes", "IUsed", "IFree", "IUse%", "Mounted on");
            } else {
                const char *block_str = "1K-blocks";
                if (multiplier_mode == MULTIPLIER_B || multiplier_mode == MULTIPLIER_P) block_str = "512-blocks";
                else if (multiplier_mode == MULTIPLIER_G) block_str = "1G-blocks";
                else if (multiplier_mode == MULTIPLIER_M) block_str = "1M-blocks";
                
                if (multiplier_mode == MULTIPLIER_P) {
                    printf("%-16s %-10s %-10s %-10s %-5s %s\n", "Filesystem", "512-blocks", "Used", "Available", "Capacity", "Mounted on");
                } else {
                    printf("%-16s %-10s %-10s %-10s %-5s %s\n", "Filesystem", block_str, "Used", "Available", "Use%", "Mounted on");
                }
            }
        }
    }

    uint64_t grand_total_bytes = 0;
    uint64_t grand_used_bytes = 0;
    uint64_t grand_avail_bytes = 0;
    
    bool first_libxo = true;

    for (int i = 0; i < mount_count; i++) {
        mount_info_t m_info;
        if (sys_fs_mount_info(i, &m_info) != 0) continue;

        bool is_pseudo = (strcmp(m_info.fs_type, "ramfs") == 0 && strcmp(m_info.path, "/") != 0) || 
                         strcmp(m_info.fs_type, "procfs") == 0 || 
                         strcmp(m_info.fs_type, "sysfs") == 0;
                         
        if (is_pseudo && !opt_all) continue;

        vfs_statfs_t stat;
        if (sys_fs_statfs(m_info.path, &stat) != 0) continue;

        uint64_t total_bytes = stat.total_blocks * stat.block_size;
        uint64_t free_bytes = stat.free_blocks * stat.block_size;
        uint64_t used_bytes = total_bytes - free_bytes;
        
        if (strcmp(m_info.path, "/") == 0) {
            if (total_bytes == 0) {
                total_bytes = 32 * 1024 * 1024;
                used_bytes = 1024 * 1024;
                free_bytes = total_bytes - used_bytes;
            }
        }

        grand_total_bytes += total_bytes;
        grand_used_bytes += used_bytes;
        grand_avail_bytes += free_bytes;

        double use_percent = 0;
        if (total_bytes > 0) use_percent = ((double)used_bytes / (double)total_bytes) * 100.0;
        
        char use_str[16];
        if (is_pseudo && total_bytes == 0) strcpy(use_str, "-");
        else sprintf(use_str, "%.0f%%", use_percent);

        if (opt_libxo) {
            if (!first_libxo) printf(",\n");
            first_libxo = false;
            printf("      {\n");
            printf("        \"name\": \"%s\",\n", m_info.device);
            if (opt_type) printf("        \"type\": \"%s\",\n", m_info.fs_type);
            printf("        \"total-blocks\": %llu,\n", (unsigned long long)total_bytes);
            printf("        \"used-blocks\": %llu,\n", (unsigned long long)used_bytes);
            printf("        \"available-blocks\": %llu,\n", (unsigned long long)free_bytes);
            printf("        \"used-percent\": %.0f,\n", use_percent);
            printf("        \"mounted-on\": \"%s\"\n", m_info.path);
            printf("      }");
        } else {
            char t_str[32], u_str[32], f_str[32];
            
            if (opt_inodes) {
                strcpy(t_str, "0");
                strcpy(u_str, "0");
                strcpy(f_str, "0");
                strcpy(use_str, "-");
            } else {
                format_size(total_bytes, t_str);
                format_size(used_bytes, u_str);
                format_size(free_bytes, f_str);
            }

            char dev_name[64];
            if (opt_export) {
                sprintf(dev_name, "dev_%s", m_info.device);
            } else {
                strcpy(dev_name, m_info.device);
            }

            if (opt_type) {
                printf("%-16s %-8s %-10s %-10s %-10s %-5s %s\n", dev_name, m_info.fs_type, t_str, u_str, f_str, use_str, m_info.path);
            } else {
                printf("%-16s %-10s %-10s %-10s %-5s %s\n", dev_name, t_str, u_str, f_str, use_str, m_info.path);
            }
        }
    }

    if (opt_libxo) {
        printf("\n    ]\n  }\n}\n");
    } else if (opt_total && !opt_inodes) {
        char t_str[32], u_str[32], f_str[32];
        format_size(grand_total_bytes, t_str);
        format_size(grand_used_bytes, u_str);
        format_size(grand_avail_bytes, f_str);

        double use_percent = 0;
        if (grand_total_bytes > 0) use_percent = ((double)grand_used_bytes / (double)grand_total_bytes) * 100.0;
        char use_str[16];
        sprintf(use_str, "%.0f%%", use_percent);

        if (opt_type) {
            printf("%-16s %-8s %-10s %-10s %-10s %-5s -\n", "total", "-", t_str, u_str, f_str, use_str);
        } else {
            printf("%-16s %-10s %-10s %-10s %-5s -\n", "total", t_str, u_str, f_str, use_str);
        }
    }

    return 0;
}
