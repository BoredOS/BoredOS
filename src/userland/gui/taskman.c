// Copyright (c) 2023-2026 Chris (boreddevnl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// BOREDOS_APP_DESC: Task and process manager.
// BOREDOS_APP_ICONS: /Library/images/icons/colloid/utilities-system-monitor.png
#include "syscall.h"
#include "libui.h"
#include "stdlib.h"

#define COLOR_DARK_BG       0xFF121212
#define COLOR_DARK_PANEL    0xFF1E1E1E
#define COLOR_DARK_TEXT     0xFFE0E0E0
#define COLOR_DARK_BORDER   0xFF333333
#define COLOR_ACCENT        0xFF4FC3F7
#define COLOR_CPU           0xFF81C784
#define COLOR_MEM           0xFFFFB74D
#define COLOR_KILL          0xFFE57373
#define COLOR_DIM_TEXT      0xFF999999

#define MAX_VISIBLE_PROCS 10
#define GRAPH_POINTS 60

static ui_window_t win_taskman;
static ProcessInfo proc_list[32];
static int proc_count = 0;
static int selected_proc = -1;

// History as fixed-point (x100)
static int cpu_history[GRAPH_POINTS];
static int mem_history[GRAPH_POINTS];
static int history_idx = 0;

static uint64_t user_ticks_prev = 0;
static uint64_t total_ticks_prev = 0;
static uint64_t total_mem_system = 0;
static uint64_t used_mem_system = 0;
static char cpu_model_name[64] = "Unknown CPU";
static int cpu_cores = 1;

static int find_value(const char *buf, const char *key) {
    char *p = (char*)buf;
    int key_len = strlen(key);
    while (*p) {
        if (memcmp(p, key, key_len) == 0 && p[key_len] == ':') {
            p += key_len + 1;
            while (*p == ' ') p++;
            return atoi(p);
        }
        while (*p && *p != '\n') p++;
        if (*p == '\n') p++;
    }
    return 0;
}

static void find_string(const char *buf, const char *key, char *out, int max_len) {
    char *p = (char*)buf;
    int key_len = strlen(key);
    while (*p) {
        if (memcmp(p, key, key_len) == 0 && p[key_len] == ':') {
            p += key_len + 1;
            while (*p == ' ') p++;
            int i = 0;
            while (*p && *p != '\n' && i < max_len - 1) {
                out[i++] = *p++;
            }
            out[i] = 0;
            return;
        }
        while (*p && *p != '\n') p++;
        if (*p == '\n') p++;
    }
    strcpy(out, "Unknown");
}

static void update_proc_list(void) {
    FAT32_FileInfo entries[64];
    int count = sys_list("/proc", entries, 64);
    if (count < 0) return;

    proc_count = 0;
    uint64_t user_ticks_now = 0;
    uint64_t total_ticks_now = 0;

    for (int i = 0; i < count; i++) {
        if (entries[i].is_directory) {
            // Check if name is numeric (PID)
            bool numeric = true;
            for (int j = 0; entries[i].name[j]; j++) {
                if (entries[i].name[j] < '0' || entries[i].name[j] > '9') {
                    numeric = false;
                    break;
                }
            }
            if (!numeric) continue;

            int pid = atoi(entries[i].name);
            char path[64];
            strcpy(path, "/proc/");
            strcat(path, entries[i].name);
            strcat(path, "/status");

            int fd = sys_open(path, "r");
            if (fd >= 0) {
                char buf[512];
                int bytes = sys_read(fd, buf, 511);
                sys_close(fd);
                if (bytes > 0) {
                    buf[bytes] = 0;
                    proc_list[proc_count].pid = pid;
                    find_string(buf, "Name", proc_list[proc_count].name, 64);
                    proc_list[proc_count].used_memory = (size_t)find_value(buf, "Memory") * 1024;
                    uint64_t ticks = (uint64_t)find_value(buf, "Ticks");
                    proc_list[proc_count].ticks = ticks;
                    total_ticks_now += ticks;
                    
                    proc_list[proc_count].is_idle = find_value(buf, "Idle") == 1;
                    
                    if (!proc_list[proc_count].is_idle) user_ticks_now += ticks;
                    proc_count++;
                    if (proc_count >= 32) break;
                }
            }
        }
    }

    if (total_ticks_prev > 0) {
        uint64_t total_delta = total_ticks_now - total_ticks_prev;
        if (total_delta > 0) {
            uint64_t used_delta = user_ticks_now - user_ticks_prev;
            int usage = (int)((used_delta * 100) / total_delta);
            if (usage > 100) usage = 100;
            if (usage < 0) usage = 0;
            cpu_history[history_idx] = usage;
        }
    }

    user_ticks_prev = user_ticks_now;
    total_ticks_prev = total_ticks_now;
    
    int fd_m = sys_open("/proc/meminfo", "r");
    if (fd_m >= 0) {
        char buf[1024];
        int bytes = sys_read(fd_m, buf, 1023);
        sys_close(fd_m);
        if (bytes > 0) {
            buf[bytes] = 0;
            total_mem_system = (uint64_t)find_value(buf, "MemTotal") * 1024;
            used_mem_system = (uint64_t)find_value(buf, "MemUsed") * 1024;
            mem_history[history_idx] = (int)(used_mem_system / 1024);
        }
    }
    
    history_idx = (history_idx + 1) % GRAPH_POINTS;
}

