// Copyright (c) 2023-2026 Chris (boreddevnl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
#include <stdarg.h>
#include "../libc/syscall.h"
#include "../libc/stdlib.h"
#include "../libc/string.h"
#include "../libc/stdio.h"

static int sc_strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

static int sc_strncpy(char *dst, const char *src, int n) {
    int i = 0;
    while (i < n - 1 && src[i]) { dst[i] = src[i]; i++; }
    dst[i] = 0;
    return i;
}

static void read_line_blocking(char *buf, int max_len) {
    int len = 0;
    while (1) {
        char ch;
        if (sys_tty_read_in(&ch, 1) <= 0) {
            sleep(10);
            continue;
        }
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

static int copy_file(const char *src, const char *dst) {
    printf("  [FILE] %s\n", dst);
    int sfd = sys_open(src, "r");
    if (sfd < 0) { printf("[ERROR] Cannot open: %s\n", src); return -1; }
    sys_delete(dst);
    int dfd = sys_open(dst, "w");
    if (dfd < 0) { sys_close(sfd); printf("[ERROR] Cannot create: %s\n", dst); return -1; }

    char *buf = (char*)malloc(65536);
    if (!buf) { sys_close(sfd); sys_close(dfd); printf("[ERROR] Out of memory copying: %s\n", dst); return -1; }
    int n;
    while ((n = sys_read(sfd, buf, 65536)) > 0) {
        if (sys_write_fs(dfd, buf, n) != (uint32_t)n) {
            sys_close(sfd); sys_close(dfd);
            free(buf);
            printf("[ERROR] Write error: %s\n", dst);
            return -1;
        }
    }
    free(buf);
    sys_close(sfd);
    sys_close(dfd);
    return 0;
}

static int copy_file_optional(const char *src, const char *dst) {
    if (!sys_exists(src)) return 0;
    return copy_file(src, dst);
}

static int copy_tree(const char *src_dir, const char *dst_dir) {
    printf("  [DIR ] %s\n", dst_dir);
    if (sys_mkdir(dst_dir) != 0) {
    }
    FAT32_FileInfo entries[64];
    int n = sys_list(src_dir, entries, 64);
    for (int i = 0; i < n; i++) {
        if (entries[i].name[0] == '.' && entries[i].name[1] == '_') continue;

        char src_path[512], dst_path[512];
        int sl = 0, dl = 0;
        for (; src_dir[sl]; sl++) src_path[sl] = src_dir[sl];
        src_path[sl++] = '/';
        for (int j = 0; entries[i].name[j]; j++) src_path[sl++] = entries[i].name[j];
        src_path[sl] = 0;

        for (; dst_dir[dl]; dl++) dst_path[dl] = dst_dir[dl];
        dst_path[dl++] = '/';
        for (int j = 0; entries[i].name[j]; j++) dst_path[dl++] = entries[i].name[j];
        dst_path[dl] = 0;

        if (entries[i].is_directory) {
            if (copy_tree(src_path, dst_path) != 0) return -1;
        } else {
            if (copy_file(src_path, dst_path) != 0) return -1;
        }
    }
    return 0;
}

static void print_usage(void) {
    printf("boredos_install [OPTIONS] /dev/DEVICE\n");
    printf("  --uefi              UEFI (GPT + ESP) [default]\n");
    printf("  --bios              BIOS/MBR\n");
    printf("  --no-partition      Skip fdisk\n");
    printf("  --no-format         Skip mkfs\n");
    printf("  --no-files          Skip file copy\n");
    printf("  --no-bootloader     Skip bootloader files\n");
    printf("  --esp-size N        ESP MB (default: 512)\n");
    printf("  --esp-dev DEV       Explicit ESP device\n");
    printf("  --root-dev DEV      Explicit root device\n");
    printf("  -y, --yes           Auto-accept warning\n");
    printf("  -v, --verbose\n");
    printf("  -h, --help\n");
}

static uint64_t get_ticks(void) {
    return (uint64_t)sys_system(16 /* SYSTEM_CMD_GET_TICKS */, 0, 0, 0, 0);
}

#define SYS_SERIAL_WRITE 8

static void serial_write_user(const char *str) {
    __asm__ volatile("mov $8, %%rax; mov %0, %%rdi; syscall" : : "r"(str) : "rax", "rdi");
}

static void serial_printf(const char *fmt, ...) {
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    serial_write_user(buf);
}

int main(int argc, char **argv) {
    int is_uefi       = 1;
    int do_partition  = 1;
    int do_format     = 1;
    int do_files      = 1;
    int do_bootloader = 1;
    int opt_yes       = 0;
    int esp_size_mb   = 512;
    const char *devname      = NULL;
    const char *esp_dev_arg  = NULL;
    const char *root_dev_arg = NULL;

    for (int i = 1; i < argc; i++) {
        if      (sc_strcmp(argv[i], "--uefi") == 0)         is_uefi = 1;
        else if (sc_strcmp(argv[i], "--bios") == 0)         is_uefi = 0;
        else if (sc_strcmp(argv[i], "--no-partition") == 0) do_partition = 0;
        else if (sc_strcmp(argv[i], "--no-format") == 0)    do_format = 0;
        else if (sc_strcmp(argv[i], "--no-files") == 0)     do_files = 0;
        else if (sc_strcmp(argv[i], "--no-bootloader") == 0) do_bootloader = 0;
        else if ((sc_strcmp(argv[i], "-y") == 0 || sc_strcmp(argv[i], "--yes") == 0)) opt_yes = 1;
        else if (sc_strcmp(argv[i], "--esp-size") == 0 && i + 1 < argc) {
            esp_size_mb = 0;
            const char *s = argv[++i];
            while (*s >= '0' && *s <= '9') esp_size_mb = esp_size_mb * 10 + (*s++ - '0');
        } else if (sc_strcmp(argv[i], "--esp-dev") == 0 && i + 1 < argc) {
            esp_dev_arg = argv[++i];
            if (esp_dev_arg[0]=='/' && esp_dev_arg[1]=='d' && esp_dev_arg[2]=='e' && esp_dev_arg[3]=='v' && esp_dev_arg[4]=='/')
                esp_dev_arg += 5;
        } else if (sc_strcmp(argv[i], "--root-dev") == 0 && i + 1 < argc) {
            root_dev_arg = argv[++i];
            if (root_dev_arg[0]=='/' && root_dev_arg[1]=='d' && root_dev_arg[2]=='e' && root_dev_arg[3]=='v' && root_dev_arg[4]=='/')
                root_dev_arg += 5;
        } else if (sc_strcmp(argv[i], "-h") == 0 || sc_strcmp(argv[i], "--help") == 0) {
            print_usage(); return 0;
        } else if (argv[i][0] != '-') {
            devname = argv[i];
            if (devname[0]=='/' && devname[1]=='d' && devname[2]=='e' && devname[3]=='v' && devname[4]=='/')
                devname += 5;
        }
    }

    if (!devname) { print_usage(); return 1; }

    /* --- Step 0: Prerequisites --- */
    if (!sys_exists("/boot/boredos.elf")) {
        printf("[ERROR] /boot/boredos.elf not found. Cannot install.\n");
        return 1;
    }
    if (is_uefi && !sys_exists("/boot/BOOTX64.EFI")) {
        printf("[ERROR] /boot/BOOTX64.EFI not found. Cannot install UEFI.\n");
        return 1;
    }

    disk_info_t disk_info;
    {
        int found = 0;
        int n = sys_disk_get_count();
        for (int i = 0; i < n; i++) {
            if (sys_disk_get_info(i, &disk_info) != 0) continue;
            if (!disk_info.is_partition && sc_strcmp(disk_info.devname, devname) == 0) { found = 1; break; }
        }
        if (!found) { printf("[ERROR] Device not found: /dev/%s\n", devname); return 1; }
    }

    if (disk_info.total_sectors < MIN_INSTALL_SECTORS) {
        printf("[ERROR] Disk too small: %u sectors. Minimum is 1 GB (%u sectors).\n",
               disk_info.total_sectors, MIN_INSTALL_SECTORS);
        return 1;
    }

    if (do_partition || do_format) {
        uint32_t mb = disk_info.total_sectors / 2048;
        printf("╔══════════════════════════════════════════════════════╗\n");
        printf("║  WARNING: ALL DATA on /dev/%s will be ERASED.      ║\n", devname);
        printf("║  Disk: %u sectors (%u MB)                           ║\n", disk_info.total_sectors, mb);
        printf("║  This is IRREVERSIBLE.                               ║\n");
        printf("╚══════════════════════════════════════════════════════╝\n");
        if (opt_yes) {
            printf("Auto-accepted (--yes).\n");
        } else {
            printf("Proceed with installation? (y/n): ");
            char resp[8];
            read_line_blocking(resp, 8);
            if (resp[0] != 'y' && resp[0] != 'Y') {
                printf("Aborted.\n");
                return 0;
            }
        }
    }

    sys_mkdir("/mnt");
    sys_mkdir("/mnt/boot");
    sys_mkdir("/mnt/esp");

    if (do_partition) {
        char fdisk_args[128];
        int ai = 0;
        const char *cmd = "--script ";
        for (; *cmd; cmd++) fdisk_args[ai++] = *cmd;
        if (is_uefi) {
            const char *u = "--uefi --esp-size ";
            for (; *u; u++) fdisk_args[ai++] = *u;
            char num[16]; int ni = 0;
            int v = esp_size_mb;
            if (v == 0) { num[ni++] = '0'; } else {
                char tmp[16]; int ti = 0;
                while (v > 0) { tmp[ti++] = '0' + (v % 10); v /= 10; }
                for (int j = ti - 1; j >= 0; j--) num[ni++] = tmp[j];
            }
            for (int j = 0; j < ni; j++) fdisk_args[ai++] = num[j];
            fdisk_args[ai++] = ' ';
        } else {
            const char *b = "--mbr "; for (; *b; b++) fdisk_args[ai++] = *b;
        }
        fdisk_args[ai++] = '/'; fdisk_args[ai++] = 'd'; fdisk_args[ai++] = 'e';
        fdisk_args[ai++] = 'v'; fdisk_args[ai++] = '/';
        for (int j = 0; devname[j]; j++) fdisk_args[ai++] = devname[j];
        fdisk_args[ai] = 0;

        int pid = sys_spawn("/bin/fdisk.elf", fdisk_args, SPAWN_FLAG_INHERIT_TTY, 0);
        if (pid < 0) { printf("[ERROR] Failed to run fdisk.\n"); return 1; }
        int status = 0;
        sys_waitpid(pid, &status, 0);
        if (status != 0) { printf("[ERROR] fdisk failed.\n"); return 1; }

        sys_disk_rescan(devname);
    }

    char esp_dev[16]  = {0};
    char root_dev[16] = {0};

    if (esp_dev_arg)  sc_strncpy(esp_dev,  esp_dev_arg,  16);
    if (root_dev_arg) sc_strncpy(root_dev, root_dev_arg, 16);

    if (!esp_dev[0] || !root_dev[0]) {
        int n = sys_disk_get_count();
        for (int i = 0; i < n; i++) {
            disk_info_t d;
            if (sys_disk_get_info(i, &d) != 0) continue;
            if (!d.is_partition) continue;
            int match = 1;
            for (int j = 0; devname[j]; j++) {
                if (d.devname[j] != devname[j]) { match = 0; break; }
            }
            if (!match) continue;
            if (is_uefi && d.is_esp && !esp_dev[0])
                sc_strncpy(esp_dev, d.devname, 16);
            else if (!d.is_esp && !root_dev[0]) {
                if (d.is_fat32 || do_format)
                    sc_strncpy(root_dev, d.devname, 16);
            }
        }
    }

    if (!root_dev[0]) { printf("[ERROR] Could not find root partition.\n"); return 1; }
    if (is_uefi && !esp_dev[0]) { printf("[ERROR] Could not find ESP.\n"); return 1; }

    if (do_format) {
        if (is_uefi) {
            if (sys_disk_mkfs_fat32(esp_dev, "EFI") != 0) {
                printf("[ERROR] Failed to format ESP.\n"); return 1;
            }
        }
        if (sys_disk_mkfs_fat32(root_dev, "BOREDOS") != 0) {
            printf("[ERROR] Failed to format root partition.\n"); return 1;
        }
    }

    if (sys_disk_mount(root_dev, "/mnt") != 0) {
        printf("[ERROR] Cannot mount /dev/%s to /mnt\n", root_dev);
        return -1;
    }

    if (is_uefi) {
        sys_mkdir("/mnt/boot");
        if (sys_disk_mount(esp_dev, "/mnt/boot") != 0) {
            printf("[ERROR] Cannot mount /dev/%s to /mnt/boot\n", esp_dev);
            return -1;
        }
    } else {
        // In BIOS mode, /boot is just a directory on the root partition
        sys_mkdir("/mnt/boot");
    }

    if (sys_mkdir("/mnt/Library") == 0 || sys_exists("/mnt/Library")) {
        int mfd = sys_open("/mnt/Library/.boredos_root", "w");
        if (mfd >= 0) sys_close(mfd);
    }

    if (do_files) {
        serial_printf("Copying system files...\n");
        uint64_t t0 = get_ticks();
        if (copy_tree("/bin", "/mnt/bin") != 0)          { serial_printf("[ERROR] copy /bin failed\n"); return 1; }
        if (copy_tree("/Library", "/mnt/Library") != 0)  { serial_printf("[ERROR] copy /Library failed\n"); return 1; }
        if (copy_tree("/docs", "/mnt/docs") != 0)        { serial_printf("[ERROR] copy /docs failed\n"); return 1; }
        if (copy_tree("/root", "/mnt/root") != 0)        { serial_printf("[ERROR] copy /root failed\n"); return 1; }
        if (copy_tree("/usr", "/mnt/usr") != 0)          { serial_printf("[ERROR] copy /usr failed\n"); return 1; }
        if (copy_tree("/etc", "/mnt/etc") != 0)          { serial_printf("[ERROR] copy /etc failed\n"); return 1; }
        
        serial_printf("Copying kernel and initrd...\n");
        if (copy_file("/boot/boredos.elf", "/mnt/boot/boredos.elf") != 0) {
            serial_printf("[ERROR] Failed to copy kernel to ESP\n");
            return 1;
        }
        if (sys_exists("/boot/initrd.tar")) {
            if (copy_file("/boot/initrd.tar", "/mnt/boot/initrd.tar") != 0) {
                printf("[ERROR] Failed to copy /boot/initrd.tar to ESP\n");
                return 1;
            }
        } else if (sys_exists("/initrd.tar")) {
            if (copy_file("/initrd.tar", "/mnt/boot/initrd.tar") != 0) {
                serial_printf("[ERROR] Failed to copy /initrd.tar to ESP\n");
                return 1;
            }
        } else {
            serial_printf("[WARNING] initrd.tar not found in live system (checked /boot/ and /)!\n");
        }
        
        copy_file_optional("/README.md", "/mnt/README.md");
        copy_file_optional("/LICENSE", "/mnt/LICENSE");
        copy_file_optional("/about.c", "/mnt/about.c");
        uint64_t t1 = get_ticks();
        serial_printf("Files copied (%llu ticks).\n", (unsigned long long)(t1 - t0));
    }


    if (do_bootloader) {
        if (is_uefi) {
            if (sys_mkdir("/mnt/boot/EFI") != 0 && !sys_exists("/mnt/boot/EFI")) {
                printf("[ERROR] Failed to create /mnt/boot/EFI\n");
                return 1;
            }
            if (sys_mkdir("/mnt/boot/EFI/BOOT") != 0 && !sys_exists("/mnt/boot/EFI/BOOT")) {
                printf("[ERROR] Failed to create /mnt/boot/EFI/BOOT\n");
                return 1;
            }
            if (copy_file("/boot/BOOTX64.EFI", "/mnt/boot/EFI/BOOT/BOOTX64.EFI") != 0) {
                printf("[ERROR] Failed to copy BOOTX64.EFI\n");
                return 1;
            }
            copy_file_optional("/boot/BOOTIA32.EFI", "/mnt/boot/EFI/BOOT/BOOTIA32.EFI");

            int fd = sys_open("/mnt/boot/limine.conf", "w");
            if (fd >= 0) {
                char cfg[512];
                int len = snprintf(cfg, sizeof(cfg),
                    "timeout: 3\n"
                    "verbose: yes\n"
                    "\n"
                    "/BoredOS\n"
                    "    protocol: limine\n"
                    "    path: boot():/boredos.elf\n"
                    "    cmdline: -v root=/dev/%s --disk --accept-tos\n"
                    "    module_path: boot():/initrd.tar\n",
                    root_dev);
                if (len > 0) sys_write_fs(fd, cfg, len);
                sys_close(fd);
            }
        } else {
            copy_file_optional("/boot/limine-bios.sys", "/mnt/limine-bios.sys");

            int fd = sys_open("/mnt/limine.conf", "w");
            if (fd >= 0) {
                char cfg[512];
                int len = snprintf(cfg, sizeof(cfg),
                    "timeout: 3\n"
                    "verbose: yes\n"
                    "\n"
                    "/BoredOS\n"
                    "    protocol: limine\n"
                    "    root: boot()\n"
                    "    path: /boredos.elf\n"
                    "    cmdline: -v root=/dev/%s --disk --accept-tos\n"
                    "    module_path: /initrd.tar\n",
                    root_dev);
                if (len > 0) sys_write_fs(fd, cfg, len);
                sys_close(fd);
            }

            printf("[NOTICE] BIOS bootstrap requires host-side:  limine bios-install /dev/%s\n", devname);
            printf("         limine-bios.sys has been copied to the root of the target partition.\n");
        }
    }

    if (is_uefi) {
        printf("Verifying BOOTX64.EFI...\n");
        int vfd = sys_open("/mnt/boot/EFI/BOOT/BOOTX64.EFI", "r");
        if (vfd >= 0) {
            char magic[2];
            if (sys_read(vfd, magic, 2) == 2) {
                if (magic[0] == 'M' && magic[1] == 'Z') {
                    printf("[OK] BOOTX64.EFI verification successful (MZ found)\n");
                } else {
                    printf("[ERROR] BOOTX64.EFI verification failed: Invalid magic bytes %02x %02x\n", magic[0], magic[1]);
                }
            } else {
                printf("[ERROR] BOOTX64.EFI verification failed: Could not read 2 bytes\n");
            }
            sys_close(vfd);
        } else {
            printf("[ERROR] BOOTX64.EFI verification failed: Could not open file for verification\n");
        }
        sys_disk_sync("/mnt/boot");
        sys_disk_umount("/mnt/boot");
    }
    sys_disk_sync("/mnt");
    sys_disk_umount("/mnt");
    
    // Sync physical disk
    sys_disk_sync(devname);


    printf("\nInstallation complete.\n");
    printf("  Root partition:  /dev/%s\n", root_dev);
    if (is_uefi) printf("  ESP:             /dev/%s\n", esp_dev);
    printf("  Mode:            %s\n", is_uefi ? "UEFI" : "BIOS");
    printf("Reboot and select the target disk to boot BoredOS.\n");
    return 0;
}
