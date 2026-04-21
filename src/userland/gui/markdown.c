// Copyright (c) 2023-2026 Chris (boreddevnl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
// BOREDOS_APP_DESC: Markdown document viewer.
// BOREDOS_APP_ICONS: /Library/images/icons/colloid/text-editor.png
#include "libc/syscall.h"
#include "libc/libui.h"
#include "libc/stdlib.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MD_MAX_CONTENT 16384
#define MD_MAX_LINES 256
#define MD_CHAR_WIDTH 8
#define MD_LINE_HEIGHT 16

#define COLOR_DARK_PANEL    0xFF202020
#define COLOR_DARK_BG       0xFF121212
#define COLOR_DARK_TEXT     0xFFE0E0E0
#define COLOR_DARK_TITLEBAR 0xFF303030
#define COLOR_BLACK         0xFF000000

typedef enum {
    MD_LINE_NORMAL,
    MD_LINE_HEADING1,
    MD_LINE_HEADING2,
    MD_LINE_HEADING3,
    MD_LINE_BOLD,
    MD_LINE_ITALIC,
    MD_LINE_LIST,
    MD_LINE_BLOCKQUOTE,
    MD_LINE_CODE
} MDLineType;

#define MD_MAX_LINKS 8
#define COLOR_LINK       0xFF569CD6

typedef struct {
    char url[256];
    int start_char;
    int end_char;
} MDLink;

typedef struct {
    char content[256];
    int length;
    MDLineType type;
    int indent_level;
    MDLink links[MD_MAX_LINKS];
    int link_count;
} MDLine;

#define MD_MAX_CLICK_LINKS 128
typedef struct {
    int x, y, w, h;
    char url[256];
} ClickLink;
static ClickLink click_links[MD_MAX_CLICK_LINKS];
static int click_link_count = 0;

static MDLine *lines = NULL;
static int line_capacity = 0;
static int line_count = 0;
static int scroll_top = 0;
static char open_filename[256] = "";

static int win_w = 600;
static int win_h = 400;

static size_t md_strlen(const char *str) {
    size_t len = 0;
    while (str[len]) len++;
    return len;
}

static void md_strcpy(char *dest, const char *src) {
    while (*src) *dest++ = *src++;
    *dest = 0;
}

static int md_strncpy(char *dest, const char *src, int n) {
    int i = 0;
    while (i < n && src[i]) {
        dest[i] = src[i];
        i++;
    }
    dest[i] = 0;
    return i;
}

static void md_parse_line(const char *raw_line, MDLine *line_out) {
    int i = 0;
    int out_idx = 0;
    int indent = 0;
    MDLineType type = MD_LINE_NORMAL;
    line_out->link_count = 0;
    char *output = line_out->content;
    
    while (raw_line[i] == ' ' || raw_line[i] == '\t') {
        if (raw_line[i] == '\t') indent += 2;
        else indent += 1;
        i++;
    }
    
    if (raw_line[i] == '#') {
        int hash_count = 0;
        while (raw_line[i] == '#') {
            hash_count++;
            i++;
        }
        if (raw_line[i] == ' ') i++;
        
        if (hash_count == 1) type = MD_LINE_HEADING1;
        else if (hash_count == 2) type = MD_LINE_HEADING2;
        else if (hash_count <= 6) type = MD_LINE_HEADING3;
    } else if (raw_line[i] == '-' || raw_line[i] == '*') {
        if ((raw_line[i] == '-' || raw_line[i] == '*') && (raw_line[i+1] == ' ' || raw_line[i+1] == '\t')) {
            type = MD_LINE_LIST;
            i += 2;
            while (raw_line[i] == ' ' || raw_line[i] == '\t') i++;
        }
    } else if (raw_line[i] == '>') {
        type = MD_LINE_BLOCKQUOTE;
        i++;
        if (raw_line[i] == ' ') i++;
    }
    
    while (raw_line[i] && out_idx < 255) {
        if (raw_line[i] == '*' && raw_line[i+1] == '*') {
            i += 2;
            continue;
        }
        if ((raw_line[i] == '*' || raw_line[i] == '_') && (i == 0 || raw_line[i-1] != '\\')) {
            i++;
            continue;
        }
        if (raw_line[i] == '`') {
            i++;
            continue;
        }
        if (raw_line[i] == '[') {
            int link_start_char = out_idx;
            i++;
            while (raw_line[i] && raw_line[i] != ']' && out_idx < 255) {
                output[out_idx++] = raw_line[i++];
            }
            int link_end_char = out_idx;
            if (raw_line[i] == ']') i++;
            char url[256]; url[0] = 0;
            if (raw_line[i] == '(') {
                i++;
                int u_idx = 0;
                while (raw_line[i] && raw_line[i] != ')' && u_idx < 255) {
                    url[u_idx++] = raw_line[i++];
                }
                url[u_idx] = 0;
                if (raw_line[i] == ')') i++;
                
                if (line_out->link_count < MD_MAX_LINKS && link_end_char > link_start_char) {
                    MDLink *link = &line_out->links[line_out->link_count++];
                    link->start_char = link_start_char;
                    link->end_char = link_end_char;
                    md_strcpy(link->url, url);
                }
            }
            continue;
        }
        output[out_idx++] = raw_line[i++];
    }
    output[out_idx] = 0;
    line_out->type = type;
    line_out->indent_level = indent;
}