static void draw_graph(int x, int y, int w, int h, int *data, uint32_t color, int max_val) {
    ui_draw_rect(win_taskman, x, y, w, h, COLOR_DARK_PANEL);
    ui_draw_rect(win_taskman, x, y, w, 1, COLOR_DARK_BORDER);
    ui_draw_rect(win_taskman, x, y + h - 1, w, 1, COLOR_DARK_BORDER);
    
    if (max_val == 0) max_val = 1;
    
    for (int i = 0; i < GRAPH_POINTS - 1; i++) {
        int idx1 = (history_idx + i) % GRAPH_POINTS;
        
        long long val = (long long)data[idx1];
        int h_val = (int)((val * h) / max_val);
        if (h_val > h) h_val = h;
        if (h_val < 0) h_val = 0;
        
        int x1 = x + (i * w) / GRAPH_POINTS;
        int next_x = x + ((i + 1) * w) / GRAPH_POINTS;
        int draw_w = next_x - x1;
        if (draw_w <= 0) draw_w = 1;
        
        ui_draw_rect(win_taskman, x1, y + h - h_val, draw_w, h_val ? h_val : 1, color);
    }
}

static void format_mem_smart(uint64_t bytes, char *out) {
    if (bytes < 1024) {
        itoa((int)bytes, out);
        strcat(out, " B");
    } else if (bytes < 1024 * 1024) {
        itoa((int)(bytes / 1024), out);
        strcat(out, " KB");
    } else if (bytes < 1024 * 1024 * 1024) {
        // Show MiB with two decimal places
        uint64_t mib_int = bytes / (1024 * 1024);
        uint64_t mib_frac = ((bytes % (1024 * 1024)) * 100) / (1024 * 1024);
        char s_int[16], s_frac[16];
        itoa((int)mib_int, s_int);
        itoa((int)mib_frac, s_frac);
        strcpy(out, s_int);
        strcat(out, ".");
        if (mib_frac < 10) strcat(out, "0");
        strcat(out, s_frac);
        strcat(out, " MiB");
    } else {
        // Show GiB with two decimal places
        uint64_t gib_int = bytes / (1024 * 1024 * 1024);
        uint64_t gib_frac = ((bytes % (1024 * 1024 * 1024)) * 100) / (1024 * 1024 * 1024);
        char s_int[16], s_frac[16];
        itoa((int)gib_int, s_int);
        itoa((int)gib_frac, s_frac);
        strcpy(out, s_int);
        strcat(out, ".");
        if (gib_frac < 10) strcat(out, "0");
        strcat(out, s_frac);
        strcat(out, " GiB");
    }
}

