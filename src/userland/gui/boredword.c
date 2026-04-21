// Copyright (c) 2023-2026 Chris (boreddevnl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
// BOREDOS_APP_DESC: Document and PDF viewer/editor.
// BOREDOS_APP_ICONS: /Library/images/icons/colloid/libreoffice-writer.png;/Library/images/icons/colloid/text-editor.png
#include "libc/syscall.h"
#include "libc/libui.h"
#include "libc/stdlib.h"
#include "../../wm/libwidget.h"
#include <stddef.h>

#define COLOR_DARK_PANEL  0xFF202020
#define COLOR_DARK_BG     0xFF121212
#define COLOR_DARK_TEXT   0xFFE0E0E0
#define COLOR_DARK_TITLEBAR 0xFF303030
#define COLOR_BLACK       0xFF000000
#define COLOR_BLUE        0xFF4A90E2
#define COLOR_WHITE       0xFFFFFFFF
#define COLOR_TOOLBAR_BTN 0xFF404040
#define COLOR_TOOLBAR_BTN_ACTIVE 0xFF707070

#define MAX_FONTS 16
static char font_names[MAX_FONTS][64];
static int font_count = 0;
static int current_font_idx = 0;
static float current_font_size = 15.0f;
static uint32_t current_text_color = COLOR_BLACK;

static int active_dialog = 0;
static char dialog_input[256];
static int dialog_input_len = 0;
static int active_dropdown = 0;

static uint32_t const palette[] = { COLOR_BLACK, 0xFFE74C3C, 0xFF3498DB, 0xFF2ECC71, 0xFF95A5A6 };

static _Bool is_bold = 0;
static _Bool is_italic = 0;
static _Bool is_underline = 0;
static int align_mode = 0;
static float line_spacing = 1.0f;

static _Bool is_dragging = 0;
static _Bool selection_started = 0;
static int sel_start_para = -1, sel_start_run = -1, sel_start_pos = -1;
static int sel_end_para = -1, sel_end_run = -1, sel_end_pos = -1;

static int current_page_size = 0; // 0=A4, 1=A3, 2=A2
static const int page_widths[] = { 595, 842, 1191 };
static const int page_heights[] = { 842, 1191, 1684 };

#define MAX_PARAGRAPHS 256
#define MAX_RUNS_PER_PARAGRAPH 64
#define MAX_RUN_TEXT 128

typedef struct {
    char text[MAX_RUN_TEXT];
    int len;
    _Bool bold;
    _Bool italic;
    _Bool underline;
    int font_idx;
    float font_size;
    uint32_t color;
} TextRun;

typedef struct {
    TextRun runs[MAX_RUNS_PER_PARAGRAPH];
    int run_count;
    int align;
    float spacing;
} Paragraph;

#define MAX_UNDO_STATES 10
typedef struct {
    Paragraph paragraphs[MAX_PARAGRAPHS];
    int para_count;
    int cursor_para;
    int cursor_run;
    int cursor_pos;
} UndoState;

static UndoState undo_stack[MAX_UNDO_STATES];
static int undo_head = 0;
static int undo_tail = 0;

static Paragraph paragraphs[MAX_PARAGRAPHS];
static int para_count = 1;
static int cursor_para = 0;
static int cursor_run = 0;
static int cursor_pos = 0;

static char open_filename[256] = "";
static _Bool file_modified = 0;
static int scroll_y = 0;

static widget_scrollbar_t doc_scrollbar;

static void word_draw_rect(void *user_data, int x, int y, int w, int h, uint32_t color) {
    ui_draw_rect((ui_window_t)user_data, x, y, w, h, color);
}
static void word_draw_rounded_rect_filled(void *user_data, int x, int y, int w, int h, int r, uint32_t color) {
    ui_draw_rounded_rect_filled((ui_window_t)user_data, x, y, w, h, r, color);
}
static void word_draw_string(void *user_data, int x, int y, const char *str, uint32_t color) {
    ui_draw_string((ui_window_t)user_data, x, y, str, color);
}

static widget_context_t word_ctx = {
    .user_data = 0,
    .draw_rect = word_draw_rect,
    .draw_rounded_rect_filled = word_draw_rounded_rect_filled,
    .draw_string = word_draw_string,
    .mark_dirty = NULL
};

static void word_on_scroll(void *user_data, int new_scroll_y) {
    (void)user_data;
    scroll_y = new_scroll_y;
}

static _Bool is_in_selection(int p, int r, int c);

static int win_w = 800;
static int win_h = 600;

static size_t string_len(const char *str) {
    size_t l = 0;
    while(str[l]) l++;
    return l;
}

static void string_copy(char *dest, const char *src) {
    while(*src) *dest++ = *src++;
    *dest = 0;
}

static void load_fonts(void) {
    FAT32_FileInfo entries[MAX_FONTS];
    int count = sys_list("/Library/Fonts", entries, MAX_FONTS);
    font_count = 0;
    for(int i = 0; i < count; i++) {
        if (!entries[i].is_directory) {
            int len = string_len(entries[i].name);
            if (len > 4 && 
                entries[i].name[len-4] == '.' && 
                entries[i].name[len-3] == 't' && 
                entries[i].name[len-2] == 't' && 
                entries[i].name[len-1] == 'f') {
                string_copy(font_names[font_count], entries[i].name);
                font_count++;
                if (font_count >= MAX_FONTS) break;
            }
        }
    }
    if (font_count == 0) {
        string_copy(font_names[0], "firamono.ttf");
        font_count = 1;
    }
}

static int last_set_font_idx = -1;

static void set_active_font(ui_window_t win, int idx) {
    if (idx < 0 || idx >= font_count) return;
    if (idx != last_set_font_idx) {
        char full_path[128];
        string_copy(full_path, "/Library/Fonts/");
        char *d = full_path + string_len(full_path);
        string_copy(d, font_names[idx]);
        ui_set_font(win, full_path);
        last_set_font_idx = idx;
    }
}

static void ensure_cursor_visible(ui_window_t win);

static void save_undo_state(void) {
    UndoState *s = &undo_stack[undo_head];
    s->para_count = para_count;
    s->cursor_para = cursor_para;
    s->cursor_run = cursor_run;
    s->cursor_pos = cursor_pos;
    for(int i=0; i<para_count; i++) {
        s->paragraphs[i] = paragraphs[i];
    }
    undo_head = (undo_head + 1) % MAX_UNDO_STATES;
    if (undo_head == undo_tail) {
        undo_tail = (undo_tail + 1) % MAX_UNDO_STATES;
    }
}

static void perform_undo(void) {
    if (undo_head == undo_tail) return;
    undo_head = (undo_head - 1 + MAX_UNDO_STATES) % MAX_UNDO_STATES;
    UndoState *s = &undo_stack[undo_head];
    
    para_count = s->para_count;
    cursor_para = s->cursor_para;
    cursor_run = s->cursor_run;
    cursor_pos = s->cursor_pos;
    for(int i=0; i<para_count; i++) {
        paragraphs[i] = s->paragraphs[i];
    }
}

static void init_doc(void) {
    para_count = 1;
    cursor_para = 0;
    cursor_run = 0;
    cursor_pos = 0;
    scroll_y = 0;
    
    for (int i=0; i<MAX_PARAGRAPHS; i++) {
        paragraphs[i].run_count = 0;
        paragraphs[i].align = align_mode;
        paragraphs[i].spacing = line_spacing;
    }
    
    paragraphs[0].run_count = 1;
    TextRun *r = &paragraphs[0].runs[0];
    r->len = 0;
    r->text[0] = 0;
    r->bold = is_bold;
    r->italic = is_italic;
    r->underline = is_underline;
    r->font_idx = current_font_idx;
    r->font_size = current_font_size;
    r->color = current_text_color;
}

static void update_formatting_state(void) {
    if (cursor_para != -1 && cursor_run != -1) {
        if (cursor_para < para_count && cursor_run < paragraphs[cursor_para].run_count) {
            TextRun *r = &paragraphs[cursor_para].runs[cursor_run];
            is_bold = r->bold;
            is_italic = r->italic;
            is_underline = r->underline;
            current_font_idx = r->font_idx;
            current_font_size = r->font_size;
            current_text_color = r->color;
            align_mode = paragraphs[cursor_para].align;
        }
    }
}

static void handle_arrows(ui_window_t win, char c) {
    if (c == 17) {
        if (cursor_para > 0) {
            cursor_para--;
            cursor_run = paragraphs[cursor_para].run_count - 1;
            if (cursor_run < 0) cursor_run = 0;
            cursor_pos = paragraphs[cursor_para].runs[cursor_run].len;
        }
    } else if (c == 18) {
        if (cursor_para < para_count - 1) {
            cursor_para++;
            cursor_run = 0;
            cursor_pos = 0;
        }
    } else if (c == 19) {
        if (cursor_pos > 0) {
            cursor_pos--;
        } else if (cursor_run > 0) {
            cursor_run--;
            cursor_pos = paragraphs[cursor_para].runs[cursor_run].len;
        } else if (cursor_para > 0) {
            cursor_para--;
            cursor_run = paragraphs[cursor_para].run_count - 1;
            if (cursor_run < 0) cursor_run = 0;
            cursor_pos = paragraphs[cursor_para].runs[cursor_run].len;
        }
    } else if (c == 20) {
        if (cursor_pos < paragraphs[cursor_para].runs[cursor_run].len) {
            cursor_pos++;
        } else if (cursor_run < paragraphs[cursor_para].run_count - 1) {
            cursor_run++;
            cursor_pos = 0;
        } else if (cursor_para < para_count - 1) {
            cursor_para++;
            cursor_run = 0;
            cursor_pos = 0;
        }
    }
    update_formatting_state();
    ensure_cursor_visible(win);
}

