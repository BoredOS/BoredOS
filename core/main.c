// Copyright (c) 2023-2026 Christiaan (chris@boreddev.nl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "limine.h"
#include "graphics.h"
#include "gdt.h"
#include "idt.h"
#include "syscall.h"
#include "process.h"
#include "ps2.h"
#include "tty.h"
#include "pty.h"

#include "io.h"
#include "fat32.h"
#include "tar.h"
#include "vfs.h"
#include "kconsole.h"
#include "kutils.h"
#include "slab.h"
#include "pmm.h"
#include "pmm_test.h"
#include "slab.h"
#include "slab_test.h"
#include "platform.h"
#include "smp.h"
#include "work_queue.h"
#include "lapic.h"
#include "panic.h"
#include "sysfs.h"
#include "procfs.h"
#include "disk.h"
#include "tmpfs.h"
#include "pagecache.h"
#include "kernel_subsystem.h"
#include "module_manager.h"
#include "keymap.h"
#include "keyboard.h"
#include "acpi.h"
#include "ac97.h"
#include "serial.h"
#include "spinlock.h"

bool g_headless_mode = false;

extern void sysfs_init_subsystems(void);

// --- Limine Requests ---
__attribute__((used, section(".requests")))
static volatile LIMINE_BASE_REVISION(2);

__attribute__((used, section(".requests")))
static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST,
    .revision = 1
};

__attribute__((used, section(".requests")))
static volatile struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST,
    .revision = 0
};

__attribute__((used, section(".requests")))
static volatile struct limine_module_request module_request = {
    .id = LIMINE_MODULE_REQUEST,
    .revision = 0
};

__attribute__((used, section(".requests")))
static volatile struct limine_smp_request smp_request = {
    .id = LIMINE_SMP_REQUEST,
    .revision = 0,
    .flags = LIMINE_SMP_X2APIC
};

__attribute__((used, section(".requests")))
static volatile struct limine_bootloader_info_request bootloader_info_request = {
    .id = LIMINE_BOOTLOADER_INFO_REQUEST,
    .revision = 0
};

__attribute__((used, section(".requests")))
static volatile struct limine_kernel_file_request kernel_file_request = {
    .id = LIMINE_KERNEL_FILE_REQUEST,
    .revision = 0
};

__attribute__((used, section(".requests")))
volatile struct limine_rsdp_request acpi_rsdp_request = {
    .id = LIMINE_RSDP_REQUEST,
    .revision = 0
};

__attribute__((used, section(".requests_start")))
static volatile struct limine_request *const requests_start_marker[] = {
    (struct limine_request *)&framebuffer_request,
    (struct limine_request *)&memmap_request,
    (struct limine_request *)&module_request,
    (struct limine_request *)&smp_request,
    (struct limine_request *)&bootloader_info_request,
    (struct limine_request *)&kernel_file_request,
    (struct limine_request *)&acpi_rsdp_request,
    NULL
};

__attribute__((used, section(".requests_end")))
static volatile struct limine_request *const requests_end_marker[] = {
    NULL
};

static void hcf(void) {
    asm("cli");
    for (;;) {
        asm("hlt");
    }
}


static spinlock_t serial_lock = SPINLOCK_INIT;

#define KMSG_BUFFER_SIZE 131072
static char g_kmsg_buffer[KMSG_BUFFER_SIZE];
static size_t g_kmsg_len = 0;

static void kmsg_append_locked(const char *str) {
    if (!str) return;
    size_t len = strlen(str);
    if (len == 0) return;
    if (len >= KMSG_BUFFER_SIZE) {
        str += (len - (KMSG_BUFFER_SIZE - 1));
        len = KMSG_BUFFER_SIZE - 1;
    }
    if (g_kmsg_len + len > KMSG_BUFFER_SIZE) {
        size_t drop = (g_kmsg_len + len) - KMSG_BUFFER_SIZE;
        size_t skip = drop;
        while (skip < g_kmsg_len && g_kmsg_buffer[skip] != '\n') {
            skip++;
        }
        if (skip < g_kmsg_len) skip++;
        else skip = drop;
        memmove(g_kmsg_buffer, g_kmsg_buffer + skip, g_kmsg_len - skip);
        g_kmsg_len -= skip;
    }
    memcpy(g_kmsg_buffer + g_kmsg_len, str, len);
    g_kmsg_len += len;
}