static void draw_taskman(void) {
    int win_w = 400;
    int win_h = 480;
    
    ui_draw_rect(win_taskman, 0, 0, win_w, win_h, COLOR_DARK_BG);
    
    // CPU Graph Area
    ui_draw_string(win_taskman, 10, 10, "PROCESSOR", COLOR_CPU);
    char cpu_label[16];
    int current_cpu = cpu_history[(history_idx + GRAPH_POINTS - 1) % GRAPH_POINTS];
    itoa(current_cpu, cpu_label);
    strcat(cpu_label, "%");
    ui_draw_string(win_taskman, 140, 10, cpu_label, COLOR_CPU);
    draw_graph(10, 25, 185, 60, cpu_history, COLOR_CPU, 100);
    
    // CPU Model (Safe truncation)
    char model_disp[32]; 
    int mlen = strlen(cpu_model_name);
    if (mlen > 22) {
        memcpy(model_disp, cpu_model_name, 19);
        model_disp[19] = '.'; model_disp[20] = '.'; model_disp[21] = '.'; model_disp[22] = 0;
    } else {
        strcpy(model_disp, cpu_model_name);
    }
    ui_draw_string(win_taskman, 10, 92, model_disp, COLOR_DIM_TEXT);
    
    // Memory Graph Area
    ui_draw_string(win_taskman, 205, 10, "MEMORY", COLOR_MEM);
    char mem_pct_label[16];
    int current_mem_pct_x10 = 0;
    if (total_mem_system > 0) current_mem_pct_x10 = (int)((used_mem_system * 1000) / total_mem_system);
    itoa(current_mem_pct_x10 / 10, mem_pct_label);
    strcat(mem_pct_label, ".");
    char frac[4]; itoa(current_mem_pct_x10 % 10, frac);
    strcat(mem_pct_label, frac);
    strcat(mem_pct_label, "%");
    ui_draw_string(win_taskman, 340, 10, mem_pct_label, COLOR_MEM);
    
    int max_mem_kb = (int)(total_mem_system / 1024);
    draw_graph(205, 25, 185, 60, mem_history, COLOR_MEM, max_mem_kb);
    
    // Memory GiB usage
    char s_used[32], s_total[32], mem_text[64];
    format_mem_smart(used_mem_system, s_used);
    format_mem_smart(total_mem_system, s_total);
    mem_text[0] = 0;
    strcat(mem_text, s_used);
    strcat(mem_text, " / ");
    strcat(mem_text, s_total);
    
    ui_draw_string(win_taskman, 205, 92, mem_text, COLOR_DIM_TEXT);
    
    // Process List Header
    ui_draw_rect(win_taskman, 10, 120, 380, 24, COLOR_DARK_PANEL);
    ui_draw_string(win_taskman, 15, 125, "PID", COLOR_DIM_TEXT);
    ui_draw_string(win_taskman, 60, 125, "NAME", COLOR_DIM_TEXT);
    ui_draw_string(win_taskman, 250, 125, "MEMORY", COLOR_DIM_TEXT);
    
    // Process Rows
    int row = 0;
    for (int i = 0; i < proc_count && row < MAX_VISIBLE_PROCS; i++) {
        if (proc_list[i].pid == 0xFFFFFFFF) continue;
        
        int ry = 150 + row * 26;
        uint32_t bg = (selected_proc == row) ? 0xFF334455 : COLOR_DARK_PANEL;
        ui_draw_rounded_rect_filled(win_taskman, 10, ry, 380, 24, 4, bg);
        
        char pid_str[16];
        itoa(proc_list[i].pid, pid_str);
        ui_draw_string(win_taskman, 20, ry + 6, pid_str, COLOR_DARK_TEXT);
        
        char name_disp[28];
        if (strlen(proc_list[i].name) > 22) {
            memcpy(name_disp, proc_list[i].name, 19);
            name_disp[19] = '.'; name_disp[20] = '.'; name_disp[21] = '.'; name_disp[22] = 0;
        } else {
            strcpy(name_disp, proc_list[i].name);
        }
        ui_draw_string(win_taskman, 65, ry + 6, name_disp, COLOR_DARK_TEXT);
        
        char m_str[32];
        format_mem_smart(proc_list[i].used_memory, m_str);
        ui_draw_string(win_taskman, 255, ry + 6, m_str, COLOR_DARK_TEXT);
        
        row++;
    }
    
    // Kill button (Positioned relative to window height)
    int btn_x = 400 - 110;
    int btn_y = 480 - 70;
    int btn_w = 100;
    int btn_h = 30;

    // Disable kill for PID 0
    bool can_kill = (selected_proc != -1);
    if (can_kill) {
        int v_cnt = 0;
        for (int i = 0; i < proc_count; i++) {
            if (proc_list[i].pid != 0xFFFFFFFF) {
                if (v_cnt == selected_proc) {
                    if (proc_list[i].pid == 0) can_kill = false;
                    break;
                }
                v_cnt++;
            }
        }
    }

    ui_draw_rounded_rect_filled(win_taskman, btn_x, btn_y, btn_w, btn_h, 6, can_kill ? COLOR_KILL : COLOR_DARK_BORDER);
    
    const char *btn_text = "FORCE KILL";
    int tx = btn_x + (btn_w - 80) / 2;
    int ty = btn_y + (btn_h - 12) / 2;
    ui_draw_string(win_taskman, tx, ty, btn_text, can_kill ? 0xFFFFFFFF : 0xFF666666);
}

