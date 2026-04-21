// Copyright (c) 2023-2026 Chris (boreddevnl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
// BOREDOS_APP_DESC: Clock and time utility.
// BOREDOS_APP_ICONS: /Library/images/icons/colloid/preferences-system-time.png
#include "syscall.h"
#include "libui.h"
#include "stdlib.h"
#include <stdbool.h>

#define WIN_W 250
#define WIN_H 250

#define COLOR_DARK_BG       0xFF1E1E1E
#define COLOR_DARK_PANEL    0xFF2D2D2D
#define COLOR_DARK_TEXT     0xFFF0F0F0
#define COLOR_DARK_BORDER   0xFF3A3A3A
#define COLOR_TAB_ACTIVE    0xFF4A90E2
#define COLOR_ACCENT        0xFF4A90E2

typedef enum {
    TAB_CLOCK,
    TAB_TIMER,
    TAB_STOPWATCH
} tab_t;

static ui_window_t win_clock;
static tab_t current_tab = TAB_CLOCK;

// Clock State
static int adj_h = -1, adj_m = 0, adj_s = 0;
static int cur_h, cur_m, cur_s;
static int cur_y, cur_mon, cur_d;

// Timer State
static int timer_h = 0, timer_m = 0, timer_s = 0;
static long long timer_end_ticks = 0;
static bool timer_running = false;

// Stopwatch State
static long long sw_start_ticks = 0;
static long long sw_elapsed_ticks = 0;
static bool sw_running = false;

static long long sys_ticks_now(void) {
    return sys_system(SYSTEM_CMD_GET_TICKS, 0, 0, 0, 0);
}

static void format_time(char *buf, int h, int m, int s) {
    buf[0] = '0' + (h / 10);
    buf[1] = '0' + (h % 10);
    buf[2] = ':';
    buf[3] = '0' + (m / 10);
    buf[4] = '0' + (m % 10);
    buf[5] = ':';
    buf[6] = '0' + (s / 10);
    buf[7] = '0' + (s % 10);
    buf[8] = 0;
}

static void update_rtc_state(void) {
    int dt[6];
    int old_s = cur_s;
    sys_system(SYSTEM_CMD_RTC_GET, (uint64_t)dt, 0, 0, 0);
    cur_y = dt[0]; cur_mon = dt[1]; cur_d = dt[2];
    cur_h = dt[3]; cur_m = dt[4]; cur_s = dt[5];
    
    if (adj_h == -1) {
        adj_h = cur_h; adj_m = cur_m; adj_s = cur_s;
    } else if (cur_s != old_s) {
        adj_s++;
        if (adj_s >= 60) { adj_s = 0; adj_m++; }
        if (adj_m >= 60) { adj_m = 0; adj_h++; }
        if (adj_h >= 24) { adj_h = 0; }
    }
}

static void draw_btn(int x, int y, int w, int h, const char *label, bool highlight) {
    ui_draw_rounded_rect_filled(win_clock, x, y, w, h, 8, highlight ? COLOR_ACCENT : COLOR_DARK_BORDER);
    int tw = strlen(label) * 8;
    ui_draw_string(win_clock, x + (w - tw) / 2, y + (h - 8) / 2, label, COLOR_DARK_TEXT);
}