static void md_clear_all(void) {
    if (lines) {
        for (int i = 0; i < line_count; i++) {
            lines[i].content[0] = 0;
            lines[i].length = 0;
            lines[i].type = MD_LINE_NORMAL;
            lines[i].indent_level = 0;
            lines[i].link_count = 0;
        }
    }
    line_count = 0;
    scroll_top = 0;
    open_filename[0] = 0;
}

static void ensure_line_capacity(int line) {
    if (line >= line_capacity) {
        line_capacity = (line_capacity == 0) ? 256 : line_capacity * 2;
        lines = realloc(lines, sizeof(MDLine) * line_capacity);
    }
}

void markdown_open_file(const char *filename) {
    md_clear_all();
    md_strcpy(open_filename, filename);
    
    int fd = sys_open(filename, "r");
    if (fd < 0) return;
    
    int buf_cap = 16384;
    int buf_size = 0;
    char *buffer = malloc(buf_cap);
    if (!buffer) { sys_close(fd); return; }
    
    while (1) {
        int r = sys_read(fd, buffer + buf_size, buf_cap - buf_size - 1);
        if (r <= 0) break;
        buf_size += r;
        if (buf_size >= buf_cap - 1) {
            buf_cap *= 2;
            char *new_buf = realloc(buffer, buf_cap);
            if (!new_buf) break;
            buffer = new_buf;
        }
    }
    sys_close(fd);
    
    if (buf_size <= 0) {
        free(buffer);
        return;
    }
    buffer[buf_size] = 0;
    
    if (!lines) {
        line_capacity = 256;
        lines = malloc(sizeof(MDLine) * line_capacity);
    }
    
    int line = 0;
    int col = 0;
    char raw_line[256] = "";
    bool in_code_block = false;
    
    for (int i = 0; i < buf_size; i++) {
        char ch = buffer[i];
        if (ch == '\n') {
            raw_line[col] = 0;
            if (raw_line[0] == '`' && raw_line[1] == '`' && raw_line[2] == '`') {
                in_code_block = !in_code_block;
            } else {
                ensure_line_capacity(line);
                if (in_code_block) {
                    md_strcpy(lines[line].content, raw_line);
                    lines[line].length = md_strlen(raw_line);
                    lines[line].type = MD_LINE_CODE;
                    lines[line].indent_level = 0;
                    line++;
                } else {
                    md_parse_line(raw_line, &lines[line]);
                    lines[line].length = md_strlen(lines[line].content);
                    line++;
                }
            }
            col = 0;
            raw_line[0] = 0;
        } else if (col < 255) {
            raw_line[col++] = ch;
        }
    }
    
    if (col > 0) {
        raw_line[col] = 0;
        if (raw_line[0] == '`' && raw_line[1] == '`' && raw_line[2] == '`') {
        } else {
            ensure_line_capacity(line);
            if (in_code_block) {
                md_strcpy(lines[line].content, raw_line);
                lines[line].length = md_strlen(raw_line);
                lines[line].type = MD_LINE_CODE;
                lines[line].indent_level = 0;
                line++;
            } else {
                md_parse_line(raw_line, &lines[line]);
                lines[line].length = md_strlen(lines[line].content);
                line++;
            }
        }
    }
    line_count = line;
    free(buffer);
}

