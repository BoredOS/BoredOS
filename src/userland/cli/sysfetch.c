// Copyright (c) 2023-2026 Chris (boreddevnl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
#include <stdlib.h>
#include <syscall.h>
#include <stdbool.h>

#define MAX_ASCII_LINES 32
#define MAX_ASCII_WIDTH 80
#define MAX_INFO_LINES 15

static char* strncpy(char *dest, const char *src, size_t n) {
    size_t i;
    for (i = 0; i < n && src[i] != '\0'; i++) dest[i] = src[i];
    for ( ; i < n; i++) dest[i] = '\0';
    return dest;
}

typedef struct {
    char ascii_art_file[256];
    char user_host_string[64];
    char separator[64];
    char os_label[32];
    char kernel_label[32];
    char uptime_label[32];
    char shell_label[32];
    char memory_label[32];
    char cpu_label[32];
    char disk_label[32];
    char date_label[32];
} SysfetchConfig;

static SysfetchConfig config;
static char ascii_lines[MAX_ASCII_LINES][MAX_ASCII_WIDTH];
static int ascii_line_count = 0;

static uint32_t ansi_to_boredos_color(int code) {
    uint32_t default_color = sys_get_shell_config("default_text_color");
    if (default_color == 0) default_color = 0xFFCCCCCC;

    switch (code) {
        case 0: return default_color;
        case 30: return 0xFF000000; // Black
        case 31: return 0xFFFF4444; // Red
        case 32: return 0xFF6A9955; // Green
        case 33: return 0xFFFFFF00; // Yellow
        case 34: return 0xFF569CD6; // Blue
        case 35: return 0xFFB589D6; // Magenta
        case 36: return 0xFF4EC9B0; // Cyan
        case 37: return 0xFFCCCCCC; // White
        case 90: return 0xFF808080; // Bright Black (Gray)
        case 91: return 0xFFFF6B6B; // Bright Red
        case 92: return 0xFF78DE78; // Bright Green
        case 93: return 0xFFFFFF55; // Bright Yellow
        case 94: return 0xFF87CEEB; // Bright Blue
        case 95: return 0xFFFF77FF; // Bright Magenta
        case 96: return 0xFF66D9EF; // Bright Cyan
        case 97: return 0xFFFFFFFF; // Bright White
        case 38: return 0xFF473ba3; // Bored Blue
        default: return default_color;
    }
}

static void printf_ansi(const char *str) {
    uint32_t original_color = sys_get_shell_config("default_text_color");
    if (original_color == 0) original_color = 0xFFCCCCCC;

    while (*str) {
        bool is_escape = false;
        if (*str == '\033' && *(str + 1) == '[') {
            str += 2;
            is_escape = true;
        } else if (*str == '\\' && *(str + 1) == '0' && *(str + 2) == '3' && *(str + 3) == '3' && *(str + 4) == '[') {
            str += 5;
            is_escape = true;
        }

        if (is_escape) {
            int code = 0;
            while (*str >= '0' && *str <= '9') {
                code = code * 10 + (*str - '0');
                str++;
            }
            if (*str == 'm') {
                sys_set_text_color(ansi_to_boredos_color(code));
                str++;
            }
        } else {
            char c[2] = {*str, 0};
            sys_write(1, c, 1);
            str++;
        }
    }
    sys_set_text_color(original_color);
}

static int strlen_ansi(const char *str) {
    int len = 0;
    while (*str) {
        if (*str == '\033' && *(str + 1) == '[') {
            str += 2;
            while (*str && *str != 'm') str++;
            if (*str == 'm') str++;
        } else if (*str == '\\' && *(str + 1) == '0' && *(str + 2) == '3' && *(str + 3) == '3' && *(str + 4) == '[') {
            str += 5;
            while (*str && *str != 'm') str++;
            if (*str == 'm') str++;
        } else {
            len++;
            str++;
        }
    }
    return len;
}

