// Copyright (c) 2023-2026 Chris (boreddevnl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
// BOREDOS_APP_DESC: Jotting down notes and thoughts.
// BOREDOS_APP_ICONS: /Library/images/icons/colloid/text-editor.png
#include "libc/syscall.h"
#include "libc/libui.h"
#include "libc/stdlib.h"
#include "libc/syscall_user.h"
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
    // Ensure dir exists
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
        const char *ptr = &buffer[i];
        uint32_t cp = 0;
        int advance = 1;

        // --- utf-8 decode ---
        if ((ptr[0] & 0x80) == 0) {
            cp = ptr[0];
            advance = 1;
        } else if ((ptr[0] & 0xE0) == 0xC0) {
            cp = ((ptr[0] & 0x1F) << 6) | (ptr[1] & 0x3F);
            advance = 2;
        } else if ((ptr[0] & 0xF0) == 0xE0) {
            cp = ((ptr[0] & 0x0F) << 12) |
                 ((ptr[1] & 0x3F) << 6) |
                 (ptr[2] & 0x3F);
            advance = 3;
        } else {
            cp = '?';
            advance = 1;
        }

        // --- scroll skip ---
        if (visual_line < notepad_scroll_line) {
            if (cp == '\n') {
                visual_line++;
                current_x = 4;
                current_y = 4;
            } else {
                char out[5] = {0};

                if (cp < 0x80) {
                    out[0] = cp;
                } else if (cp < 0x800) {
                    out[0] = 0xC0 | (cp >> 6);
                    out[1] = 0x80 | (cp & 0x3F);
                } else {
                    out[0] = 0xE0 | (cp >> 12);
                    out[1] = 0x80 | ((cp >> 6) & 0x3F);
                    out[2] = 0x80 | (cp & 0x3F);
                }

                int cw = (int)ui_get_string_width(out);
                if (cw < 1) cw = 8;

                if (current_x + cw >= window_right) {
                    visual_line++;
                    current_x = 4;
                    current_y += fh;
                }
                current_x += cw;
            }

            i += advance;
            continue;
        }

        if (visual_line >= notepad_scroll_line + (h - 8) / fh) break;

        // --- render ---
        if (cp == '\n') {
            current_x = 4;
            current_y += fh;
            visual_line++;
        } else {
            char out[5] = {0};

            if (cp < 0x80) {
                out[0] = cp;
            } else if (cp < 0x800) {
                out[0] = 0xC0 | (cp >> 6);
                out[1] = 0x80 | (cp & 0x3F);
            } else {
                out[0] = 0xE0 | (cp >> 12);
                out[1] = 0x80 | ((cp >> 6) & 0x3F);
                out[2] = 0x80 | (cp & 0x3F);
            }

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

        i += advance;
    }

    // --- CURSOR ---
    int cx = 4;
    int cy = 4;
    int c_visual_line = 0;

    for (int i = 0; i < cursor_pos; ) {
        const char *ptr = &buffer[i];
        uint32_t cp = 0;
        int advance = 1;

        if ((ptr[0] & 0x80) == 0) {
            cp = ptr[0];
            advance = 1;
        } else if ((ptr[0] & 0xE0) == 0xC0) {
            cp = ((ptr[0] & 0x1F) << 6) | (ptr[1] & 0x3F);
            advance = 2;
        } else if ((ptr[0] & 0xF0) == 0xE0) {
            cp = ((ptr[0] & 0x0F) << 12) |
                 ((ptr[1] & 0x3F) << 6) |
                 (ptr[2] & 0x3F);
            advance = 3;
        } else {
            cp = '?';
            advance = 1;
        }

        if (cp == '\n') {
            cx = 4;
            cy += fh;
            c_visual_line++;
        } else {
            char out[5] = {0};

            if (cp < 0x80) {
                out[0] = cp;
            } else if (cp < 0x800) {
                out[0] = 0xC0 | (cp >> 6);
                out[1] = 0x80 | (cp & 0x3F);
            } else {
                out[0] = 0xE0 | (cp >> 12);
                out[1] = 0x80 | ((cp >> 6) & 0x3F);
                out[2] = 0x80 | (cp & 0x3F);
            }

            int cw = (int)ui_get_string_width(out);
            if (cw < 1) cw = 8;

            if (cx + cw >= window_right) {
                cx = 4;
                cy += fh;
                c_visual_line++;
            }
            cx += cw;
        }

        i += advance;
    }

    if (c_visual_line >= notepad_scroll_line && 
        c_visual_line < notepad_scroll_line + (h - 8) / fh) {
        ui_draw_rect(win, cx, cy, 2, fh - 2, COLOR_BLACK);
    }

    ui_mark_dirty(win, 0, 0, w, h);
}