static void md_draw_text_bold(ui_window_t win, int x, int y, const char *text, uint32_t color) {
    ui_draw_string(win, x, y, text, color);
    ui_draw_string(win, x + 1, y, text, color);
}

static void md_paint(ui_window_t win) {
    ui_draw_rect(win, 0, 0, win_w, win_h, COLOR_DARK_BG);
    click_link_count = 0;
    int offset_x = 4;
    int offset_y = 0;
    int content_width = win_w - 8;
    int content_height = win_h - 28;
    
    ui_draw_rounded_rect_filled(win, offset_x, offset_y, content_width, 20, 6, COLOR_DARK_PANEL);
    ui_draw_string(win, offset_x + 4, offset_y + 4, "File", COLOR_DARK_TEXT);
    ui_draw_string(win, offset_x + 50, offset_y + 4, open_filename, COLOR_DARK_TEXT);
    
    int btn_x_up = offset_x + content_width - 50;
    int btn_y = offset_y + 2;
    ui_draw_rounded_rect_filled(win, btn_x_up, btn_y, 20, 16, 4, COLOR_DARK_TITLEBAR);
    ui_draw_string(win, btn_x_up + 6, btn_y + 4, "^", COLOR_DARK_TEXT);
    ui_draw_rounded_rect_filled(win, btn_x_up + 24, btn_y, 20, 16, 4, COLOR_DARK_TITLEBAR);
    ui_draw_string(win, btn_x_up + 30, btn_y, "v", COLOR_DARK_TEXT);
    
    int content_start_y = offset_y + 24;
    int content_start_x = offset_x + 8;
    int usable_content_width = win_w - 24;
    int usable_content_height = win_h - content_start_y - 4;
    
    ui_draw_rounded_rect_filled(win, 4, content_start_y, win_w - 8, usable_content_height, 6, COLOR_DARK_BG);
    
    int current_y = content_start_y + 4;
    int i = scroll_top;
    
    while (i < line_count && current_y < content_start_y + usable_content_height) {
        MDLine *line = &lines[i];
        
        int line_height = MD_LINE_HEIGHT;
        int extra_spacing = 0;
        uint32_t text_color = COLOR_DARK_TEXT;
        bool use_bold = false;
        float text_scale = 15.0f;
        
        switch (line->type) {
            case MD_LINE_HEADING1:
                line_height = MD_LINE_HEIGHT * 2;
                text_color = 0xFF87CEEB;
                use_bold = true;
                extra_spacing = 4;
                text_scale = 30.0f;
                break;
            case MD_LINE_HEADING2:
                line_height = MD_LINE_HEIGHT + 6;
                text_color = 0xFF4A90E2;
                use_bold = true;
                extra_spacing = 2;
                text_scale = 24.0f;
                break;
            case MD_LINE_HEADING3:
                line_height = MD_LINE_HEIGHT + 2;
                text_color = 0xFF87CEEB;
                use_bold = false;
                text_scale = 18.0f;
                break;
            case MD_LINE_BLOCKQUOTE:
                text_color = 0xFFA0A0A0;
                break;
            case MD_LINE_CODE:
                text_color = 0xFF90EE90;
                break;
            default:
                text_color = COLOR_DARK_TEXT;
                break;
        }
        
        if (current_y + line_height > content_start_y + usable_content_height) break;
        
        int start_x = content_start_x + (line->indent_level * 4);
        int available_width = usable_content_width - (line->indent_level * 4);
        
        if (line->type == MD_LINE_LIST) {
            start_x += 12;
            available_width -= 12;
        }
        
        int x_offset = start_x;
        int local_display_line = 0;
        const char *text = line->content;
        int text_len = line->length;
        int char_idx = 0;
        
        if (line->type == MD_LINE_LIST && text_len > 0 && text[0] == ' ') {
            char_idx++;
        }
        
        while (char_idx < text_len || (text_len == 0 && local_display_line == 0)) {
            int limit = 0;
            int cur_w = 0;
            int test_idx = char_idx;
            while (test_idx < text_len) {
                char tmp[2] = {text[test_idx], 0};
                int char_w = (text_scale != 15.0f) ? ui_get_string_width_scaled(tmp, text_scale) : MD_CHAR_WIDTH;
                if (x_offset - start_x + cur_w + char_w > available_width && limit > 0) break;
                cur_w += char_w;
                limit++;
                test_idx++;
            }
            if (limit < 1) limit = 1;
            
            bool hit_max_width = (test_idx < text_len);
            
            int next_boundary = text_len;
            for (int l = 0; l < line->link_count; l++) {
                 if (line->links[l].start_char > char_idx && line->links[l].start_char < next_boundary) next_boundary = line->links[l].start_char;
                 if (line->links[l].end_char > char_idx && line->links[l].end_char < next_boundary) next_boundary = line->links[l].end_char;
            }
            
            bool hit_boundary = false;
            if (next_boundary - char_idx <= limit && next_boundary > char_idx) {
                limit = next_boundary - char_idx;
                hit_boundary = true;
                hit_max_width = false;
            }
            
            char line_segment[256];
            int segment_len = 0;
            int segment_start = char_idx;
            
            while (segment_len < limit && char_idx < text_len) {
                line_segment[segment_len++] = text[char_idx++];
            }
            line_segment[segment_len] = 0;
            
            if (hit_max_width && !hit_boundary) {
                int last_space = -1;
                for (int j = segment_len - 1; j >= 0; j--) {
                    if (line_segment[j] == ' ') {
                        last_space = j; break;
                    }
                }
                if (last_space > 0) {
                    segment_len = last_space;
                    line_segment[segment_len] = 0;
                    char_idx = segment_start + last_space + 1;
                    while (char_idx < text_len && text[char_idx] == ' ') char_idx++;
                }
            }
            
            if (x_offset == start_x && local_display_line == 0) {
                if (line->type == MD_LINE_LIST) {
                    ui_draw_rect(win, start_x - 12, current_y + MD_LINE_HEIGHT/2 - 1, 2, 2, COLOR_DARK_TEXT);
                } else if (line->type == MD_LINE_BLOCKQUOTE) {
                    ui_draw_rect(win, start_x - 4, current_y, 2, line_height, 0xFF404080);
                }
            }
            
            if (segment_len > 0) {
                int link_idx = -1;
                for (int l = 0; l < line->link_count; l++) {
                    if (segment_start >= line->links[l].start_char && segment_start < line->links[l].end_char) {
                        link_idx = l;
                        break;
                    }
                }
                uint32_t draw_color = (link_idx != -1) ? COLOR_LINK : text_color;
                
                if (line->type == MD_LINE_CODE) {
                    int seg_w = (text_scale != 15.0f) ? ui_get_string_width_scaled(line_segment, text_scale) : segment_len * MD_CHAR_WIDTH;
                    ui_draw_rect(win, x_offset - 2, current_y - 2, seg_w + 4, 12, COLOR_BLACK);
                }
                
                if (text_scale != 15.0f) {
                    ui_draw_string_scaled(win, x_offset, current_y + extra_spacing, line_segment, draw_color, text_scale);
                    if (use_bold) ui_draw_string_scaled(win, x_offset + 1, current_y + extra_spacing, line_segment, draw_color, text_scale);
                } else {
                    if (use_bold) {
                        md_draw_text_bold(win, x_offset, current_y + extra_spacing, line_segment, draw_color);
                    } else {
                        ui_draw_string(win, x_offset, current_y, line_segment, draw_color);
                    }
                }
                
                int text_w = (text_scale != 15.0f) ? ui_get_string_width_scaled(line_segment, text_scale) : segment_len * MD_CHAR_WIDTH;
                
                if (link_idx != -1) {
                    int ul_y = current_y + extra_spacing + (int)text_scale - 2;
                    if (text_scale == 15.0f) ul_y = current_y + extra_spacing + MD_LINE_HEIGHT - 3;
                    ui_draw_rect(win, x_offset, ul_y, text_w, 1, draw_color);
                    
                    if (click_link_count < MD_MAX_CLICK_LINKS) {
                        ClickLink *cl = &click_links[click_link_count++];
                        cl->x = x_offset;
                        cl->y = current_y;
                        cl->w = text_w;
                        cl->h = line_height;
                        md_strcpy(cl->url, line->links[link_idx].url);
                    }
                }
                
                x_offset += text_w;
            }
            
            if (char_idx < text_len && !hit_boundary) {
                x_offset = start_x;
                current_y += line_height;
                local_display_line++;
            } else if (char_idx >= text_len) {
                current_y += line_height;
                local_display_line++;
            }
        }
        i++;
    }
}