size_t kmsg_copy(char *dst, size_t max_len) {
    if (!dst || max_len == 0) return 0;
    uint64_t flags = spinlock_acquire_irqsave(&serial_lock);
    size_t to_copy = g_kmsg_len < max_len ? g_kmsg_len : max_len;
    memcpy(dst, g_kmsg_buffer, to_copy);
    spinlock_release_irqrestore(&serial_lock, flags);
    return to_copy;
}

void serial_write(const char *str) {
    if (!str) return;
    uint64_t flags = spinlock_acquire_irqsave(&serial_lock);
    if (!serial_is_log_silenced()) {
        serial_write_str(serial_get_debug_port(), str);
    }
    kconsole_write(str);
    kmsg_append_locked(str);
    spinlock_release_irqrestore(&serial_lock, flags);
}

void serial_write_num_locked(uint32_t n) {
    char buf[16];
    itoa(n, buf);
    serial_write(buf);
}

void serial_write_num(uint32_t n) {
    char buf[16];
    itoa(n, buf);
    serial_write(buf);
}

void serial_write_hex_locked(uint64_t n) {
    char buf[32];
    itoa_hex(n, buf);
    serial_write(buf);
}

void serial_write_hex(uint64_t n) {
    char buf[32];
    itoa_hex(n, buf);
    serial_write(buf);
}

void serial_write_mac(const char *label, const uint8_t *mac) {
    serial_write(label);
    for (int i = 0; i < 6; i++) {
        char buf[4];
        itoa_hex(mac[i], buf);
        serial_write(buf);
        if (i < 5) serial_write(":");
    }
    serial_write("\n");
}

void log_ok(const char *msg) {
    serial_write("[  ");
    kconsole_set_color(0xFF00FF00); 
    serial_write("OK");
    kconsole_set_color(0xFFFFFFFF); 
    serial_write("  ] ");
    serial_write(msg);
    serial_write("\n");
}

void log_fail(const char *msg) {
    serial_write("[ ");
    kconsole_set_color(0xFFFF0000); 
    serial_write("FAIL");
    kconsole_set_color(0xFFFFFFFF); 
    serial_write(" ] ");
    serial_write(msg);
    serial_write("\n");
}


// Kernel Entry Point


static bool cmdline_has_flag(const char *cmdline, const char *flag) {
    if (!cmdline || !flag || !flag[0]) return false;
    size_t flag_len = strlen(flag);
    const char *p = cmdline;
    while (*p) {
        while (*p == ' ') p++;
        if (!*p) break;
        const char *start = p;
        while (*p && *p != ' ') p++;
        size_t len = p - start;
        if (len == flag_len && strncmp(start, flag, flag_len) == 0) return true;
    }
    return false;
}

static bool cmdline_read_value(const char *cmdline, const char *key, char *out, size_t out_len) {
    if (!cmdline || !key || !out || out_len <= 1) return false;
    size_t key_len = strlen(key);
    const char *p = cmdline;
    while (*p) {
        while (*p == ' ') p++;
        if (!*p) break;
        if (strncmp(p, key, key_len) == 0) {
            const char *val = p + key_len;
            size_t i = 0;
            while (*val && *val != ' ' && i < out_len - 1) {
                out[i++] = *val++;
            }
            out[i] = '\0';
            return i > 0;
        }
        while (*p && *p != ' ') p++;
    }
    return false;
}

#define BOOT_FLAG_LIVE          0x01
#define BOOT_FLAG_DISK          0x02
#define BOOT_FLAG_ROOT_SET      0x08