static void split_run(int p_idx, int r_idx, int pos) {
    Paragraph *p = &paragraphs[p_idx];
    if (pos <= 0 || pos >= p->runs[r_idx].len) return;
    if (p->run_count >= MAX_RUNS_PER_PARAGRAPH) return;
    
    for (int i = p->run_count; i > r_idx + 1; i--) {
        p->runs[i] = p->runs[i-1];
    }
    p->run_count++;
    
    TextRun *r1 = &p->runs[r_idx];
    TextRun *r2 = &p->runs[r_idx+1];
    
    r2->bold = r1->bold;
    r2->italic = r1->italic;
    r2->underline = r1->underline;
    r2->font_idx = r1->font_idx;
    r2->font_size = r1->font_size;
    r2->color = r1->color;
    
    r2->len = r1->len - pos;
    for(int i=0; i<r2->len; i++) r2->text[i] = r1->text[pos + i];
    r2->text[r2->len] = 0;
    
    r1->len = pos;
    r1->text[pos] = 0;
}

static void delete_selection(void) {
    if (sel_start_para == -1 || sel_end_para == -1) return;
    
    int s_p = sel_start_para, s_r = sel_start_run, s_c = sel_start_pos;
    int e_p = sel_end_para, e_r = sel_end_run, e_c = sel_end_pos;
    
    if (e_p < s_p || (e_p == s_p && e_r < s_r) || (e_p == s_p && e_r == s_r && e_c < s_c)) {
        s_p = sel_end_para; s_r = sel_end_run; s_c = sel_end_pos;
        e_p = sel_start_para; e_r = sel_start_run; e_c = sel_start_pos;
    }
    
    if (s_p == e_p && s_r == e_r && s_c == e_c) {
        sel_start_para = -1; sel_end_para = -1;
        return;
    }

    save_undo_state();
    
    split_run(e_p, e_r, e_c);
    split_run(s_p, s_r, s_c);
    
    if (s_c > 0) s_r++;
    if (s_p == e_p && e_r >= s_r) e_r++;
    
    for (int p = s_p; p <= e_p; p++) {
        Paragraph *para = &paragraphs[p];
        int start_r = (p == s_p) ? s_r : 0;
        int end_r = (p == e_p) ? e_r - 1 : para->run_count - 1;
        
        for (int r = start_r; r <= end_r; r++) {
            if (r >= para->run_count) break;
            para->runs[r].len = 0;
        }
    }
    
    cursor_para = s_p;
    cursor_run = s_r;
    cursor_pos = 0;
    
    
    sel_start_para = -1; sel_end_para = -1;
    file_modified = 1;
}

static void insert_char(ui_window_t win, char c) {
    _Bool has_selection = 0;
    if (sel_start_para != -1 && sel_end_para != -1) {
        if (!(sel_start_para == sel_end_para && sel_start_run == sel_end_run && sel_start_pos == sel_end_pos)) {
            has_selection = 1;
        }
    }

    if (has_selection) {
        delete_selection();
        if (c == '\b') return;
    } else {
        if (sel_start_para != -1) {
            sel_start_para = -1;
            sel_end_para = -1;
        }
    }

    if (c < 32 && c != '\n' && c != '\b') {
        if (c >= 17 && c <= 20) {
            handle_arrows(win, c);
        }
        return;
    }

    if (c == '\b') {
        save_undo_state();
        if (cursor_pos > 0) {
            TextRun *r = &paragraphs[cursor_para].runs[cursor_run];
            for(int i=cursor_pos-1; i<r->len; i++) r->text[i] = r->text[i+1];
            r->len--;
            cursor_pos--;
            
            if (r->len == 0 && paragraphs[cursor_para].run_count > 1) {
                for(int i = cursor_run; i < paragraphs[cursor_para].run_count - 1; i++) {
                    paragraphs[cursor_para].runs[i] = paragraphs[cursor_para].runs[i+1];
                }
                paragraphs[cursor_para].run_count--;
                if (cursor_run >= paragraphs[cursor_para].run_count) {
                    cursor_run = paragraphs[cursor_para].run_count - 1;
                }
                cursor_pos = paragraphs[cursor_para].runs[cursor_run].len;
            }
        } else if (cursor_run > 0) {
            if (paragraphs[cursor_para].runs[cursor_run].len == 0 && paragraphs[cursor_para].run_count > 1) {
                for(int i = cursor_run; i < paragraphs[cursor_para].run_count - 1; i++) {
                    paragraphs[cursor_para].runs[i] = paragraphs[cursor_para].runs[i+1];
                }
                paragraphs[cursor_para].run_count--;
            }
            cursor_run--;
            if (cursor_run < 0) cursor_run = 0;
            TextRun *r = &paragraphs[cursor_para].runs[cursor_run];
            if (r->len > 0) {
                r->len--;
                cursor_pos = r->len;
                
                if (r->len == 0 && paragraphs[cursor_para].run_count > 1) {
                    for(int i = cursor_run; i < paragraphs[cursor_para].run_count - 1; i++) {
                        paragraphs[cursor_para].runs[i] = paragraphs[cursor_para].runs[i+1];
                    }
                    paragraphs[cursor_para].run_count--;
                    if (cursor_run >= paragraphs[cursor_para].run_count) cursor_run = paragraphs[cursor_para].run_count - 1;
                    if (cursor_run < 0) cursor_run = 0;
                    cursor_pos = paragraphs[cursor_para].runs[cursor_run].len;
                }
            } else {
                cursor_pos = 0;
            }
        } else if (cursor_para > 0) {
            Paragraph *prev = &paragraphs[cursor_para - 1];
            Paragraph *curr = &paragraphs[cursor_para];
            
            cursor_para--;
            cursor_run = prev->run_count - 1;
            if (cursor_run < 0) cursor_run = 0;
            TextRun *r = &prev->runs[cursor_run];
            cursor_pos = r->len;
            
            for (int i = 0; i < curr->run_count; i++) {
                if (curr->runs[i].len == 0 && i == 0 && curr->run_count == 1) continue;
                if (prev->run_count < MAX_RUNS_PER_PARAGRAPH) {
                    prev->runs[prev->run_count] = curr->runs[i];
                    prev->run_count++;
                }
            }
            
            for(int i=cursor_para+1; i<para_count-1; i++) paragraphs[i] = paragraphs[i+1];
            para_count--;
            
            }
        file_modified = 1;
        ensure_cursor_visible(win);
        return;
    }

    if (c == '\n') {
        save_undo_state();
        if (para_count >= MAX_PARAGRAPHS) return;
        for(int i=para_count; i>cursor_para+1; i--) paragraphs[i] = paragraphs[i-1];
        para_count++;
        
        Paragraph *next = &paragraphs[cursor_para+1];
        next->align = align_mode;
        next->spacing = line_spacing;
        
        next->run_count = 1;
        TextRun *nr = &next->runs[0];
        nr->len = 0;
        nr->text[0] = 0;
        nr->bold = is_bold;
        nr->italic = is_italic;
        nr->underline = is_underline;
        nr->font_idx = current_font_idx;
        nr->font_size = current_font_size;
        nr->color = current_text_color;
        
        cursor_para++;
        cursor_run = 0;
        cursor_pos = 0;
        file_modified = 1;
        ensure_cursor_visible(win);
        return;
    }

    Paragraph *p = &paragraphs[cursor_para];
    if (p->run_count == 0) p->run_count = 1;
    TextRun *r = &p->runs[cursor_run];
    
    if (r->bold != is_bold || r->italic != is_italic || r->underline != is_underline || 
        r->font_idx != current_font_idx || r->font_size != current_font_size || r->color != current_text_color) {
        
        if (cursor_pos > 0 && cursor_pos < r->len) {
            split_run(cursor_para, cursor_run, cursor_pos);
        }
        
        if (cursor_pos == 0 && r->len > 0) {
            if (p->run_count < MAX_RUNS_PER_PARAGRAPH) {
                for(int i = p->run_count; i > cursor_run; i--) p->runs[i] = p->runs[i-1];
                p->run_count++;
                r = &p->runs[cursor_run];
                r->len = 0; r->text[0] = 0;
            }
        } else if (cursor_pos == r->len && r->len > 0) {
            if (p->run_count < MAX_RUNS_PER_PARAGRAPH) {
                cursor_run++;
                for(int i = p->run_count; i > cursor_run; i--) p->runs[i] = p->runs[i-1];
                p->run_count++;
                r = &p->runs[cursor_run];
                r->len = 0; r->text[0] = 0;
                cursor_pos = 0;
            }
        }
        
        r->bold = is_bold;
        r->italic = is_italic;
        r->underline = is_underline;
        r->font_idx = current_font_idx;
        r->font_size = current_font_size;
        r->color = current_text_color;
    }
    
    if (c == ' ') save_undo_state();
    
    if (r->len < MAX_RUN_TEXT - 1) {
        for(int i=r->len; i>cursor_pos; i--) r->text[i] = r->text[i-1];
        r->text[cursor_pos] = c;
        r->len++;
        cursor_pos++;
        file_modified = 1;
    }
    ensure_cursor_visible(win);
}

static bool str_contains(const char *haystack, const char *needle) {
    if (!haystack || !needle) return false;
    int hlen = string_len(haystack);
    int nlen = string_len(needle);
    for(int i=0; i<=hlen-nlen; i++) {
        bool match = true;
        for(int j=0; j<nlen; j++) {
            if (haystack[i+j] != needle[j]) { match = false; break; }
        }
        if (match) return true;
    }
    return false;
}

static void append_pdf_float(char *buf, int *len, int val) {
    int frac = (val * 1000) / 255;
    if (frac == 1000) {
        buf[(*len)++] = '1'; buf[(*len)++] = '.'; buf[(*len)++] = '0'; buf[(*len)++] = ' ';
        return;
    }
    buf[(*len)++] = '0'; buf[(*len)++] = '.';
    if (frac < 100) buf[(*len)++] = '0';
    if (frac < 10) buf[(*len)++] = '0';
    char fbuf[16]; itoa(frac, fbuf);
    int p = 0; while(fbuf[p]) buf[(*len)++] = fbuf[p++];
    buf[(*len)++] = ' ';
}

