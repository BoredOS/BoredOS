// Copyright (c) 2023-2026 Chris (boreddevnl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
// BOREDOS_APP_DESC: Image viewer utility.
// BOREDOS_APP_ICONS: /Library/images/icons/colloid/preferences-desktop-wallpaper.png;/Library/images/icons/colloid/org.gnome.Loupe.png
#include "stb_image.h"
#include "libc/syscall.h"
#include "libc/libui.h"
#include "libc/stdlib.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define VIEWER_MAX_W 4096
#define VIEWER_MAX_H 4096

static uint32_t *viewer_pixels = NULL;
static uint32_t **viewer_frames = NULL;
static int *viewer_delays = NULL;
static int viewer_frame_count = 0;
static int viewer_current_frame = 0;
static uint64_t viewer_next_frame_tick = 0;

static int viewer_img_w = 0;
static int viewer_img_h = 0;
static char viewer_title[64] = "Viewer";
static bool viewer_has_image = false;
static char viewer_file_path[256];
static bool resize_pending = false;
static uint64_t last_resize_tick = 0;

static int win_w = 500;
static int win_h = 400;

static void viewer_strcpy(char *dst, const char *src) {
    while (*src) *dst++ = *src++;
    *dst = 0;
}

static int viewer_strlen(const char *s) {
    int len = 0;
    while (s[len]) len++;
    return len;
}

static void viewer_strncat(char *dst, const char *src, int max_len) {
    if (!dst || !src || max_len <= 0) return;
    int dlen = viewer_strlen(dst);
    int i = 0;
    while (dlen + i < max_len - 1 && src[i]) {
        dst[dlen + i] = src[i];
        i++;
    }
    dst[dlen + i] = 0;
}

static void viewer_resolve_path(const char *input, char *out, int max_len) {
    if (!out || max_len <= 0) return;
    if (!input || input[0] == 0) {
        out[0] = 0;
        return;
    }
    if (input[0] == '/') {
        viewer_strcpy(out, input);
        return;
    }

    char cwd[256];
    if (sys_getcwd(cwd, sizeof(cwd)) < 0) {
        viewer_strcpy(out, input);
        return;
    }

    viewer_strcpy(out, cwd);
    int len = viewer_strlen(out);
    if (len > 0 && out[len - 1] != '/') viewer_strncat(out, "/", max_len);
    viewer_strncat(out, input, max_len);
}

static void viewer_scale_rgba_to_argb(const unsigned char *rgba, int src_w, int src_h,
                                      uint32_t *dst, int dst_w, int dst_h) {
    if (src_w == dst_w && src_h == dst_h) {
        // Fast path: 1:1 copy
        for (int i = 0; i < dst_w * dst_h; i++) {
            int idx = i * 4;
            dst[i] = ((uint32_t)rgba[idx + 3] << 24) | ((uint32_t)rgba[idx] << 16) | 
                     ((uint32_t)rgba[idx + 1] << 8) | rgba[idx + 2];
        }
        return;
    }

    // Fixed-point 16.16
    uint32_t step_x = (src_w << 16) / dst_w;
    uint32_t step_y = (src_h << 16) / dst_h;
    uint32_t curr_y = 0;

    for (int y = 0; y < dst_h; y++) {
        uint32_t src_y = curr_y >> 16;
        if (src_y >= (uint32_t)src_h) src_y = src_h - 1;
        
        uint32_t curr_x = 0;
        uint32_t src_row_offset = src_y * src_w;
        uint32_t dst_row_offset = y * dst_w;

        for (int x = 0; x < dst_w; x++) {
            uint32_t src_x = curr_x >> 16;
            if (src_x >= (uint32_t)src_w) src_x = src_w - 1;
            
            int idx = (src_row_offset + src_x) * 4;
            dst[dst_row_offset + x] = ((uint32_t)rgba[idx + 3] << 24) | 
                                      ((uint32_t)rgba[idx] << 16) | 
                                      ((uint32_t)rgba[idx + 1] << 8) | 
                                      rgba[idx + 2];
            curr_x += step_x;
        }
        curr_y += step_y;
    }
}