static uint8_t g_boot_flags = 0;
static char g_boot_root_device[32] = {0};
static char g_boot_init_path[128] = {0};

static void boot_parse_cmdline(const char *cmdline, uint32_t media_type) {
    (void)media_type;
    g_boot_flags = 0;
    g_boot_root_device[0] = '\0';
    g_boot_init_path[0] = '\0';

    cmdline_read_value(cmdline, "init=", g_boot_init_path, sizeof(g_boot_init_path));

    char root_arg[32];
    if (cmdline_read_value(cmdline, "root=", root_arg, (int)sizeof(root_arg))) {
        const char *dev = root_arg;
        if (dev[0] == '/' && dev[1] == 'd' && dev[2] == 'e' && dev[3] == 'v' && dev[4] == '/') {
            dev += 5;
        }
        int i = 0;
        while (dev[i] && i < (int)sizeof(g_boot_root_device) - 1) {
            g_boot_root_device[i] = dev[i];
            i++;
        }
        g_boot_root_device[i] = '\0';
        if (i > 0) g_boot_flags |= BOOT_FLAG_ROOT_SET;
    }

    if (g_boot_flags & BOOT_FLAG_ROOT_SET) {
        g_boot_flags |= BOOT_FLAG_DISK;
    } else {
        g_boot_flags |= BOOT_FLAG_LIVE;
    }
}


static void vfs_mkdir_recursive(const char *path) {
    char temp[256];
    int len = 0;
    while (path[len] && len < 255) {
        temp[len] = path[len];
        if (temp[len] == '/' && len > 0) {
            temp[len] = '\0';
            if (!vfs_exists(temp)) {
                vfs_mkdir(temp);
            }
            temp[len] = '/';
        }
        len++;
    }
    temp[len] = '\0';
    if (!vfs_exists(temp)) {
        vfs_mkdir(temp);
    }
}

static void init_graphics(void) {
    const char *cmdline = NULL;
    if (kernel_file_request.response != NULL && kernel_file_request.response->kernel_file != NULL) {
        cmdline = kernel_file_request.response->kernel_file->cmdline;
    }

    bool force_headless = cmdline_has_flag(cmdline, "headless=1") || cmdline_has_flag(cmdline, "--headless");

    if (force_headless || framebuffer_request.response == NULL || framebuffer_request.response->framebuffer_count < 1) {
        g_headless_mode = true;
        if (serial_is_com2_present()) {
            serial_set_debug_port(COM2_PORT);
            serial_set_log_silenced(false);
        }
        serial_write("[INIT] Headless mode active\n");
        return;
    }

    struct limine_framebuffer *fb = framebuffer_request.response->framebuffers[0];
    graphics_init(fb);
    kconsole_init();

    if (cmdline_has_flag(cmdline, "-v") || (cmdline != NULL && k_strstr(cmdline, "-v") != NULL)) {
        kconsole_set_active(true);
    }

    // Graphical mode active: set kernel debug to COM1
    serial_set_debug_port(COM1_PORT);
    serial_set_log_silenced(false);

    log_ok("Graphics and Console ready");
}

static void init_early(void) {
    platform_init();
    serial_init();
    init_graphics();

    serial_write("\n");
    kconsole_set_color(0xFF00FFFF);
    serial_write("===================\n");
    serial_write("Welcome to BoredOS!\n");
    serial_write("===================\n");
    kconsole_set_color(0xFFFFFFFF);
    serial_write("Built on " __DATE__ " " __TIME__ "\n");

    if (kernel_file_request.response != NULL && kernel_file_request.response->kernel_file != NULL) {
        const char *cmdline = kernel_file_request.response->kernel_file->cmdline;
        if (cmdline && cmdline[0]) {
            serial_write("[BOOT] Cmdline: ");
            serial_write(cmdline);
            serial_write("\n");
        }
    }

    vfs_init();
    log_ok("Platform initialized");
    
    extern uint64_t hhdm_offset;
    extern uint64_t kernel_phys_base;
    extern uint64_t kernel_virt_base;
    
    serial_write("[INIT] HHDM Offset: 0x");
    serial_write_hex(hhdm_offset);
    serial_write("\n");
    serial_write("[INIT] Kernel Phys: 0x");
    serial_write_hex(kernel_phys_base);
    serial_write("\n");
    serial_write("[INIT] Kernel Virt: 0x");
    serial_write_hex(kernel_virt_base);
    serial_write("\n");
}

