#include "vfs.h"
#include "../sys/process.h"
#include "../sys/syscall.h"
#include "../dev/disk.h"
#include "memory_manager.h"
#include "core/kutils.h"
#include "core/platform.h"

typedef struct {
    uint32_t pid;
    char type[32]; 
    int offset;
    bool is_root;
} procfs_handle_t;

void* procfs_open(void *fs_private, const char *path, const char *mode) {
    if (path[0] == '/') path++;
    
    procfs_handle_t *h = (procfs_handle_t*)kmalloc(sizeof(procfs_handle_t));
    k_memset(h, 0, sizeof(procfs_handle_t));
    h->offset = 0;

    if (path[0] == '\0') {
        h->is_root = true;
        return h;
    }

    if (path[0] >= '0' && path[0] <= '9') {
        char pid_str[16];
        int i = 0;
        while (path[i] && path[i] != '/' && i < 15) {
            pid_str[i] = path[i];
            i++;
        }
        pid_str[i] = 0;
        h->pid = k_atoi(pid_str);

        if (path[i] == '/') {
            k_strcpy(h->type, path + i + 1);
        } else {
            h->type[0] = 0; 
        }
        return h;
    }

    h->pid = 0xFFFFFFFF;
    k_strcpy(h->type, path);
    return h;
}

void procfs_close(void *fs_private, void *handle) {
    if (handle) kfree(handle);
}