static void viewer_paint(ui_window_t win) {
    int cx = 0;
    int cy = 0;
    int cw = win_w;
    int ch = win_h - 20; // 20px header

    // Clear background
    ui_draw_rect(win, 0, 0, win_w, win_h, 0xFF000000); // Black background

    if (!viewer_has_image) {
        ui_draw_string(win, cx + 20, cy + ch / 2, "No image loaded", 0xFF888888);
        return;
    }

    uint32_t *pixels = viewer_pixels;
    if (viewer_frames) pixels = viewer_frames[viewer_current_frame];

    // Maintain aspect ratio while fitting to window
    int disp_w = viewer_img_w;
    int disp_h = viewer_img_h;
    
    float sw = (float)cw / (float)viewer_img_w;
    float sh = (float)ch / (float)viewer_img_h;
    float scale = (sw < sh) ? sw : sh;
    
    disp_w = (int)(viewer_img_w * scale);
    disp_h = (int)(viewer_img_h * scale);

    int ox = cx + (cw - disp_w) / 2;
    int oy = cy + (ch - disp_h) / 2;

    if (disp_w <= 0 || disp_h <= 0) return;

    uint32_t *temp_buf = malloc(disp_w * disp_h * sizeof(uint32_t));
    if (temp_buf) {
        // Fixed-point 16.16
        uint32_t step_x = (viewer_img_w << 16) / disp_w;
        uint32_t step_y = (viewer_img_h << 16) / disp_h;
        uint32_t curr_y = 0;

        for (int y = 0; y < disp_h; y++) {
            uint32_t src_y = curr_y >> 16;
            if (src_y >= (uint32_t)viewer_img_h) src_y = viewer_img_h - 1;
            uint32_t curr_x = 0;
            uint32_t src_row_off = src_y * viewer_img_w;
            uint32_t dst_row_off = y * disp_w;
            for (int x = 0; x < disp_w; x++) {
                uint32_t src_x = curr_x >> 16;
                if (src_x >= (uint32_t)viewer_img_w) src_x = viewer_img_w - 1;
                temp_buf[dst_row_off + x] = pixels[src_row_off + src_x];
                curr_x += step_x;
            }
            curr_y += step_y;
        }
        ui_draw_image(win, ox, oy, disp_w, disp_h, temp_buf);
        free(temp_buf);
    }
}

void viewer_open_file(const char *path) {
    char resolved[256];
    viewer_resolve_path(path, resolved, sizeof(resolved));
    int fd = sys_open(resolved, "r");
    if (fd < 0) return;

    uint32_t file_size = sys_size(fd);
    if (file_size == 0 || file_size > 32 * 1024 * 1024) { // 32MB limit
        sys_close(fd);
        return;
    }

    unsigned char *buf = malloc(file_size);
    if (!buf) {
        sys_close(fd);
        return;
    }

    int total_read = 0;
    while (total_read < (int)file_size) {
        int chunk = sys_read(fd, (char*)buf + total_read, (int)file_size - total_read);
        if (chunk <= 0) break;
        total_read += chunk;
    }
    sys_close(fd);

    if (total_read <= 0) {
        free(buf);
        return;
    }

    // Free previous image if any
    if (viewer_pixels) { free(viewer_pixels); viewer_pixels = NULL; }
    if (viewer_frames) {
        for (int i = 0; i < viewer_frame_count; i++) {
            if (viewer_frames[i]) free(viewer_frames[i]);
        }
        free(viewer_frames); viewer_frames = NULL;
    }
    if (viewer_delays) { free(viewer_delays); viewer_delays = NULL; }
    viewer_frame_count = 0;
    viewer_current_frame = 0;
    viewer_has_image = false;

    int img_w, img_h, channels;
    unsigned char *rgba = NULL;
    int *delays = NULL;
    int frame_count = 1;

    // Check if it's a GIF - more robust check
    bool is_gif = (total_read > 4 && buf[0] == 'G' && buf[1] == 'I' && buf[2] == 'F');

    if (is_gif) {
        rgba = stbi_load_gif_from_memory(buf, total_read, &delays, &img_w, &img_h, &frame_count, &channels, 4);
    } else {
        rgba = stbi_load_from_memory(buf, total_read, &img_w, &img_h, &channels, 4);
    }

    if (!rgba || img_w <= 0 || img_h <= 0) {
        if (rgba) stbi_image_free(rgba);
        free(buf);
        return;
    }

    int fit_w = img_w;
    int fit_h = img_h;
    if (fit_w > VIEWER_MAX_W) {
        fit_h = fit_h * VIEWER_MAX_W / fit_w;
        fit_w = VIEWER_MAX_W;
    }
    if (fit_h > VIEWER_MAX_H) {
        fit_w = fit_w * VIEWER_MAX_H / fit_h;
        fit_h = VIEWER_MAX_H;
    }

    if (frame_count > 1 && delays) {
        viewer_frames = malloc(frame_count * sizeof(uint32_t *));
        viewer_delays = malloc(frame_count * sizeof(int));
        if (viewer_frames && viewer_delays) {
            viewer_frame_count = frame_count;
            for (int i = 0; i < frame_count; i++) {
                viewer_frames[i] = malloc(fit_w * fit_h * sizeof(uint32_t));
                if (viewer_frames[i]) {
                    viewer_scale_rgba_to_argb(rgba + (i * img_w * img_h * 4), img_w, img_h, viewer_frames[i], fit_w, fit_h);
                    viewer_delays[i] = delays[i];
                } else {
                    // Memory exhausted, stop here
                    viewer_frame_count = i;
                    break;
                }
            }
            viewer_img_w = fit_w;
            viewer_img_h = fit_h;
            viewer_has_image = (viewer_frame_count > 0);
            if (viewer_has_image) {
                viewer_next_frame_tick = sys_system(SYSTEM_CMD_GET_TICKS, 0, 0, 0, 0) + (viewer_delays[0] * 60 / 1000);
            }
        }
        free(delays);
    } else {
        viewer_pixels = malloc(fit_w * fit_h * sizeof(uint32_t));
        if (viewer_pixels) {
            viewer_scale_rgba_to_argb(rgba, img_w, img_h, viewer_pixels, fit_w, fit_h);
            viewer_img_w = fit_w;
            viewer_img_h = fit_h;
            viewer_has_image = true;
        }
    }

    stbi_image_free(rgba);
    free(buf);

    viewer_strcpy(viewer_file_path, resolved);

    const char *fname = resolved;
    int plen = viewer_strlen(resolved);
    for (int i = plen - 1; i >= 0; i--) {
        if (resolved[i] == '/') {
            fname = &resolved[i + 1];
            break;
        }
    }
    
    viewer_title[0] = 'V'; viewer_title[1] = 'i'; viewer_title[2] = 'e';
    viewer_title[3] = 'w'; viewer_title[4] = 'e'; viewer_title[5] = 'r';
    viewer_title[6] = ' '; viewer_title[7] = '-'; viewer_title[8] = ' ';
    int ti = 9;
    for (int i = 0; fname[i] && ti < 60; i++) {
        viewer_title[ti++] = fname[i];
    }
    viewer_title[ti] = 0;

    win_w = fit_w + 16;
    win_h = fit_h + 34;
    
    // Cap initial window size to 1024x768 (or image size if smaller)
    if (win_w > 1024) win_w = 1024;
    if (win_h > 768) win_h = 768;
    
    if (win_w < 200) win_w = 200;
    if (win_h < 100) win_h = 100;
}