static void init_cpu_state(void) {
    gdt_init();
    log_ok("GDT initialized");

    idt_init();
    idt_register_interrupts();

    syscall_init();
    log_ok("Syscalls ready");
}

static void init_memory(void) {
    if (memmap_request.response != NULL) {
        extern void pmm_init(const pmm_boot_map_t *boot_map);
        extern bool pmm_run_tests(void);
        extern uint64_t hhdm_offset;

        uint64_t total_mem = 0;
        uint64_t usable_mem = 0;
        pmm_mem_region_t pmm_regions[memmap_request.response->entry_count];
        for (uint64_t i = 0; i < memmap_request.response->entry_count; i++) {
            struct limine_memmap_entry *entry = memmap_request.response->entries[i];
            pmm_regions[i].base = entry->base;
            pmm_regions[i].length = entry->length;
            pmm_regions[i].type = (entry->type == LIMINE_MEMMAP_USABLE) ? PMM_REGION_USABLE : PMM_REGION_RESERVED;
            total_mem += entry->length;
            if (entry->type == LIMINE_MEMMAP_USABLE) {
                usable_mem += entry->length;
            }
        }
        pmm_boot_map_t boot_map = {
            .regions = pmm_regions,
            .region_count = memmap_request.response->entry_count,
            .direct_map_base = hhdm_offset,
        };
        pmm_init(&boot_map);

        char mem_msg[96];
        char num_buf[16];
        strcpy(mem_msg, "[MEM] Physical RAM: ");
        utoa((size_t)(usable_mem / (1024 * 1024)), num_buf);
        strcat(mem_msg, num_buf);
        strcat(mem_msg, " MB usable / ");
        utoa((size_t)(total_mem / (1024 * 1024)), num_buf);
        strcat(mem_msg, num_buf);
        strcat(mem_msg, " MB total (");
        utoa((size_t)memmap_request.response->entry_count, num_buf);
        strcat(mem_msg, num_buf);
        strcat(mem_msg, " entries)\n");
        serial_write(mem_msg);

        const char *cmdline = NULL;
        if (kernel_file_request.response != NULL && kernel_file_request.response->kernel_file != NULL) {
            cmdline = kernel_file_request.response->kernel_file->cmdline;
        }
        bool run_selftests = cmdline_has_flag(cmdline, "selftest") || cmdline_has_flag(cmdline, "test");

        if (run_selftests) {
            if (pmm_run_tests()) {
                log_ok("PMM unit tests passed");
            } else {
                log_fail("PMM unit tests failed");
            }
        }

        slab_init();
        if (run_selftests) {
            if (slab_run_tests()) {
                log_ok("SLAB Allocator unit tests passed");
            } else {
                log_fail("SLAB Allocator unit tests failed");
            }
        }

        graphics_alloc_backing_buffer();

        extern void mmu_init(void);
        extern bool mmu_run_tests(void);
        mmu_init();
        if (run_selftests) {
            if (mmu_run_tests()) {
                log_ok("MMU Hardware Driver unit tests passed");
            } else {
                log_fail("MMU Hardware Driver unit tests failed");
            }
        }

        extern bool vma_run_tests(void);
        if (run_selftests) {
            if (vma_run_tests()) {
                log_ok("VMA Augmented RB-Tree unit tests passed");
            } else {
                log_fail("VMA Augmented RB-Tree unit tests failed");
            }
        }

        extern void vmm_init(void);
        extern bool vmm_run_tests(void);
        vmm_init();
        if (run_selftests) {
            if (vmm_run_tests()) {
                log_ok("VMM Demand Paging unit tests passed");
            } else {
                log_fail("VMM Demand Paging unit tests failed");
            }
        }

        extern void pagecache_init(void);
        extern bool pagecache_run_tests(void);
        pagecache_init();
        if (run_selftests) {
            if (pagecache_run_tests()) {
                log_ok("Page Cache unit tests passed");
            } else {
                log_fail("Page Cache unit tests failed");
            }
        }

        smp_init_bsp();
        log_ok("SMP BSP initialized");
    } else {
        log_fail("No usable memory for heap! Check Limine memmap.");
        hcf();
    }
}

