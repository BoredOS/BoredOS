// Copyright (c) 2023-2026 Chris (boreddevnl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
// BOREDOS_APP_DESC: Jotting down notes and thoughts.
// BOREDOS_APP_ICONS: /Library/images/icons/colloid/text-editor.png
#include "libc/syscall.h"
#include "libc/libui.h"
#include "libc/stdlib.h"
#include "libc/syscall_user.h"
#include "libc/utf-8.h"
#include "libc/input.h"
#include <stddef.h>

#define COLOR_NOTEPAD_BG 0xFFFFFFFF
#define COLOR_BLACK     0xFF000000

#define NOTEPAD_BUF_SIZE (64 * 1024)
static char buffer[NOTEPAD_BUF_SIZE];
static int buf_len = 0;
static int cursor_pos = 0;
static int notepad_scroll_line = 0;

static void notepad_ensure_cursor_visible(int h) {
    int fh = (int)ui_get_font_height();
    if (fh < 8) fh = 8;
    int visible_lines = (h - 10) / fh;
    if (visible_lines < 1) visible_lines = 1;

    int cursor_line = 0;
    for (int i = 0; i < cursor_pos && i < buf_len; i++) {
        if (buffer[i] == '\n') cursor_line++;
    }

    if (cursor_line < notepad_scroll_line) {
        notepad_scroll_line = cursor_line;
    }

    if (cursor_line >= notepad_scroll_line + visible_lines) {
        notepad_scroll_line = cursor_line - visible_lines + 1;
    }
}

static void notepad_load_state() {
    int fd = sys_open("/tmp/notepad_state.txt", "r");
    if (fd >= 0) {
        sys_serial_write("Notepad: Loading state...\n");
        buf_len = sys_read(fd, buffer, NOTEPAD_BUF_SIZE - 1);
        if (buf_len < 0) buf_len = 0;
        buffer[buf_len] = 0;
        sys_close(fd);
    }
    cursor_pos = buf_len;
}

static void notepad_save_state() {
    // Ensure dir exist
    sys_mkdir("/tmp");
    int fd = sys_open("/tmp/notepad_state.txt", "w");
    if (fd >= 0) {
        sys_write_fs(fd, buffer, buf_len);
        sys_close(fd);
    }
}

static void notepad_paint(ui_window_t win, int w, int h) {
    ui_draw_rect(win, 0, 0, w, h, COLOR_NOTEPAD_BG);

    int fh = (int)ui_get_font_height();
    if (fh < 8) fh = 8;

    int visual_line = 0;
    int current_x = 4;
    int current_y = 4;
    int window_right = w - 8;

    for (int i = 0; i < buf_len; ) {
        int adv;
        uint32_t cp = text_decode_utf8(&buffer[i], &adv);

        if (visual_line < notepad_scroll_line) {
            if (cp == '\n') {
                visual_line++;
                current_x = 4;
                current_y = 4;
            } else {
                char out[5];
                int len = text_encode_utf8(cp, out);
                out[len] = 0;

                int cw = (int)ui_get_string_width(out);
                if (cw < 1) cw = 8;

                if (current_x + cw >= window_right) {
                    visual_line++;
                    current_x = 4;
                    current_y += fh;
                }
                current_x += cw;
            }

            i += adv;
            continue;
        }

        if (visual_line >= notepad_scroll_line + (h - 8) / fh) break;

        if (cp == '\n') {
            current_x = 4;
            current_y += fh;
            visual_line++;
        } else {
            char out[5];
            int len = text_encode_utf8(cp, out);
            out[len] = 0;

            int cw = (int)ui_get_string_width(out);
            if (cw < 1) cw = 8;

            if (current_x + cw >= window_right) {
                current_x = 4;
                current_y += fh;
                visual_line++;

                if (visual_line >= notepad_scroll_line + (h - 8) / fh) break;
            }

            ui_draw_string(win, current_x, current_y, out, COLOR_BLACK);
            current_x += cw;
        }

        i += adv;
    }

    // --- CURSOR ---
    int cx = 4;
    int cy = 4;
    int c_visual_line = 0;

    for (int i = 0; i < cursor_pos; ) {
        int adv;
        uint32_t cp = text_decode_utf8(&buffer[i], &adv);

        if (cp == '\n') {
            cx = 4;
            cy += fh;
            c_visual_line++;
        } else {
            char out[5];
            int len = text_encode_utf8(cp, out);
            out[len] = 0;

            int cw = (int)ui_get_string_width(out);
            if (cw < 1) cw = 8;

            if (cx + cw >= window_right) {
                cx = 4;
                cy += fh;
                c_visual_line++;
            }
            cx += cw;
        }

        i += adv;
    }

    if (c_visual_line >= notepad_scroll_line &&
        c_visual_line < notepad_scroll_line + (h - 8) / fh) {
        ui_draw_rect(win, cx, cy, 2, fh - 2, COLOR_BLACK);
    }

    ui_mark_dirty(win, 0, 0, w, h);
}