static void get_page_size(int *pw, int *ph) {
    if (current_page_size >= 0 && current_page_size <= 2) {
        *pw = page_widths[current_page_size];
        *ph = page_heights[current_page_size];
    } else {
        *pw = 595; *ph = 842;
    }
}

static void export_pdf(void) {
    char name[256];
    if (open_filename[0] == 0) string_copy(name, "document.pdf");
    else string_copy(name, open_filename);
    
    int fd = sys_open(name, "w");
    if (fd < 0) return;
    
    int offset = 0;
    int xref[1024]; // Increase xref size to handle more objects
    int obj_count = 1;

    #define WRITE_STR(s) do { sys_write_fs(fd, s, string_len(s)); offset += string_len(s); } while(0)
    
    WRITE_STR("%PDF-1.4\n");
    
    int catalog_obj = obj_count++;
    xref[catalog_obj] = offset;
    WRITE_STR("1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n");
    
    int pages_obj = obj_count++;
    int page_obj_ids[256];
    int total_pdf_pages = 0;
    
    int resources_obj = obj_count++;
    xref[resources_obj] = offset;
    WRITE_STR("3 0 obj\n<< /ProcSet [/PDF /Text] /Font << ");
    for(int i=1; i<=12; i++) {
        WRITE_STR("/F");
        char f_n[16]; itoa(i, f_n); WRITE_STR(f_n);
        WRITE_STR(" ");
        char fo_n[16]; itoa(3+i, fo_n); WRITE_STR(fo_n);
        WRITE_STR(" 0 R ");
    }
    WRITE_STR(">> >>\nendobj\n");
    
    // Write 12 fonts
    const char *base_fonts[12] = {
        "Helvetica", "Helvetica-Bold", "Helvetica-Oblique", "Helvetica-BoldOblique",
        "Times-Roman", "Times-Bold", "Times-Italic", "Times-BoldItalic",
        "Courier", "Courier-Bold", "Courier-Oblique", "Courier-BoldOblique"
    };
    for(int i=0; i<12; i++) {
        xref[obj_count++] = offset; // 4 to 15
        char num[16];
        itoa(obj_count - 1, num);
        WRITE_STR(num);
        WRITE_STR(" 0 obj\n<< /Type /Font /Subtype /Type1 /BaseFont /");
        WRITE_STR(base_fonts[i]);
        WRITE_STR(" >>\nendobj\n");
    }

    static char stream[65536];
    stream[0] = 0;
    int slen = 0;
    
    #define S_WRITE(s) do { string_copy(stream + slen, s); slen += string_len(s); } while(0)
    
    int pw, ph;
    get_page_size(&pw, &ph);

    // Initial page
    page_obj_ids[total_pdf_pages++] = obj_count;
    xref[obj_count++] = offset; // Page obj
    int current_page_obj = obj_count - 1;
    int current_contents_obj = obj_count;
    
    // We will write the Page object:
    char num1[16]; itoa(current_page_obj, num1);
    WRITE_STR(num1); WRITE_STR(" 0 obj\n<< /Type /Page /Parent 2 0 R /MediaBox [0 0 ");
    char num2[16]; itoa(pw, num2); WRITE_STR(num2); WRITE_STR(" ");
    char num3[16]; itoa(ph, num3); WRITE_STR(num3); WRITE_STR("] /Contents ");
    char num4[16]; itoa(current_contents_obj, num4); WRITE_STR(num4); WRITE_STR(" 0 R /Resources 3 0 R >>\nendobj\n");

    // Starting Contents
    xref[obj_count++] = offset; // Contents obj
    current_contents_obj = obj_count - 1;
    char cont_buf[64];
    itoa(current_contents_obj, cont_buf);
    WRITE_STR(cont_buf); WRITE_STR(" 0 obj\n");
    
    // We will leave length empty and calculate exactly later
    int length_placeholder_offset = offset;
    WRITE_STR("<< /Length 00000000 >>\nstream\n"); // 8 chars for length
    
    S_WRITE("BT\n");
    
    float cur_y = (float)ph - 42.0f; // 42 is top margin
    float bottom_margin = 40.0f;
    
    for(int p=0; p<para_count; p++) {
        Paragraph *para = &paragraphs[p];
        
        int tw = 0;
        for(int r=0; r<para->run_count; r++) {
            TextRun *run_m = &para->runs[r];
            if (run_m->len > 0) {
                const char *ttf_m = font_names[run_m->font_idx];
                float char_w = (str_contains(ttf_m, "mono") || str_contains(ttf_m, "fira")) ? (run_m->font_size * 0.6f) : (run_m->font_size * 0.45f);
                tw += (int)(char_w * run_m->len);
                if (run_m->bold) tw += run_m->len;
            }
        }
        
        int px = 20;
        if (para->align == 1) px = (pw - tw) / 2;
        else if (para->align == 2) px = pw - 20 - tw;
        
        float max_lh = 15.0f;
        for(int r=0; r<para->run_count; r++) {
            if (para->runs[r].font_size > max_lh) max_lh = para->runs[r].font_size;
        }

        if (cur_y - max_lh < bottom_margin) {
            S_WRITE("ET\n");
            sys_write_fs(fd, stream, slen); offset += slen;
            sys_write_fs(fd, "\nendstream\nendobj\n", 18); offset += 18;
            
            // Patch length
            int current_offset = sys_seek(fd, 0, 1);
            sys_seek(fd, length_placeholder_offset + 12, 0); // "12" is the offset to '00000000' part of "/Length 00000000"
            char num_len[16]; itoa(slen, num_len);
            int pad = 8 - (int)string_len(num_len);
            for(int k=0; k<pad; k++) sys_write_fs(fd, "0", 1);
            sys_write_fs(fd, num_len, string_len(num_len));
            sys_seek(fd, current_offset, 0);

            // New page reset
            slen = 0;
            cur_y = (float)ph - 42.0f;
            
            page_obj_ids[total_pdf_pages++] = obj_count;
            xref[obj_count++] = offset; // New Page obj
            current_page_obj = obj_count - 1;
            current_contents_obj = obj_count;
            
            char p_num[16]; itoa(current_page_obj, p_num);
            WRITE_STR(p_num); WRITE_STR(" 0 obj\n<< /Type /Page /Parent 2 0 R /MediaBox [0 0 ");
            char numw[16]; itoa(pw, numw); WRITE_STR(numw); WRITE_STR(" ");
            char numh[16]; itoa(ph, numh); WRITE_STR(numh); WRITE_STR("] /Contents ");
            char c_num[16]; itoa(current_contents_obj, c_num); WRITE_STR(c_num); WRITE_STR(" 0 R /Resources 3 0 R >>\nendobj\n");

            xref[obj_count++] = offset; // New Contents obj
            current_contents_obj = obj_count - 1;
            char n_cont_buf[64]; itoa(current_contents_obj, n_cont_buf);
            WRITE_STR(n_cont_buf); WRITE_STR(" 0 obj\n");
            
            length_placeholder_offset = offset;
            WRITE_STR("<< /Length 00000000 >>\nstream\n");
            
            S_WRITE("BT\n");
        }

        char tm_buf[64];
        int tlen = 0;
        string_copy(tm_buf, "1 0 0 1 "); tlen += 8;
        char num[16];
        itoa(px, num);
        string_copy(tm_buf + tlen, num); tlen += string_len(num);
        tm_buf[tlen++] = ' ';
        itoa((int)cur_y, num);
        string_copy(tm_buf + tlen, num); tlen += string_len(num);
        string_copy(tm_buf + tlen, " Tm\n"); tlen += 4;
        tm_buf[tlen] = 0;
        
        S_WRITE(tm_buf);
        
        char align_cmt[32];
        string_copy(align_cmt, "%ALIGN_0\n");
        align_cmt[7] = '0' + para->align;
        S_WRITE(align_cmt);
        
        for(int r=0; r<para->run_count; r++) {
            TextRun *run = &para->runs[r];
            if (run->len > 0) {
                int base_idx = 0;
                const char *ttf = font_names[run->font_idx];
                if (str_contains(ttf, "mono") || str_contains(ttf, "console") || str_contains(ttf, "fira")) base_idx = 8;
                else if (str_contains(ttf, "serif") || str_contains(ttf, "times") || str_contains(ttf, "georgia")) base_idx = 4;
                
                int style = 0;
                if (run->bold && run->italic) style = 3;
                else if (run->italic) style = 2;
                else if (run->bold) style = 1;
                
                int fkey = base_idx + style + 1;
                
                char fbuf[128];
                int flen = 0;
                string_copy(fbuf, "/F"); flen = 2;
                char keynum[16]; itoa(fkey, keynum);
                string_copy(fbuf + flen, keynum); flen += string_len(keynum);
                fbuf[flen++] = ' ';
                char sizenum[16]; itoa((int)run->font_size, sizenum);
                string_copy(fbuf + flen, sizenum); flen += string_len(sizenum);
                string_copy(fbuf + flen, " Tf\n"); flen += 4;
                
                int rr = (run->color >> 16) & 0xFF;
                int gg = (run->color >> 8) & 0xFF;
                int bb = run->color & 0xFF;
                append_pdf_float(fbuf, &flen, rr);
                append_pdf_float(fbuf, &flen, gg);
                append_pdf_float(fbuf, &flen, bb);
                fbuf[flen++] = 'r'; fbuf[flen++] = 'g'; fbuf[flen++] = '\n';
                
                string_copy(fbuf + flen, "%FMT_"); flen += 5;
                fbuf[flen++] = run->bold ? '1' : '0';
                fbuf[flen++] = run->italic ? '1' : '0';
                fbuf[flen++] = run->underline ? '1' : '0';
                uint32_t c = run->color;
                for(int i=7; i>=0; i--) { int nibble = c & 0xF; fbuf[flen+i] = (nibble < 10) ? ('0' + nibble) : ('A' + (nibble - 10)); c >>= 4; }
                flen += 8;
                for(int i=0; i<(int)string_len(sizenum); i++) fbuf[flen++] = sizenum[i];
                fbuf[flen++] = '\n';
                fbuf[flen] = 0;
                
                S_WRITE(fbuf);
                
                S_WRITE("(");
                for(int i=0; i<run->len; i++) {
                    if (run->text[i] == '(' || run->text[i] == ')' || run->text[i] == '\\') {
                        stream[slen++] = '\\';
                    }
                    stream[slen++] = run->text[i];
                }
                stream[slen] = 0;
                S_WRITE(") Tj\n");
            }
        }
        
        cur_y -= (max_lh + 5.0f);
    }
    S_WRITE("ET\n");

    // Finalize the last pending page stream
    sys_write_fs(fd, stream, slen); offset += slen;
    sys_write_fs(fd, "\nendstream\nendobj\n", 18); offset += 18;
    
    // Patch length of last page
    int current_offset = sys_seek(fd, 0, 1);
    sys_seek(fd, length_placeholder_offset + 12, 0);
    char num_len[16]; itoa(slen, num_len);
    int pad = 8 - (int)string_len(num_len);
    for(int k=0; k<pad; k++) sys_write_fs(fd, "0", 1);
    sys_write_fs(fd, num_len, string_len(num_len));
    sys_seek(fd, current_offset, 0);
    
    int total_objects = obj_count;
    
    // Now write the Pages object since we know all page objects
    xref[2] = offset; // Pages object is always 2 0 obj
    WRITE_STR("2 0 obj\n<< /Type /Pages /Kids [");
    for(int i=0; i<total_pdf_pages; i++) {
        char pnum[16]; itoa(page_obj_ids[i], pnum);
        WRITE_STR(pnum); WRITE_STR(" 0 R ");
    }
    WRITE_STR("] /Count ");
    char cnum[16]; itoa(total_pdf_pages, cnum);
    WRITE_STR(cnum); WRITE_STR(" >>\nendobj\n");


    int final_xref_offset = offset;
    WRITE_STR("xref\n0 ");
    char num_total_obj[16];
    itoa(total_objects, num_total_obj); WRITE_STR(num_total_obj); WRITE_STR("\n0000000000 65535 f \n");
    for(int i=1; i<total_objects; i++) {
        char entry[32];
        int w = 0;
        char num_str[16];
        itoa(xref[i], num_str);
        int pad = 10 - (int)string_len(num_str);
        for(int p=0; p<pad; p++) entry[w++] = '0';
        for(int p=0; p<(int)string_len(num_str); p++) entry[w++] = num_str[p];
        string_copy(entry + w, " 00000 n \n");
        WRITE_STR(entry);
    }
    
    WRITE_STR("trailer\n<< /Size ");
    char num_trailer[16];
    itoa(total_objects, num_trailer); WRITE_STR(num_trailer);
    WRITE_STR(" /Root 1 0 R >>\nstartxref\n");
    char xnum[16];
    itoa(final_xref_offset, xnum);
    WRITE_STR(xnum);
    WRITE_STR("\n%%EOF\n");
    
    sys_close(fd);
    file_modified = 0;
}

