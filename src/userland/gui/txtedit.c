// Copyright (c) 2023-2026 Chris (boreddevnl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
#include "libc/syscall.h"
#include "libc/libui.h"
#include "libc/stdlib.h"
#include "libc/input.h"
#include "utf-8.h"
#include <stddef.h>

#define COLOR_DARK_PANEL  0xFF202020
#define COLOR_DARK_TEXT   0xFFE0E0E0
#define COLOR_DARK_BORDER 0xFF404040
#define COLOR_RED         0xFFFF4444
#define COLOR_DARK_BG     0xFF121212
#define COLOR_DKGRAY      0xFF808080
#define COLOR_WHITE       0xFFFFFFFF

#define EDITOR_MAX_LINE_LEN 512
static int editor_line_height = 16;

typedef struct {
    char content[EDITOR_MAX_LINE_LEN];
    int length;
} EditorLine;

static EditorLine **lines = NULL;
static int line_count = 0;
static int line_capacity = 0;
static int cursor_line = 0;
static int cursor_col = 0;
static int scroll_top = 0;
static char open_filename[256] = "";
static _Bool file_modified = 0;

static int win_w = 700;
static int win_h = 450;

static void editor_strcpy(char *dest, const char *src) {
    while (*src) *dest++ = *src++;
    *dest = 0;
}

static int editor_strlen(const char *s) {
    int len = 0;
    while (s && s[len]) len++;
    return len;
}

static void editor_strncat(char *dst, const char *src, int max_len) {
    if (!dst || !src || max_len <= 0) return;
    int dlen = editor_strlen(dst);
    int i = 0;
    while (dlen + i < max_len - 1 && src[i]) {
        dst[dlen + i] = src[i];
        i++;
    }
    dst[dlen + i] = 0;
}

static void editor_resolve_path(const char *input, char *out, int max_len) {
    if (!out || max_len <= 0) return;
    if (!input || input[0] == 0) {
        out[0] = 0;
        return;
    }
    if (input[0] == '/') {
        editor_strcpy(out, input);
        return;
    }

    char cwd[256];
    if (sys_getcwd(cwd, sizeof(cwd)) < 0) {
        editor_strcpy(out, input);
        return;
    }

    editor_strcpy(out, cwd);
    int len = editor_strlen(out);
    if (len > 0 && out[len - 1] != '/') editor_strncat(out, "/", max_len);
    editor_strncat(out, input, max_len);
}

static void add_line(int at) {
    if (line_count >= line_capacity) {
        int new_capacity = (line_capacity == 0) ? 128 : line_capacity * 2;
        EditorLine **new_lines = (EditorLine**)realloc(lines, new_capacity * sizeof(EditorLine*));
        if (!new_lines) return;
        lines = new_lines;
        line_capacity = new_capacity;
    }
    for (int i = line_count; i > at; i--) lines[i] = lines[i - 1];
    lines[at] = (EditorLine*)malloc(sizeof(EditorLine));
    lines[at]->length = 0;
    lines[at]->content[0] = 0;
    line_count++;
}

static void remove_line(int at) {
    if (at < 0 || at >= line_count) return;
    free(lines[at]);
    for (int i = at; i < line_count - 1; i++) lines[i] = lines[i + 1];
    line_count--;
}

static void editor_clear_all(void) {
    if (lines) {
        for (int i = 0; i < line_count; i++) {
            if (lines[i]) free(lines[i]);
        }
        free(lines);
    }
    lines = NULL;
    line_count = 0;
    line_capacity = 0;
    add_line(0);
    cursor_line = 0;
    cursor_col = 0;
    scroll_top = 0;
    open_filename[0] = 0;
    file_modified = 0;
}

static int count_display_lines(int physical_line, int available_width) {
    if (physical_line < 0 || physical_line >= line_count) return 0;
    const char *text = lines[physical_line]->content;
    int text_len = lines[physical_line]->length;
    if (text_len == 0) return 1;
    
    int count = 0;
    int char_idx = 0;
    while (char_idx < text_len) {
        char segment[256];
        int segment_len = 0;
        int segment_start = char_idx;
        while (char_idx < text_len && segment_len < 254) {
            segment[segment_len] = text[char_idx];
            segment[segment_len + 1] = 0;
            if (ui_get_string_width(segment) > available_width) break;
            segment_len++;
            char_idx++;
        }
        if (char_idx < text_len && segment_len > 0) {
            int last_space = -1;
            for (int i = segment_len - 1; i >= 0; i--) {
                if (segment[i] == ' ') { last_space = i; break; }
            }
            if (last_space > 0) char_idx = segment_start + last_space + 1;
        }
        count++;
    }
    return count;
}