int procfs_read(void *fs_private, void *handle, void *buf, int size) {
    procfs_handle_t *h = (procfs_handle_t*)handle;
    if (!h) return -1;

    char *out = (char*)kmalloc(16384);
    if (!out) return -1;
    out[0] = 0;

    if (h->pid == 0xFFFFFFFF) {
        if (k_strcmp(h->type, "version") == 0) {
            extern void get_os_info(os_info_t *info);
            os_info_t info;
            get_os_info(&info);
            k_strcpy(out, info.os_name);
            k_strcpy(out + k_strlen(out), " [");
            k_strcpy(out + k_strlen(out), info.os_codename);
            k_strcpy(out + k_strlen(out), "] Version ");
            k_strcpy(out + k_strlen(out), info.os_version);
            k_strcpy(out + k_strlen(out), "\nKernel: ");
            k_strcpy(out + k_strlen(out), info.kernel_name);
            k_strcpy(out + k_strlen(out), " ");
            k_strcpy(out + k_strlen(out), info.kernel_version);
            k_strcpy(out + k_strlen(out), "\nBuild: ");
            k_strcpy(out + k_strlen(out), info.build_date);
            k_strcpy(out + k_strlen(out), " ");
            k_strcpy(out + k_strlen(out), info.build_time);
            k_strcpy(out + k_strlen(out), "\n");
        } else if (k_strcmp(h->type, "uptime") == 0) {
            extern uint32_t wm_get_ticks(void);
            uint32_t ticks = wm_get_ticks();
            k_itoa(ticks / 60, out);
            k_strcpy(out + k_strlen(out), " seconds\nRaw_Ticks:");
            char t_s[16]; k_itoa(ticks, t_s);
            k_strcpy(out + k_strlen(out), t_s);
            k_strcpy(out + k_strlen(out), "\n");
        } else if (k_strcmp(h->type, "cpuinfo") == 0) {
            extern uint32_t smp_cpu_count(void);
            extern void platform_get_cpu_model(char *model);
            extern void platform_get_cpu_vendor(char *vendor);
            extern void platform_get_cpu_info(cpu_info_t *info);
            extern void platform_get_cpu_flags(char *flags_str);
            
            char model[64];
            char vendor[16];
            char flags[1024];
            cpu_info_t info;
            
            platform_get_cpu_model(model);
            platform_get_cpu_vendor(vendor);
            platform_get_cpu_info(&info);
            platform_get_cpu_flags(flags);
            
            uint32_t cpu_count = smp_cpu_count();
            out[0] = '\0';
            
            // Output info for each processor
            for (uint32_t i = 0; i < cpu_count; i++) {
                char buf[32];
                
                k_strcpy(out + k_strlen(out), "processor\t: ");
                k_itoa(i, buf);
                k_strcpy(out + k_strlen(out), buf);
                k_strcpy(out + k_strlen(out), "\n");
                
                k_strcpy(out + k_strlen(out), "vendor_id\t: ");
                k_strcpy(out + k_strlen(out), vendor);
                k_strcpy(out + k_strlen(out), "\n");
                
                k_strcpy(out + k_strlen(out), "cpu family\t: ");
                k_itoa(info.family, buf);
                k_strcpy(out + k_strlen(out), buf);
                k_strcpy(out + k_strlen(out), "\n");
                
                k_strcpy(out + k_strlen(out), "model\t\t: ");
                k_itoa(info.model, buf);
                k_strcpy(out + k_strlen(out), buf);
                k_strcpy(out + k_strlen(out), "\n");
                
                k_strcpy(out + k_strlen(out), "model name\t: ");
                k_strcpy(out + k_strlen(out), model);
                k_strcpy(out + k_strlen(out), "\n");
                
                k_strcpy(out + k_strlen(out), "stepping\t: ");
                k_itoa(info.stepping, buf);
                k_strcpy(out + k_strlen(out), buf);
                k_strcpy(out + k_strlen(out), "\n");
                
                k_strcpy(out + k_strlen(out), "microcode\t: 0x");
                char hex[16];
                int temp = info.microcode;
                int hex_pos = 0;
                for (int j = 7; j >= 0; j--) {
                    int digit = (temp >> (j * 4)) & 0xF;
                    hex[hex_pos++] = digit < 10 ? '0' + digit : 'a' + (digit - 10);
                }
                hex[hex_pos] = '\0';
                k_strcpy(out + k_strlen(out), hex);
                k_strcpy(out + k_strlen(out), "\n");
                
                k_strcpy(out + k_strlen(out), "cache size\t: ");
                k_itoa(info.cache_size, buf);
                k_strcpy(out + k_strlen(out), buf);
                k_strcpy(out + k_strlen(out), " KB\n");
                
                k_strcpy(out + k_strlen(out), "physical id\t: 0\n");
                k_strcpy(out + k_strlen(out), "siblings\t: ");
                k_itoa(cpu_count, buf);
                k_strcpy(out + k_strlen(out), buf);
                k_strcpy(out + k_strlen(out), "\n");
                
                k_strcpy(out + k_strlen(out), "core id\t\t: ");
                k_itoa(i, buf);
                k_strcpy(out + k_strlen(out), buf);
                k_strcpy(out + k_strlen(out), "\n");
                
                k_strcpy(out + k_strlen(out), "cpu cores\t: ");
                k_itoa(cpu_count, buf);
                k_strcpy(out + k_strlen(out), buf);
                k_strcpy(out + k_strlen(out), "\n");
                
                k_strcpy(out + k_strlen(out), "apicid\t\t: ");
                k_itoa(i, buf);
                k_strcpy(out + k_strlen(out), buf);
                k_strcpy(out + k_strlen(out), "\n");
                
                k_strcpy(out + k_strlen(out), "initial apicid\t: ");
                k_itoa(i, buf);
                k_strcpy(out + k_strlen(out), buf);
                k_strcpy(out + k_strlen(out), "\n");
                
                k_strcpy(out + k_strlen(out), "fpu\t\t: yes\n");
                k_strcpy(out + k_strlen(out), "fpu_exception\t: yes\n");
                
                k_strcpy(out + k_strlen(out), "cpuid level\t: 13\n");
                
                k_strcpy(out + k_strlen(out), "wp\t\t: yes\n");
                
                k_strcpy(out + k_strlen(out), "flags\t\t: ");
                k_strcpy(out + k_strlen(out), flags);
                k_strcpy(out + k_strlen(out), "\n");
                
                k_strcpy(out + k_strlen(out), "bugs\t\t: \n");
                k_strcpy(out + k_strlen(out), "bogomips\t: 4800.00\n");
                
                if (i < cpu_count - 1) {
                    k_strcpy(out + k_strlen(out), "\n");
                }
            }
        } else if (k_strcmp(h->type, "datetime") == 0) {
            extern void rtc_get_datetime(int *year, int *month, int *day, int *hour, int *minute, int *second);
            int y, m, d, h_val, min, s;
            rtc_get_datetime(&y, &m, &d, &h_val, &min, &s);
            
            char buf[16];
            k_itoa(y, buf);
            k_strcpy(out, buf);
            k_strcpy(out + k_strlen(out), "-");
            if (m < 10) k_strcpy(out + k_strlen(out), "0");
            k_itoa(m, buf);
            k_strcpy(out + k_strlen(out), buf);
            k_strcpy(out + k_strlen(out), "-");
            if (d < 10) k_strcpy(out + k_strlen(out), "0");
            k_itoa(d, buf);
            k_strcpy(out + k_strlen(out), buf);
            k_strcpy(out + k_strlen(out), " ");
            if (h_val < 10) k_strcpy(out + k_strlen(out), "0");
            k_itoa(h_val, buf);
            k_strcpy(out + k_strlen(out), buf);
            k_strcpy(out + k_strlen(out), ":");
            if (min < 10) k_strcpy(out + k_strlen(out), "0");
            k_itoa(min, buf);
            k_strcpy(out + k_strlen(out), buf);
            k_strcpy(out + k_strlen(out), ":");
            if (s < 10) k_strcpy(out + k_strlen(out), "0");
            k_itoa(s, buf);
            k_strcpy(out + k_strlen(out), buf);
            k_strcpy(out + k_strlen(out), "\n");
        } else if (k_strcmp(h->type, "meminfo") == 0) {
            extern MemStats memory_get_stats(void);
            MemStats stats = memory_get_stats();
            char m_s[32];
            
            k_strcpy(out, "MemTotal:\t");
            k_itoa(stats.total_memory / 1024, m_s);
            k_strcpy(out + k_strlen(out), m_s);
            k_strcpy(out + k_strlen(out), " kB\n");
            
            k_strcpy(out + k_strlen(out), "MemFree:\t");
            k_itoa(stats.available_memory / 1024, m_s);
            k_strcpy(out + k_strlen(out), m_s);
            k_strcpy(out + k_strlen(out), " kB\n");
            
            k_strcpy(out + k_strlen(out), "MemAvailable:\t");
            k_itoa(stats.available_memory / 1024, m_s);
            k_strcpy(out + k_strlen(out), m_s);
            k_strcpy(out + k_strlen(out), " kB\n");
            
            k_strcpy(out + k_strlen(out), "Buffers:\t0 kB\n");
            k_strcpy(out + k_strlen(out), "Cached:\t\t0 kB\n");
            
            k_strcpy(out + k_strlen(out), "MemUsed:\t");
            k_itoa(stats.used_memory / 1024, m_s);
            k_strcpy(out + k_strlen(out), m_s);
            k_strcpy(out + k_strlen(out), " kB\n");
            
            k_strcpy(out + k_strlen(out), "MemPeak:\t");
            k_itoa(stats.peak_memory_used / 1024, m_s);
            k_strcpy(out + k_strlen(out), m_s);
            k_strcpy(out + k_strlen(out), " kB\n");
            
            k_strcpy(out + k_strlen(out), "SwapTotal:\t0 kB\n");
            k_strcpy(out + k_strlen(out), "SwapFree:\t0 kB\n");
            
            k_strcpy(out + k_strlen(out), "Dirty:\t\t0 kB\n");
            k_strcpy(out + k_strlen(out), "Writeback:\t0 kB\n");
            k_strcpy(out + k_strlen(out), "AnonPages:\t");
            k_itoa(stats.used_memory / 1024, m_s);
            k_strcpy(out + k_strlen(out), m_s);
            k_strcpy(out + k_strlen(out), " kB\n");
            
            k_strcpy(out + k_strlen(out), "Mapped:\t\t0 kB\n");
            k_strcpy(out + k_strlen(out), "Shmem:\t\t0 kB\n");
            
            k_strcpy(out + k_strlen(out), "Blocks:\t\t");
            k_itoa(stats.allocated_blocks, m_s);
            k_strcpy(out + k_strlen(out), m_s);
            k_strcpy(out + k_strlen(out), "\n");
            
            k_strcpy(out + k_strlen(out), "FreeBlocks:\t");
            k_itoa(stats.free_blocks, m_s);
            k_strcpy(out + k_strlen(out), m_s);
            k_strcpy(out + k_strlen(out), "\n");
            
            k_strcpy(out + k_strlen(out), "Fragmentation:\t");
            k_itoa(stats.fragmentation_percent, m_s);
            k_strcpy(out + k_strlen(out), m_s);
            k_strcpy(out + k_strlen(out), "%\n");
        } else if (k_strcmp(h->type, "devices") == 0) {
            extern int disk_get_count(void);
            extern Disk* disk_get_by_index(int index);
            int dcount = disk_get_count();
            out[0] = '\0';
            
            k_strcpy(out, "Character devices:\n");
            k_strcpy(out + k_strlen(out), "  1 mem\n");
            k_strcpy(out + k_strlen(out), "  4 tty\n");
            k_strcpy(out + k_strlen(out), "  5 cua\n");
            k_strcpy(out + k_strlen(out), "  7 vcs\n");
            k_strcpy(out + k_strlen(out), "  8 stdin\n");
            k_strcpy(out + k_strlen(out), " 13 input\n");
            k_strcpy(out + k_strlen(out), " 14 sound\n");
            k_strcpy(out + k_strlen(out), " 29 fb\n");
            k_strcpy(out + k_strlen(out), "189 usb\n\n");
            
            k_strcpy(out + k_strlen(out), "Block devices:\n");
            for (int i = 0; i < dcount; i++) {
                Disk *d = disk_get_by_index(i);
                if (d && !d->is_partition) {
                    k_strcpy(out + k_strlen(out), "  8 ");
                    k_strcpy(out + k_strlen(out), d->devname);
                    k_strcpy(out + k_strlen(out), "\n");
                }
            }
            k_strcpy(out + k_strlen(out), " 11 sr\n");
            k_strcpy(out + k_strlen(out), "253 virtblk\n");
        }
    }
 else {
        process_t *proc = process_get_by_pid(h->pid);
        if (!proc) { kfree(out); return -1; }

        if (k_strcmp(h->type, "name") == 0 || k_strcmp(h->type, "cmdline") == 0) {
            k_strcpy(out, proc->name);
            k_strcpy(out + k_strlen(out), "\n");
        } else if (k_strcmp(h->type, "cwd") == 0) {
            k_strcpy(out, proc->cwd);
            k_strcpy(out + k_strlen(out), "\n");
        } else if (k_strcmp(h->type, "status") == 0) {
            k_strcpy(out, "Name: ");
            k_strcpy(out + k_strlen(out), proc->name);
            k_strcpy(out + k_strlen(out), "\nPID: ");
            char pid_s[16]; k_itoa(proc->pid, pid_s);
            k_strcpy(out + k_strlen(out), pid_s);
            k_strcpy(out + k_strlen(out), "\nState: RUNNING\nMemory: ");
            uint64_t mem_val = proc->used_memory;
            if (h->pid == 0) {
                extern MemStats memory_get_stats(void);
                mem_val = memory_get_stats().used_memory;
            }
            char mem_s[32]; k_itoa(mem_val / 1024, mem_s);
            k_strcpy(out + k_strlen(out), mem_s);
            k_strcpy(out + k_strlen(out), " KB\nTicks: ");
            char tick_s[32]; k_itoa(proc->ticks, tick_s);
            k_strcpy(out + k_strlen(out), tick_s);
            k_strcpy(out + k_strlen(out), "\nIdle: ");
            k_strcpy(out + k_strlen(out), proc->is_idle ? "1" : "0");
            k_strcpy(out + k_strlen(out), "\n");
        }
    }

    int len = k_strlen(out);
    if (h->offset >= len) { kfree(out); return 0; }

    int to_copy = len - h->offset;
    if (to_copy > size) to_copy = size;

    k_memcpy(buf, out + h->offset, to_copy);
    h->offset += to_copy;
    kfree(out);
    return to_copy;
}