static void md_handle_key(char c, bool pressed) {
    if (!pressed) return;
    if (c == 'w' || c == 'W' || c == 17) {
        scroll_top -= 3;
        if (scroll_top < 0) scroll_top = 0;
    } else if (c == 's' || c == 'S' || c == 18) {
        scroll_top += 3;
        int max_scroll = line_count - 10;
        if (scroll_top > max_scroll) scroll_top = max_scroll;
        if (scroll_top < 0) scroll_top = 0;
    }
}

static void md_handle_click(int x, int y) {
    for (int i = 0; i < click_link_count; i++) {
        if (x >= click_links[i].x && x < click_links[i].x + click_links[i].w &&
            y >= click_links[i].y && y < click_links[i].y + click_links[i].h) {
            
            char full_path[512];
            int last_slash = -1;
            for (int k = 0; open_filename[k]; k++) {
                if (open_filename[k] == '/') last_slash = k;
            }
            if (last_slash >= 0) {
                int k;
                for (k = 0; k <= last_slash; k++) full_path[k] = open_filename[k];
                full_path[k] = 0;
            } else {
                full_path[0] = 0;
            }
            
            char *d = full_path;
            while (*d) d++;
            const char *s = click_links[i].url;
            while (*s) *d++ = *s++;
            *d = 0;
            
            markdown_open_file(full_path);
            return;
        }
    }

    int content_width = win_w - 8;
    int btn_x_up = 4 + content_width - 50;
    int btn_y = 2;
    if (x >= btn_x_up && x < btn_x_up + 20 && y >= btn_y && y < btn_y + 16) {
        scroll_top -= 3;
        if (scroll_top < 0) scroll_top = 0;
        return;
    }
    
    int btn_x_down_top = 4 + content_width - 50 + 24;
    if (x >= btn_x_down_top && x < btn_x_down_top + 20 && y >= btn_y && y < btn_y + 16) {
        scroll_top += 3;
        int max_scroll = line_count - 10;
        if (scroll_top > max_scroll) scroll_top = max_scroll;
        if (scroll_top < 0) scroll_top = 0;
        return;
    }
}