static void load_file(ui_window_t win, const char* path) {
    int fd = sys_open(path, "r");
    if (fd < 0) return;
    int size = sys_size(fd);
    if(size <= 0) { sys_close(fd); return; }
    
    if (size > 65535) size = 65535;
    
    static char buf[65536];
    sys_read(fd, buf, size);
    sys_close(fd);
    buf[size] = 0;
    
    init_doc();
    string_copy(open_filename, path);
    cursor_para = 0; cursor_run = 0; cursor_pos = 0;
    para_count = 1; paragraphs[0].run_count = 0;
    
    if (buf[0] == '%' && buf[1] == 'P' && buf[2] == 'D' && buf[3] == 'F') {
        int i=0;
        
        is_bold = 0; is_italic = 0; is_underline = 0;
        current_font_size = 15.0f; current_text_color = COLOR_BLACK; current_font_idx = 0;
        
        while(i < size) {
             if(buf[i] == '%' && i+7 < size && buf[i+1] == 'A' && buf[i+2] == 'L' && buf[i+3] == 'I' && buf[i+4] == 'G' && buf[i+5] == 'N' && buf[i+6] == '_') {
                 align_mode = buf[i+7] - '0';
                 paragraphs[cursor_para].align = align_mode;
                 i += 8;
                 while(i < size && buf[i] != '\n') i++;
             }
             else if (buf[i] == '/' && i+1 < size && buf[i+1] == 'F') {
                 int start = i;
                 i += 2;
                 int fkey = 0; while(i < size && buf[i] >= '0' && buf[i] <= '9') { fkey = fkey * 10 + (buf[i++] - '0'); }
                 while(i < size && buf[i] == ' ') i++;
                 int fsize = 0; while(i < size && buf[i] >= '0' && buf[i] <= '9') { fsize = fsize * 10 + (buf[i++] - '0'); }
                 while(i < size && buf[i] == ' ') i++;
                 if (i + 1 < size && buf[i] == 'T' && buf[i+1] == 'f') {
                     if (fsize >= 8) current_font_size = (float)fsize;
                     if (fkey >= 1 && fkey <= 12) {
                         int base = (fkey - 1) / 4;
                         int style = (fkey - 1) % 4;
                         is_bold = (style == 1 || style == 3);
                         is_italic = (style == 2 || style == 3);
                         const char *target = (base == 2) ? "mono" : (base == 1 ? "serif" : "sans");
                         for(int f=0; f<font_count; f++) {
                             if (str_contains(font_names[f], target)) { current_font_idx = f; break; }
                         }
                     }
                     i += 2;
                 } else { i = start + 1; }
             }
             else if (buf[i] == 'r' && i+1 < size && buf[i+1] == 'g') {

                 i += 2;
             }
             else if(buf[i] == '%' && i+5 < size && buf[i+1] == 'F' && buf[i+2] == 'M' && buf[i+3] == 'T' && buf[i+4] == '_') {
                 i += 5;
                 if (i+11 < size) {
                     is_bold = (buf[i++] == '1');
                     is_italic = (buf[i++] == '1');
                     is_underline = (buf[i++] == '1');
                     
                     uint32_t c = 0;
                     for(int k=0; k<8; k++) {
                         char hex = buf[i++];
                         int val = 0;
                         if (hex >= '0' && hex <= '9') val = hex - '0';
                         else if (hex >= 'A' && hex <= 'F') val = hex - 'A' + 10;
                         else if (hex >= 'a' && hex <= 'f') val = hex - 'a' + 10;
                         c = (c << 4) | val;
                     }
                     current_text_color = c;
                     
                     int fsize = 0;
                     while(i < size && buf[i] != '\n' && buf[i] >= '0' && buf[i] <= '9') {
                         fsize = fsize * 10 + (buf[i++] - '0');
                     }
                     if (fsize >= 8) current_font_size = (float)fsize;
                 }
                 while(i < size && buf[i] != '\n') i++;
             }
             else if(buf[i] == '(') {
                 i++;
                 while(i < size && buf[i] != ')') {
                     if (buf[i] == '\\' && i+1 < size) i++;
                     insert_char(win, buf[i]);
                     i++;
                 }
             }
             else if ((buf[i] == 'T' && buf[i+1] == 'd') || (buf[i] == 'T' && buf[i+1] == 'm')) {
                 insert_char(win, '\n');
                 i += 2;
             }
             else {
                 i++;
             }
        }
    } else {
        for(int i=0; i<size; i++) {
            if (buf[i] != '\r') insert_char(win, buf[i]);
        }
    }
    file_modified = 0;
}

static void draw_btn_icon(ui_window_t win, int x, int y, int w, int h, int icon_type, _Bool active) {
    uint32_t color = active ? COLOR_TOOLBAR_BTN_ACTIVE : COLOR_TOOLBAR_BTN;
    ui_draw_rounded_rect_filled(win, x, y, w, h, 4, color);
    
    if (icon_type == 0) {
        ui_draw_rect(win, x+8, y+6, 2, 12, COLOR_WHITE);
        ui_draw_rect(win, x+10, y+6, 4, 2, COLOR_WHITE);
        ui_draw_rect(win, x+14, y+8, 2, 2, COLOR_WHITE);
        ui_draw_rect(win, x+10, y+11, 4, 2, COLOR_WHITE);
        ui_draw_rect(win, x+14, y+13, 2, 3, COLOR_WHITE);
        ui_draw_rect(win, x+10, y+16, 4, 2, COLOR_WHITE);
    } else if (icon_type == 1) {
        ui_draw_rect(win, x+10, y+6, 6, 2, COLOR_WHITE);
        ui_draw_rect(win, x+13, y+8, 2, 8, COLOR_WHITE);
        ui_draw_rect(win, x+8, y+16, 6, 2, COLOR_WHITE);
    } else if (icon_type == 2) {
        ui_draw_rect(win, x+8, y+6, 2, 8, COLOR_WHITE);
        ui_draw_rect(win, x+14, y+6, 2, 8, COLOR_WHITE);
        ui_draw_rect(win, x+10, y+14, 4, 2, COLOR_WHITE);
        ui_draw_rect(win, x+8, y+18, 8, 1, COLOR_WHITE);
    } else if (icon_type >= 3 && icon_type <= 6) {
        for(int i=0; i<4; i++) {
            int lw = (i%2 == 0) ? 12 : 8;
            if (icon_type == 6) lw = 12;
            int lx = x + 6;
            if (icon_type == 4) lx = x + 6 + (12-lw)/2;
            if (icon_type == 5) lx = x + 6 + (12-lw);
            ui_draw_rect(win, lx, y+6 + i*3, lw, 2, COLOR_WHITE);
        }
    } else if (icon_type == 7) {
        ui_draw_rect(win, x+6, y+11, 12, 2, COLOR_WHITE);
    } else if (icon_type == 8) {
        ui_draw_rect(win, x+6, y+11, 12, 2, COLOR_WHITE);
        ui_draw_rect(win, x+11, y+6, 2, 12, COLOR_WHITE);
    } else if (icon_type == 9) {
        ui_draw_rect(win, x+12, y+12, 6, 2, COLOR_WHITE);
        ui_draw_rect(win, x+18, y+14, 2, 4, COLOR_WHITE);
        ui_draw_rect(win, x+12, y+18, 6, 2, COLOR_WHITE);
        ui_draw_rect(win, x+10, y+10, 2, 6, COLOR_WHITE);
        ui_draw_rect(win, x+8, y+12, 2, 2, COLOR_WHITE);
    } else if (icon_type == 10) {
        int cx = x + w/2; int cy = y + h/2;
        ui_draw_rect(win, cx-6, cy-6, 12, 12, COLOR_WHITE);
        ui_draw_rect(win, cx-5, cy-5, 10, 10, COLOR_DARK_BG);
        ui_draw_rect(win, cx-4, cy-6, 8, 4, COLOR_WHITE);
        ui_draw_rect(win, cx-2, cy+2, 4, 4, COLOR_WHITE);
    }
}