int procfs_write(void *fs_private, void *handle, const void *buf, int size) {
    procfs_handle_t *h = (procfs_handle_t*)handle;
    if (!h || h->pid == 0xFFFFFFFF) return -1;

    if (k_strcmp(h->type, "signal") == 0) {
        char cmd[16];
        int to_copy = size < 15 ? size : 15;
        k_memcpy(cmd, buf, to_copy);
        cmd[to_copy] = 0;

        if (k_strcmp(cmd, "9") == 0 || k_strcmp(cmd, "kill") == 0) {
            process_t *proc = process_get_by_pid(h->pid);
            if (proc && proc->pid != 0) {
                process_terminate(proc);
                return size;
            }
        }
    }

    return -1;
}

int procfs_readdir(void *fs_private, const char *path, vfs_dirent_t *entries, int max) {
    if (path[0] == '/') path++;

    if (path[0] == '\0') {
        int out = 0;
        k_strcpy(entries[out++].name, "version");
        entries[out-1].is_directory = 0;
        k_strcpy(entries[out++].name, "uptime");
        entries[out-1].is_directory = 0;
        k_strcpy(entries[out++].name, "cpuinfo");
        entries[out-1].is_directory = 0;
        k_strcpy(entries[out++].name, "meminfo");
        entries[out-1].is_directory = 0;
        k_strcpy(entries[out++].name, "datetime");
        entries[out-1].is_directory = 0;
        k_strcpy(entries[out++].name, "devices");
        entries[out-1].is_directory = 0;

        extern process_t processes[];
        for (int i = 0; i < 16 && out < max; i++) {
            if (processes[i].pid != 0xFFFFFFFF) {
                k_itoa(processes[i].pid, entries[out].name);
                entries[out].is_directory = 1;
                entries[out].size = 0;
                out++;
            }
        }
        return out;
    }

    if (path[0] >= '0' && path[0] <= '9') {
        int out = 0;
        k_strcpy(entries[out++].name, "name");
        k_strcpy(entries[out++].name, "status");
        k_strcpy(entries[out++].name, "cmdline");
        k_strcpy(entries[out++].name, "cwd");
        k_strcpy(entries[out++].name, "signal");
        for(int i=0; i<out; i++) entries[i].is_directory = 0;
        return out;
    }

    return 0;
}

