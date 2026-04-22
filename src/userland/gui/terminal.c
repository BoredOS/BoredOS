// Copyright (c) 2023-2026 Chris (boreddevnl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
// BOREDOS_APP_DESC: BoredOS Terminal shell.
// BOREDOS_APP_ICONS: /Library/images/icons/colloid/xterm.png;/Library/images/icons/colloid/utilities-terminal_su.png
#include <stdlib.h>
#include <syscall.h>
#include "libc/libui.h"
#include "libc/input.h"
#include <stdbool.h>
#include <stdint.h>

#define DEFAULT_COLS 116
#define DEFAULT_ROWS 41
#define SCROLLBACK_LINES 800
#define SCROLLBACK_COLS 256
static int g_char_w = 8;
static int g_line_h = 10;
#define TAB_BAR_H 20
#define CONTENT_PAD_BOTTOM 20
#define TAB_CLOSE_W 12
#define TAB_CLOSE_PAD 6
#define MAX_TABS 4
#define TTY_READ_CHUNK 512
#define LINE_MAX 256

typedef struct {
    char c;
    uint32_t color;
} CharCell;

typedef struct {
    int tty_id;
    int bsh_pid;
    CharCell *cells;
    CharCell *scrollback;
    int *scroll_cols;
    int scroll_head;
    int scroll_count;
    int scroll_offset;
    int cursor_row;
    int cursor_col;
    uint32_t fg_color;
    uint32_t bg_color;
    uint32_t input_color;

    int ansi_state;
    int ansi_params[8];
    int ansi_param_count;
    int saved_row;
    int saved_col;

    bool colors_enabled;

    // for color
    char current_input[LINE_MAX];
    int input_len;
} TerminalSession;

static ui_window_t g_win;
static TerminalSession g_tabs[MAX_TABS];
static int g_tab_count = 0;
static int g_active_tab = 0;
static int g_cols = DEFAULT_COLS;
static int g_rows = DEFAULT_ROWS;
static int g_win_w = DEFAULT_COLS * 8;
static int g_win_h = TAB_BAR_H + (DEFAULT_ROWS * 10);

static void update_font_metrics(void) {
    int h = (int)ui_get_font_height();
    if (h > 0) g_line_h = h;
    else g_line_h = 15;

    // For non-monospace fonts, we find the widest character to use as our grid cell width
    int max_w = 0;
    for (int i = 32; i < 127; i++) {
        char buf[2] = { (char)i, 0 };
        int w = (int)ui_get_string_width(buf);
        if (w > max_w) max_w = w;
    }
    
    if (max_w > 0) g_char_w = max_w;
    else g_char_w = 8;
}