static void notepad_key(ui_window_t win, int h, int legacy, uint32_t codepoint) {
    (void)win;

    if (legacy == KEY_UP) { // UP
        if (cursor_pos > 0) cursor_pos--;
    } else if (legacy == KEY_DOWN) { // DOWN
        if (cursor_pos < buf_len) cursor_pos++;
    } else if (legacy == KEY_LEFT) { // LEFT
        if (cursor_pos > 0)
            cursor_pos = (int)(text_prev_utf8(buffer, &buffer[cursor_pos]) - buffer);
    } else if (legacy == KEY_RIGHT) { // RIGHT
        if (cursor_pos < buf_len)
            cursor_pos = (int)(text_next_utf8(&buffer[cursor_pos]) - buffer);
    } else if (legacy == '\b') {
        if (cursor_pos > 0) {
            int prev = (int)(text_prev_utf8(buffer, &buffer[cursor_pos]) - buffer);
            int len = cursor_pos - prev;

            for (int i = cursor_pos; i < buf_len; i++)
                buffer[i - len] = buffer[i];

            buf_len -= len;
            cursor_pos = prev;
            buffer[buf_len] = 0;
        }
    } else if (legacy == '\n') {
        if (buf_len < NOTEPAD_BUF_SIZE - 1) {
            for (int i = buf_len; i > cursor_pos; i--)
                buffer[i] = buffer[i - 1];

            buffer[cursor_pos++] = '\n';
            buf_len++;
            buffer[buf_len] = 0;
        }
    } 
    // Only insert printable characters (excluding DEL)
    else if (codepoint >= 32 && codepoint != 127) {
        char utf8[4];
        int len = text_encode_utf8(codepoint, utf8);

        if (len > 0 && buf_len + len < NOTEPAD_BUF_SIZE) {
            for (int i = buf_len - 1; i >= cursor_pos; i--)
                buffer[i + len] = buffer[i];

            for (int i = 0; i < len; i++)
                buffer[cursor_pos + i] = utf8[i];

            buf_len += len;
            cursor_pos += len;
            buffer[buf_len] = 0;
        }
    }

    notepad_ensure_cursor_visible(h);
}

int main(int argc, char **argv) {
    sys_serial_write("Notepad: Starting userspace main...\n");
    ui_window_t win = ui_window_create("Notepad", 100, 100, 400, 300);

    if (win == 0) {
        sys_serial_write("Notepad: Failed to create window!\n");
        return 1;
    }

    sys_serial_write("Notepad: Window created successfully.\n");

    notepad_load_state();

    gui_event_t ev;
    sys_serial_write("Notepad: Entering event loop...\n");

    while (1) {
        if (ui_get_event(win, &ev)) {
            if (ev.type == GUI_EVENT_PAINT) {
                notepad_paint(win, 400, 300);
            } else if (ev.type == GUI_EVENT_KEY) {
                notepad_key(win, 300, ev.arg1, (uint32_t)ev.arg4);
                notepad_paint(win, 400, 300);
            } else if (ev.type == GUI_EVENT_CLOSE) {
                sys_serial_write("Notepad: CLOSE\n");
                notepad_save_state();
                sys_exit(0);
            }
        } else {
            sleep(10);
        }
    }

    return 0;
}