static void draw_toolbar(ui_window_t win) {
    ui_draw_rect(win, 0, 0, win_w, 40, COLOR_DARK_PANEL);
    
    draw_btn_icon(win, 10, 8, 24, 24, 0, is_bold);
    draw_btn_icon(win, 40, 8, 24, 24, 1, is_italic);
    draw_btn_icon(win, 70, 8, 24, 24, 2, is_underline);
    
    ui_draw_rect(win, 100, 8, 2, 24, COLOR_DARK_BG);
    
    draw_btn_icon(win, 110, 8, 24, 24, 3, align_mode == 0);
    draw_btn_icon(win, 140, 8, 24, 24, 4, align_mode == 1);
    draw_btn_icon(win, 170, 8, 24, 24, 5, align_mode == 2);
    draw_btn_icon(win, 200, 8, 24, 24, 6, align_mode == 3);
    
    ui_draw_rect(win, 230, 8, 2, 24, COLOR_DARK_BG);
    
    ui_draw_rounded_rect_filled(win, 240, 8, 120, 24, 4, COLOR_TOOLBAR_BTN);
    ui_draw_string(win, 245, 12, font_names[current_font_idx], COLOR_WHITE);
    ui_draw_rect(win, 240+105, 18, 4, 2, COLOR_WHITE);
    
    ui_draw_rounded_rect_filled(win, 365, 8, 24, 24, 4, current_text_color);
    ui_draw_rect(win, 365, 8, 24, 24, COLOR_WHITE);
    ui_draw_rounded_rect_filled(win, 366, 9, 22, 22, 3, current_text_color);
    
    draw_btn_icon(win, 395, 8, 24, 24, 7, 0);
    
    char size_str[16];
    int isize = (int)current_font_size;
    int didx = 0;
    if(isize >= 10) { size_str[didx++] = (isize/10) + '0'; }
    size_str[didx++] = (isize%10) + '0';
    size_str[didx] = 0;
    ui_draw_string(win, 425, 12, size_str, COLOR_WHITE);
    
    draw_btn_icon(win, 445, 8, 24, 24, 8, 0);
    
    draw_btn_icon(win, 485, 8, 30, 24, 9, 0);
    
    ui_draw_rect(win, 520, 8, 2, 24, COLOR_DARK_BG);
    ui_draw_rounded_rect_filled(win, 530, 8, 50, 24, 4, COLOR_TOOLBAR_BTN);
    const char *ps_str = (current_page_size == 0) ? "A4" : (current_page_size == 1 ? "A3" : "A2");
    ui_draw_string(win, 545, 12, ps_str, COLOR_WHITE);
    ui_draw_rect(win, 530+35, 18, 4, 2, COLOR_WHITE);
    
    draw_btn_icon(win, 590, 8, 40, 24, 10, file_modified);
}

static void draw_dropdowns(ui_window_t win) {
    if (active_dropdown == 1) {
        ui_draw_rect(win, 240, 32, 120, font_count * 20, COLOR_DARK_PANEL);
        ui_draw_rect(win, 240, 32, 120, font_count * 20, COLOR_DARK_BG);
        for(int i=0; i<font_count; i++) {
            if (i == current_font_idx) {
                ui_draw_rect(win, 240, 32 + i*20, 120, 20, COLOR_TOOLBAR_BTN_ACTIVE);
            } else {
                ui_draw_rect(win, 240, 32 + i*20, 120, 20, COLOR_DARK_PANEL);
            }
            ui_draw_string(win, 245, 32 + i*20 + 4, font_names[i], COLOR_WHITE);
        }
    } else if (active_dropdown == 2) {
        int p_count = sizeof(palette)/sizeof(uint32_t);
        ui_draw_rect(win, 365, 32, 40, p_count * 20, COLOR_DARK_PANEL);
        for(int i=0; i<p_count; i++) {
            ui_draw_rect(win, 365, 32 + i*20, 40, 20, palette[i]);
            if (palette[i] == current_text_color) {
                ui_draw_rect(win, 365, 32 + i*20 + 8, 4, 4, COLOR_WHITE);
            }
        }
    } else if (active_dropdown == 3) {
        ui_draw_rect(win, 530, 32, 50, 60, COLOR_DARK_PANEL);
        const char *ps[3] = {"A4", "A3", "A2"};
        for(int i=0; i<3; i++) {
            if (i == current_page_size) {
                ui_draw_rect(win, 530, 32 + i*20, 50, 20, COLOR_TOOLBAR_BTN_ACTIVE);
            } else {
                ui_draw_rect(win, 530, 32 + i*20, 50, 20, COLOR_DARK_PANEL);
            }
            ui_draw_string(win, 545, 32 + i*20 + 4, ps[i], COLOR_WHITE);
        }
    }
}

static void draw_dialogs(ui_window_t win) {
    if (active_dialog == 1) {
        int dw = 300; int dh = 150;
        int dx = (win_w - dw)/2; int dy = (win_h - dh)/2;
        ui_draw_rounded_rect_filled(win, dx, dy, dw, dh, 6, COLOR_DARK_PANEL);
        ui_draw_string(win, dx + 10, dy + 10, "Save Document As:", COLOR_WHITE);
        
        ui_draw_rect(win, dx+10, dy+50, dw-20, 30, COLOR_DARK_BG);
        ui_draw_string_scaled(win, dx+15, dy+55, dialog_input, COLOR_WHITE, 15.0f);
        ui_draw_rect(win, dx+15 + ui_get_string_width_scaled(dialog_input, 15.0f), dy+55, 2, 15, COLOR_WHITE);
        
        ui_draw_rounded_rect_filled(win, dx + 10, dy + 100, 100, 30, 4, COLOR_TOOLBAR_BTN);
        ui_draw_string(win, dx + 35, dy + 108, "Cancel", COLOR_WHITE);
        
        ui_draw_rounded_rect_filled(win, dx + dw - 110, dy + 100, 100, 30, 4, COLOR_BLUE);
        ui_draw_string(win, dx + dw - 110 + 35, dy + 108, "Save", COLOR_WHITE);
    }
}

