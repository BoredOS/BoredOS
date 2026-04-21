// Copyright (c) 2023-2026 Chris (boreddevnl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
// BOREDOS_APP_DESC: Simple drawing and paint app.
// BOREDOS_APP_ICONS: /Library/images/icons/colloid/gnome-paint.png
#include "libc/syscall.h"
#include "libc/libui.h"
#include "libc/stdlib.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CANVAS_W 300
#define CANVAS_H 200
#define PAINT_MAGIC 0x544E5042 // 'BPNT'

#define COLOR_BLACK         0xFF000000
#define COLOR_WHITE         0xFFFFFFFF
#define COLOR_RED           0xFFFF0000
#define COLOR_GREEN   0xFF4CD964
#define COLOR_BLUE    0xFF007AFF
#define COLOR_YELLOW  0xFFFFCC00

#define COLOR_DARK_BG       0xFF121212
#define COLOR_DARK_PANEL    0xFF202020
#define COLOR_DARK_BORDER   0xFF404040
#define COLOR_DARK_TEXT     0xFFE0E0E0

static uint32_t *canvas_buffer = NULL;
static uint32_t current_color = COLOR_BLACK;
static int last_mx = -1;
static int last_my = -1;
static char current_file_path[256] = "/root/Desktop/drawing.pnt";

static void paint_strcpy(char *dest, const char *src) {
    while (*src) *dest++ = *src++;
    *dest = 0;
}

static void debug_print(const char *msg) {
    sys_write(1, msg, 0);
    int i = 0;
    while (msg[i]) i++;
    sys_write(1, msg, i);
    sys_write(1, "\n", 1);
}

static void paint_reset(void) {
    if (canvas_buffer) {
        for (int i = 0; i < CANVAS_W * CANVAS_H; i++) {
            canvas_buffer[i] = COLOR_WHITE;
        }
    }
}

static void paint_paint(ui_window_t win) {
    int canvas_x = 60;
    int canvas_y = 0;
    
    ui_draw_rounded_rect_filled(win, canvas_x - 2, canvas_y - 2, CANVAS_W + 4, CANVAS_H + 4, 4, COLOR_DARK_BG);
    ui_draw_rounded_rect_filled(win, 10, 0, 40, 230, 6, COLOR_DARK_PANEL);
    
    uint32_t colors[] = {COLOR_BLACK, COLOR_RED, COLOR_GREEN, COLOR_BLUE, COLOR_YELLOW, COLOR_WHITE};
    for (int i = 0; i < 6; i++) {
        int cy = 10 + (i * 25);
        ui_draw_rounded_rect_filled(win, 15, cy, 30, 20, 3, colors[i]);
        
        if (current_color == colors[i]) {
            ui_draw_rect(win, 13, cy - 2, 34, 1, COLOR_DARK_TEXT);
            ui_draw_rect(win, 13, cy - 2 + 24, 34, 1, COLOR_DARK_TEXT);
            ui_draw_rect(win, 13, cy - 2, 1, 24, COLOR_DARK_TEXT);
            ui_draw_rect(win, 13 + 34, cy - 2, 1, 24, COLOR_DARK_TEXT);
        }
    }

    ui_draw_rounded_rect_filled(win, 12, 230 - 65, 36, 20, 4, COLOR_DARK_BORDER);
    ui_draw_string(win, 18, 230 - 58, "CLR", COLOR_DARK_TEXT);
    
    ui_draw_rounded_rect_filled(win, 12, 230 - 40, 36, 20, 4, COLOR_DARK_BORDER);
    ui_draw_string(win, 18, 230 - 33, "SAV", COLOR_DARK_TEXT);

    // Draw canvas content
    if (canvas_buffer) {
        ui_draw_image(win, canvas_x, canvas_y, CANVAS_W, CANVAS_H, canvas_buffer);
    }
}

static void paint_put_brush(ui_window_t win, int cx, int cy, int *min_x, int *min_y, int *max_x, int *max_y) {
    if (!canvas_buffer) return;
    for (int dy = 0; dy < 2; dy++) {
        for (int dx = 0; dx < 2; dx++) {
            int px = cx + dx;
            int py = cy + dy;
            if (px >= 0 && px < CANVAS_W && py >= 0 && py < CANVAS_H) {
                canvas_buffer[py * CANVAS_W + px] = current_color;
                
                if (px < *min_x) *min_x = px;
                if (py < *min_y) *min_y = py;
                if (px > *max_x) *max_x = px;
                if (py > *max_y) *max_y = py;
            }
        }
    }
}