static void editor_ensure_cursor_visible(void) {
    int header_h = 32;
    int footer_h = 24;
    int editor_h = win_h - header_h - footer_h;
    int padding = 4;
    
    char max_line_str[16];
    itoa(line_count, max_line_str);
    int line_num_w = ui_get_string_width(max_line_str) + 10;
    if (line_num_w < 30) line_num_w = 30;
    int text_start_x = padding + line_num_w + 5;
    int available_width = win_w - text_start_x - padding - 10;
    
    if (editor_line_height <= 0) editor_line_height = ui_get_font_height();
    if (editor_line_height < 10) editor_line_height = 16;
    int visible_display_lines = (editor_h - 10) / editor_line_height;

    int cursor_disp = 0;
    for (int i = 0; i < cursor_line; i++) {
        cursor_disp += count_display_lines(i, available_width);
    }
    
    const char *text = lines[cursor_line]->content;
    int text_len = lines[cursor_line]->length;
    int char_idx = 0;
    int segment_idx = 0;
    while (char_idx < cursor_col) {
        char segment[256];
        int segment_len = 0;
        int segment_start = char_idx;
        while (char_idx < text_len && segment_len < 254) {
            segment[segment_len] = text[char_idx];
            segment[segment_len + 1] = 0;
            if (ui_get_string_width(segment) > available_width) break;
            segment_len++;
            char_idx++;
        }
        if (char_idx < text_len && segment_len > 0) {
            int last_space = -1;
            for (int i = segment_len - 1; i >= 0; i--) {
                if (segment[i] == ' ') { last_space = i; break; }
            }
            if (last_space > 0) {
                int end_of_seg = segment_start + last_space + 1;
                if (cursor_col < end_of_seg) break; 
                char_idx = end_of_seg;
            }
        }
        if (char_idx <= cursor_col) segment_idx++;
        else break;
    }
    cursor_disp += segment_idx;

    int scroll_disp = 0;
    for (int i = 0; i < scroll_top; i++) {
        scroll_disp += count_display_lines(i, available_width);
    }

    if (cursor_disp < scroll_disp) {
        while (scroll_top > 0) {
            scroll_top--;
            int new_scroll_disp = 0;
            for (int i = 0; i < scroll_top; i++) new_scroll_disp += count_display_lines(i, available_width);
            if (cursor_disp >= new_scroll_disp) break;
        }
    } else if (cursor_disp >= scroll_disp + visible_display_lines) {
        // Move scroll_top down until cursor is visible
        while (scroll_top < cursor_line) {
            scroll_top++;
            int new_scroll_disp = 0;
            for (int i = 0; i < scroll_top; i++) new_scroll_disp += count_display_lines(i, available_width);
            if (cursor_disp < new_scroll_disp + visible_display_lines) break;
        }
    }
}

void editor_open_file(const char *filename) {
    editor_clear_all();
    char resolved[256];
    editor_resolve_path(filename, resolved, sizeof(resolved));
    editor_strcpy(open_filename, resolved);
    
    int fd = sys_open(resolved, "r");
    if (fd < 0) {
        file_modified = 0;
        return;
    }
    
    static char buffer[16384];
    int bytes_read = sys_read(fd, buffer, sizeof(buffer));
    sys_close(fd);
    
    if (bytes_read <= 0) {
        file_modified = 0;
        return;
    }
    
    int line = 0;
    int col = 0;
    
    for (int i = 0; i < bytes_read; i++) {
        char ch = buffer[i];
        if (ch == '\n') {
            lines[line]->content[col] = 0;
            lines[line]->length = col;
            add_line(++line);
            col = 0;
        } else if (ch != '\r') {
            if (col < EDITOR_MAX_LINE_LEN - 1) {
                lines[line]->content[col] = ch;
                col++;
            }
        }
    }
    
    if (col > 0) {
        lines[line]->content[col] = 0;
        lines[line]->length = col;
        line++;
    }
    
    line_count = (line > 0) ? line : 1;
    file_modified = 0;
}