static void draw_document(ui_window_t win) {
    int pw, ph;
    get_page_size(&pw, &ph);
    
    int doc_view_w = win_w - 40;
    float scale = (float)doc_view_w / (float)pw;
    if (scale > 1.0f) scale = 1.0f; // Don't scale up if window is huge
    
    int page_w = (int)(pw * scale);
    int page_h = (int)(ph * scale);
    
    int doc_x = 20 + (doc_view_w - page_w) / 2;
    int doc_y = 50 - scroll_y;
    
    ui_draw_rect(win, 0, 40, win_w, win_h - 40, COLOR_DARK_BG);
    
    int cur_y = doc_y + 10;
    int current_page = 0;
    
    // Draw first page background
    int bg_y = doc_y + current_page * (page_h + 20);
    int draw_h = page_h;
    if (bg_y < 40) {
        draw_h -= (40 - bg_y);
        bg_y = 40;
    }
    if (draw_h > 0 && bg_y < win_h) {
        ui_draw_rect(win, doc_x, bg_y, page_w, draw_h, COLOR_WHITE);
    }
    
    cur_y = doc_y + current_page * (page_h + 20) + 10;
    
    for(int p=0; p<para_count; p++) {
        Paragraph *para = &paragraphs[p];
        int start_run = 0;
        int start_char = 0;
        
        while (start_run < para->run_count) {
            int line_w = 0;
            int max_h = 16;
            int end_run = start_run;
            int end_char = start_char;
            
            int r_idx = start_run;
            int c_idx = start_char;
            int last_space_run = -1;
            int last_space_char = -1;
            int last_space_w = 0;
            
            while(r_idx < para->run_count) {
                TextRun *run = &para->runs[r_idx];
                set_active_font(win, run->font_idx);
                int fh = ui_get_font_height_scaled(run->font_size);
                if (fh > max_h) max_h = fh;
                
                while(c_idx < run->len) {
                    char buf[2] = {run->text[c_idx], 0};
                    int cw = ui_get_string_width_scaled(buf, run->font_size);
                    
                    if (run->text[c_idx] == ' ') {
                        last_space_run = r_idx;
                        last_space_char = c_idx;
                        last_space_w = line_w + cw;
                    }
                    
                    if (line_w + cw > page_w - 20) {
                        break;
                    }
                    line_w += cw;
                    c_idx++;
                }
                
                if (c_idx < run->len) break;
                
                r_idx++;
                c_idx = 0;
            }
            
            if (r_idx < para->run_count || (r_idx == para->run_count - 1 && c_idx < para->runs[r_idx].len)) {
                if (last_space_run != -1 && (last_space_run > start_run || last_space_char > start_char)) {
                    end_run = last_space_run;
                    end_char = last_space_char;
                    line_w = last_space_w;
                } else {
                    end_run = r_idx;
                    end_char = c_idx;
                }
            } else {
                end_run = para->run_count;
                end_char = 0;
            }
            
            int line_h = (int)(max_h * para->spacing) + 4;
            
            if (cur_y + line_h > doc_y + current_page * (page_h + 20) + page_h - 10) {
                current_page++;
                int next_bg_y = doc_y + current_page * (page_h + 20);
                int next_draw_h = page_h;
                if (next_bg_y < 40) {
                    next_draw_h -= (40 - next_bg_y);
                    next_bg_y = 40;
                }
                if (next_draw_h > 0 && next_bg_y < win_h) {
                    ui_draw_rect(win, doc_x, next_bg_y, page_w, next_draw_h, COLOR_WHITE);
                }
                cur_y = doc_y + current_page * (page_h + 20) + 10;
            }
            
            int cur_x = doc_x + 10;
            if (para->align == 1) {
                cur_x = doc_x + 10 + (page_w - 20 - line_w) / 2;
            } else if (para->align == 2) {
                cur_x = doc_x + 10 + (page_w - 20 - line_w);
            }
            
            int d_run = start_run;
            int d_char = start_char;
            
            int line_cursor_x = -1;
            
            while(d_run < end_run || (d_run == end_run && d_char < end_char)) {
                TextRun *run = &para->runs[d_run];
                int chars_to_draw;
                if (d_run == end_run) chars_to_draw = end_char - d_char;
                else chars_to_draw = run->len - d_char;
                
                if (p == cursor_para && d_run == cursor_run && cursor_pos >= d_char && cursor_pos <= d_char + chars_to_draw) {
                    char sub[128];
                    for(int i=0; i<cursor_pos - d_char; i++) sub[i] = run->text[d_char + i];
                    sub[cursor_pos - d_char] = 0;
                    line_cursor_x = cur_x + ui_get_string_width_scaled(sub, run->font_size);
                }
                
                if (chars_to_draw > 0) {
                    set_active_font(win, run->font_idx);
                    
                    int run_h = ui_get_font_height_scaled(run->font_size);
                    int y_offset = 0;
                    if (max_h > run_h) y_offset = max_h - run_h;
                    
                    int text_y = cur_y + y_offset;
                    if (text_y + run_h > 40 && text_y < win_h) {
                        for(int i=0; i<chars_to_draw; i++) {
                            char buf[2] = {run->text[d_char + i], 0};
                            _Bool in_sel = is_in_selection(p, d_run, d_char + i);
                            uint32_t text_col = in_sel ? COLOR_WHITE : run->color;
                            int cw = ui_get_string_width_scaled(buf, run->font_size);
                            
                            if (text_y + run_h > 40) {
                                if (in_sel) {
                                    ui_draw_rect(win, cur_x, text_y + 4, cw, run_h, COLOR_BLUE);
                                }
                                
                                if (run->italic) {
                                    ui_draw_string_scaled_sloped(win, cur_x, text_y, buf, text_col, run->font_size, 0.2f);
                                    if (run->bold) ui_draw_string_scaled_sloped(win, cur_x+1, text_y, buf, text_col, run->font_size, 0.2f);
                                } else {
                                    ui_draw_string_scaled(win, cur_x, text_y, buf, text_col, run->font_size);
                                    if (run->bold) ui_draw_string_scaled(win, cur_x+1, text_y, buf, text_col, run->font_size);
                                }
                                
                                if (run->underline) {
                                    ui_draw_rect(win, cur_x, cur_y + max_h - 2, cw, 1, text_col);
                                }
                            }
                            cur_x += cw;
                        }
                    } else {
                        char buf[128];
                        for(int i=0; i<chars_to_draw; i++) buf[i] = run->text[d_char + i];
                        buf[chars_to_draw] = 0;
                        cur_x += ui_get_string_width_scaled(buf, run->font_size);
                    }
                }
                
                d_char = 0;
                d_run++;
            }
            
            if (p == cursor_para && (d_run == cursor_run || (end_run == cursor_run && end_char == cursor_pos))) {
                if (line_cursor_x == -1) line_cursor_x = cur_x;
            }
            
            if (line_cursor_x != -1 && cur_y > 40 && cur_y < win_h) {
                if (cursor_para < para_count && cursor_run < paragraphs[cursor_para].run_count) {
                    set_active_font(win, paragraphs[cursor_para].runs[cursor_run].font_idx);
                    int fh = ui_get_font_height_scaled(paragraphs[cursor_para].runs[cursor_run].font_size);
                    int c_offset = 0; if (max_h > fh) c_offset = max_h - fh;
                    ui_draw_rect(win, line_cursor_x, cur_y + c_offset, 2, fh, COLOR_BLACK);
                } else {
                    ui_draw_rect(win, line_cursor_x, cur_y, 2, max_h, COLOR_BLACK);
                }
            }
            
            cur_y += (int)(max_h * para->spacing) + 4;
            
            start_run = end_run;
            start_char = end_char;
            if (start_run < para->run_count && para->runs[start_run].text[start_char] == ' ') {
                start_char++;
                if (start_char >= para->runs[start_run].len) {
                    start_char = 0;
                    start_run++;
                }
            }
        }
    }
    
    set_active_font(win, 0);
    
    int content_h = current_page * (page_h + 20) + page_h + 20;
    doc_scrollbar.x = win_w - 12;
    doc_scrollbar.y = 40;
    doc_scrollbar.w = 12;
    doc_scrollbar.h = win_h - 40;
    doc_scrollbar.on_scroll = word_on_scroll;
    widget_scrollbar_update(&doc_scrollbar, content_h, scroll_y);
    widget_scrollbar_draw(&word_ctx, &doc_scrollbar);
}

static void ensure_cursor_visible(ui_window_t win) {
    int pw, ph;
    get_page_size(&pw, &ph);
    int doc_view_w = win_w - 40;
    float scale = (float)doc_view_w / (float)pw;
    if (scale > 1.0f) scale = 1.0f;
    int page_w = (int)(pw * scale);
    int page_h = (int)(ph * scale);
    
    int cur_y = 10;
    int current_page = 0;
    int target_y = -1;
    
    for(int p=0; p<para_count; p++) {
        Paragraph *para = &paragraphs[p];
        int start_run = 0; int start_char = 0;
        
        while (start_run < para->run_count) {
            int max_h = 16;
            int r_idx = start_run;
            int c_idx = start_char;
            int end_run = start_run; int end_char = start_char;
            int line_w = 0;
            int last_space_run = -1; int last_space_char = -1; int last_space_w = 0;
            
            while(r_idx < para->run_count) { 
                TextRun *run = &para->runs[r_idx];
                set_active_font(win, run->font_idx); 
                int fh = ui_get_font_height_scaled(run->font_size);
                if (fh > max_h) max_h = fh;
                
                while(c_idx < run->len) {
                    char buf[2] = {run->text[c_idx], 0};
                    int cw = ui_get_string_width_scaled(buf, run->font_size);
                    if (run->text[c_idx] == ' ') { last_space_run = r_idx; last_space_char = c_idx; last_space_w = line_w + cw; }
                    if (line_w + cw > page_w - 20) break;
                    line_w += cw;
                    c_idx++;
                }
                if (c_idx < run->len) break;
                r_idx++; c_idx = 0;
            }
            if (r_idx < para->run_count || (r_idx == para->run_count - 1 && c_idx < para->runs[r_idx].len)) {
                if (last_space_run != -1 && (last_space_run > start_run || last_space_char > start_char)) { end_run = last_space_run; end_char = last_space_char; line_w = last_space_w; }
                else { end_run = r_idx; end_char = c_idx; }
            } else { end_run = para->run_count; end_char = 0; }
            
            int line_h = (int)(max_h * para->spacing) + 4;
            if (cur_y + line_h > current_page * (page_h + 20) + page_h - 10) {
                current_page++;
                cur_y = current_page * (page_h + 20) + 10;
            }
            
            if (p == cursor_para) {
                if (cursor_run >= start_run && cursor_run <= end_run) {
                    _Bool is_in = 0;
                    if (cursor_run == start_run && cursor_run == end_run) {
                        if (cursor_pos >= start_char && cursor_pos <= end_char) is_in = 1;
                    } else if (cursor_run == start_run) {
                        if (cursor_pos >= start_char) is_in = 1;
                    } else if (cursor_run == end_run) {
                        if (cursor_pos <= end_char) is_in = 1;
                    } else { is_in = 1; }
                    
                    if (is_in) target_y = cur_y;
                }
            }
            
            cur_y += line_h;
            start_run = end_run; start_char = end_char;
        }
    }
    
    if (target_y != -1) {
        if (target_y - scroll_y < 50) scroll_y = target_y - 50;
        else if (target_y - scroll_y > win_h - 120) scroll_y = target_y - (win_h - 120);
        if (scroll_y < 0) scroll_y = 0;
    }
}

static void update_selection(int p, int r, int char_pos) {
    sel_end_para = p;
    sel_end_run = r;
    sel_end_pos = char_pos;
}