void paint_handle_mouse(ui_window_t win, int x, int y) {
    int cx = x - 60;
    int cy = y;

    if (cx < 0 || cx >= CANVAS_W || cy < 0 || cy >= CANVAS_H) {
        last_mx = -1;
        return;
    }

    int min_x = CANVAS_W, min_y = CANVAS_H;
    int max_x = -1, max_y = -1;

    if (last_mx == -1) {
        paint_put_brush(win, cx, cy, &min_x, &min_y, &max_x, &max_y);
    } else {
        int x0 = last_mx, y0 = last_my;
        int x1 = cx, y1 = cy;
        int dx = (x1 - x0 > 0) ? (x1 - x0) : (x0 - x1);
        int dy = (y1 - y0 > 0) ? (y1 - y0) : (y0 - y1);
        int sx = x0 < x1 ? 1 : -1;
        int sy = y0 < y1 ? 1 : -1;
        int err = dx - dy;

        while (1) {
            paint_put_brush(win, x0, y0, &min_x, &min_y, &max_x, &max_y);
            if (x0 == x1 && y0 == y1) break;
            int e2 = 2 * err;
            if (e2 > -dy) { err -= dy; x0 += sx; }
            if (e2 < dx) { err += dx; y0 += sy; }
        }
    }
    
    if (min_x <= max_x && min_y <= max_y) {
        ui_draw_image(win, 60, 0, CANVAS_W, CANVAS_H, canvas_buffer);
        ui_mark_dirty(win, 60 + min_x, 0 + min_y, (max_x - min_x) + 1, (max_y - min_y) + 1);
    }

    last_mx = cx;
    last_my = cy;
}

void paint_reset_last_pos(void) {
    last_mx = -1;
    last_my = -1;
}


static void wm_show_message(const char *title, const char *msg) {

}

static void paint_save(const char *path) {
    int fd = sys_open(path, "w");
    if (fd >= 0) {
        uint32_t header[3] = {PAINT_MAGIC, CANVAS_W, CANVAS_H};
        sys_write_fs(fd, (char*)header, sizeof(header));
        sys_write_fs(fd, (char*)canvas_buffer, CANVAS_W * CANVAS_H * sizeof(uint32_t));
        sys_close(fd);
        wm_show_message("Paint", "Image saved.");
    }
}

void paint_load(const char *path) {
    paint_strcpy(current_file_path, path);
    int fd = sys_open(path, "r");
    if (fd >= 0) {
        uint32_t header[3];
        if (sys_read(fd, (char*)header, sizeof(header)) == sizeof(header)) {
            if (header[0] == PAINT_MAGIC) {
                sys_read(fd, (char*)canvas_buffer, CANVAS_W * CANVAS_H * sizeof(uint32_t));
            }
        }
        sys_close(fd);
    }
}

static void paint_click(ui_window_t win, int x, int y) {
    // Check Buttons
    if (x >= 12 && x < 48) {
        if (y >= 230 - 65 && y < 230 - 45) {
            paint_reset();
            paint_paint(win);
            ui_mark_dirty(win, 0, 0, 380, 230);
            return;
        }
        if (y >= 230 - 40 && y < 230 - 20) {
            paint_save(current_file_path);
            return;
        }
    }

    // Check Palette
    if (x >= 15 && x < 45) {
        for (int i = 0; i < 6; i++) {
            int cy = 10 + (i * 25);
            if (y >= cy && y < cy + 20) {
                uint32_t colors[] = {COLOR_BLACK, COLOR_RED, COLOR_GREEN, COLOR_BLUE, COLOR_YELLOW, COLOR_WHITE};
                current_color = colors[i];
                paint_paint(win);
                ui_mark_dirty(win, 0, 0, 380, 230);
                return;
            }
        }
    }
    paint_handle_mouse(win, x, y);
}

int main(int argc, char **argv) {
    ui_window_t win = ui_window_create("Paint", 150, 100, 380, 260);
    if (!win) return 1;

    canvas_buffer = malloc(CANVAS_W * CANVAS_H * sizeof(uint32_t));
    if (!canvas_buffer) return 1;

    paint_reset();

    if (argc > 1) {
        paint_load(argv[1]);
    }

    gui_event_t ev;
    while (1) {
        if (ui_get_event(win, &ev)) {
            if (ev.type == GUI_EVENT_PAINT) {
                paint_paint(win);
                ui_mark_dirty(win, 0, 0, 380, 240);
            } else if (ev.type == GUI_EVENT_CLICK) {
                paint_click(win, ev.arg1, ev.arg2);
            } else if (ev.type == GUI_EVENT_MOUSE_DOWN) {
                paint_handle_mouse(win, ev.arg1, ev.arg2);
            } else if (ev.type == GUI_EVENT_MOUSE_UP) {
                paint_reset_last_pos();
            } else if (ev.type == GUI_EVENT_MOUSE_MOVE) {
                if (ev.arg3 & 0x01) { // Left button down
                    paint_handle_mouse(win, ev.arg1, ev.arg2);
                } else {
                    paint_reset_last_pos();
                }
            } else if (ev.type == GUI_EVENT_CLOSE) {
                sys_exit(0);
            }
        } else {
            sleep(10);
        }
    }
    return 0;
}