static void init_banner_and_acpi(void) {
    idt_load();
    log_ok("IDT ready");
    acpi_init();
}

static void init_subsystems(void) {
    process_init();

    extern void futex_init(void);
    futex_init();

    extern void time_init(void);
    time_init();

    fat32_init();
    log_ok("FAT32 ready");

    disk_manager_init();
    disk_manager_scan();
    
    // Initialize AC97 sound card
    ac97_init();

    sysfs_init_subsystems();
    vfs_mount("/sys", "sysfs", "sysfs", sysfs_get_ops(), NULL);
    vfs_mount("/proc", "procfs", "procfs", procfs_get_ops(), NULL);
}

static void init_rootfs(void) {
    if (kernel_file_request.response != NULL && kernel_file_request.response->kernel_file != NULL) {
        const char *cmdline = kernel_file_request.response->kernel_file->cmdline;
        uint32_t media_type = kernel_file_request.response->kernel_file->media_type;
        boot_parse_cmdline(cmdline, media_type);
    } else {
        boot_parse_cmdline(NULL, LIMINE_MEDIA_TYPE_GENERIC);
    }

    tmpfs_init();
    vfs_mount("/", "tmpfs", "tmpfs", tmpfs_get_ops(), NULL);

    if (g_boot_flags & BOOT_FLAG_DISK) {
        Disk *d = NULL;
        if (g_boot_root_device[0] != '\0') {
            d = disk_get_by_name(g_boot_root_device);
        }
        if (!d) {
            int total_disks = disk_get_count();
            for (int i = 0; i < total_disks; i++) {
                Disk *cand = disk_get_by_index(i);
                if (cand && cand->is_partition && !cand->is_esp) {
                    d = cand;
                    break;
                }
            }
        }

        void *vol = NULL;
        const char *fs_type = NULL;
        vfs_fs_ops_t *ops = NULL;

        if (d) {
            extern void *ext4fs_mount_volume(Disk *d);
            extern vfs_fs_ops_t *ext4fs_get_ops(void);
            extern void *fat32_mount_volume(void *disk_ptr);
            extern struct vfs_fs_ops *fat32_get_realfs_ops(void);

            if (!d->is_fat32) {
                vol = ext4fs_mount_volume(d);
                if (vol) {
                    fs_type = "ext4";
                    ops = ext4fs_get_ops();
                }
            }
            if (!vol) {
                vol = fat32_mount_volume(d);
                if (vol) {
                    fs_type = "fat32";
                    ops = (vfs_fs_ops_t *)fat32_get_realfs_ops();
                }
            }
        }

        if (d && vol && fs_type && ops) {
            vfs_umount("/");
            extern void tmpfs_destroy_all(void);
            tmpfs_destroy_all();
            vfs_mount("/", d->devname, fs_type, ops, vol);
            if (strcmp(fs_type, "fat32") == 0) {
                fat32_set_root_volume(vol);
            }
            serial_write("[INIT] Switched root to /dev/");
            serial_write(d->devname);
            serial_write(" (");
            serial_write(fs_type);
            serial_write(")\n");

            // Auto-mount ESP to /boot if available
            int total_disks = disk_get_count();
            for (int i = 0; i < total_disks; i++) {
                Disk *esp = disk_get_by_index(i);
                if (esp && esp->is_partition && esp->is_esp && esp->is_fat32) {
                    void *esp_vol = fat32_mount_volume(esp);
                    if (esp_vol) {
                        vfs_mount("/boot", esp->devname, "fat32", (vfs_fs_ops_t *)fat32_get_realfs_ops(), esp_vol);
                        serial_write("[INIT] Mounted ESP at /boot\n");
                    }
                    break;
                }
            }

            // Ensure runtime directories and /tmp tmpfs exist on the new root
            vfs_mkdir("/tmp");
            tmpfs_init();
            vfs_mount("/tmp", "tmpfs", "tmpfs", tmpfs_get_ops(), NULL);
            vfs_mkdir("/var");
            vfs_mkdir("/var/run");
            vfs_mkdir("/dev");
        } else {
            serial_write("[INIT] Warning: Root device volume not found! Running from tmpfs.\n");
        }
    }

    extern void flusher_init(void);
    flusher_init();
}