static void apply_style_to_selection(void) {
    if (sel_start_para == -1 || sel_end_para == -1) {
        Paragraph *p = &paragraphs[cursor_para];
        if (p->run_count == 0) p->run_count = 1;
        TextRun *r = &p->runs[cursor_run];
        
        if (r->len == 0) {
            r->bold = is_bold;
            r->italic = is_italic;
            r->underline = is_underline;
            r->font_idx = current_font_idx;
            r->font_size = current_font_size;
            r->color = current_text_color;
        } else {
            if (r->bold == is_bold && r->italic == is_italic && r->underline == is_underline &&
                r->font_idx == current_font_idx && r->font_size == current_font_size && r->color == current_text_color) {
                return;
            }
            
            if (p->run_count < MAX_RUNS_PER_PARAGRAPH) {
                if (cursor_pos > 0 && cursor_pos < r->len) {
                    split_run(cursor_para, cursor_run, cursor_pos);
                    cursor_run++;
                }
                
                if (cursor_pos > 0) {
                    cursor_run++;
                }
                for(int i = p->run_count; i > cursor_run; i--) p->runs[i] = p->runs[i-1];
                p->run_count++;
                
                TextRun *nr = &p->runs[cursor_run];
                nr->len = 0;
                nr->text[0] = 0;
                nr->bold = is_bold;
                nr->italic = is_italic;
                nr->underline = is_underline;
                nr->font_idx = current_font_idx;
                nr->font_size = current_font_size;
                nr->color = current_text_color;
                
                cursor_pos = 0;
            }
        }
        return;
    }
    
    int s_p = sel_start_para, s_r = sel_start_run, s_c = sel_start_pos;
    int e_p = sel_end_para, e_r = sel_end_run, e_c = sel_end_pos;
    
    if (e_p < s_p || (e_p == s_p && e_r < s_r) || (e_p == s_p && e_r == s_r && e_c < s_c)) {
        s_p = sel_end_para; s_r = sel_end_run; s_c = sel_end_pos;
        e_p = sel_start_para; e_r = sel_start_run; e_c = sel_start_pos;
    }
    
    if (s_p == e_p && s_r == e_r && s_c == e_c) return;

    save_undo_state();
    
    split_run(e_p, e_r, e_c);
    split_run(s_p, s_r, s_c);
    
    if (s_c > 0) s_r++;
    if (s_p == e_p && e_r >= s_r) e_r++;
    
    sel_start_para = s_p; sel_start_run = s_r; sel_start_pos = 0;
    sel_end_para = e_p; sel_end_run = e_r - 1;
    if (sel_end_run >= 0 && sel_end_run < paragraphs[sel_end_para].run_count) {
        sel_end_pos = paragraphs[sel_end_para].runs[sel_end_run].len;
    } else {
        sel_end_pos = 0;
    }
    
    for (int p = s_p; p <= e_p; p++) {
        Paragraph *para = &paragraphs[p];
        int start_r = (p == s_p) ? s_r : 0;
        int end_r = (p == e_p) ? e_r - 1 : para->run_count - 1;
        
        for (int r = start_r; r <= end_r; r++) {
            if (r >= para->run_count) break;
            TextRun *run = &para->runs[r];
            run->bold = is_bold;
            run->italic = is_italic;
            run->underline = is_underline;
            run->font_idx = current_font_idx;
            run->font_size = current_font_size;
            run->color = current_text_color;
        }
    }
    
    file_modified = 1;
}

static void apply_align_to_selection(int mode) {
    align_mode = mode;
    active_dropdown = 0;
    
    if (sel_start_para != -1 && sel_end_para != -1) {
        int s = sel_start_para < sel_end_para ? sel_start_para : sel_end_para;
        int e = sel_start_para > sel_end_para ? sel_start_para : sel_end_para;
        for (int p = s; p <= e; p++) {
            paragraphs[p].align = mode;
        }
    } else if (cursor_para != -1) {
        paragraphs[cursor_para].align = mode;
    }
    file_modified = 1;
}

static void handle_click(ui_window_t win, int x, int y) {
    if (active_dialog == 1) {
        int dw = 300; int dh = 150;
        int dx = (win_w - dw)/2; int dy = (win_h - dh)/2;
        if (y >= dy+100 && y <= dy+130) {
            if (x >= dx+10 && x <= dx+110) { active_dialog = 0; }
            else if (x >= dx+dw-110 && x <= dx+dw-10) {
                string_copy(open_filename, dialog_input);
                export_pdf();
                active_dialog = 0;
            }
        }
        return;
    }

    if (active_dropdown == 1) {
        if (x >= 240 && x < 360 && y >= 32 && y < 32 + font_count*20) {
            current_font_idx = (y - 32) / 20;
            apply_style_to_selection();
        }
        active_dropdown = 0;
        return;
    }
    
    if (active_dropdown == 2) {
        int p_count = sizeof(palette)/sizeof(uint32_t);
        if (x >= 365 && x < 405 && y >= 32 && y < 32 + p_count*20) {
            current_text_color = palette[(y - 32) / 20];
            apply_style_to_selection();
        }
        active_dropdown = 0;
        return;
    }

    if (active_dropdown == 3) {
        if (x >= 530 && x < 580 && y >= 32 && y < 32 + 3*20) {
            current_page_size = (y - 32) / 20;
            printf("Selected page size: %d\n", current_page_size);
        }
        active_dropdown = 0;
        return;
    }

    if (y < 40) {
        if (x >= 10 && x < 34) { is_bold = !is_bold; active_dropdown = 0; apply_style_to_selection(); }
        else if (x >= 40 && x < 64) { is_italic = !is_italic; active_dropdown = 0; apply_style_to_selection(); }
        else if (x >= 70 && x < 94) { is_underline = !is_underline; active_dropdown = 0; apply_style_to_selection(); }
        else if (x >= 110 && x < 134) { apply_align_to_selection(0); }
        else if (x >= 140 && x < 164) { apply_align_to_selection(1); }
        else if (x >= 170 && x < 194) { apply_align_to_selection(2); }
        else if (x >= 200 && x < 224) { apply_align_to_selection(3); }
        
        else if (x >= 240 && x < 360) {
            active_dropdown = 1;
        }
        else if (x >= 365 && x < 389) {
            active_dropdown = 2;
        }
        else if (x >= 395 && x < 419) {
            if (current_font_size > 8.0f) current_font_size -= 1.0f;
            active_dropdown = 0;
            apply_style_to_selection();
        }
        else if (x >= 445 && x < 469) {
            if (current_font_size < 72.0f) current_font_size += 1.0f;
            active_dropdown = 0;
            apply_style_to_selection();
        }
        else if (x >= 485 && x < 515) {
            perform_undo();
            active_dropdown = 0;
        }
        else if (x >= 530 && x < 580) {
            active_dropdown = 3;
        }
        else if (x >= 590 && x < 630) {
            active_dialog = 1;
            string_copy(dialog_input, open_filename[0] ? open_filename : "document.pdf");
            dialog_input_len = string_len(dialog_input);
            active_dropdown = 0;
        }
    } else {
        int pw, ph;
        get_page_size(&pw, &ph);
        
        int doc_view_w = win_w - 40;
        float scale = (float)doc_view_w / (float)pw;
        if (scale > 1.0f) scale = 1.0f;
        
        int page_w = (int)(pw * scale);
        int page_h = (int)(ph * scale);

        int content_h = 0;
        int dummy_y = 10;
        int dummy_page = 0;
        for(int p=0; p<para_count; p++) {
            Paragraph *para = &paragraphs[p];
            int start_run = 0; int start_char = 0;
            while (start_run < para->run_count) {
                int max_h = 16; int end_run = start_run; int end_char = start_char; int line_w = 0;
                int r_idx = start_run; int c_idx = start_char; int last_space_run = -1; int last_space_char = -1; int last_space_w = 0;
                while(r_idx < para->run_count) {
                    TextRun *run = &para->runs[r_idx];
                    set_active_font(win, run->font_idx);
                    int fh = ui_get_font_height_scaled(run->font_size);
                    if (fh > max_h) max_h = fh;
                    while(c_idx < run->len) {
                        char buf[2] = {run->text[c_idx], 0};
                        int cw = ui_get_string_width_scaled(buf, run->font_size);
                        if (run->text[c_idx] == ' ') { last_space_run = r_idx; last_space_char = c_idx; last_space_w = line_w + cw; }
                        if (line_w + cw > page_w - 20) break;
                        line_w += cw;
                        c_idx++;
                    }
                    if (c_idx < run->len) break;
                    r_idx++; c_idx = 0;
                }
                if (r_idx < para->run_count || (r_idx == para->run_count - 1 && c_idx < para->runs[r_idx].len)) {
                    if (last_space_run != -1 && (last_space_run > start_run || last_space_char > start_char)) { end_run = last_space_run; end_char = last_space_char; line_w = last_space_w; }
                    else { end_run = r_idx; end_char = c_idx; }
                } else { end_run = para->run_count; end_char = 0; }
                
                int line_h = (int)(max_h * para->spacing) + 4;
                if (dummy_y + line_h > dummy_page * (page_h + 20) + page_h - 10) { dummy_page++; dummy_y = dummy_page * (page_h + 20) + 10; }
                dummy_y += line_h;
                start_run = end_run; start_char = end_char;
                if (start_run < para->run_count && para->runs[start_run].text[start_char] == ' ') {
                    start_char++;
                    if (start_char >= para->runs[start_run].len) { start_char = 0; start_run++; }
                }
            }
        }
        content_h = dummy_page * (page_h + 20) + page_h + 20;
        widget_scrollbar_update(&doc_scrollbar, content_h, scroll_y);
        if (widget_scrollbar_handle_mouse(&doc_scrollbar, x, y, true, NULL)) {
            return;
        }

        int doc_x = 20 + (doc_view_w - page_w) / 2;
        int doc_y = 50 - scroll_y;
        int target_y = y;
        int target_x = x;
        int cur_y = doc_y + 10;
        int current_page = 0;
        
        for(int p=0; p<para_count; p++) {
            Paragraph *para = &paragraphs[p];
            int start_run = 0;
            int start_char = 0;
            
            while (start_run < para->run_count) {
                int line_w = 0; int max_h = 16;
                int end_run = start_run; int end_char = start_char;
                int r_idx = start_run; int c_idx = start_char;
                int last_space_run = -1; int last_space_char = -1; int last_space_w = 0;
                
                while(r_idx < para->run_count) {
                    TextRun *run = &para->runs[r_idx];
                    set_active_font(win, run->font_idx);
                    int fh = ui_get_font_height_scaled(run->font_size);
                    if (fh > max_h) max_h = fh;
                    while(c_idx < run->len) {
                        char buf[2] = {run->text[c_idx], 0};
                        int cw = ui_get_string_width_scaled(buf, run->font_size);
                        if (run->text[c_idx] == ' ') { last_space_run = r_idx; last_space_char = c_idx; last_space_w = line_w + cw; }
                        if (line_w + cw > page_w - 20) break;
                        line_w += cw;
                        c_idx++;
                    }
                    if (c_idx < run->len) break;
                    r_idx++; c_idx = 0;
                }
                
                if (r_idx < para->run_count || (r_idx == para->run_count - 1 && c_idx < para->runs[r_idx].len)) {
                    if (last_space_run != -1 && (last_space_run > start_run || last_space_char > start_char)) {
                        end_run = last_space_run; end_char = last_space_char; line_w = last_space_w;
                    } else {
                         end_run = r_idx; end_char = c_idx;
                    }
                } else {
                    end_run = para->run_count; end_char = 0;
                }
                
                int line_h = (int)(max_h * para->spacing) + 4;
                
                if (cur_y + line_h > doc_y + current_page * (page_h + 20) + page_h - 10) {
                    current_page++;
                    cur_y = doc_y + current_page * (page_h + 20) + 10;
                }
                
                if (target_y >= cur_y && target_y < cur_y + line_h) {
                    int cur_x = doc_x + 10;
                    if (para->align == 1) cur_x = doc_x + 10 + (page_w - 20 - line_w) / 2;
                    else if (para->align == 2) cur_x = doc_x + 10 + (page_w - 20 - line_w);
                    
                    int d_run = start_run;
                    int d_char = start_char;
                    while(d_run < end_run || (d_run == end_run && d_char < end_char)) {
                        TextRun *run = &para->runs[d_run];
                        set_active_font(win, run->font_idx);
                        int chars_to_draw = (d_run == end_run) ? (end_char - d_char) : (run->len - d_char);
                        for(int i=0; i<chars_to_draw; i++) {
                            char buf[2] = {run->text[d_char + i], 0};
                            int cw = ui_get_string_width_scaled(buf, run->font_size);
                            if (target_x >= cur_x && target_x < cur_x + cw/2) {
                                cursor_para = p; cursor_run = d_run; cursor_pos = d_char + i;
                                if (selection_started) {
                                    sel_start_para = p; sel_start_run = d_run; sel_start_pos = d_char + i;
                                    sel_end_para = -1;
                                    selection_started = 0;
                                } else if (is_dragging) {
                                    update_selection(p, d_run, d_char + i);
                                }
                                return;
                            } else if (target_x >= cur_x + cw/2 && target_x < cur_x + cw) {
                                cursor_para = p; cursor_run = d_run; cursor_pos = d_char + i + 1;
                                if (selection_started) {
                                    sel_start_para = p; sel_start_run = d_run; sel_start_pos = d_char + i + 1;
                                    sel_end_para = -1;
                                    selection_started = 0;
                                } else if (is_dragging) {
                                    update_selection(p, d_run, d_char + i + 1);
                                }
                                return;
                            }
                            cur_x += cw;
                        }
                        d_char = 0; d_run++;
                    }
                    if (target_x >= cur_x) {
                        cursor_para = p;
                        if (end_run < para->run_count) {
                            cursor_run = end_run; cursor_pos = end_char;
                        } else {
                            cursor_run = end_run - 1; if(cursor_run<0) cursor_run=0;
                            cursor_pos = para->runs[cursor_run].len;
                        }
                        if (selection_started) {
                            sel_start_para = cursor_para; sel_start_run = cursor_run; sel_start_pos = cursor_pos; sel_end_para = -1; selection_started = 0;
                        } else if (is_dragging) {
                            update_selection(cursor_para, cursor_run, cursor_pos);
                        }
                    } else if (target_x < cur_x - line_w) {
                        cursor_para = p; cursor_run = start_run; cursor_pos = start_char;
                        if (selection_started) {
                            sel_start_para = cursor_para; sel_start_run = cursor_run; sel_start_pos = cursor_pos; sel_end_para = -1; selection_started = 0;
                        } else if (is_dragging) {
                            update_selection(cursor_para, cursor_run, cursor_pos);
                        }
                    }
                    return;
                }
                
                cur_y += line_h;
                start_run = end_run; start_char = end_char;
                if (start_run < para->run_count && para->runs[start_run].text[start_char] == ' ') {
                    start_char++;
                    if (start_char >= para->runs[start_run].len) { start_char = 0; start_run++; }
                }
            }
        }
        
        cursor_para = para_count - 1;
        cursor_run = paragraphs[cursor_para].run_count - 1;
        if (cursor_run < 0) cursor_run = 0;
        cursor_pos = paragraphs[cursor_para].runs[cursor_run].len;
        if (selection_started) {
            sel_start_para = cursor_para; sel_start_run = cursor_run; sel_start_pos = cursor_pos; sel_end_para = -1; selection_started = 0;
        } else if (is_dragging) {
            update_selection(cursor_para, cursor_run, cursor_pos);
        }
    }
}