int main(int argc, char **argv) {
    if (argc > 1) {
        viewer_open_file(argv[1]);
    }

    ui_window_t win = ui_window_create(viewer_title, 100, 50, win_w, win_h);
    if (!win) return 1;

    ui_window_set_resizable(win, true);

    gui_event_t ev;
    while (1) {
        if (ui_get_event(win, &ev)) {
            if (ev.type == GUI_EVENT_PAINT) {
                viewer_paint(win);
                ui_mark_dirty(win, 0, 0, win_w, win_h - 20);
            } else if (ev.type == GUI_EVENT_RESIZE) {
                win_w = ev.arg1;
                win_h = ev.arg2;
                resize_pending = true;
                last_resize_tick = sys_system(SYSTEM_CMD_GET_TICKS, 0, 0, 0, 0);
                // Fast background clear during active resize
                ui_draw_rect(win, 0, 0, win_w, win_h, 0xFF000000);
                ui_mark_dirty(win, 0, 0, win_w, win_h - 20);
            } else if (ev.type == GUI_EVENT_CLICK) {
                // No actions currently
            } else if (ev.type == GUI_EVENT_CLOSE) {
                sys_exit(0);
            }
        } else {
            if (resize_pending) {
                uint64_t now = sys_system(SYSTEM_CMD_GET_TICKS, 0, 0, 0, 0);
                if (now > last_resize_tick + 10) {
                    viewer_paint(win);
                    ui_mark_dirty(win, 0, 0, win_w, win_h - 20);
                    resize_pending = false;
                }
            }

            if (viewer_has_image && viewer_frame_count > 1) {
                uint64_t now = sys_system(SYSTEM_CMD_GET_TICKS, 0, 0, 0, 0);
                if (now >= viewer_next_frame_tick) {
                    viewer_current_frame = (viewer_current_frame + 1) % viewer_frame_count;
                    viewer_next_frame_tick = now + (viewer_delays[viewer_current_frame] * 60 / 1000);
                    if (viewer_next_frame_tick <= now) viewer_next_frame_tick = now + 1;
                    viewer_paint(win);
                    ui_mark_dirty(win, 0, 0, win_w, win_h - 20);
                }
            }
            // Small sleep to avoid eating 100% CPU
            sleep(10);
        }
    }
    return 0;
}