int main(void) {
    win_taskman = ui_window_create("Task Manager", 100, 100, 400, 480);
    
    int fd_c = sys_open("/proc/cpuinfo", "r");
    if (fd_c >= 0) {
        char buf[1024];
        int bytes = sys_read(fd_c, buf, 1023);
        sys_close(fd_c);
        if (bytes > 0) {
            buf[bytes] = 0;
            find_string(buf, "Processor", cpu_model_name, 64);
            int cores = find_value(buf, "Cores");
            if (cores > 0) cpu_cores = cores;
        }
    }
    
    for(int i=0; i<GRAPH_POINTS; i++) { cpu_history[i] = 0; mem_history[i] = 0; }
    
    gui_event_t ev;
    
    while (1) {
        // Drain events
        while (ui_get_event(win_taskman, &ev)) {
            if (ev.type == GUI_EVENT_CLOSE) {
                sys_exit(0);
            } else if (ev.type == GUI_EVENT_CLICK) {
                int mx = ev.arg1;
                int my = ev.arg2;
                
                if (mx >= 10 && mx < 390 && my >= 150 && my < 150 + MAX_VISIBLE_PROCS * 26) {
                    int idx = (my - 150) / 26;
                    int valid_count = 0;
                    int target_i = -1;
                    for (int i = 0; i < proc_count; i++) {
                        if (proc_list[i].pid != 0xFFFFFFFF) {
                            if (valid_count == idx) { target_i = i; break; }
                            valid_count++;
                        }
                    }
                    if (target_i != -1) selected_proc = idx;
                    else selected_proc = -1;
                    
                    draw_taskman();
                    ui_mark_dirty(win_taskman, 0, 0, 400, 480);
                } else if (mx >= 290 && mx < 390 && my >= 410 && my < 440) {
                    if (selected_proc != -1) {
                        int valid_count = 0;
                        for (int i = 0; i < proc_count; i++) {
                            if (proc_list[i].pid != 0xFFFFFFFF) {
                                if (valid_count == selected_proc) {
                                    if (proc_list[i].pid != 0) sys_kill(proc_list[i].pid);
                                    break;
                                }
                                valid_count++;
                            }
                        }
                        selected_proc = -1;
                        update_proc_list();
                        draw_taskman();
                        ui_mark_dirty(win_taskman, 0, 0, 400, 480);
                    }
                }
            } else if (ev.type == GUI_EVENT_PAINT) {
                draw_taskman();
                ui_mark_dirty(win_taskman, 0, 0, 400, 480);
            }
        }
        
        update_proc_list();
        draw_taskman();
        ui_mark_dirty(win_taskman, 0, 0, 400, 480);
        
        // Proper blocking sleep (200ms)
        sys_system(SYSTEM_CMD_SLEEP, 200, 0, 0, 0);
    }
    
    return 0;
}