static _Bool is_in_selection(int p, int r, int c) {
    if (sel_start_para == -1 || sel_end_para == -1) return 0;
    
    int s_p = sel_start_para, s_r = sel_start_run, s_c = sel_start_pos;
    int e_p = sel_end_para, e_r = sel_end_run, e_c = sel_end_pos;
    
    if (e_p < s_p || (e_p == s_p && e_r < s_r) || (e_p == s_p && e_r == s_r && e_c < s_c)) {
        s_p = sel_end_para; s_r = sel_end_run; s_c = sel_end_pos;
        e_p = sel_start_para; e_r = sel_start_run; e_c = sel_start_pos;
    }
    
    if (p < s_p || p > e_p) return 0;
    if (p == s_p && p == e_p) {
        if (r < s_r || r > e_r) return 0;
        if (r == s_r && r == e_r) return c >= s_c && c < e_c;
        if (r == s_r) return c >= s_c;
        if (r == e_r) return c < e_c;
        return 1;
    }
    
    if (p == s_p) {
        if (r < s_r) return 0;
        if (r == s_r) return c >= s_c;
        return 1;
    }
    if (p == e_p) {
        if (r > e_r) return 0;
        if (r == e_r) return c < e_c;
        return 1;
    }
    return 1;
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    ui_window_t win = ui_window_create("BoredWord", 100, 100, win_w, win_h);
    if (!win) return 1;
    word_ctx.user_data = (void*)win;
    ui_window_set_resizable(win, 1);

    load_fonts();
    set_active_font(win, 0);
    init_doc();
    
    widget_scrollbar_init(&doc_scrollbar, win_w - 12, 40, 12, win_h - 40);
    
    if (argc > 1) {
        load_file(win, argv[1]);
    }

    gui_event_t ev;
    _Bool needs_repaint = 1;

    while(1) {
        while (ui_get_event(win, &ev)) {
            if (ev.type == GUI_EVENT_PAINT) {
                needs_repaint = 1;
            } else if (ev.type == GUI_EVENT_RESIZE) {
                win_w = ev.arg1;
                win_h = ev.arg2;
                needs_repaint = 1;
            } else if (ev.type == GUI_EVENT_MOUSE_WHEEL) {
                scroll_y -= ev.arg2 * 30; // arg2 is scroll amount
                if (scroll_y < 0) scroll_y = 0;
                needs_repaint = 1;
            } else if (ev.type == GUI_EVENT_MOUSE_DOWN) {
                if (ev.arg1 >= 0 && ev.arg1 < win_w && ev.arg2 >= 0 && ev.arg2 < win_h) {
                    if (ev.arg2 < 40 || active_dialog == 1 || active_dropdown != 0) {
                        handle_click(win, ev.arg1, ev.arg2);
                    } else {
                        is_dragging = 1;
                        selection_started = 1;
                        sel_start_para = -1; sel_end_para = -1;
                        handle_click(win, ev.arg1, ev.arg2);
                        selection_started = 0;
                        
                        update_formatting_state();
                        
                    }
                }
                needs_repaint = 1;
                needs_repaint = 1;
            } else if (ev.type == GUI_EVENT_MOUSE_UP) {
                is_dragging = 0;
                widget_scrollbar_handle_mouse(&doc_scrollbar, ev.arg1, ev.arg2, false, NULL);
                needs_repaint = 1;
            } else if (ev.type == GUI_EVENT_MOUSE_MOVE) {
                if (doc_scrollbar.is_dragging) {
                    widget_scrollbar_handle_mouse(&doc_scrollbar, ev.arg1, ev.arg2, true, NULL);
                    needs_repaint = 1;
                } else if (is_dragging && ev.arg2 >= 40 && active_dialog == 0 && active_dropdown == 0) {
                    handle_click(win, ev.arg1, ev.arg2);
                    needs_repaint = 1;
                }
            } else if (ev.type == GUI_EVENT_CLICK) {
                needs_repaint = 1;
            } else if (ev.type == GUI_EVENT_KEY) {
                if (active_dialog == 1) {
                    char c = (char)ev.arg1;
                    if (c == '\b' && dialog_input_len > 0) {
                        dialog_input[--dialog_input_len] = 0;
                    } else if (c >= 32 && c < 127 && dialog_input_len < 127) {
                        dialog_input[dialog_input_len++] = c;
                        dialog_input[dialog_input_len] = 0;
                    }
                } else {
                    insert_char(win, (char)ev.arg1);
                }
                needs_repaint = 1;
            } else if (ev.type == GUI_EVENT_CLOSE) {
                sys_exit(0);
            }
        }
        
        if (needs_repaint) {
            draw_document(win);
            draw_toolbar(win);
            draw_dropdowns(win);
            draw_dialogs(win);
            ui_mark_dirty(win, 0, 0, win_w, win_h);
            needs_repaint = 0;
        } else {
            sys_yield();
        }
    }
    return 0;
}
