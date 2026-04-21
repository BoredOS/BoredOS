// Copyright (c) 2023-2026 Chris (boreddevnl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
// BOREDOS_APP_DESC: Shows BoredOS information.
// BOREDOS_APP_ICONS: /Library/images/icons/colloid/indicator-cpufreq.png
#include "syscall.h"
#include "libc/libui.h"
#include "libc/stdlib.h"
#include <stdbool.h>

static uint32_t ansi_to_boredos_color(int code) {
    uint32_t default_color = 0xFFFFFFFF;

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
        default: return default_color;
    }
}

static void draw_ansi_string(ui_window_t win, int x, int y, const char *str) {
    uint32_t current_color = 0xFFFFFFFF;
    int current_x = x;
    char segment[256];
    int seg_idx = 0;

    while (*str) {
        if (*str == '\033' && *(str + 1) == '[') {
            if (seg_idx > 0) {
                segment[seg_idx] = 0;
                ui_draw_string_bitmap(win, current_x, y, segment, current_color);
                current_x += seg_idx * 8; // Bitmap font is exactly 8px wide
                seg_idx = 0;
            }

            str += 2;
            int code = 0;
            while (*str >= '0' && *str <= '9') {
                code = code * 10 + (*str - '0');
                str++;
            }
            if (*str == 'm') {
                current_color = ansi_to_boredos_color(code);
                str++;
            }
        } else {
            segment[seg_idx++] = *str++;
        }
    }

    if (seg_idx > 0) {
        segment[seg_idx] = 0;
        ui_draw_string_bitmap(win, current_x, y, segment, current_color);
    }
}

static void draw_ascii_logo(ui_window_t win, int x, int y) {
    const char *logo[] = {
        "\033[35m==================== \033[97m__    ____  ____ \033[0m",
        "\033[35m=================== \033[97m/ /_  / __ \\/ ___\\\033[0m",
        "\033[34m================== \033[97m/ __ \\/ / / /\\___ \\\033[0m",
        "\033[34m================= \033[97m/ /_/ / /_/ /____/ /\033[0m",
        "\033[36m================ \033[97m/_.___/\\____//_____/ \033[0m",
        "\033[36m===============                       \033[0m",
        NULL
    };

    for (int i = 0; logo[i] != NULL; i++) {
        draw_ansi_string(win, x, y + (i * 10), logo[i]); // Bitmap line height is 10px
    }
}

static void about_paint(ui_window_t win) {
    int w = 380;
    int h = 260;
    
    ui_draw_rect(win, 0, 0, w, h, 0xFF1E1E1E);
    
    int offset_x = 15;
    int offset_y = 35;
    
    draw_ascii_logo(win, 14, offset_y);
    
    int fh = ui_get_font_height();
    int fd_v = sys_open("/proc/version", "r");
    char v_buf[1024]; v_buf[0] = 0;
    if (fd_v >= 0) {
        int b = sys_read(fd_v, v_buf, 1023);
        v_buf[b] = 0;
        sys_close(fd_v);
    }

    char os_name_str[128] = "Unknown OS";
    char os_version_str[128] = "Unknown Version";
    char kernel_version_str[128] = "Unknown Kernel";
    char build_date_str[128] = "Unknown Build";

    if (v_buf[0]) {
        char *line1 = v_buf;
        char *line2 = strchr(line1, '\n'); if (line2) { *line2 = 0; line2++; }
        char *line3 = line2 ? strchr(line2, '\n') : NULL; if (line3) { *line3 = 0; line3++; }

        strcpy(os_name_str, line1);
        if (line2) {
            strcpy(os_version_str, line2);
        }
        if (line3) {
            strcpy(kernel_version_str, line3);
            char *line4 = strchr(line3, '\n');
            if (line4) {
                *line4 = 0; line4++;
                strcpy(build_date_str, line4);
                char *line5 = strchr(build_date_str, '\n');
                if (line5) *line5 = 0;
            }
        }
    }

    ui_draw_string(win, offset_x, offset_y + 105, os_name_str, 0xFFFFFFFF);
    ui_draw_string(win, offset_x, offset_y + 105 + fh, os_version_str, 0xFFFFFFFF);
    ui_draw_string(win, offset_x, offset_y + 105 + fh*2, kernel_version_str, 0xFFFFFFFF);
    ui_draw_string(win, offset_x, offset_y + 105 + fh*3, build_date_str, 0xFFFFFFFF);
    
    // Copyright
    ui_draw_string(win, offset_x, offset_y + 105 + fh*4, "(C) 2026 boreddevnl.", 0xFFFFFFFF);
    ui_draw_string(win, offset_x, offset_y + 105 + fh*5, "All rights reserved.", 0xFFFFFFFF);
    
    ui_mark_dirty(win, 0, 0, w, h);
}

int main(void) {
    ui_window_t win_about = ui_window_create("About BoredOS", 250, 180, 380, 260);
    
    about_paint(win_about);
    
    gui_event_t ev;
    while (1) {
        if (ui_get_event(win_about, &ev)) {
            if (ev.type == GUI_EVENT_PAINT) {
                about_paint(win_about);
            } else if (ev.type == GUI_EVENT_CLOSE) {
                sys_exit(0);
            }
        } else {
            sleep(10);
        }
    }
    
    return 0;
}
