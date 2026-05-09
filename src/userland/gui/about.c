// Copyright (c) 2023-2026 Chris (boreddevnl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
// BOREDOS_APP_DESC: Shows BoredOS information.
// BOREDOS_APP_ICONS: /Library/images/icons/colloid/indicator-cpufreq.png
#include "syscall.h"
#include "libc/libui.h"
#include "libc/stdlib.h"
#include "libc/string.h"
#include "stb_image.h"
#include <stdbool.h>

static uint32_t *branding_pixels = NULL;
static int branding_w = 0;
static int branding_h = 0;

static void scale_rgba_to_argb(const unsigned char *rgba, int src_w, int src_h, uint32_t *dst, int dst_w, int dst_h) {
    if (src_w == dst_w && src_h == dst_h) {
        for (int i = 0; i < dst_w * dst_h; i++) {
            int idx = i * 4;
            dst[i] = ((uint32_t)rgba[idx + 3] << 24) | ((uint32_t)rgba[idx] << 16) | 
                     ((uint32_t)rgba[idx + 1] << 8) | rgba[idx + 2];
        }
        return;
    }

    uint32_t step_x = (src_w << 16) / dst_w;
    uint32_t step_y = (src_h << 16) / dst_h;
    uint32_t curr_y = 0;

    for (int y = 0; y < dst_h; y++) {
        uint32_t src_y = curr_y >> 16;
        if (src_y >= (uint32_t)src_h) src_y = src_h - 1;
        uint32_t curr_x = 0;
        uint32_t src_row_off = src_y * src_w;
        uint32_t dst_row_off = y * dst_w;
        for (int x = 0; x < dst_w; x++) {
            uint32_t src_x = curr_x >> 16;
            if (src_x >= (uint32_t)src_w) src_x = src_w - 1;
            int idx = (src_row_off + src_x) * 4;
            dst[dst_row_off + x] = ((uint32_t)rgba[idx + 3] << 24) | 
                                   ((uint32_t)rgba[idx] << 16) | 
                                   ((uint32_t)rgba[idx + 1] << 8) | 
                                   rgba[idx + 2];
            curr_x += step_x;
        }
        curr_y += step_y;
    }
}

static void load_branding_image(void) {
    const char *path = "/Library/images/branding/bOS_full_gradient_cropped.png";
    int fd = sys_open(path, "r");
    if (fd < 0) return;

    uint32_t size = sys_size(fd);
    if (size == 0) { sys_close(fd); return; }

    unsigned char *buf = malloc(size);
    if (!buf) { sys_close(fd); return; }

    sys_read(fd, (char*)buf, size);
    sys_close(fd);

    int img_w, img_h, channels;
    unsigned char *rgba = stbi_load_from_memory(buf, size, &img_w, &img_h, &channels, 4);
    free(buf);

    if (!rgba) return;

    // Scale to fit width (350px)
    branding_w = 350;
    branding_h = (img_h * branding_w) / img_w;
    
    branding_pixels = malloc(branding_w * branding_h * sizeof(uint32_t));
    if (branding_pixels) {
        scale_rgba_to_argb(rgba, img_w, img_h, branding_pixels, branding_w, branding_h);
    }
    stbi_image_free(rgba);
}

static void about_paint(ui_window_t win) {
    int w = 380;
    int h = 260;
    
    ui_draw_rect(win, 0, 0, w, h, 0xFF1E1E1E);
    
    int offset_x = 15;
    int offset_y = 35;
    
    if (branding_pixels) {
        ui_draw_image(win, offset_x, offset_y, branding_w, branding_h, branding_pixels);
    }
    
    int text_y = offset_y + branding_h + 15;
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

    ui_draw_string(win, offset_x, text_y, os_name_str, 0xFFFFFFFF);
    ui_draw_string(win, offset_x, text_y + fh, os_version_str, 0xFFFFFFFF);
    ui_draw_string(win, offset_x, text_y + fh*2, kernel_version_str, 0xFFFFFFFF);
    ui_draw_string(win, offset_x, text_y + fh*3, build_date_str, 0xFFFFFFFF);
    
    // Copyright
    ui_draw_string(win, offset_x, text_y + fh*4, "(C) 2026 boreddevnl.", 0xFFFFFFFF);
    ui_draw_string(win, offset_x, text_y + fh*5, "All rights reserved.", 0xFFFFFFFF);
    
    ui_mark_dirty(win, 0, 0, w, h);
}

int main(void) {
    load_branding_image();
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