static void clock_paint(void) {
    ui_draw_rect(win_clock, 0, 0, WIN_W -5, WIN_H -20, COLOR_DARK_BG);
    
    const char *tabs[] = {"Clock", "Timer", "Stopwatch"};
    int tab_w = 70;
    int total_tabs_w = tab_w * 3 + 16;
    int start_x = (WIN_W - total_tabs_w) / 2;
    int tab_y = 8;
    int tab_h = 24;

    for (int i = 0; i < 3; i++) {
        uint32_t bg = (current_tab == i) ? COLOR_TAB_ACTIVE : COLOR_DARK_PANEL;
        ui_draw_rounded_rect_filled(win_clock, start_x + i * (tab_w + 8), tab_y, tab_w, tab_h, 12, bg);
        int tw = strlen(tabs[i]) * 8;
        ui_draw_string(win_clock, start_x + i * (tab_w + 8) + (tab_w - tw) / 2, tab_y + (tab_h - 8) / 2, tabs[i], COLOR_DARK_TEXT);
    }

    char buf[64];
    if (current_tab == TAB_CLOCK) {
        update_rtc_state();
        ui_draw_string(win_clock, (WIN_W - 12 * 8) / 2, 60, "Current Time", COLOR_DARK_TEXT);
        format_time(buf, cur_h, cur_m, cur_s);
        ui_draw_string(win_clock, (WIN_W - 8 * 8) / 2, 80, buf, COLOR_DARK_TEXT);
        
        ui_draw_string(win_clock, (WIN_W - 12 * 8) / 2, 115, "Adjust Time:", COLOR_DARK_TEXT);
        
        char adj_buf[32];
        format_time(buf, adj_h, adj_m, adj_s);
        adj_buf[0] = buf[0]; adj_buf[1] = buf[1]; adj_buf[2] = ' '; 
        adj_buf[3] = ':'; adj_buf[4] = ' '; 
        adj_buf[5] = buf[3]; adj_buf[6] = buf[4]; adj_buf[7] = ' '; 
        adj_buf[8] = ':'; adj_buf[9] = ' '; 
        adj_buf[10] = buf[6]; adj_buf[11] = buf[7]; adj_buf[12] = 0;
        int ax = (WIN_W - 12 * 8) / 2;
        ui_draw_string(win_clock, ax, 150, adj_buf, COLOR_DARK_TEXT);
        
        // Arrows
        ui_draw_string(win_clock, ax + 4, 135, "^", COLOR_ACCENT);
        ui_draw_string(win_clock, ax + 44, 135, "^", COLOR_ACCENT);
        ui_draw_string(win_clock, ax + 84, 135, "^", COLOR_ACCENT);
        
        ui_draw_string(win_clock, ax + 4, 165, "v", COLOR_ACCENT);
        ui_draw_string(win_clock, ax + 44, 165, "v", COLOR_ACCENT);
        ui_draw_string(win_clock, ax + 84, 165, "v", COLOR_ACCENT);
        
        draw_btn((WIN_W - 120) / 2, 195, 120, 28, "Apply changes", false);
        
    } else if (current_tab == TAB_TIMER) {
        long long now = sys_ticks_now();
        int display_h = timer_h, display_m = timer_m, display_s = timer_s;
        
        if (timer_running) {
            long long rem = timer_end_ticks - now;
            if (rem <= 0) {
                timer_running = false;
                display_h = display_m = display_s = 0;
                for (int i = 0; i < 3; i++) sys_system(SYSTEM_CMD_BEEP, 440, 200, 0, 0);
            } else {
                int s = rem / 60;
                display_h = s / 3600;
                display_m = (s % 3600) / 60;
                display_s = s % 60;
            }
        }
        
        format_time(buf, display_h, display_m, display_s);
        ui_draw_string(win_clock, (WIN_W - 8 * 8) / 2, 85, buf, COLOR_DARK_TEXT);
        
        int ax = (WIN_W - 8 * 8) / 2;
        if (!timer_running) {
            ui_draw_string(win_clock, ax + 4, 65, "^", COLOR_ACCENT);
            ui_draw_string(win_clock, ax + 28, 65, "^", COLOR_ACCENT);
            ui_draw_string(win_clock, ax + 52, 65, "^", COLOR_ACCENT);
            ui_draw_string(win_clock, ax + 4, 105, "v", COLOR_ACCENT);
            ui_draw_string(win_clock, ax + 28, 105, "v", COLOR_ACCENT);
            ui_draw_string(win_clock, ax + 52, 105, "v", COLOR_ACCENT);
        }
        
        draw_btn((WIN_W - 120) / 2, 150, 120, 30, timer_running ? "Stop Timer" : "Start Timer", false);
        
    } else if (current_tab == TAB_STOPWATCH) {
        long long elapsed = sw_elapsed_ticks;
        if (sw_running) elapsed += (sys_ticks_now() - sw_start_ticks);
        
        int ms_val = (elapsed % 60) * 1000 / 60;
        int s_val = (elapsed / 60) % 60;
        int m_val = (elapsed / 3600) % 60;
        
        buf[0] = '0' + (m_val / 10); buf[1] = '0' + (m_val % 10); buf[2] = ':';
        buf[3] = '0' + (s_val / 10); buf[4] = '0' + (s_val % 10); buf[5] = '.';
        buf[6] = '0' + (ms_val / 100); buf[7] = '0' + ((ms_val / 10) % 10); buf[8] = '0' + (ms_val % 10); buf[9] = 0;
        ui_draw_string(win_clock, (WIN_W - 9 * 8) / 2, 85, buf, COLOR_DARK_TEXT);
        
        draw_btn(20, 150, 90, 30, sw_running ? "Stop" : "Start", false);
        draw_btn(140, 150, 90, 30, "Reset", false);
    }
}