int main(int argc, char **argv) {
    ui_window_t win = ui_window_create("Markdown Viewer", 150, 180, win_w, win_h);
    if (!win) return 1;

    ui_window_set_resizable(win, true);

    md_clear_all();
    if (argc > 1) {
        markdown_open_file(argv[1]);
    }

    gui_event_t ev;
    bool needs_repaint = false;
    while (1) {
        while (ui_get_event(win, &ev)) {
            if (ev.type == 11) { // GUI_EVENT_RESIZE
                win_w = ev.arg1;
                win_h = ev.arg2;
                needs_repaint = true;
            } else if (ev.type == GUI_EVENT_PAINT) {
                needs_repaint = true;
            } else if (ev.type == GUI_EVENT_CLICK) {
                md_handle_click(ev.arg1, ev.arg2);
                needs_repaint = true;
            } else if (ev.type == GUI_EVENT_KEY) {
                md_handle_key((char)ev.arg1, true);
                needs_repaint = true;
            } else if (ev.type == GUI_EVENT_KEYUP) {
                md_handle_key((char)ev.arg1, false);
            } else if (ev.type == GUI_EVENT_CLOSE) {
                sys_exit(0);
            }
        }
        
        if (needs_repaint) {
            md_paint(win);
            ui_mark_dirty(win, 0, 0, win_w, win_h);
            needs_repaint = false;
        } else {
            sleep(10);
        }
    }
    return 0;
}