static void editor_save_file(void) {
    if (!open_filename[0]) return;
    
    int fd = sys_open(open_filename, "w");
    if (fd < 0) return;
    
    for (int i = 0; i < line_count; i++) {
        sys_write_fs(fd, lines[i]->content, lines[i]->length);
        sys_write_fs(fd, "\n", 1);
    }
    sys_close(fd);
    file_modified = 0;
}

static void editor_insert_char(int legacy, uint32_t codepoint) {
    EditorLine *line = lines[cursor_line];
    
    if (legacy == '\n') {
        add_line(cursor_line + 1);
        
        int current_len = lines[cursor_line]->length;
        int new_len = current_len - cursor_col;
        
        for (int i = 0; i < new_len; i++) {
            lines[cursor_line + 1]->content[i] = lines[cursor_line]->content[cursor_col + i];
        }
        lines[cursor_line + 1]->content[new_len] = 0;
        lines[cursor_line + 1]->length = new_len;
        
        lines[cursor_line]->content[cursor_col] = 0;
        lines[cursor_line]->length = cursor_col;
        
        cursor_line++;
        cursor_col = 0;
    } else if (legacy == KEY_TAB) {
        for (int i = 0; i < 4; i++) {
            if (line->length < EDITOR_MAX_LINE_LEN - 1) {
                for (int j = line->length; j > cursor_col; j--) {
                    line->content[j] = line->content[j - 1];
                }
                line->content[cursor_col] = ' ';
                line->length++;
                cursor_col++;
            }
        }
        line->content[line->length] = 0;
    } else if (legacy == KEY_BACKSPACE) {
        if (cursor_col > 0) {
            const char *prev = text_prev_utf8(line->content, line->content + cursor_col);
            int char_len = (int)((line->content + cursor_col) - prev);
            
            for (int i = cursor_col - char_len; i < line->length - char_len; i++) {
                line->content[i] = line->content[i + char_len];
            }
            line->length -= char_len;
            cursor_col -= char_len;
            line->content[line->length] = 0;
        } else if (cursor_line > 0) {
            int prev_idx = cursor_line - 1;
            int merge_point = lines[prev_idx]->length;
            
            int i = 0;
            while (i < line->length && (merge_point + i) < EDITOR_MAX_LINE_LEN - 1) {
                lines[prev_idx]->content[merge_point + i] = line->content[i];
                i++;
            }
            lines[prev_idx]->content[merge_point + i] = 0;
            lines[prev_idx]->length = merge_point + i;
            
            int old_line = cursor_line;
            cursor_line--;
            cursor_col = merge_point;
            remove_line(old_line);
        }
    } else if (codepoint >= 32 && codepoint != 127) {
        char utf8[4];
        int len = text_encode_utf8(codepoint, utf8);
        if (len > 0 && line->length + len < EDITOR_MAX_LINE_LEN) {
            for (int i = line->length; i > cursor_col; i--) {
                line->content[i + len - 1] = line->content[i - 1];
            }
            for (int i = 0; i < len; i++) {
                line->content[cursor_col + i] = utf8[i];
            }
            line->length += len;
            cursor_col += len;
            line->content[line->length] = 0;
        }
    }
    file_modified = 1;
    editor_ensure_cursor_visible();
}