static void init_modules(void) {
    if (module_request.response == NULL) {
        log_fail("Limine module response NULL");
    } else if (!(g_boot_flags & BOOT_FLAG_DISK) || !vfs_exists("/bin/yawn.elf")) {
        log_ok("Limine modules loaded");
        for (uint64_t i = 0; i < module_request.response->module_count; i++) {
            struct limine_file *mod = module_request.response->modules[i];

            const char *clean_path = mod->path;
            if (str_starts_with(clean_path, "boot():")) clean_path += 7;
            else if (str_starts_with(clean_path, "boot:///")) clean_path += 8;
            
            int len = 0;
            while(clean_path[len]) len++;
            
            bool is_tar = (len >= 4 && 
                           clean_path[len-4] == '.' && clean_path[len-3] == 't' && 
                           clean_path[len-2] == 'a' && clean_path[len-1] == 'r');
            bool is_lz4 = (len >= 8 && 
                           clean_path[len-8] == '.' && clean_path[len-7] == 't' && 
                           clean_path[len-6] == 'a' && clean_path[len-5] == 'r' && 
                           clean_path[len-4] == '.' && clean_path[len-3] == 'l' && 
                           clean_path[len-2] == 'z' && clean_path[len-1] == '4');

            if (is_tar || is_lz4) {
                serial_write("[INIT] Parsing initrd: ");
                serial_write(clean_path);
                serial_write("\n");
                
                if (is_lz4) {
                    uint8_t *src = (uint8_t *)mod->address;
                    uint8_t flg = src[4];
                    uint64_t uncomp_size = 0;
                    if (flg & 0x08) {
                        uncomp_size = src[6] | (src[7] << 8) | (src[8] << 16) | (src[9] << 24) |
                                      ((uint64_t)src[10] << 32) | ((uint64_t)src[11] << 40) |
                                      ((uint64_t)src[12] << 48) | ((uint64_t)src[13] << 56);
                    }
                    if (uncomp_size == 0) {
                        uncomp_size = 128 * 1024 * 1024; 
                    }
                    
                    serial_write("[INIT] Decompressing LZ4 initrd (uncompressed size: 0x");
                    serial_write_hex(uncomp_size);
                    serial_write(" [");
                    serial_write_num((uint32_t)(uncomp_size / (1024 * 1024)));
                    serial_write(" MB])...\n");
                    
                    uint8_t *decomp_buf = (uint8_t *)kmalloc(uncomp_size);
                    if (!decomp_buf) {
                        serial_write("[INIT] ERROR: Failed to allocate decompression buffer!\n");
                        hcf();
                    }
                    
                    extern int lz4_decompress_frame(const uint8_t *src, int src_len, uint8_t *dst, int dst_len);
                    int decomp_size = lz4_decompress_frame(mod->address, mod->size, decomp_buf, uncomp_size);
                    if (decomp_size < 0) {
                        serial_write("[INIT] ERROR: LZ4 decompression failed!\n");
                        hcf();
                    }
                    
                    serial_write("[INIT] Decompression successful! Parsing TAR...\n");
                    
                    tar_parse(decomp_buf, decomp_size);
                    kfree(decomp_buf);
                } else {
                    tar_parse(mod->address, mod->size);
                }
            } else {
                char dir_path[256];
                int last_slash = -1;
                for (int j = 0; clean_path[j]; j++) {
                    if (clean_path[j] == '/') last_slash = j;
                }
                if (last_slash > 0) {
                    for (int j = 0; j < last_slash; j++) dir_path[j] = clean_path[j];
                    dir_path[last_slash] = '\0';
                    vfs_mkdir_recursive(dir_path);
                }
                
                vfs_file_t *fh = vfs_open(clean_path, "w");
                if (fh) {
                    vfs_write(fh, mod->address, (int)mod->size);
                    vfs_close(fh);
                }
            }
            module_manager_register(clean_path, (uint64_t)mod->address, mod->size);
        }
    }
}