static int utf8_encode(uint32_t cp, char out[4]) {
    if (cp <= 0x7F) {
        out[0] = (char)cp;
        return 1;
    } else if (cp <= 0x7FF) {
        out[0] = (char)(0xC0 | ((cp >> 6) & 0x1F));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    } else if (cp <= 0xFFFF) {
        out[0] = (char)(0xE0 | ((cp >> 12) & 0x0F));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    } else if (cp <= 0x10FFFF) {
        out[0] = (char)(0xF0 | ((cp >> 18) & 0x07));
        out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[3] = (char)(0x80 | (cp & 0x3F));
        return 4;
    }
    return 0;
}

static int utf8_prev_char_start(const char *buf, int pos) {
    if (pos <= 0) return 0;
    pos--;
    while (pos > 0 && (((unsigned char)buf[pos] & 0xC0) == 0x80)) {
        pos--;
    }
    return pos;
}

static int utf8_next_char_start(const char *buf, int len, int pos) {
    if (pos >= len) return len;
    pos++;
    while (pos < len && (((unsigned char)buf[pos] & 0xC0) == 0x80)) {
        pos++;
    }
    return pos;
}

static void notepad_key(ui_window_t win, int h, int legacy, uint32_t codepoint) {
    (void)win;

    if (legacy == 17) { // UP
        if (cursor_pos > 0) {
            int curr = cursor_pos;
            int line_start = curr;
            while (line_start > 0 && buffer[line_start - 1] != '\n') {
                line_start--;
            }
            int col = curr - line_start;

            if (line_start > 0) {
                int prev_line_end = line_start - 1;
                int prev_line_start = prev_line_end;
                while (prev_line_start > 0 && buffer[prev_line_start - 1] != '\n') {
                    prev_line_start--;
                }
                int prev_line_len = prev_line_end - prev_line_start;
                if (col > prev_line_len) col = prev_line_len;
                cursor_pos = prev_line_start + col;
            }
        }
    } else if (legacy == 18) { // DOWN
        if (cursor_pos < buf_len) {
            int curr = cursor_pos;
            int line_start = curr;
            while (line_start > 0 && buffer[line_start - 1] != '\n') {
                line_start--;
            }
            int col = curr - line_start;

            int next_line_start = curr;
            while (next_line_start < buf_len && buffer[next_line_start] != '\n') {
                next_line_start++;
            }

            if (next_line_start < buf_len) {
                next_line_start++;
                int next_line_end = next_line_start;
                while (next_line_end < buf_len && buffer[next_line_end] != '\n') {
                    next_line_end++;
                }
                int next_line_len = next_line_end - next_line_start;
                if (col > next_line_len) col = next_line_len;
                cursor_pos = next_line_start + col;
            } else {
                cursor_pos = buf_len;
            }
        }
    } else if (legacy == 19) { // LEFT
        if (cursor_pos > 0) {
            cursor_pos = utf8_prev_char_start(buffer, cursor_pos);
        }
    } else if (legacy == 20) { // RIGHT
        if (cursor_pos < buf_len) {
            cursor_pos = utf8_next_char_start(buffer, buf_len, cursor_pos);
        }
    } else if (legacy == '\b') { // Backspace
        if (cursor_pos > 0) {
            int prev = utf8_prev_char_start(buffer, cursor_pos);
            int char_len = cursor_pos - prev;
            for (int i = cursor_pos; i < buf_len; i++) {
                buffer[i - char_len] = buffer[i];
            }
            buf_len -= char_len;
            cursor_pos = prev;
            buffer[buf_len] = 0;
        }
    } else if (legacy == '\n') { // Enter
        if (buf_len < NOTEPAD_BUF_SIZE - 1) {
            for (int i = buf_len; i > cursor_pos; i--) {
                buffer[i] = buffer[i - 1];
            }
            buffer[cursor_pos] = '\n';
            buf_len++;
            cursor_pos++;
            buffer[buf_len] = 0;
        }
    } else if (codepoint >= 32 && codepoint != 127) {
        char utf8[4];
        int utf8_len = utf8_encode(codepoint, utf8);

        if (utf8_len > 0 && buf_len + utf8_len < NOTEPAD_BUF_SIZE) {
            for (int i = buf_len - 1; i >= cursor_pos; i--) {
                buffer[i + utf8_len] = buffer[i];
            }
            for (int i = 0; i < utf8_len; i++) {
                buffer[cursor_pos + i] = utf8[i];
            }
            buf_len += utf8_len;
            cursor_pos += utf8_len;
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