static void str_copy(char *dst, const char *src, int max_len) {
    int i = 0;
    if (max_len <= 0) return;
    while (i < max_len - 1 && src && src[i]) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

static void str_append(char *dst, const char *src, int max_len) {
    if (!dst || !src || max_len <= 0) return;
    int dlen = (int)strlen(dst);
    int i = 0;
    while (dlen + i < max_len - 1 && src[i]) {
        dst[dlen + i] = src[i];
        i++;
    }
    dst[dlen + i] = 0;
}

static void trim_end(char *s) {
    if (!s) return;
    int len = (int)strlen(s);
    while (len > 0 && (s[len - 1] == '\n' || s[len - 1] == '\r' || s[len - 1] == ' ' || s[len - 1] == '\t')) {
        s[len - 1] = 0;
        len--;
    }
}

static void scrollback_init(TerminalSession *s) {
    s->scroll_head = 0;
    s->scroll_count = 0;
    s->scroll_offset = 0;
    if (s->scrollback) {
        for (int i = 0; i < SCROLLBACK_LINES * SCROLLBACK_COLS; i++) {
            s->scrollback[i].c = ' ';
            s->scrollback[i].color = s->fg_color;
        }
    }
    if (s->scroll_cols) {
        for (int i = 0; i < SCROLLBACK_LINES; i++) s->scroll_cols[i] = 0;
    }
}

static void scrollback_push_row(TerminalSession *s, const CharCell *row, int cols) {
    if (!s->scrollback || !s->scroll_cols) return;
    int idx = s->scroll_head;
    CharCell *dest = s->scrollback + (idx * SCROLLBACK_COLS);
    int copy_cols = cols;
    if (copy_cols > SCROLLBACK_COLS) copy_cols = SCROLLBACK_COLS;

    for (int i = 0; i < SCROLLBACK_COLS; i++) {
        dest[i].c = ' ';
        dest[i].color = s->fg_color;
    }
    for (int i = 0; i < copy_cols; i++) {
        dest[i] = row[i];
    }

    s->scroll_cols[idx] = copy_cols;
    s->scroll_head = (idx + 1) % SCROLLBACK_LINES;
    if (s->scroll_count < SCROLLBACK_LINES) {
        s->scroll_count++;
    } else if (s->scroll_offset > 0) {
        s->scroll_offset--;
    }

    if (s->scroll_offset > 0) {
        s->scroll_offset++;
    }
}

static int scrollback_max_offset(TerminalSession *s) {
    int total = s->scroll_count + g_rows;
    if (total <= g_rows) return 0;
    return total - g_rows;
}

static void session_adjust_scroll(TerminalSession *s, int delta) {
    if (!s) return;
    int max_offset = scrollback_max_offset(s);
    s->scroll_offset += delta;
    if (s->scroll_offset < 0) s->scroll_offset = 0;
    if (s->scroll_offset > max_offset) s->scroll_offset = max_offset;
}

static CharCell *scrollback_get_line(TerminalSession *s, int line_index, int *out_cols) {
    if (!s || line_index < 0 || line_index >= s->scroll_count) return NULL;
    int start = s->scroll_head - s->scroll_count;
    if (start < 0) start += SCROLLBACK_LINES;
    int idx = (start + line_index) % SCROLLBACK_LINES;
    if (out_cols) *out_cols = s->scroll_cols[idx];
    return s->scrollback + (idx * SCROLLBACK_COLS);
}

static uint32_t ansi_color_16(int idx) {
    static uint32_t base[16] = {
        0xFF000000, 0xFFAA0000, 0xFF00AA00, 0xFFAA5500,
        0xFF0000AA, 0xFFAA00AA, 0xFF00AAAA, 0xFFAAAAAA,
        0xFF555555, 0xFFFF5555, 0xFF55FF55, 0xFFFFFF55,
        0xFF5555FF, 0xFFFF55FF, 0xFF55FFFF, 0xFFFFFFFF
    };
    if (idx < 0) idx = 0;
    if (idx > 15) idx = 15;
    return base[idx];
}

static void session_reset_colors(TerminalSession *s) {
    s->fg_color = 0xFFFFFFFF;
    s->bg_color = 0xFF1E1E1E;
}

static void session_clear(TerminalSession *s) {
    for (int i = 0; i < g_cols * g_rows; i++) {
        s->cells[i].c = ' ';
        s->cells[i].color = s->fg_color;
    }
    s->cursor_row = 0;
    s->cursor_col = 0;
    s->scroll_offset = 0;
}

static void session_scroll(TerminalSession *s) {
    int row_bytes = g_cols * (int)sizeof(CharCell);
    if (g_rows > 0) {
        scrollback_push_row(s, s->cells, g_cols);
    }
    memmove(s->cells, s->cells + g_cols, row_bytes * (g_rows - 1));
    for (int i = 0; i < g_cols; i++) {
        int idx = (g_rows - 1) * g_cols + i;
        s->cells[idx].c = ' ';
        s->cells[idx].color = s->fg_color;
    }
    s->cursor_row = g_rows - 1;
    s->cursor_col = 0;
}

static void session_put_char(TerminalSession *s, char c) {
    if (c == '\n') {
        s->cursor_row++;
        s->cursor_col = 0;
        if (s->cursor_row >= g_rows) session_scroll(s);
        return;
    }
    if (c == '\r') {
        s->cursor_col = 0;
        return;
    }
    if (c == '\b') {
        if (s->cursor_col > 0) s->cursor_col--;
        int idx = s->cursor_row * g_cols + s->cursor_col;
        if (idx >= 0 && idx < g_cols * g_rows) {
            s->cells[idx].c = ' ';
            s->cells[idx].color = s->fg_color;
        }
        return;
    }
    if (c == '\t') {
        int next = ((s->cursor_col / 4) + 1) * 4;
        while (s->cursor_col < next) {
            session_put_char(s, ' ');
        }
        return;
    }

    if (c < 32) return;

    int idx = s->cursor_row * g_cols + s->cursor_col;
    if (idx >= 0 && idx < g_cols * g_rows) {
        s->cells[idx].c = c;
        s->cells[idx].color = s->fg_color;
    }
    s->cursor_col++;
    if (s->cursor_col >= g_cols) {
        s->cursor_col = 0;
        s->cursor_row++;
        if (s->cursor_row >= g_rows) session_scroll(s);
    }
}

static void ansi_handle_sgr(TerminalSession *s) {
    if (s->ansi_param_count == 0) {
        session_reset_colors(s);
        return;
    }
    for (int i = 0; i < s->ansi_param_count; i++) {
        int p = s->ansi_params[i];
        if (p == 0) session_reset_colors(s);
        else if (p >= 30 && p <= 37) s->fg_color = ansi_color_16(p - 30);
        else if (p >= 90 && p <= 97) s->fg_color = ansi_color_16(8 + (p - 90));
        else if (p >= 40 && p <= 47) s->bg_color = ansi_color_16(p - 40);
        else if (p >= 100 && p <= 107) s->bg_color = ansi_color_16(8 + (p - 100));
        else if (p == 38 || p == 48) {
            if (i + 4 < s->ansi_param_count && s->ansi_params[i + 1] == 2) {
                int r = s->ansi_params[i + 2] & 0xFF;
                int g = s->ansi_params[i + 3] & 0xFF;
                int b = s->ansi_params[i + 4] & 0xFF;
                uint32_t color = 0xFF000000 | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
                if (p == 38) s->fg_color = color;
                else s->bg_color = color;
                i += 4;
            }
        }
    }
}

static void ansi_finalize(TerminalSession *s, char cmd) {
    if (cmd == 'm') {
        ansi_handle_sgr(s);
    } else if (cmd == 'J') {
        int mode = s->ansi_param_count > 0 ? s->ansi_params[0] : 0;
        if (mode == 2 || mode == 0) session_clear(s);
    } else if (cmd == 'K') {
        int row = s->cursor_row;
        for (int col = s->cursor_col; col < g_cols; col++) {
            int idx = row * g_cols + col;
            s->cells[idx].c = ' ';
            s->cells[idx].color = s->fg_color;
        }
    } else if (cmd == 'H' || cmd == 'f') {
        int row = (s->ansi_param_count > 0) ? s->ansi_params[0] : 1;
        int col = (s->ansi_param_count > 1) ? s->ansi_params[1] : 1;
        if (row < 1) row = 1;
        if (col < 1) col = 1;
        s->cursor_row = row - 1;
        s->cursor_col = col - 1;
    } else if (cmd == 'A') {
        int n = (s->ansi_param_count > 0) ? s->ansi_params[0] : 1;
        s->cursor_row -= n;
        if (s->cursor_row < 0) s->cursor_row = 0;
    } else if (cmd == 'B') {
        int n = (s->ansi_param_count > 0) ? s->ansi_params[0] : 1;
        s->cursor_row += n;
        if (s->cursor_row >= g_rows) s->cursor_row = g_rows - 1;
    } else if (cmd == 'C') {
        int n = (s->ansi_param_count > 0) ? s->ansi_params[0] : 1;
        s->cursor_col += n;
        if (s->cursor_col >= g_cols) s->cursor_col = g_cols - 1;
    } else if (cmd == 'D') {
        int n = (s->ansi_param_count > 0) ? s->ansi_params[0] : 1;
        s->cursor_col -= n;
        if (s->cursor_col < 0) s->cursor_col = 0;
    } else if (cmd == 's') {
        s->saved_row = s->cursor_row;
        s->saved_col = s->cursor_col;
    } else if (cmd == 'u') {
        s->cursor_row = s->saved_row;
        s->cursor_col = s->saved_col;
    }

    s->ansi_state = 0;
    s->ansi_param_count = 0;
}

static void session_process_char(TerminalSession *s, char c) {
    if (s->ansi_state == 0) {
        if (c == 27) {
            s->ansi_state = 1;
            s->ansi_param_count = 0;
            s->ansi_params[0] = 0;
            return;
        }
        session_put_char(s, c);
        return;
    }

    if (s->ansi_state == 1) {
        if (c == '[') {
            s->ansi_state = 2;
            s->ansi_param_count = 0;
            s->ansi_params[0] = 0;
            return;
        }
        s->ansi_state = 0;
        session_put_char(s, c);
        return;
    }

    if (s->ansi_state == 2) {
        if (c >= '0' && c <= '9') {
            int idx = s->ansi_param_count;
            s->ansi_params[idx] = s->ansi_params[idx] * 10 + (c - '0');
            return;
        }
        if (c == ';') {
            if (s->ansi_param_count < 7) {
                s->ansi_param_count++;
                s->ansi_params[s->ansi_param_count] = 0;
            }
            return;
        }
        s->ansi_param_count++;
        ansi_finalize(s, c);
        return;
    }
}

static void session_process_output(TerminalSession *s, const char *buf, int len) {
    for (int i = 0; i < len; i++) {
        session_process_char(s, buf[i]);
    }
}

static int get_tab_width(void) {
    if (g_tab_count <= 0) return g_win_w;
    int tab_w = g_win_w / g_tab_count;
    if (tab_w < 60) tab_w = 60;
    return tab_w;
}

static int read_proc_field(int pid, const char *field, char *out, int max_len) {
    if (!out || max_len <= 0) return -1;
    char path[64];
    path[0] = 0;
    str_append(path, "/proc/", sizeof(path));
    char pid_buf[16];
    itoa(pid, pid_buf);
    str_append(path, pid_buf, sizeof(path));
    str_append(path, "/", sizeof(path));
    str_append(path, field, sizeof(path));

    int fd = sys_open(path, "r");
    if (fd < 0) return -1;
    int bytes = sys_read(fd, out, max_len - 1);
    sys_close(fd);
    if (bytes <= 0) return -1;
    out[bytes] = 0;
    trim_end(out);
    return 0;
}

static void truncate_label(const char *src, char *out, int max_chars) {
    if (!out || max_chars <= 0) return;
    int len = (int)strlen(src);
    if (len <= max_chars) {
        str_copy(out, src, max_chars + 1);
        return;
    }
    if (max_chars <= 3) {
        for (int i = 0; i < max_chars; i++) out[i] = src[i];
        out[max_chars] = 0;
        return;
    }
    for (int i = 0; i < max_chars - 3; i++) out[i] = src[i];
    out[max_chars - 3] = '.';
    out[max_chars - 2] = '.';
    out[max_chars - 1] = '.';
    out[max_chars] = 0;
}

static void get_tab_title(TerminalSession *s, char *out, int max_len) {
    if (!s || !out) return;
    int fg = sys_tty_get_fg(s->tty_id);
    if (fg > 0) {
        if (read_proc_field(fg, "name", out, max_len) == 0) return;
    }
    if (s->bsh_pid > 0) {
        if (read_proc_field(s->bsh_pid, "cwd", out, max_len) == 0) return;
    }
    str_copy(out, "Bsh", max_len);
}

static int read_config_value(const char *key, char *out, int max_len) {
    if (!key || !out || max_len <= 0) return -1;

    int fd = sys_open("/Library/bsh/bshrc", "r");
    if (fd < 0) return -1;

    char buf[4096];
    int bytes = sys_read(fd, buf, sizeof(buf) - 1);
    sys_close(fd);

    if (bytes <= 0) return -1;
    buf[bytes] = 0;

    char *line = buf;

    while (*line) {
        char *end = line;
        while (*end && *end != '\n' && *end != '\r') end++;

        char saved = *end;
        *end = 0;

        trim_end(line);

        if (line[0] != '#' && line[0] != 0) {
            char *sep = line;

            while (*sep && *sep != '=') sep++;

            if (*sep == '=') {
                *sep = 0;

                char *k = line;

                // skip leading spaces
                while (*k == ' ' || *k == '\t') k++;

                // skip "export "
                if (strncmp(k, "export ", 7) == 0) {
                    k += 7;
                }

                // skip spaces again after export
                while (*k == ' ' || *k == '\t') k++;

                // trim end of key
                char *kend = k + strlen(k) - 1;
                while (kend > k && (*kend == ' ' || *kend == '\t')) {
                    *kend = 0;
                    kend--;
                }

                if (strcmp(k, key) == 0) {
                    char *val = sep + 1;

                    // skip leading spaces
                    while (*val == ' ' || *val == '\t') val++;

                    // remove "
                    if (*val == '"') {
                        val++;
                        char *vend = strchr(val, '"');
                        if (vend) *vend = 0;
                    }

                    str_copy(out, val, max_len);
                    return 0;
                }
            }
        }

        line = end + (saved ? 1 : 0);
        if (saved == '\r' && *line == '\n') line++;
    }

    return -1;
}

static void resolve_bsh_path(char *out, int max_len) {
    char path_line[256];
    if (read_config_value("PATH", path_line, sizeof(path_line)) != 0) {
        str_copy(path_line, "/bin", sizeof(path_line));
    }

    int i = 0;
    int start = 0;
    while (1) {
        if (path_line[i] == ':' || path_line[i] == 0) {
            int len = i - start;
            if (len > 0) {
                char base[128];
                if (len >= (int)sizeof(base)) len = (int)sizeof(base) - 1;
                for (int j = 0; j < len; j++) base[j] = path_line[start + j];
                base[len] = 0;

                char candidate[160];
                candidate[0] = 0;
                str_append(candidate, base, sizeof(candidate));
                if (candidate[0] && candidate[strlen(candidate) - 1] != '/') str_append(candidate, "/", sizeof(candidate));
                str_append(candidate, "bsh.elf", sizeof(candidate));
                if (sys_exists(candidate)) {
                    str_copy(out, candidate, max_len);
                    return;
                }

                candidate[0] = 0;
                str_append(candidate, base, sizeof(candidate));
                if (candidate[0] && candidate[strlen(candidate) - 1] != '/') str_append(candidate, "/", sizeof(candidate));
                str_append(candidate, "bsh", sizeof(candidate));
                if (sys_exists(candidate)) {
                    str_copy(out, candidate, max_len);
                    return;
                }
            }
            start = i + 1;
        }
        if (path_line[i] == 0) break;
        i++;
    }

    str_copy(out, "/bin/bsh.elf", max_len);
}

static bool has_space(const char *s) {
    if (!s) return false;
    while (*s) {
        if (*s == ' ') return true;
        s++;
    }
    return false;
}

static void terminal_resize(int w, int h) {
    int min_w = g_char_w * 40;
    int min_h = TAB_BAR_H + (g_line_h * 10);
    if (w < min_w) w = min_w;
    if (h < min_h) h = min_h;

    g_win_w = w;
    g_win_h = h;

    int new_cols = w / g_char_w;
    int content_h = h - TAB_BAR_H - CONTENT_PAD_BOTTOM;
    if (g_line_h <= 0) g_line_h = 15;
    if (content_h < g_line_h) content_h = g_line_h;
    int new_rows = content_h / g_line_h;
    if (new_cols < 10) new_cols = 10;
    if (new_rows < 5) new_rows = 5;

    if (new_cols == g_cols && new_rows == g_rows) return;

    int old_cols = g_cols;
    int old_rows = g_rows;
    g_cols = new_cols;
    g_rows = new_rows;

    for (int i = 0; i < g_tab_count; i++) {
        TerminalSession *s = &g_tabs[i];
        int old_scroll_count = s->scroll_count;
        int old_scroll_offset = s->scroll_offset;
        int old_total_lines = old_scroll_count + old_rows;
        int old_bottom_line = old_total_lines - 1 - old_scroll_offset;
        int old_top_line = old_bottom_line - (old_rows - 1);
        CharCell *old_cells = s->cells;
        int old_cursor_row = s->cursor_row;
        int old_cursor_col = s->cursor_col;

        s->cells = (CharCell *)malloc(sizeof(CharCell) * g_cols * g_rows);
        for (int j = 0; j < g_cols * g_rows; j++) {
            s->cells[j].c = ' ';
            s->cells[j].color = s->fg_color;
        }

        int row_start = 0;
        if (old_rows > g_rows) {
            if (old_cursor_row >= g_rows) {
                row_start = old_cursor_row - (g_rows - 1);
            } else {
                row_start = 0;
            }
            int max_start = old_rows - g_rows;
            if (row_start > max_start) row_start = max_start;
            if (row_start < 0) row_start = 0;
        }
        int dropped = 0;
        if (row_start > 0) {
            int projected = old_scroll_count + row_start;
            if (projected > SCROLLBACK_LINES) dropped = projected - SCROLLBACK_LINES;
        }
        int desired_top_line = old_top_line - dropped;
        if (desired_top_line < 0) desired_top_line = 0;
        for (int r = 0; r < row_start; r++) {
            scrollback_push_row(s, old_cells + (r * old_cols), old_cols);
        }

        int copy_rows = old_rows;
        if (copy_rows > g_rows) copy_rows = g_rows;
        int copy_cols = old_cols < g_cols ? old_cols : g_cols;
        for (int r = 0; r < copy_rows; r++) {
            CharCell *src = old_cells + ((row_start + r) * old_cols);
            CharCell *dst = s->cells + (r * g_cols);
            for (int c = 0; c < copy_cols; c++) {
                dst[c] = src[c];
            }
        }

        s->cursor_row = old_cursor_row - row_start;
        if (old_rows <= g_rows) s->cursor_row = old_cursor_row;
        if (s->cursor_row < 0) s->cursor_row = 0;
        if (s->cursor_row >= g_rows) s->cursor_row = g_rows - 1;

        s->cursor_col = old_cursor_col;
        if (s->cursor_col < 0) s->cursor_col = 0;
        if (s->cursor_col >= g_cols) s->cursor_col = g_cols - 1;

        if (old_scroll_offset == 0) {
            s->scroll_offset = 0;
        } else {
            int new_total_lines = s->scroll_count + g_rows;
            int desired_bottom = desired_top_line + (g_rows - 1);
            s->scroll_offset = (new_total_lines - 1) - desired_bottom;
        }
        session_adjust_scroll(s, 0);

        if (old_cells) free(old_cells);
    }
}

static void draw_tabs(void) {
    if (g_tab_count <= 0) return;
    ui_draw_rect(g_win, 0, 0, g_win_w, TAB_BAR_H, 0xFF1A1A1A);
    int tab_w = get_tab_width();
    for (int i = 0; i < g_tab_count; i++) {
        int x = i * tab_w;
        uint32_t bg = (i == g_active_tab) ? 0xFF333333 : 0xFF222222;
        ui_draw_rect(g_win, x, 0, tab_w - 2, TAB_BAR_H, bg);
        int close_size = TAB_CLOSE_W;
        int close_x = x + tab_w - TAB_CLOSE_PAD - close_size;
        int close_y = (TAB_BAR_H - close_size) / 2;
        uint32_t close_bg = (i == g_active_tab) ? 0xFF444444 : 0xFF333333;
        ui_draw_rect(g_win, close_x, close_y, close_size, close_size, close_bg);
        ui_draw_string(g_win, close_x + 2, close_y + 1, "x", 0xFFFFFFFF);
        char title[64];
        char label[64];
        get_tab_title(&g_tabs[i], title, sizeof(title));
        int text_w = tab_w - 10 - (TAB_CLOSE_PAD + TAB_CLOSE_W);
        int max_chars = text_w / g_char_w;
        if (max_chars < 4) max_chars = 4;
        truncate_label(title, label, max_chars);
        ui_draw_string(g_win, x + 6, 4, label, 0xFFFFFFFF);
    }
}

static void draw_session(TerminalSession *s) {
    int base_y = TAB_BAR_H;
    ui_draw_rect(g_win, 0, base_y, g_win_w, g_win_h - base_y, s->bg_color);

    int max_offset = scrollback_max_offset(s);
    if (s->scroll_offset > max_offset) s->scroll_offset = max_offset;
    int total_lines = s->scroll_count + g_rows;
    int bottom_line = total_lines - 1 - s->scroll_offset;
    int top_line = bottom_line - (g_rows - 1);

    for (int row = 0; row < g_rows; row++) {
        int line_index = top_line + row;
        CharCell *line = NULL;
        int line_cols = 0;
        if (line_index >= 0 && line_index < s->scroll_count) {
            line = scrollback_get_line(s, line_index, &line_cols);
        } else if (line_index >= s->scroll_count && line_index < total_lines) {
            int live_row = line_index - s->scroll_count;
            if (live_row >= 0 && live_row < g_rows) {
                line = s->cells + (live_row * g_cols);
                line_cols = g_cols;
            }
        }

        int input_start = s->cursor_col - s->input_len;
        if (input_start < 0) input_start = 0;
        if (input_start >= g_cols) input_start = g_cols - 1;

        // lenght of a command
        int cmd_len = 0;
        while (cmd_len < s->input_len &&
            s->current_input[cmd_len] != ' ') {
            cmd_len++;
        }

        int input_end = input_start + cmd_len;
        if (input_end > g_cols) input_end = g_cols;

        for (int col = 0; col < g_cols; col++) {
            char ch = ' ';
            uint32_t color = s->fg_color;

            if (line && col < line_cols) {
                ch = line[col].c;
                if (ch == 0) ch = ' ';
                color = line[col].color;
            }

            if (s->scroll_offset == 0 && row == s->cursor_row) {
                if (col >= input_start && col < input_end) {
                    color = s->input_color;
                }
            }

            char str[2] = { ch, 0 };
            int x = col * g_char_w;
            int y = base_y + row * g_line_h;
            ui_draw_string(g_win, x, y, str, color);
        }
    }

    if (s->scroll_offset == 0) {
        int cx = s->cursor_col * g_char_w;
        int cy = base_y + s->cursor_row * g_line_h;
        ui_draw_rect(g_win, cx, cy + g_line_h - 2, g_char_w, 2, 0xFFFFFFFF);
    }

    ui_mark_dirty(g_win, 0, 0, g_win_w, g_win_h);
}

static void tab_init(TerminalSession *s, int tty_id, int bsh_pid) {
    s->tty_id = tty_id;
    s->bsh_pid = bsh_pid;
    s->cells = (CharCell *)malloc(sizeof(CharCell) * g_cols * g_rows);
    s->scrollback = (CharCell *)malloc(sizeof(CharCell) * SCROLLBACK_LINES * SCROLLBACK_COLS);
    s->scroll_cols = (int *)malloc(sizeof(int) * SCROLLBACK_LINES);
    s->cursor_row = 0;
    s->cursor_col = 0;
    s->ansi_state = 0;
    s->ansi_param_count = 0;
    s->saved_row = 0;
    s->saved_col = 0;

    s->input_len = 0;
    s->current_input[0] = 0;
    session_reset_colors(s);
    scrollback_init(s);
    
    char value[64];

    if (read_config_value("TERMINAL_COLOR", value, sizeof(value)) == 0) {
        if (strcmp(value, "1") == 0 || strcmp(value, "true") == 0) {
            s->colors_enabled = true;
        } else {
            s->colors_enabled = false;
        }
    } else {
        s->colors_enabled = false;
    }
        
    session_clear(s);
}

static void tab_free(TerminalSession *s) {
    if (!s) return;
    if (s->cells) {
        free(s->cells);
        s->cells = NULL;
    }
    if (s->scrollback) {
        free(s->scrollback);
        s->scrollback = NULL;
    }
    if (s->scroll_cols) {
        free(s->scroll_cols);
        s->scroll_cols = NULL;
    }
}

static void close_tab(int idx) {
    if (idx < 0 || idx >= g_tab_count) return;
    TerminalSession *s = &g_tabs[idx];

    sys_tty_kill_all(s->tty_id);
    sys_tty_destroy(s->tty_id);

    tab_free(s);

    for (int i = idx; i < g_tab_count - 1; i++) {
        g_tabs[i] = g_tabs[i + 1];
    }
    g_tab_count--;
    if (g_tab_count <= 0) {
        sys_exit(0);
    }
    if (g_active_tab > idx) {
        g_active_tab--;
    } else if (g_active_tab == idx) {
        if (g_active_tab >= g_tab_count) g_active_tab = g_tab_count - 1;
    }
}

static int create_tab(void) {
    if (g_tab_count >= MAX_TABS) return -1;

    int tty_id = sys_tty_create();
    if (tty_id < 0) return -1;

    char start_dir[256];
    start_dir[0] = 0;
    if (g_tab_count > 0) {
        TerminalSession *active = &g_tabs[g_active_tab];
        if (active->bsh_pid > 0) {
            read_proc_field(active->bsh_pid, "cwd", start_dir, sizeof(start_dir));
        }
    }

    char args[32];
    args[0] = 0;
    str_append(args, "-t ", sizeof(args));
    char id_buf[8];
    itoa(tty_id, id_buf);
    str_append(args, id_buf, sizeof(args));

    if (start_dir[0]) {
        str_append(args, " -d ", sizeof(args));
        if (has_space(start_dir)) {
            str_append(args, "\"", sizeof(args));
            str_append(args, start_dir, sizeof(args));
            str_append(args, "\"", sizeof(args));
        } else {
            str_append(args, start_dir, sizeof(args));
        }
    }

    char bsh_path[128];
    resolve_bsh_path(bsh_path, sizeof(bsh_path));
    int pid = sys_spawn(bsh_path, args, SPAWN_FLAG_TERMINAL | SPAWN_FLAG_TTY_ID, tty_id);
    if (pid < 0) return -1;

    tab_init(&g_tabs[g_tab_count], tty_id, pid);
    g_tab_count++;
    return g_tab_count - 1;
}

static int parse_path(char paths[][128], int max_paths) {
    char path_line[256];

    if (read_config_value("PATH", path_line, sizeof(path_line)) != 0) {
        str_copy(path_line, "/bin", sizeof(path_line));
    }

    if (path_line[0] == '"') {
        memmove(path_line, path_line + 1, strlen(path_line));
        char *end = strchr(path_line, '"');
        if (end) *end = 0;
    }

    int count = 0;
    int start = 0;

    for (int i = 0;; i++) {
        if (path_line[i] == ':' || path_line[i] == ';' || path_line[i] == 0) {
            int len = i - start;

            if (len > 0 && count < max_paths) {
                if (len >= 128) len = 127;

                memcpy(paths[count], &path_line[start], len);
                paths[count][len] = 0;
                count++;
            }

            start = i + 1;
        }

        if (path_line[i] == 0) break;
    }

    return count;
}

static bool command_exists(const char *cmd) {
    if (!cmd || !cmd[0]) return false;

    char paths[16][128];
    int path_count = parse_path(paths, 16);

    char full[256];

    for (int i = 0; i < path_count; i++) {
        // folder/cmd
        full[0] = 0;
        str_append(full, paths[i], sizeof(full));
        if (full[strlen(full) - 1] != '/') str_append(full, "/", sizeof(full));
        str_append(full, cmd, sizeof(full));

        if (sys_exists(full)) return true;

        // folder/cmd.elf
        full[0] = 0;
        str_append(full, paths[i], sizeof(full));
        if (full[strlen(full) - 1] != '/') str_append(full, "/", sizeof(full));
        str_append(full, cmd, sizeof(full));
        str_append(full, ".elf", sizeof(full));

        if (sys_exists(full)) return true;
    }

    return false;
}

static bool command_starts_with(const char *prefix) {
    if (!prefix || !prefix[0]) return false;

    char paths[16][128];
    int path_count = parse_path(paths, 16);

    FAT32_FileInfo entries[128];

    for (int p = 0; p < path_count; p++) {
        int count = sys_list(paths[p], entries, 128);
        if (count <= 0) continue;

        for (int i = 0; i < count; i++) {
            if (entries[i].is_directory) continue;

            char name[256];
            str_copy(name, entries[i].name, sizeof(name));

            int len = strlen(name);

            // remove .elf
            if (len > 4 && strcmp(name + len - 4, ".elf") == 0) {
                name[len - 4] = 0;
            }

            int j = 0;
            while (prefix[j] && name[j] && prefix[j] == name[j]) {
                j++;
            }

            if (prefix[j] == 0) {
                return true;
            }
        }
    }

    return false;
}

static void update_input_color(TerminalSession *s) {
    if (!s->colors_enabled) {
        s->input_color = s->fg_color;
        return;
    }
    
    if (s->input_len == 0) {
        s->input_color = 0xFFFFFFFF;
        return;
    }

    char cmd[64];
    int i = 0;

    while (i < s->input_len &&
        s->current_input[i] != ' ' &&
        s->current_input[i] != '\t' &&
        i < 63) {
        cmd[i] = s->current_input[i];
        i++;
    }
    cmd[i] = 0;

    if (command_exists(cmd)) {
        s->input_color = 0xFF55FF55; // green
    } else if (command_starts_with(cmd)) {
        s->input_color = 0xFFFFFF55; // yellow
    } else {
        s->input_color = 0xFFFF5555; // red
    }
}

static void handle_key(gui_event_t *ev) {
    TerminalSession *s = &g_tabs[g_active_tab];
    char c = (char)ev->arg1;
    bool ctrl = ev->arg3 != 0;

    // create new tab with ctrl + t
    if (ctrl && c == 't') {
        int idx = create_tab();
        if (idx >= 0) g_active_tab = idx;
        return;
    }

    // switch to tab right, with ctrl + arrow right
    if (ctrl && c == KEY_RIGHT) {
        if (g_tab_count > 0) g_active_tab = (g_active_tab + 1) % g_tab_count;
        return;
    }

    // switch to tab left, with ctrl + arrow left
    if (ctrl && c == KEY_LEFT) {
        if (g_tab_count > 0) g_active_tab = (g_active_tab + g_tab_count - 1) % g_tab_count;
        return;
    }

    // kill the processus with ctrl + c
    if (ctrl && (c == 'c' || c == 'C')) {
        int fg = sys_tty_get_fg(s->tty_id);
        if (fg > 0) {
            sys_tty_kill_fg(s->tty_id);
            sys_tty_set_fg(s->tty_id, 0);
            return;
        }
        char ch = 3;
        sys_tty_write_in(s->tty_id, &ch, 1);
        return;
    }

    if (!ctrl) {
        if (c == KEY_BACKSPACE) {
            if (s->input_len > 0) {
                s->input_len--;
                s->current_input[s->input_len] = 0;
            }
        } else if (c >= 32 && c < 127) {
            if (s->input_len < LINE_MAX - 1) {
                s->current_input[s->input_len++] = c;
                s->current_input[s->input_len] = 0;
            }
        }

        update_input_color(s);
    }

    if (c == KEY_ENTER) {
        s->input_color = 0xFFFFFFFF;
        s->input_len = 0;
        s->current_input[0] = 0;
    }


    sys_tty_write_in(s->tty_id, &c, 1);
}

int main(void) {
    g_win = ui_window_create("Terminal", 60, 60, g_win_w, g_win_h);
    ui_window_set_resizable(g_win, true);
    
    char font_path[128];
    if (read_config_value("TERMINAL_FONT", font_path, sizeof(font_path)) == 0) {
        ui_set_font(g_win, font_path);
    } else {
        ui_set_font(g_win, "/Library/Fonts/JetBrainsMono-Regular.ttf");
    }
    update_font_metrics();

    terminal_resize(g_win_w, g_win_h);

    int idx = create_tab();
    if (idx < 0) return 1;
    g_active_tab = idx;

    gui_event_t ev;
    char out_buf[TTY_READ_CHUNK];

    while (1) {
        bool dirty = false;

        for (int i = 0; i < g_tab_count; i++) {
            TerminalSession *s = &g_tabs[i];
            int read = 0;
            while ((read = sys_tty_read_out(s->tty_id, out_buf, sizeof(out_buf))) > 0) {
                session_process_output(s, out_buf, read);
                if (i == g_active_tab) dirty = true;
            }
        }

        while (ui_get_event(g_win, &ev)) {
            if (ev.type == GUI_EVENT_CLOSE) {
                for (int i = 0; i < g_tab_count; i++) {
                    if (g_tabs[i].bsh_pid > 0) sys_kill(g_tabs[i].bsh_pid);
                }
                sys_exit(0);
            } else if (ev.type == GUI_EVENT_KEY) {
                handle_key(&ev);
                dirty = true;
            } else if (ev.type == GUI_EVENT_CLICK) {
                if (ev.arg2 < TAB_BAR_H) {
                    int tab_w = get_tab_width();
                    int tab = ev.arg1 / tab_w;
                    if (tab >= 0 && tab < g_tab_count) {
                        int close_size = TAB_CLOSE_W;
                        int close_x = tab * tab_w + tab_w - TAB_CLOSE_PAD - close_size;
                        int close_y = (TAB_BAR_H - close_size) / 2;
                        if (ev.arg1 >= close_x && ev.arg1 < close_x + close_size &&
                            ev.arg2 >= close_y && ev.arg2 < close_y + close_size) {
                            close_tab(tab);
                        } else {
                            g_active_tab = tab;
                        }
                        dirty = true;
                    }
                }
            } else if (ev.type == GUI_EVENT_MOUSE_WHEEL) {
                int lines = ev.arg1 * 3;
                if (lines != 0) {
                    session_adjust_scroll(&g_tabs[g_active_tab], lines);
                    dirty = true;
                }
            } else if (ev.type == GUI_EVENT_RESIZE) {
                terminal_resize(ev.arg1, ev.arg2);
                dirty = true;
            } else if (ev.type == GUI_EVENT_PAINT) {
                dirty = true;
            }
        }

        if (dirty) {
            draw_tabs();
            draw_session(&g_tabs[g_active_tab]);
        } else {
            sys_yield();
        }
    }

    return 0;
}