static char* trim(char *str) {
    char *end;
    while (*str == ' ' || *str == '\t' || *str == '\n' || *str == '\r') str++;
    if (*str == 0) return str;
    end = str + strlen(str) - 1;
    while (end > str && (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r')) end--;
    *(end + 1) = 0;
    return str;
}

static void set_config_defaults() {
    strcpy(config.ascii_art_file, "/Library/art/boredos.txt");
    strcpy(config.user_host_string, "root@boredos");
    strcpy(config.separator, "------------");
    strcpy(config.os_label, "OS");
    strcpy(config.kernel_label, "Kernel");
    strcpy(config.uptime_label, "Uptime");
    strcpy(config.shell_label, "Shell");
    strcpy(config.memory_label, "Memory");
    strcpy(config.cpu_label, "CPU");
    strcpy(config.disk_label, "Disk");
    strcpy(config.date_label, "Date");
}

static void parse_config(char* buffer) {
    char *line = buffer;
    while (*line) {
        char *next_line = strchr(line, '\n');
        if (next_line) *next_line = 0;

        if (line[0] != '/' && line[0] != '\0') {
            char *key = line;
            char *val = strchr(line, '=');
            if (val) {
                *val = 0;
                val++;
                key = trim(key);
                val = trim(val);

                if (strcmp(key, "ascii_art_file") == 0) strcpy(config.ascii_art_file, val);
                else if (strcmp(key, "user_host_string") == 0) strcpy(config.user_host_string, val);
                else if (strcmp(key, "separator") == 0) strcpy(config.separator, val);
                else if (strcmp(key, "os_label") == 0) strcpy(config.os_label, val);
                else if (strcmp(key, "kernel_label") == 0) strcpy(config.kernel_label, val);
                else if (strcmp(key, "uptime_label") == 0) strcpy(config.uptime_label, val);
                else if (strcmp(key, "shell_label") == 0) strcpy(config.shell_label, val);
                else if (strcmp(key, "memory_label") == 0) strcpy(config.memory_label, val);
                else if (strcmp(key, "cpu_label") == 0) strcpy(config.cpu_label, val);
                else if (strcmp(key, "disk_label") == 0) strcpy(config.disk_label, val);
                else if (strcmp(key, "date_label") == 0) strcpy(config.date_label, val);
            }
        }

        if (next_line) line = next_line + 1;
        else break;
    }
}

static void load_config() {
    set_config_defaults();
    int fd = sys_open("/Library/conf/sysfetch.cfg", "r");
    if (fd < 0) return;

    char *buffer = malloc(4096);
    if (!buffer) {
        sys_close(fd);
        return;
    }

    int bytes = sys_read(fd, buffer, 4095);
    sys_close(fd);

    if (bytes > 0) {
        buffer[bytes] = 0;
        parse_config(buffer);
    }
    free(buffer);
}

static void load_ascii_art() {
    int fd = sys_open(config.ascii_art_file, "r");
    if (fd < 0) {
        strcpy(ascii_lines[0],  "\033[38m       @@@@\033[0m");
        strcpy(ascii_lines[1],  "\033[38m     @@@@@@@\033[0m");
        strcpy(ascii_lines[2],  "\033[38m      @@@@@@\033[0m");
        strcpy(ascii_lines[3],  "\033[38m      @@@@@@@\033[0m");
        strcpy(ascii_lines[4],  "\033[38m       @@@@@@@      @@@@@@\033[0m");
        strcpy(ascii_lines[5],  "\033[38m        @@@@@@   @@@@@@@@@@@@\033[0m");
        strcpy(ascii_lines[6],  "\033[38m         @@@@@@ @@@@@@@@@@@@@@a\033[0m");
        strcpy(ascii_lines[7],  "\033[38m         @@@@@@@@@@@X  @@@@@@@@w\033[0m");
        strcpy(ascii_lines[8],  "\033[38m          @@@@@@@@       @@@@@@@\033[0m");
        strcpy(ascii_lines[9],  "\033[38m           @@@@@@M        @@@@@@\033[0m");
        strcpy(ascii_lines[10], "\033[38m           @@@@@@@        @@@@@@\033[0m");
        strcpy(ascii_lines[11], "\033[38m            @@@@@@@     @@@@@@@@\033[0m");
        strcpy(ascii_lines[12], "\033[38m             @@@@@@@@@@@@@@@@@@\033[0m");
        strcpy(ascii_lines[13], "\033[38m             i@@@@@@@@@@@@@@@\033[0m");
        strcpy(ascii_lines[14], "\033[38m              @@@@@@@\033[0m");
        strcpy(ascii_lines[15], "\033[38m @@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@\033[0m");
        strcpy(ascii_lines[16], "\033[38m @@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@\033[0m");
        strcpy(ascii_lines[17], "\033[38m @@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@\033[0m");
        
        ascii_line_count = 18;
        return;
    }
    


    char *buffer = malloc(4096);
    if (!buffer) {
        sys_close(fd);
        return;
    }

    int bytes = sys_read(fd, buffer, 4095);
    sys_close(fd);

    if (bytes > 0) {
        buffer[bytes] = 0;
        char *line = buffer;
        while (*line && ascii_line_count < MAX_ASCII_LINES) {
            char *next_line = strchr(line, '\n');
            if (next_line) *next_line = 0;
            
            strncpy(ascii_lines[ascii_line_count], line, MAX_ASCII_WIDTH - 1);
            ascii_lines[ascii_line_count][MAX_ASCII_WIDTH - 1] = 0;
            ascii_line_count++;

            if (next_line) line = next_line + 1;
            else break;
        }
    }
    free(buffer);
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    load_config();
    load_ascii_art();

    char info_lines[MAX_INFO_LINES][128];
    char temp_buf[32];
    int info_line_count = 0;

    if (config.user_host_string[0]) {
        strcpy(info_lines[info_line_count++], config.user_host_string);
    }
    if (config.separator[0]) {
        strcpy(info_lines[info_line_count++], config.separator);
    }
    // Helper for proc parsing
    auto int find_v(const char *b, const char *k) {
        char *p = (char*)b; int kl = strlen(k);
        while (*p) {
            if (memcmp(p, k, kl) == 0 && (p[kl] == ':' || p[kl] == '\t')) {
                p += kl; while (*p == ':' || *p == '\t' || *p == ' ') p++;
                return atoi(p);
            }
            while (*p && *p != '\n') p++; if (*p == '\n') p++;
        }
        return 0;
    }

    auto void find_s(const char *b, const char *k, char *out) {
        char *p = (char*)b; int kl = strlen(k);
        while (*p) {
            if (memcmp(p, k, kl) == 0 && (p[kl] == ':' || p[kl] == '\t')) {
                p += kl; while (*p == ':' || *p == '\t' || *p == ' ') p++;
                int i = 0;
                while (*p && *p != '\n' && i < 127) out[i++] = *p++;
                out[i] = 0;
                return;
            }
            while (*p && *p != '\n') p++; if (*p == '\n') p++;
        }
        strcpy(out, "Unknown");
    }

    int fd_v = sys_open("/proc/version", "r");
    char v_buf[512];
    if (fd_v >= 0) {
        int b = sys_read(fd_v, v_buf, 511);
        v_buf[b] = 0;
        sys_close(fd_v);
    } else strcpy(v_buf, "Unknown");

    if (config.os_label[0]) {
        strcpy(info_lines[info_line_count], config.os_label);
        strcat(info_lines[info_line_count], ": ");
        // Parse "BoredOS [codename] Version X.Y.Z"
        strcat(info_lines[info_line_count], v_buf);
        // Truncate at newline
        char *nl = strchr(info_lines[info_line_count], '\n');
        if (nl) *nl = 0;
        info_line_count++;
    }
    if (config.kernel_label[0]) {
        strcpy(info_lines[info_line_count], config.kernel_label);
        strcat(info_lines[info_line_count], ": ");
        char *kstart = strchr(v_buf, '\n');
        if (kstart) {
            strcat(info_lines[info_line_count], kstart + 1);
            char *knext = strchr(info_lines[info_line_count], '\n');
            if (knext) *knext = 0;
        } else strcat(info_lines[info_line_count], "Unknown");
        info_line_count++;
    }
    if (config.uptime_label[0]) {
        int fd_u = sys_open("/proc/uptime", "r");
        if (fd_u >= 0) {
            char u_buf[64];
            int b = sys_read(fd_u, u_buf, 63);
            u_buf[b] = 0;
            sys_close(fd_u);
            int sec  = atoi(u_buf);
            int days = sec / 86400;
            int hrs  = (sec % 86400) / 3600;
            int mins = (sec % 3600) / 60;
            strcpy(info_lines[info_line_count], config.uptime_label);
            strcat(info_lines[info_line_count], ": ");
            if (days > 0) {
                itoa(days, temp_buf); strcat(info_lines[info_line_count], temp_buf);
                strcat(info_lines[info_line_count], days == 1 ? " day, " : " days, ");
            }
            if (hrs > 0 || days > 0) {
                itoa(hrs, temp_buf); strcat(info_lines[info_line_count], temp_buf);
                strcat(info_lines[info_line_count], hrs == 1 ? " hour, " : " hours, ");
            }
            itoa(mins, temp_buf); strcat(info_lines[info_line_count], temp_buf);
            strcat(info_lines[info_line_count++], mins == 1 ? " min" : " mins");
        }
    }
    if (config.cpu_label[0]) {
        int fd_c = sys_open("/proc/cpuinfo", "r");
        if (fd_c >= 0) {
            char c_buf[2048];
            int b = sys_read(fd_c, c_buf, 2047);
            c_buf[b] = 0;
            sys_close(fd_c);
            
            char model[128];
            find_s(c_buf, "model name", model);
            int cores = find_v(c_buf, "cpu cores");
            
            strcpy(info_lines[info_line_count], config.cpu_label);
            strcat(info_lines[info_line_count], ": ");
            strcat(info_lines[info_line_count], model);
            if (cores > 0) {
                strcat(info_lines[info_line_count], " (");
                itoa(cores, temp_buf);
                strcat(info_lines[info_line_count], temp_buf);
                strcat(info_lines[info_line_count], ")");
            }
            info_line_count++;
        }
    }
    if (config.shell_label[0]) {
        strcpy(info_lines[info_line_count], config.shell_label);
        strcat(info_lines[info_line_count++], ": bsh");
    }
    if (config.memory_label[0]) {
        int fd_m = sys_open("/proc/meminfo", "r");
        if (fd_m >= 0) {
            char m_buf[512];
            int b = sys_read(fd_m, m_buf, 511);
            m_buf[b] = 0;
            sys_close(fd_m);
            int total = find_v(m_buf, "MemTotal");
            int used = find_v(m_buf, "MemUsed");
            strcpy(info_lines[info_line_count], config.memory_label);
            strcat(info_lines[info_line_count], ": ");
            itoa(used / 1024, temp_buf);
            strcat(info_lines[info_line_count], temp_buf);
            strcat(info_lines[info_line_count], "MiB / ");
            itoa(total / 1024, temp_buf);
            strcat(info_lines[info_line_count], temp_buf);
            strcat(info_lines[info_line_count++], "MiB");
        }
    }
    if (config.disk_label[0]) {
        vfs_statfs_t disk_stat;
        if (sys_fs_statfs("/", &disk_stat) == 0 && disk_stat.total_blocks > 0) {
            uint64_t total_bytes = (uint64_t)disk_stat.total_blocks * disk_stat.block_size;
            uint64_t free_bytes  = (uint64_t)disk_stat.free_blocks  * disk_stat.block_size;
            uint64_t used_bytes  = total_bytes - free_bytes;
            strcpy(info_lines[info_line_count], config.disk_label);
            strcat(info_lines[info_line_count], ": ");
            itoa((int)(used_bytes / (1024 * 1024)), temp_buf);
            strcat(info_lines[info_line_count], temp_buf);
            strcat(info_lines[info_line_count], "MiB / ");
            itoa((int)(total_bytes / (1024 * 1024)), temp_buf);
            strcat(info_lines[info_line_count], temp_buf);
            strcat(info_lines[info_line_count++], "MiB");
        }
    }
    if (config.date_label[0]) {
        int fd_d = sys_open("/proc/datetime", "r");
        if (fd_d >= 0) {
            char d_buf[64];
            int b = sys_read(fd_d, d_buf, 63);
            d_buf[b] = 0;
            sys_close(fd_d);
            
            strcpy(info_lines[info_line_count], config.date_label);
            strcat(info_lines[info_line_count], ": ");
            strcat(info_lines[info_line_count], d_buf);
            char *nl = strchr(info_lines[info_line_count], '\n');
            if (nl) *nl = 0;
            info_line_count++;
        }
    }

    int max_lines = (ascii_line_count > info_line_count) ? ascii_line_count : info_line_count;
    int ascii_width = 0;
    for (int i = 0; i < ascii_line_count; i++) {
        int len = strlen_ansi(ascii_lines[i]);
        if (len > ascii_width) ascii_width = len;
    }

    for (int i = 0; i < max_lines; i++) {
        if (i < ascii_line_count) {
            printf_ansi(ascii_lines[i]);
            int padding = ascii_width - strlen_ansi(ascii_lines[i]);
            for(int j=0; j<padding; j++) printf(" ");
        } else {
            for(int j=0; j<ascii_width; j++) printf(" ");
        }

        printf("   ");

        if (i < info_line_count) {
            printf_ansi(info_lines[i]);
        }
        printf("\n");
    }

    return 0;
}