bool procfs_exists(void *fs_private, const char *path) {
    if (path[0] == '/') path++;
    if (path[0] == '\0') return true;

    if (path[0] >= '0' && path[0] <= '9') {
        char pid_str[16];
        int i = 0;
        while (path[i] && path[i] != '/' && i < 15) {
            pid_str[i] = path[i];
            i++;
        }
        pid_str[i] = 0;
        uint32_t pid = k_atoi(pid_str);
        if (process_get_by_pid(pid)) return true;
    }

    if (k_strcmp(path, "version") == 0 || k_strcmp(path, "uptime") == 0) return true;
    if (k_strcmp(path, "cpuinfo") == 0 || k_strcmp(path, "meminfo") == 0) return true;
    if (k_strcmp(path, "datetime") == 0 || k_strcmp(path, "devices") == 0) return true;

    return false;
}

bool procfs_is_dir(void *fs_private, const char *path) {
    if (path[0] == '/') path++;
    if (path[0] == '\0') return true;

    if (path[0] >= '0' && path[0] <= '9') {
        int i = 0;
        while (path[i] && path[i] != '/') i++;
        if (path[i] == '\0') return true; 
        return false; 
    }

    return false; 
}

vfs_fs_ops_t procfs_ops = {
    .open = procfs_open,
    .close = procfs_close,
    .read = procfs_read,
    .write = procfs_write,
    .readdir = procfs_readdir,
    .exists = procfs_exists,
    .is_dir = procfs_is_dir 
};

vfs_fs_ops_t* procfs_get_ops(void) {
    return &procfs_ops;
}