static void init_input(void) {
    ps2_init();
    asm("sti");  // Enable interrupts 
    keymap_init();
    lapic_init();

    if (smp_request.response != NULL) {
        smp_init(smp_request.response);
        log_ok("SMP initialized");
    } else {
        serial_write("[INIT] No SMP response from bootloader\n");
        smp_init(NULL);
    }
}

static void init_tty(void) {

    extern void hostname_init(void);
    hostname_init();

    pty_init();

    if (!g_headless_mode) {
        // Route kernel debug output to COM1 and keep shell off COM1
        serial_set_debug_port(COM1_PORT);
        serial_set_log_silenced(false);
    } else {
        // Headless mode: COM2 (if present) is used for kernel debug logs; otherwise silence debug output on COM1
        if (serial_is_com2_present()) {
            serial_set_debug_port(COM2_PORT);
            serial_set_log_silenced(false);
        } else {
            serial_set_debug_port(COM1_PORT);
            serial_set_log_silenced(true);
        }
    }

    tty_init();
    kconsole_set_active(false);

    // Spawn userspace init system as configured by the bootloader
    const char *init_binary = g_boot_init_path[0] ? g_boot_init_path : "/bin/yawn.elf";
    serial_write("[INIT] Spawning bootloader-configured init: ");
    serial_write(init_binary);
    serial_write("\n");
    int init_tty_id = g_headless_mode ? 10 : 0;
    process_t *init_proc = process_create_elf(init_binary, g_headless_mode ? "headless" : "", SPAWN_FLAG_TERMINAL | SPAWN_FLAG_TTY_ID, init_tty_id);
    if (!init_proc) {
        serial_write("[INIT] Warning: Failed to spawn ");
        serial_write(init_binary);
        serial_write(", falling back to emergency rescue shell\n");
        if (!g_headless_mode) {
            init_proc = process_create_elf("/bin/bsh.elf", "1", SPAWN_FLAG_TERMINAL | SPAWN_FLAG_TTY_ID, 0);
        } else {
            init_proc = process_create_elf("/bin/bsh.elf", "11", SPAWN_FLAG_TERMINAL | SPAWN_FLAG_TTY_ID, 10);
        }
        if (!init_proc) {
            serial_write("[INIT] FATAL: Neither /bin/yawn.elf nor /bin/bsh.elf could be spawned!\n");
            serial_write("[INIT] The root filesystem does not contain these binaries.\n");
        }
    }

    lapic_timer_start();
}

void kmain(void) {
    init_early();
    init_cpu_state();
    init_memory();
    init_banner_and_acpi();
    init_subsystems();
    init_rootfs();
    init_modules();
    init_input();
    init_tty();

    asm volatile("sti");

    // Main blitter loop
    while(1) {
        if (!g_headless_mode) {
            tty_blit_active();
        }
        k_sleep(16); 
    }

}