static void clock_click(int x, int y) {
    int tab_w = 70;
    int tab_spacing = 8;
    int total_tabs_w = tab_w * 3 + tab_spacing * 2;
    int start_x = (WIN_W - total_tabs_w) / 2;
    int tab_y = 8;
    int tab_h = 24;

    if (y >= tab_y && y <= tab_y + tab_h) {
        for (int i = 0; i < 3; i++) {
            int tx = start_x + i * (tab_w + tab_spacing);
            if (x >= tx && x <= tx + tab_w) {
                current_tab = (tab_t)i;
                clock_paint();
                ui_mark_dirty(win_clock, 0, 0, WIN_W, WIN_H);
                return;
            }
        }
    }

    int ax_clock = (WIN_W - 12 * 8) / 2;
    int ax_timer = (WIN_W - 8 * 8) / 2;

    if (current_tab == TAB_CLOCK) {
        if (y >= 130 && y <= 150) {
            if (x >= ax_clock && x <= ax_clock + 32) adj_h = (adj_h + 1) % 24;
            else if (x >= ax_clock + 33 && x <= ax_clock + 72) adj_m = (adj_m + 1) % 60;
            else if (x >= ax_clock + 73 && x <= ax_clock + 110) adj_s = (adj_s + 1) % 60;
        } else if (y >= 160 && y <= 180) {
            if (x >= ax_clock && x <= ax_clock + 32) adj_h = (adj_h + 23) % 24;
            else if (x >= ax_clock + 33 && x <= ax_clock + 72) adj_m = (adj_m + 59) % 60;
            else if (x >= ax_clock + 73 && x <= ax_clock + 110) adj_s = (adj_s + 59) % 60;
        } else if (x >= (WIN_W - 120) / 2 && x <= (WIN_W + 120) / 2 && y >= 195 && y <= 223) {
            int dt[6] = {cur_y, cur_mon, cur_d, adj_h, adj_m, adj_s};
            sys_system(SYSTEM_CMD_RTC_SET, (uint64_t)dt, 0, 0, 0);
        }
    } else if (current_tab == TAB_TIMER) {
        if (!timer_running) {
            if (y >= 60 && y <= 85) {
                if (x >= ax_timer && x <= ax_timer + 20) timer_h = (timer_h + 1) % 24;
                else if (x >= ax_timer + 24 && x <= ax_timer + 44) timer_m = (timer_m + 1) % 60;
                else if (x >= ax_timer + 48 && x <= ax_timer + 68) timer_s = (timer_s + 1) % 60;
            } else if (y >= 100 && y <= 125) {
                if (x >= ax_timer && x <= ax_timer + 20) timer_h = (timer_h + 23) % 24;
                else if (x >= ax_timer + 24 && x <= ax_timer + 44) timer_m = (timer_m + 59) % 60;
                else if (x >= ax_timer + 48 && x <= ax_timer + 68) timer_s = (timer_s + 59) % 60;
            }
        }
        if (x >= (WIN_W - 120) / 2 && x <= (WIN_W + 120) / 2 && y >= 150 && y <= 180) {
            if (!timer_running) {
                if (timer_h > 0 || timer_m > 0 || timer_s > 0) {
                    timer_running = true;
                    timer_end_ticks = sys_ticks_now() + (long long)(timer_h * 3600 + timer_m * 60 + timer_s) * 60;
                }
            } else timer_running = false;
        }
    } else if (current_tab == TAB_STOPWATCH) {
        if (x >= 20 && x <= 110 && y >= 150 && y <= 180) {
            if (sw_running) { sw_elapsed_ticks += (sys_ticks_now() - sw_start_ticks); sw_running = false; }
            else { sw_start_ticks = sys_ticks_now(); sw_running = true; }
        } else if (x >= 140 && x <= 230 && y >= 150 && y <= 180) {
            sw_elapsed_ticks = 0; sw_start_ticks = sys_ticks_now();
        }
    }
    clock_paint();
    ui_mark_dirty(win_clock, 0, 0, WIN_W, WIN_H);
}

int main(void) {
    win_clock = ui_window_create("Clock", 200, 150, WIN_W, WIN_H);
    update_rtc_state();
    clock_paint();
    ui_mark_dirty(win_clock, 0, 0, WIN_W, WIN_H);
    gui_event_t ev;
    long long last_rep = 0;
    while (1) {
        if (ui_get_event(win_clock, &ev)) {
            if (ev.type == GUI_EVENT_PAINT) { clock_paint(); ui_mark_dirty(win_clock, 0, 0, WIN_W, WIN_H); }
            else if (ev.type == GUI_EVENT_CLICK) clock_click(ev.arg1, ev.arg2);
            else if (ev.type == GUI_EVENT_CLOSE) sys_exit(0);
        } else {
            long long now = sys_ticks_now();
            if (now - last_rep >= 6) { clock_paint(); ui_mark_dirty(win_clock, 0, 0, WIN_W, WIN_H); last_rep = now; }
            sleep(10);
        }
    }
    return 0;
}