static void editor_paint(ui_window_t win) {
    int header_h = 32;
    int footer_h = 24;
    int padding = 4;
    
    int content_width = win_w - (padding * 2);
    int editor_y = header_h;
    int editor_h = win_h - header_h - footer_h;
    
    // Header bar
    ui_draw_rounded_rect_filled(win, padding, 2, content_width, header_h - 4, 6, COLOR_DARK_PANEL);
    ui_draw_string(win, padding + 10, 8, "File", COLOR_DARK_TEXT);
    ui_draw_string(win, padding + 60, 8, open_filename, COLOR_DARK_TEXT);
    
    // Save button
    int save_btn_w = 70;
    int save_btn_h = 22;
    int save_btn_x = padding + content_width - save_btn_w - 5;
    int save_btn_y = 3;
    ui_draw_rounded_rect_filled(win, save_btn_x, save_btn_y, save_btn_w, save_btn_h, 6, COLOR_DARK_BORDER);
    ui_draw_string(win, save_btn_x + 18, save_btn_y + 4, "Save", COLOR_DARK_TEXT);
    
    ui_draw_rect(win, 0, editor_y, win_w, editor_h, COLOR_DARK_BG);
    
    editor_line_height = ui_get_font_height();
    if (editor_line_height < 10) editor_line_height = 16;
    
    char max_line_str[16];
    itoa(line_count, max_line_str);
    int line_num_w = ui_get_string_width(max_line_str) + 10;
    if (line_num_w < 30) line_num_w = 30;

    int text_start_x = padding + line_num_w + 5;
    int available_width = win_w - text_start_x - padding - 10;
    
    int visible_lines = (editor_h - 10) / editor_line_height;
    int max_display_lines = visible_lines;
    
    int display_line = 0;
    int line_idx = scroll_top;
    while (line_idx < line_count && display_line < max_display_lines) {
        int display_y = editor_y + 5 + display_line * editor_line_height;
        
        // Line number
        char line_num_str[16];
        int temp = line_idx + 1;
        int str_len = 0;
        if (temp == 0) {
            line_num_str[0] = '0';
            str_len = 1;
        } else {
            while (temp > 0) {
                line_num_str[str_len++] = (temp % 10) + '0';
                temp /= 10;
            }
            for (int j = 0; j < str_len / 2; j++) {
                char t = line_num_str[j];
                line_num_str[j] = line_num_str[str_len - 1 - j];
                line_num_str[str_len - 1 - j] = t;
            }
        }
        line_num_str[str_len] = 0;
        ui_draw_string(win, padding + 4, display_y, line_num_str, COLOR_DKGRAY);
        
        const char *text = lines[line_idx]->content;
        int text_len = lines[line_idx]->length;
        int char_idx = 0;
        _Bool first_pass = 1;
        
        while ((char_idx < text_len || (text_len == 0 && first_pass)) && display_line < max_display_lines) {
            first_pass = 0;
            int current_display_y = editor_y + 5 + display_line * editor_line_height;
            
            char segment[EDITOR_MAX_LINE_LEN];
            int segment_len = 0;
            int segment_start = char_idx;
            
            while (char_idx < text_len && segment_len < EDITOR_MAX_LINE_LEN - 2) {
                segment[segment_len] = text[char_idx];
                segment[segment_len + 1] = 0;
                if (ui_get_string_width(segment) > available_width) {
                    break;
                }
                segment_len++;
                char_idx++;
            }
            segment[segment_len] = 0;
            
            // Basic word wrap
            if (char_idx < text_len && segment_len > 0) {
                int last_space = -1;
                for (int i = segment_len - 1; i >= 0; i--) {
                    if (segment[i] == ' ') {
                        last_space = i;
                        break;
                    }
                }
                if (last_space > 0) {
                    segment_len = last_space;
                    segment[segment_len] = 0;
                    char_idx = segment_start + last_space + 1;
                }
            }
            
            if (segment_len > 0) {
                ui_draw_string(win, text_start_x, current_display_y, segment, COLOR_DARK_TEXT);
            }
            
            if (line_idx == cursor_line) {
                int segment_end = segment_start + segment_len;
                _Bool draw_cursor = 0;
                if (cursor_col >= segment_start && cursor_col < segment_end) {
                    draw_cursor = 1;
                } else if (cursor_col == text_len && segment_end == text_len) {
                    draw_cursor = 1;
                }
                
                if (draw_cursor) {
                    char before_cursor[256];
                    int len_before = cursor_col - segment_start;
                    for(int i=0; i<len_before; i++) before_cursor[i] = segment[i];
                    before_cursor[len_before] = 0;
                    int cursor_x = text_start_x + ui_get_string_width(before_cursor);
                    ui_draw_rect(win, cursor_x, current_display_y, 2, editor_line_height - 4, COLOR_WHITE);
                }
            }
            
            display_line++;
            if (char_idx >= text_len) break;
        }
        line_idx++;
    }
    
    // Status bar
    int status_y = win_h - footer_h;
    ui_draw_rounded_rect_filled(win, padding, status_y + 2, content_width, footer_h - 4, 6, COLOR_DARK_PANEL);

    ui_draw_string(win, padding + 15, status_y + 5, "Line:", COLOR_DKGRAY);
    
    char line_str[32];
    int temp = cursor_line + 1;
    int idx = 0;
    while (temp > 0) { line_str[idx++] = (temp % 10) + '0'; temp /= 10; }
    if (idx == 0) line_str[idx++] = '0';
    for (int j = 0; j < idx / 2; j++) { char t = line_str[j]; line_str[j] = line_str[idx - 1 - j]; line_str[idx - 1 - j] = t; }
    line_str[idx] = 0;
    ui_draw_string(win, padding + 65, status_y + 5, line_str, COLOR_DARK_TEXT);
    
    ui_draw_string(win, padding + 120, status_y + 5, "Col:", COLOR_DKGRAY);
    char col_str[32];
    temp = cursor_col + 1;
    idx = 0;
    while (temp > 0) { col_str[idx++] = (temp % 10) + '0'; temp /= 10; }
    if (idx == 0) col_str[idx++] = '0';
    for (int j = 0; j < idx / 2; j++) { char t = col_str[j]; col_str[j] = col_str[idx - 1 - j]; col_str[idx - 1 - j] = t; }
    col_str[idx] = 0;
    ui_draw_string(win, padding + 160, status_y + 5, col_str, COLOR_DARK_TEXT);
}

static void editor_handle_key(int legacy, uint32_t codepoint, bool pressed) {
    if (!pressed) return;
    if (legacy == KEY_UP) { // UP
        if (cursor_line > 0) {
            cursor_line--;
            if (cursor_col > lines[cursor_line]->length) cursor_col = lines[cursor_line]->length;
            if (cursor_line < scroll_top) scroll_top = cursor_line;
        }
    } else if (legacy == KEY_DOWN) { // DOWN
        if (cursor_line < line_count - 1) {
            cursor_line++;
            if (cursor_col > lines[cursor_line]->length) cursor_col = lines[cursor_line]->length;
            editor_ensure_cursor_visible();
        }
    } else if (legacy == KEY_LEFT) { // LEFT
        if (cursor_col > 0) {
            const char *prev = text_prev_utf8(lines[cursor_line]->content, lines[cursor_line]->content + cursor_col);
            cursor_col = (int)(prev - lines[cursor_line]->content);
        } else if (cursor_line > 0) {
            cursor_line--;
            cursor_col = lines[cursor_line]->length;
        }
    } else if (legacy == KEY_RIGHT) { // RIGHT
        if (cursor_col < lines[cursor_line]->length) {
            const char *next = text_next_utf8(lines[cursor_line]->content + cursor_col);
            cursor_col = (int)(next - lines[cursor_line]->content);
        } else if (cursor_line < line_count - 1) {
            cursor_line++;
            cursor_col = 0;
        }
    } else {
        editor_insert_char(legacy, codepoint);
    }
}

static void editor_handle_click(int x, int y) {
    int padding = 4;
    int content_width = win_w - (padding * 2);
    int save_btn_w = 70;
    int save_btn_x = padding + content_width - save_btn_w - 5;
    int save_btn_y = 3;
    int save_btn_h = 22;
    
    if (x >= save_btn_x && x < save_btn_x + save_btn_w && y >= save_btn_y && y < save_btn_y + save_btn_h) {
        editor_save_file();
    }
}

int main(int argc, char **argv) {
    ui_window_t win = ui_window_create("Text Editor", 100, 150, win_w, win_h);
    if (!win) return 1;
    ui_window_set_resizable(win, 1);

    editor_clear_all();
    if (argc > 1) {
        editor_open_file(argv[1]);
    } else {
        editor_strcpy(open_filename, "untitled.txt");
    }

    gui_event_t ev;
    while (1) {
        if (ui_get_event(win, &ev)) {
            if (ev.type == GUI_EVENT_PAINT) {
                editor_paint(win);
                ui_mark_dirty(win, 0, 0, win_w, win_h);
            } else if (ev.type == GUI_EVENT_CLICK) {
                editor_handle_click(ev.arg1, ev.arg2);
                editor_paint(win);
                ui_mark_dirty(win, 0, 0, win_w, win_h);
            } else if (ev.type == GUI_EVENT_KEY) {
                editor_handle_key(ev.arg1, (uint32_t)ev.arg4, true);
                editor_paint(win);
                ui_mark_dirty(win, 0, 32, win_w, win_h - 32);
            } else if (ev.type == GUI_EVENT_RESIZE) {
                win_w = ev.arg1;
                win_h = ev.arg2;
                editor_ensure_cursor_visible();
                editor_paint(win);
                ui_mark_dirty(win, 0, 0, win_w, win_h);
            } else if (ev.type == GUI_EVENT_KEYUP) {
                editor_handle_key(ev.arg1, (uint32_t)ev.arg4, false);
            } else if (ev.type == GUI_EVENT_CLOSE) {
                sys_exit(0);
            }
        } else {
            sleep(10);
        }
    }
    return 0;
}
