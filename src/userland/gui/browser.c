// Copyright (c) 2023-2026 Chris (boreddevnl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// BOREDOS_APP_DESC: Web browser for internet pages.
// BOREDOS_APP_ICONS: /Library/images/icons/colloid/web-browser.png
#include "libc/syscall.h"
#include "libc/libui.h"
#include "stb_image.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "libc/stdlib.h"

static int win_w = 1280;
static int win_h = 960;
#define URL_BAR_H 30
#define SCROLL_BAR_W 16
#define RESP_BUF_SIZE (32 * 1024 * 1024)

#define COLOR_URL_BAR    0xFF303030
#define COLOR_URL_TEXT   0xFFF0F0F0
#define COLOR_BG         0xFFFFFFFF
#define COLOR_TEXT       0xFF000000
#define COLOR_LINK       0xFF0000EE
#define COLOR_SCROLL_BG  0xFFEEEEEE
#define COLOR_SCROLL_BTN 0xFFCCCCCC

#define BTN_W 30
#define BTN_H 22
#define BTN_PAD 4
#define HOME_BTN_X (win_w - SCROLL_BAR_W - BTN_W - BTN_PAD)
#define BACK_BTN_X (HOME_BTN_X - BTN_W - BTN_PAD)

#define HISTORY_MAX 32
static char history_stack[HISTORY_MAX][512];
static int history_count = 0;

static char* str_istrstr(const char* haystack, const char* needle) {
    if (!*needle) return (char*)haystack;
    for (; *haystack; haystack++) {
        const char *h = haystack;
        const char *n = needle;
        while (*h && *n) {
            char ch = *h; char cn = *n;
            if (ch >= 'A' && ch <= 'Z') ch += 32;
            if (cn >= 'A' && cn <= 'Z') cn += 32;
            if (ch != cn) break;
            h++; n++;
        }
        if (!*n) return (char*)haystack;
    }
    return NULL;
}

static long strtol(const char* nptr, char** endptr, int base) {
    long res = 0;
    while (*nptr == ' ' || *nptr == '\t' || *nptr == '\n' || *nptr == '\r') nptr++;
    bool neg = false;
    if (*nptr == '-') { neg = true; nptr++; }
    else if (*nptr == '+') nptr++;
    
    while (*nptr) {
        int v = -1;
        if (*nptr >= '0' && *nptr <= '9') v = *nptr - '0';
        else if (*nptr >= 'a' && *nptr <= 'z') v = *nptr - 'a' + 10;
        else if (*nptr >= 'A' && *nptr <= 'Z') v = *nptr - 'A' + 10;
        if (v < 0 || v >= base) break;
        res = res * base + v;
        nptr++;
    }
    if (endptr) *endptr = (char*)nptr;
    return neg ? -res : res;
}

#define TAG_NONE 0
#define TAG_IMG 1
#define TAG_INPUT 2
#define TAG_BUTTON 3
#define TAG_HR 4
#define TAG_BR 5
#define TAG_RADIO 6
#define TAG_CHECKBOX 7

typedef struct {
    char content[1024];
    int x, y, w, h;
    int tag; 
    char link_url[256];
    char attr_value[256];
    uint32_t color;
    bool centered;
    bool bold;
    bool italic;
    bool underline;
    bool checked;
    uint32_t *img_pixels;
    int img_w, img_h;
    char form_action[256];
    char input_name[64];
    int form_id;
    int input_cursor;
    int input_scroll;
    float scale;
    int list_depth;
    int blockquote_depth; 
    int attr_w;
    bool img_loading;
    bool img_failed;
    uint32_t **img_frames;
    int *img_delays;
    int img_frame_count;
    int img_current_frame;
    uint64_t next_frame_tick;
} RenderElement;

#define MAX_ELEMENTS 65536
static RenderElement elements[MAX_ELEMENTS];
static int element_count = 0;

static char url_input_buffer[512] = "http://find.boreddev.nl";
static int url_cursor = 22;
static char current_host[256] = "find.boreddev.nl";
static int current_port = 80;
static int next_form_id = 1;

static ui_window_t win_browser;
static int scroll_y = 0;
static int total_content_height = 0;
static int focused_element = -1; 

#include "../../wm/libwidget.h"

static void browser_draw_rect(void *user_data, int x, int y, int w, int h, uint32_t color) {
    ui_draw_rect((ui_window_t)user_data, x, y, w, h, color);
}
static void browser_draw_rounded_rect_filled(void *user_data, int x, int y, int w, int h, int r, uint32_t color) {
    ui_draw_rounded_rect_filled((ui_window_t)user_data, x, y, w, h, r, color);
}
static void browser_draw_string(void *user_data, int x, int y, const char *str, uint32_t color) {
    ui_draw_string((ui_window_t)user_data, x, y, str, color);
}
static int browser_measure_string_width(void *user_data, const char *str) {
    (void)user_data;
    return (int)ui_get_string_width(str);
}
static void browser_mark_dirty(void *user_data, int x, int y, int w, int h) {
    ui_mark_dirty((ui_window_t)user_data, x, y, w, h);
}

static widget_context_t browser_ctx = {
    .draw_rect = browser_draw_rect,
    .draw_rounded_rect_filled = browser_draw_rounded_rect_filled,
    .draw_string = browser_draw_string,
    .measure_string_width = browser_measure_string_width,
    .mark_dirty = browser_mark_dirty
};

static widget_scrollbar_t browser_scrollbar;
static void browser_on_scroll(void *user_data, int new_scroll_y) {
    (void)user_data;
    scroll_y = new_scroll_y;
}
static widget_textbox_t url_tb;
static widget_button_t btn_back;
static widget_button_t btn_home;

static void parse_html(const char *html);
static void parse_html_incremental(const char *html, int safe_len);
static void browser_reflow(void);
static void browser_paint(void);
static int inc_parse_offset = 0;

typedef struct {
    uint32_t color;
    float scale;
} FontState;

#define MAX_FONT_STACK 16
static FontState inc_font_stack[MAX_FONT_STACK];
static int inc_font_ptr = 0;

static void browser_clear(void) {
    for (int i = 0; i < element_count; i++) {
        if (elements[i].img_pixels) {
            free(elements[i].img_pixels);
            elements[i].img_pixels = NULL;
        }
        if (elements[i].img_frames) {
            for (int k = 0; k < elements[i].img_frame_count; k++) {
                if (elements[i].img_frames[k]) free(elements[i].img_frames[k]);
            }
            free(elements[i].img_frames);
            elements[i].img_frames = NULL;
        }
        if (elements[i].img_delays) {
            free(elements[i].img_delays);
            elements[i].img_delays = NULL;
        }
    }
    element_count = 0;
    total_content_height = 0;
    inc_font_ptr = 0;
}

static bool str_iequals(const char *s1, const char *s2) {
    while (*s1 && *s2) {
        char c1 = *s1; char c2 = *s2;
        if (c1 >= 'A' && c1 <= 'Z') c1 += 32;
        if (c2 >= 'A' && c2 <= 'Z') c2 += 32;
        if (c1 != c2) return false;
        s1++; s2++;
    }
    return *s1 == *s2;
}

static bool str_istarts_with(const char *str, const char *prefix) {
    while (*prefix) {
        char s = *str; char p = *prefix;
        if (s >= 'A' && s <= 'Z') s += 32;
        if (p >= 'A' && p <= 'Z') p += 32;
        if (s != p) return false;
        str++; prefix++;
    }
    return true;
}

static int parse_ip(const char* str, net_ipv4_address_t* ip) {
    int val = 0;
    int part = 0;
    const char* p = str;
    while (*p) {
        if (*p >= '0' && *p <= '9') {
            val = val * 10 + (*p - '0');
            if (val > 255) return -1;
        } else if (*p == '.') {
            if (part > 3) return -1;
            ip->bytes[part++] = (uint8_t)val;
            val = 0;
        } else {
            return -1;
        }
        p++;
    }
    if (part != 3) return -1;
    ip->bytes[3] = (uint8_t)val;
    return 0;
}

static char dns_cache_host[256] = "";
static net_ipv4_address_t dns_cache_ip;

static int fetch_content(const char *url, char *dest_buf, int max_len, bool progressive) {
    const char* host_start = url;
    if (url[0] == 'h' && url[1] == 't' && url[2] == 't' && url[3] == 'p') {
        if (url[4] == 's' && url[5] == ':') host_start = url + 8;
        else if (url[4] == ':') host_start = url + 7;
    }

    char hostname[256];
    int port = 80;
    int i = 0;
    while (host_start[i] && host_start[i] != '/' && host_start[i] != ':' && i < 255) {
        hostname[i] = host_start[i];
        i++;
    }
    hostname[i] = 0;

    if (host_start[i] == ':') {
        i++;
        char port_str[10];
        int j = 0;
        while (host_start[i] && host_start[i] != '/' && j < 9) {
            port_str[j++] = host_start[i++];
        }
        port_str[j] = 0;
        port = atoi(port_str);
    }
    current_port = port;

    if (hostname[0]) {
        int k=0; while(hostname[k]) { current_host[k] = hostname[k]; k++; } current_host[k] = 0;
    }
    
    net_ipv4_address_t ip;
    if (parse_ip(hostname, &ip) != 0) {
        if (str_iequals(hostname, dns_cache_host)) {
            ip = dns_cache_ip;
        } else {
            if (sys_dns_lookup(hostname, &ip) != 0) return 0;
            int k=0; while(hostname[k]) { dns_cache_host[k] = hostname[k]; k++; } dns_cache_host[k] = 0;
            dns_cache_ip = ip;
        }
    }
    
    if (sys_tcp_connect(&ip, port) != 0) return 0;
    
    const char* path = host_start + i;
    if (*path == 0) path = "/";
    
    char request[2048];
    char* r = request;
    const char* s;
    s = "GET "; while(*s) *r++ = *s++;
    s = path; while(*s) *r++ = *s++;
    s = " HTTP/1.1\r\nHost: "; while(*s) *r++ = *s++;
    s = hostname; while(*s) *r++ = *s++;
    if (current_port != 80) {
        *r++ = ':';
        char pbuf[10]; itoa(current_port, pbuf);
        s = pbuf; while(*s) *r++ = *s++;
    }
    s = "\r\nUser-Agent: BoredOS/1.0\r\nAccept: */*\r\nConnection: close\r\n\r\n"; while(*s) *r++ = *s++;
    
    sys_tcp_send(request, r - request);
    
    int total = 0;
    int last_render = 0;
    if (progressive) inc_parse_offset = 0; 
    long long last_data_tick = sys_system(SYSTEM_CMD_GET_TICKS, 0, 0, 0, 0);

    while (1) {
        int len = sys_tcp_recv_nb(dest_buf + total, max_len - 1 - total);
        if (len < 0 && len != -2) break;
        if (len == -2) break;
        
        if (len == 0) {
            long long now = sys_system(SYSTEM_CMD_GET_TICKS, 0, 0, 0, 0);
            if (now > last_data_tick + 1800) break; // 30 sec timeout
            
            gui_event_t ev;
            bool scrolled = false;
            while (ui_get_event(win_browser, &ev)) {
                if (ev.type == 9) { // GUI_EVENT_MOUSE_WHEEL
                    scroll_y += ev.arg1 * 20;
                    scrolled = true;
                } else if (ev.type == 12) { // GUI_EVENT_CLOSE
                    sys_exit(0);
                } else if (ev.type == GUI_EVENT_MOUSE_DOWN || ev.type == GUI_EVENT_MOUSE_UP || ev.type == GUI_EVENT_MOUSE_MOVE) {
                    bool is_down = (ev.type == GUI_EVENT_MOUSE_DOWN || (ev.type == GUI_EVENT_MOUSE_MOVE && browser_scrollbar.is_dragging));
                    if (widget_scrollbar_handle_mouse(&browser_scrollbar, ev.arg1, ev.arg2, is_down, &browser_ctx)) {
                        scroll_y = browser_scrollbar.scroll_y;
                        scrolled = true;
                    }
                }
            }
            if (scrolled) {
                int max_scroll = total_content_height - (win_h - URL_BAR_H);
                if (max_scroll < 0) max_scroll = 0;
                if (scroll_y > max_scroll) scroll_y = max_scroll;
                if (scroll_y < 0) scroll_y = 0;
                browser_reflow(); // Needs reflow in case of dimensions changing, but mostly just paint
                browser_paint(); 
                ui_mark_dirty(win_browser, 0, 0, win_w, win_h);
            }
            sleep(10);
            continue;
        }

        last_data_tick = sys_system(SYSTEM_CMD_GET_TICKS, 0, 0, 0, 0);
        total += len;
        if (total >= max_len - 1) break;

        dest_buf[total] = 0;
        char *body = strstr(dest_buf, "\r\n\r\n");
        if (body) {
            char temp = body[0];
            body[0] = 0; // Null-terminate headers

            int expected = -1;
            char *cl = str_istrstr(dest_buf, "Content-Length:");
            if (cl) {
                cl += 15;
                while (*cl == ' ') cl++;
                expected = 0;
                while (*cl >= '0' && *cl <= '9') {
                    expected = expected * 10 + (*cl - '0');
                    cl++;
                }
            }
            
            int is_chunked = 0;
            char *te = str_istrstr(dest_buf, "Transfer-Encoding:");
            if (te && str_istrstr(te, "chunked")) {
                is_chunked = 1;
            }

            body[0] = temp; // Restore body
            
            body += 4;
            int body_len = total - (body - dest_buf);
            
            if (expected != -1) {
                if (body_len >= expected) break;
            } else if (is_chunked) {
                if (total >= 5 && dest_buf[total-5] == '0' && dest_buf[total-4] == '\r' && 
                    dest_buf[total-3] == '\n' && dest_buf[total-2] == '\r' && dest_buf[total-1] == '\n') {
                    break;
                }
            }
        }

        if (progressive && total - last_render > 32768) {
            dest_buf[total] = 0;
            char *body = strstr(dest_buf, "\r\n\r\n");
            if (body) {
                char temp = body[0];
                body[0] = 0;
                int is_chunked = strstr(dest_buf, "Transfer-Encoding: chunked") != NULL;
                body[0] = temp;
                
                body += 4;
                if (!is_chunked) {
                    int body_len = total - (body - dest_buf);
                    int safe_len = body_len;
                    while (safe_len > 0 && body[safe_len - 1] != '>') safe_len--;
                    int check_amp = total - (body - dest_buf) - 1;
                    if (check_amp >= safe_len) check_amp = safe_len - 1;
                    int amp_pos = -1;
                    for (int k = 0; k < 15 && check_amp - k >= 0; k++) {
                        if (body[check_amp - k] == ';') break; 
                        if (body[check_amp - k] == '&') { amp_pos = check_amp - k; break; }
                    }
                    if (amp_pos != -1) safe_len = amp_pos;
                    if (safe_len > inc_parse_offset) {
                        parse_html_incremental(body, safe_len);
                        browser_reflow();
                        browser_paint();
                        ui_mark_dirty(win_browser, 0, 0, win_w, win_h);
                        last_render = total;
                    }
                }
            }
        }
    }
    dest_buf[total] = 0;
    sys_tcp_close();
    return total;
}

static void decode_image(unsigned char *data, int len, RenderElement *el) {
    int img_w_orig, img_h_orig, channels;
    int frame_count = 1;
    int *delays = NULL;
    unsigned char *rgba = NULL;

    if (len > 4 && data[0] == 'G' && data[1] == 'I' && data[2] == 'F') {
        rgba = stbi_load_gif_from_memory(data, len, &delays, &img_w_orig, &img_h_orig, &frame_count, &channels, 4);
    } else {
        rgba = stbi_load_from_memory(data, len, &img_w_orig, &img_h_orig, &channels, 4);
    }

    if (rgba && img_w_orig > 0 && img_h_orig > 0) {
        int fit_w = img_w_orig; int fit_h = img_h_orig;
        if (el->attr_w > 0) {
            fit_w = el->attr_w;
            fit_h = (img_h_orig * fit_w) / img_w_orig;
        } else {
            if (fit_w > win_w - 60) { fit_h = fit_h * (win_w - 60) / fit_w; fit_w = win_w - 60; }
            if (fit_h > 400) { fit_w = fit_w * 400 / fit_h; fit_h = 400; }
        }
        
        if (frame_count > 1 && delays) {
            el->img_frames = malloc(frame_count * sizeof(uint32_t *));
            el->img_delays = malloc(frame_count * sizeof(int));
            el->img_frame_count = frame_count;
            el->img_current_frame = 0;
            el->next_frame_tick = sys_system(SYSTEM_CMD_GET_TICKS, 0, 0, 0, 0) + (delays[0] * 60 / 1000);
            
            uint32_t step_x = (img_w_orig << 16) / fit_w;
            uint32_t step_y = (img_h_orig << 16) / fit_h;

            for (int i = 0; i < frame_count; i++) {
                el->img_frames[i] = malloc(fit_w * fit_h * sizeof(uint32_t));
                if (el->img_frames[i]) {
                    unsigned char *src_frame = rgba + (i * img_w_orig * img_h_orig * 4);
                    uint16_t *src_h_table = malloc(fit_h * sizeof(uint16_t));
                    uint16_t *src_w_table = malloc(fit_w * sizeof(uint16_t));
                    
                    if (src_h_table && src_w_table) {
                        for (int y = 0; y < fit_h; y++) src_h_table[y] = (y * step_y) >> 16;
                        for (int x = 0; x < fit_w; x++) src_w_table[x] = (x * step_x) >> 16;

                        for (int y = 0; y < fit_h; y++) {
                            int sy = src_h_table[y];
                            uint32_t src_row_off = sy * img_w_orig;
                            uint32_t dst_row_off = y * fit_w;
                            for (int x = 0; x < fit_w; x++) {
                                int sx = src_w_table[x];
                                int idx = (src_row_off + sx) * 4;
                                uint32_t r = src_frame[idx];
                                uint32_t g = src_frame[idx+1];
                                uint32_t b = src_frame[idx+2];
                                uint32_t a = src_frame[idx+3];
                                el->img_frames[i][dst_row_off + x] = (a << 24) | (r << 16) | (g << 8) | b;
                            }
                        }
                    }
                    if (src_h_table) free(src_h_table);
                    if (src_w_table) free(src_w_table);
                }
                el->img_delays[i] = delays[i];
            }
            el->img_w = fit_w; el->img_h = fit_h;
            free(delays);
        } else {
            el->img_pixels = malloc(fit_w * fit_h * sizeof(uint32_t));
            if (el->img_pixels) {
                uint32_t step_x = (img_w_orig << 16) / fit_w;
                uint32_t step_y = (img_h_orig << 16) / fit_h;
                for (int y = 0; y < fit_h; y++) {
                    int sy = (y * step_y) >> 16;
                    uint32_t src_row_off = sy * img_w_orig;
                    uint32_t dst_row_off = y * fit_w;
                    for (int x = 0; x < fit_w; x++) {
                        int sx = (x * step_x) >> 16;
                        int idx = (src_row_off + sx) * 4;
                        uint32_t r = rgba[idx];
                        uint32_t g = rgba[idx+1];
                        uint32_t b = rgba[idx+2];
                        uint32_t a = rgba[idx+3];
                        el->img_pixels[dst_row_off + x] = (a << 24) | (r << 16) | (g << 8) | b;
                    }
                }
                el->img_w = fit_w; el->img_h = fit_h;
            }
        }
        stbi_image_free(rgba);
    }
}

static int decode_chunked_bin(char *body, int total_len) {
    char *src = body; char *dst = body;
    int remaining = total_len;
    int final_len = 0;
    while (remaining > 0) {
        char *endptr;
        int chunk_size = (int)strtol(src, &endptr, 16);
        int head_len = endptr - src;
        src = endptr;
        if (*src == '\r') { src++; head_len++; }
        if (*src == '\n') { src++; head_len++; }
        remaining -= head_len;
        if (chunk_size == 0) break;
        if (remaining < chunk_size) chunk_size = remaining;
        
        for (int i = 0; i < chunk_size; i++) *dst++ = *src++;
        final_len += chunk_size;
        remaining -= chunk_size;
        if (remaining > 0 && *src == '\r') { src++; remaining--; }
        if (remaining > 0 && *src == '\n') { src++; remaining--; }
    }
    *dst = 0;
    return final_len;
}

static void load_image(RenderElement *el) {
    char url[512];
    if (str_istarts_with(el->attr_value, "http")) {
        int k=0; while(el->attr_value[k]) { url[k] = el->attr_value[k]; k++; } url[k] = 0;
    } else {
        char *u = url;
        const char *s = "http://"; while(*s) *u++ = *s++;
        s = current_host; while(*s) *u++ = *s++;
        if (current_port != 80) {
            *u++ = ':';
            char pbuf[10]; itoa(current_port, pbuf);
            const char* ps = pbuf; while(*ps) *u++ = *ps++;
        }
        if (el->attr_value[0] != '/') *u++ = '/';
        s = el->attr_value; while(*s) *u++ = *s++;
        *u = 0;
    }
    static char img_resp[RESP_BUF_SIZE];
    int resp_len = fetch_content(url, img_resp, sizeof(img_resp), false);
    char *body = strstr(img_resp, "\r\n\r\n");
    if (body) {
        body += 4;
        int hdr_len = body - img_resp;
        int body_len = resp_len - hdr_len;
        if (strstr(img_resp, "Transfer-Encoding: chunked")) {
            body_len = decode_chunked_bin(body, body_len);
        }
        decode_image((unsigned char*)body, body_len, el);
    }
    if (el->img_pixels) {
        el->w = el->img_w;
        el->h = el->img_h;
    }
    el->img_loading = false;
    if (!el->img_pixels) el->img_failed = true;
}

static int line_elements[512];
static int line_element_count = 0;
static int cur_line_y = 10;
static int cur_line_x = 10;
static int list_depth = 0;



static int inc_list_type[16];
static int inc_list_index[16];
static int inc_center_depth = 0;
static int inc_table_depth = 0;
static int inc_table_float_depth = 0;
static int inc_blockquote_depth = 0; 
static bool inc_is_bold = false;
static bool inc_is_italic = false;
static bool inc_is_underline = false;
static uint32_t inc_current_color = COLOR_TEXT;
static char inc_current_link[256] = "";
static float inc_current_scale = 15.0f;
static float inc_base_scale = 15.0f;
static bool inc_is_space_pending = false;
static char inc_form_action[256] = "";
static int inc_form_id = 0;
static bool inc_skip_content = false;
static bool inc_is_pre = false;
static bool inc_inside_title = false;
static char current_page_title[256] = "";
static void emit_br(void) {
    if (element_count >= MAX_ELEMENTS) return;
    RenderElement *el = &elements[element_count++];
    for (int k=0; k<(int)sizeof(RenderElement); k++) ((char*)el)[k] = 0;
    el->tag = TAG_BR;
}

static void flush_line(void) {
    if (line_element_count == 0) return;
    RenderElement *first_el = &elements[line_elements[0]];
    bool centered = first_el->centered;
    int ldepth = first_el->list_depth;
    int bdepth = first_el->blockquote_depth; 

    int line_w = 0;
    for (int i = 0; i < line_element_count; i++) line_w += elements[line_elements[i]].w;
    int offset_x = centered ? (win_w - SCROLL_BAR_W - line_w) / 2 : 10 + (ldepth * 20) + (bdepth * 20); 
    if (offset_x < 10) offset_x = 10;

    int max_h = 16;
    int max_baseline = 16;
    
    for (int i = 0; i < line_element_count; i++) {
        RenderElement *el = &elements[line_elements[i]];
        if (el->tag == TAG_IMG && el->img_h + 10 > max_h) max_h = el->img_h + 10;
        if ((el->tag == TAG_INPUT || el->tag == TAG_BUTTON) && 20 + 10 > max_h) max_h = 20 + 10;
        if (el->tag == TAG_NONE) {
            int fh = el->h;
            if (fh + 4 > max_h) max_h = fh + 4;
            if (fh > max_baseline) max_baseline = fh;
        }
    }
    
    for (int i = 0; i < line_element_count; i++) {
        RenderElement *el = &elements[line_elements[i]];
        el->x = offset_x;
        if (el->tag == TAG_NONE) {
            int fh = el->h;
            el->y = cur_line_y + (max_baseline - fh); 
        } else {
            el->y = cur_line_y;
        }
        offset_x += el->w;
    }
    
    cur_line_y += max_h;
    line_element_count = 0;
    total_content_height = cur_line_y + 50;
}

static void browser_reflow(void) {
    cur_line_y = 10;
    cur_line_x = 10;
    line_element_count = 0;
    total_content_height = 0;
    
    int float_right_bottom_y = 0;
    int float_start_idx = -1;
    int float_start_y = 0;
    
    for (int i = 0; i < element_count; i++) {
        RenderElement *el = &elements[i];
        
        if (el->tag == 8) {
            flush_line();
            float_start_idx = i;
            float_start_y = cur_line_y;
            continue;
        } else if (el->tag == 9) {
            if (float_start_idx != -1) {
                flush_line();
                float_right_bottom_y = cur_line_y;
                int float_offset = win_w - SCROLL_BAR_W - 320 - 10;
                
                elements[float_start_idx].x = float_offset > 0 ? float_offset + 10 : 10;
                elements[float_start_idx].y = float_start_y;
                elements[float_start_idx].w = 320;
                elements[float_start_idx].h = cur_line_y - float_start_y;

                if (float_offset > 0) {
                    for (int j = float_start_idx; j < i; j++) {
                        elements[j].x += float_offset;
                    }
                }
                cur_line_y = float_start_y;
                cur_line_x = 10;
                float_start_idx = -1;
            }
            continue;
        }

        if (el->tag == TAG_BR) {
            flush_line();
            cur_line_x = 10 + (el->list_depth * 20) + (el->blockquote_depth * 20); 
            continue;
        }
        
        if (el->tag == TAG_HR) {
            flush_line();
            el->w = win_w - SCROLL_BAR_W - 40 - (el->blockquote_depth * 40); 
            if (float_start_idx != -1) el->w = 300 - 20;
            else if (cur_line_y < float_right_bottom_y) el->w = win_w - 320 - SCROLL_BAR_W - 20;
            line_elements[line_element_count++] = i;
            flush_line();
            cur_line_x = 10 + (el->list_depth * 20) + (el->blockquote_depth * 20); 
            continue;
        }
        
        int max_x = win_w - SCROLL_BAR_W - 20 - (el->blockquote_depth * 40);
        if (float_start_idx != -1) max_x = 310;
        else if (cur_line_y < float_right_bottom_y) max_x = win_w - 320 - SCROLL_BAR_W - 20;

        if (el->tag == TAG_NONE && el->content[0] == ' ' && el->content[1] == 0) {
            if (line_element_count == 0) continue;
            if (cur_line_x + el->w > max_x) continue;
        }

        if (cur_line_x + el->w > max_x) {
            flush_line();   
            cur_line_x = 10 + (el->list_depth * 20) + (el->blockquote_depth * 20); 
        }
        
        line_elements[line_element_count++] = i;
        cur_line_x += el->w;
    }
    flush_line();
}


static uint32_t parse_html_color(const char *str) {
    if (!str) return COLOR_TEXT;
    while (*str == ' ' || *str == '\"' || *str == '\'') str++;
    if (*str == '#') {
        char *end;
        uint32_t val = (uint32_t)strtol(str + 1, &end, 16);
        return 0xFF000000 | val; 
    }
    if (str_istarts_with(str, "red")) return 0xFFFF0000;
    if (str_istarts_with(str, "green")) return 0xFF008000;
    if (str_istarts_with(str, "blue")) return 0xFF0000FF;
    if (str_istarts_with(str, "white")) return 0xFFFFFFFF;
    if (str_istarts_with(str, "black")) return 0xFF000000;
    if (str_istarts_with(str, "yellow")) return 0xFFFFFF00;
    if (str_istarts_with(str, "gray")) return 0xFF808080;
    if (str_istarts_with(str, "purple")) return 0xFF800080;
    if (str_istarts_with(str, "silver")) return 0xFFC0C0C0;
    if (str_istarts_with(str, "maroon")) return 0xFF800000;
    if (str_istarts_with(str, "navy")) return 0xFF000080;
    if (str_istarts_with(str, "teal")) return 0xFF008080;
    if (str_istarts_with(str, "olive")) return 0xFF808000;
    return COLOR_TEXT;
}

static void decode_html_entities(char *str) {
    if (!str) return;
    char *src = str;
    char *dst = str;
    while (*src) {
        if (*src == '&') {
            if (str_istarts_with(src, "&quot;")) { *dst++ = '\"'; src += 6; continue; }
            if (str_istarts_with(src, "&amp;")) { *dst++ = '&'; src += 5; continue; }
            if (str_istarts_with(src, "&lt;")) { *dst++ = '<'; src += 4; continue; }
            if (str_istarts_with(src, "&gt;")) { *dst++ = '>'; src += 4; continue; }
            if (str_istarts_with(src, "&apos;")) { *dst++ = '\''; src += 6; continue; }
            if (str_istarts_with(src, "&nbsp;")) { *dst++ = ' '; src += 6; continue; }
            if (str_istarts_with(src, "&mdash;")) { *dst++ = (char)128; src += 7; continue; }
            if (str_istarts_with(src, "&mdash"))  { *dst++ = (char)128; src += 6; continue; } 
            if (str_istarts_with(src, "&ndash;")) { *dst++ = (char)129; src += 7; continue; }
            if (str_istarts_with(src, "&ndash"))  { *dst++ = (char)129; src += 6; continue; }
            if (str_istarts_with(src, "&bull;"))  { *dst++ = (char)130; src += 6; continue; }
            if (str_istarts_with(src, "&bull"))   { *dst++ = (char)130; src += 5; continue; }
            if (str_istarts_with(src, "&hellip;")){ *dst++ = (char)131; src += 8; continue; }
            if (str_istarts_with(src, "&hellip")){ *dst++ = (char)131; src += 7; continue; }
            if (str_istarts_with(src, "&trade;")) { *dst++ = (char)132; src += 7; continue; }
            if (str_istarts_with(src, "&euro;"))  { *dst++ = (char)133; src += 6; continue; }
            if (str_istarts_with(src, "&middot;")){ *dst++ = (char)134; src += 8; continue; }
            if (str_istarts_with(src, "&lsquo;")) { *dst++ = '\''; src += 7; continue; }
            if (str_istarts_with(src, "&rsquo;")) { *dst++ = '\''; src += 7; continue; }
            if (str_istarts_with(src, "&ldquo;")) { *dst++ = '\"'; src += 7; continue; }
            if (str_istarts_with(src, "&rdquo;")) { *dst++ = '\"'; src += 7; continue; }      
            if (str_istarts_with(src, "&iexcl;")) { *dst++ = (char)161; src += 7; continue; }
            if (str_istarts_with(src, "&cent;")) { *dst++ = (char)162; src += 6; continue; }
            if (str_istarts_with(src, "&pound;")) { *dst++ = (char)163; src += 7; continue; }
            if (str_istarts_with(src, "&yen;")) { *dst++ = (char)165; src += 5; continue; }
            if (str_istarts_with(src, "&copy;")) { *dst++ = (char)169; src += 6; continue; }
            if (str_istarts_with(src, "&reg;")) { *dst++ = (char)174; src += 5; continue; }
            if (str_istarts_with(src, "&deg;")) { *dst++ = (char)176; src += 5; continue; }
            if (str_istarts_with(src, "&aacute;")) { *dst++ = (char)225; src += 8; continue; }
            if (str_istarts_with(src, "&eacute;")) { *dst++ = (char)233; src += 8; continue; }
            if (str_istarts_with(src, "&iacute;")) { *dst++ = (char)237; src += 8; continue; }
            if (str_istarts_with(src, "&oacute;")) { *dst++ = (char)243; src += 8; continue; }
            if (str_istarts_with(src, "&uacute;")) { *dst++ = (char)250; src += 8; continue; }
            if (str_istarts_with(src, "&ntilde;")) { *dst++ = (char)241; src += 8; continue; }
            if (str_istarts_with(src, "&uuml;")) { *dst++ = (char)252; src += 6; continue; }
            if (str_istarts_with(src, "&iquest;")) { *dst++ = (char)191; src += 8; continue; }
            if (str_istarts_with(src, "&Agrave;")) { *dst++ = (char)192; src += 8; continue; }
            if (str_istarts_with(src, "&Aacute;")) { *dst++ = (char)193; src += 8; continue; }
            if (str_istarts_with(src, "&times;")) { *dst++ = (char)215; src += 7; continue; }
            if (str_istarts_with(src, "&divide;")) { *dst++ = (char)247; src += 8; continue; }
            if (str_istarts_with(src, "&plusmn;")) { *dst++ = (char)177; src += 8; continue; }
            if (str_istarts_with(src, "&micro;")) { *dst++ = (char)181; src += 7; continue; }
            if (str_istarts_with(src, "&para;")) { *dst++ = (char)182; src += 6; continue; }
            if (str_istarts_with(src, "&brvbar;")) { *dst++ = (char)166; src += 8; continue; }
            if (str_istarts_with(src, "&sect;")) { *dst++ = (char)167; src += 6; continue; }
            if (str_istarts_with(src, "&uml;")) { *dst++ = (char)168; src += 5; continue; }
            if (str_istarts_with(src, "&ordf;")) { *dst++ = (char)170; src += 6; continue; }
            if (str_istarts_with(src, "&laquo;")) { *dst++ = (char)171; src += 7; continue; }
            if (str_istarts_with(src, "&not;")) { *dst++ = (char)172; src += 5; continue; }
            if (str_istarts_with(src, "&shy;")) { src += 5; continue; } // Soft hyphen, ignore
            if (str_istarts_with(src, "&macr;")) { *dst++ = (char)175; src += 6; continue; }
            if (str_istarts_with(src, "&sup2;")) { *dst++ = (char)178; src += 6; continue; }
            if (str_istarts_with(src, "&sup3;")) { *dst++ = (char)179; src += 6; continue; }
            if (str_istarts_with(src, "&acute;")) { *dst++ = (char)180; src += 7; continue; }
            if (str_istarts_with(src, "&cedil;")) { *dst++ = (char)184; src += 7; continue; }
            if (str_istarts_with(src, "&sup1;")) { *dst++ = (char)185; src += 6; continue; }
            if (str_istarts_with(src, "&ordm;")) { *dst++ = (char)186; src += 6; continue; }
            if (str_istarts_with(src, "&raquo;")) { *dst++ = (char)187; src += 7; continue; }
            if (str_istarts_with(src, "&frac14;")) { *dst++ = (char)188; src += 8; continue; }
            if (str_istarts_with(src, "&frac12;")) { *dst++ = (char)189; src += 8; continue; }
            if (str_istarts_with(src, "&frac34;")) { *dst++ = (char)190; src += 8; continue; }

            if (src[1] == '#') {
                int val = 0;
                char *end = NULL;
                if (src[2] == 'x' || src[2] == 'X') {
                    val = strtol(src + 3, &end, 16);
                } else {
                    val = strtol(src + 2, &end, 10);
                }
                if (end && *end == ';' && end > src + 2) {
                    if (val == 8211) val = 129; // &ndash;
                    else if (val == 8212) val = 128; // &mdash;
                    else if (val == 8226) val = 130; // &bull;
                    else if (val == 8230) val = 131; // &hellip;
                    else if (val == 8482) val = 132; // &trade;
                    else if (val == 8364) val = 133; // &euro;
                    else if (val == 183) val = 134; // &middot;
                    else if (val == 8216 || val == 8217) val = '\'';
                    else if (val == 8220 || val == 8221) val = '\"';
                    else if (val == 160) val = ' ';
                    
                    if (val > 0 && val < 256) {
                        *dst++ = (char)val;
                        src = end + 1;
                        continue;
                    }
                }
            }
        }
        *dst++ = *src++;
    }
    *dst = 0;
}

static void parse_html(const char *html) {
    browser_clear();
    list_depth = 0;
    cur_line_y = 10; cur_line_x = 10; line_element_count = 0;
    int i = 0; int center_depth = 0; int table_depth = 0; int table_float_depth = 0; int blockquote_depth = 0; bool is_bold = false; bool is_italic = false; bool is_underline = false;
    uint32_t current_color = COLOR_TEXT;
    char current_link[256] = "";
    float current_scale = 15.0f; float base_scale = 15.0f;
    
    FontState font_stack[MAX_FONT_STACK];
    int font_ptr = 0;

    #define EFF_CENTER ((center_depth > 0) && (table_depth == 0))
    bool is_space_pending = false;
    char current_form_action[256] = ""; int current_form_id = 0;
    bool skip_content = false;

    int list_type[16] = {0}; int list_index[16] = {0};

    bool is_pre = false;
    bool is_plaintext = false;
    int table_col = 0;
    next_form_id = 1;
    bool inside_title = false;
    char page_title[256] = "";

    while (html[i] && element_count < MAX_ELEMENTS) {
        if (html[i] == '<' && !is_plaintext) {
            if (html[i+1] == '!' && html[i+2] == '-' && html[i+3] == '-') {
                i += 4;
                while (html[i] && !(html[i] == '-' && html[i+1] == '-' && html[i+2] == '>')) i++;
                if (html[i]) i += 3;
                continue;
            }
            i++; char tag_name[64]; int tag_idx = 0;
            while (html[i] && html[i] != '>' && html[i] != ' ' && tag_idx < 63) tag_name[tag_idx++] = html[i++];
            tag_name[tag_idx] = 0;
            char attr_buf[1024] = "";
            if (html[i] == ' ') {
                i++; int a_idx = 0;
                while (html[i] && html[i] != '>' && a_idx < 1023) attr_buf[a_idx++] = html[i++];
                attr_buf[a_idx] = 0;
            }
            if (html[i] == '>') i++;
            decode_html_entities(attr_buf);

            if (tag_name[0] == '/') {
                if (str_iequals(tag_name+1, "center")) { emit_br(); if (center_depth > 0) center_depth--; }
                else if (str_iequals(tag_name+1, "table")) { 
                    emit_br();
                    if (table_depth > 0 && table_depth == table_float_depth) {
                        table_float_depth = 0;
                        if (element_count < MAX_ELEMENTS) { RenderElement *el = &elements[element_count++]; memset(el, 0, sizeof(RenderElement)); el->tag = 9; }
                    }
                    if (table_depth > 0) table_depth--; table_col = 0; 
                }
                else if (str_iequals(tag_name+1, "tr")) { emit_br(); table_col = 0; }
                else if (str_iequals(tag_name+1, "td") || str_iequals(tag_name+1, "th")) { 
                    table_col++;
                    if (table_col == 1) {
                         // Add spacer to align second column
                         RenderElement *el = &elements[element_count++];
                         memset(el, 0, sizeof(RenderElement));
                         el->tag = TAG_NONE; el->content[0] = ' '; el->content[1] = 0;
                         int current_x = cur_line_x;
                         for (int k=0; k<line_element_count; k++) current_x += elements[line_elements[k]].w;
                         int target_x = 160 + (blockquote_depth * 20) + (list_depth * 20);
                         if (current_x < target_x) {
                             el->w = target_x - current_x;
                         } else {
                             el->w = 10;
                         }
                         el->scale = current_scale; el->list_depth = list_depth; el->blockquote_depth = blockquote_depth;
                    }
                    if (str_iequals(tag_name+1, "th")) is_bold = false;
                }
                else if (str_iequals(tag_name+1, "caption")) { emit_br(); is_bold = false; if (center_depth > 0) center_depth--; }
                else if (str_iequals(tag_name+1, "blockquote")) { emit_br(); if (blockquote_depth > 0) blockquote_depth--; } 
                
                else if (str_iequals(tag_name+1, "ul") || str_iequals(tag_name+1, "ol") || str_iequals(tag_name+1, "dl") || str_iequals(tag_name+1, "dir") || str_iequals(tag_name+1, "menu")) { emit_br(); if (list_depth > 0) list_depth--; }
                
                else if (str_iequals(tag_name+1, "dt")) { emit_br(); is_bold = false; }
                else if (str_iequals(tag_name+1, "dd")) { emit_br(); }
                else if (str_iequals(tag_name+1, "b") || str_iequals(tag_name+1, "strong")) is_bold = false;
                else if (str_iequals(tag_name+1, "i") || str_iequals(tag_name+1, "em") || str_iequals(tag_name+1, "cite") || str_iequals(tag_name+1, "var")) is_italic = false;
                else if (str_iequals(tag_name+1, "u")) is_underline = false;

                else if (tag_name[1] == 'h' && tag_name[2] >= '1' && tag_name[2] <= '6') { emit_br(); emit_br(); is_bold = false; is_italic = false; is_underline = false; base_scale = 15.0f; current_scale = 15.0f; }
                else if (str_iequals(tag_name+1, "form")) {
                    emit_br();
                    current_form_id = 0; current_form_action[0] = 0;
                }
                else if (str_iequals(tag_name+1, "a")) current_link[0] = 0;
                else if (str_iequals(tag_name+1, "p") || str_iequals(tag_name+1, "li") || str_iequals(tag_name+1, "div") || str_iequals(tag_name+1, "address")) { emit_br(); }
                else if (str_iequals(tag_name+1, "pre") || str_iequals(tag_name+1, "xmp") || str_iequals(tag_name+1, "listing")) { emit_br(); is_pre = false; }
                else if (str_iequals(tag_name+1, "font") || str_iequals(tag_name+1, "tt") || str_iequals(tag_name+1, "code") || str_iequals(tag_name+1, "samp") || str_iequals(tag_name+1, "kbd")) {
                    if (font_ptr > 0) {
                        font_ptr--;
                        current_color = font_stack[font_ptr].color;
                        current_scale = font_stack[font_ptr].scale;
                    } else {
                        current_color = COLOR_TEXT;
                        current_scale = base_scale;
                    }
                }
                else if (str_iequals(tag_name+1, "head") || str_iequals(tag_name+1, "script") || str_iequals(tag_name+1, "style") || str_iequals(tag_name+1, "noscript")) skip_content = false;
                else if (str_iequals(tag_name+1, "title")) {
                    inside_title = false;
                    ui_window_set_title(win_browser, page_title);
                    skip_content = true;
                }
            } else {
                if (str_iequals(tag_name, "center")) { emit_br(); center_depth++; }
                else if (str_iequals(tag_name, "table")) { 
                    emit_br(); table_depth++; table_col = 0; 
                    if (str_istrstr(attr_buf, "align=\"right\"") && table_float_depth == 0) {
                        table_float_depth = table_depth;
                        if (element_count < MAX_ELEMENTS) { 
                            RenderElement *el = &elements[element_count++]; memset(el, 0, sizeof(RenderElement)); el->tag = 8;
                            char *bg_str = str_istrstr(attr_buf, "bgcolor=\"");
                            if (bg_str) el->color = parse_html_color(bg_str + 9);
                        }
                    }
                }
                else if (str_iequals(tag_name, "tr")) { emit_br(); table_col = 0; }
                else if (str_iequals(tag_name, "td") || str_iequals(tag_name, "th")) {
                    if (table_col > 0) {
                        RenderElement *el = &elements[element_count++];
                        memset(el, 0, sizeof(RenderElement));
                        el->tag = TAG_NONE; el->content[0] = ' '; el->content[1] = 0; el->w = 10;
                        el->h = ui_get_font_height_scaled(current_scale);
                        el->scale = current_scale; el->list_depth = list_depth; el->blockquote_depth = blockquote_depth;
                    }
                    if (str_iequals(tag_name, "th")) is_bold = true;
                }
                else if (str_iequals(tag_name, "caption")) { emit_br(); center_depth++; is_bold = true; }
                else if (str_iequals(tag_name, "blockquote")) { emit_br(); blockquote_depth++; } // Handle blockquote start
                
                else if (str_iequals(tag_name, "ul") || str_iequals(tag_name, "dir") || str_iequals(tag_name, "menu")) { emit_br(); list_type[list_depth] = 0; list_depth++; }
                else if (str_iequals(tag_name, "ol")) { emit_br(); list_type[list_depth] = 1; list_index[list_depth] = 1; list_depth++; }
                else if (str_iequals(tag_name, "dl")) { emit_br(); list_type[list_depth] = 2; list_depth++; }
                else if (str_iequals(tag_name, "dt")) { emit_br(); is_bold = true; }
                else if (str_iequals(tag_name, "dd")) { 
                    emit_br();
                    RenderElement *el = &elements[element_count++];
                    memset(el, 0, sizeof(RenderElement));
                    el->tag = TAG_NONE;
                    el->content[0] = ' '; el->content[1] = ' '; el->content[2] = ' '; el->content[3] = ' '; el->content[4] = 0;
                    el->w = ui_get_string_width_scaled(el->content, current_scale);
                    el->h = ui_get_font_height_scaled(current_scale);
                    el->color = current_color; el->centered = EFF_CENTER; el->bold = is_bold; el->italic = is_italic; el->underline = is_underline; el->scale = current_scale; el->list_depth = list_depth;
                    el->blockquote_depth = blockquote_depth; // Set blockquote depth
                }

                else if (str_iequals(tag_name, "b") || str_iequals(tag_name, "strong")) is_bold = true;
                else if (str_iequals(tag_name, "i") || str_iequals(tag_name, "em") || str_iequals(tag_name, "cite") || str_iequals(tag_name, "var") || str_iequals(tag_name, "dfn")) is_italic = true;
                else if (str_iequals(tag_name, "u") || str_iequals(tag_name, "s") || str_iequals(tag_name, "strike")) is_underline = true;
                else if (str_iequals(tag_name, "tt") || str_iequals(tag_name, "code") || str_iequals(tag_name, "samp") || str_iequals(tag_name, "kbd") || str_iequals(tag_name, "xmp") || str_iequals(tag_name, "listing")) {
                    if (font_ptr < MAX_FONT_STACK) {
                        font_stack[font_ptr].color = current_color;
                        font_stack[font_ptr].scale = current_scale;
                        font_ptr++;
                    }
                    current_scale = 14.0f;
                    if (str_iequals(tag_name, "xmp") || str_iequals(tag_name, "listing")) { emit_br(); is_pre = true; }
                }
                else if (str_iequals(tag_name, "plaintext")) { emit_br(); is_plaintext = true; is_pre = true; current_scale = 14.0f; }
                else if (str_iequals(tag_name, "address")) { emit_br(); }
                else if (str_iequals(tag_name, "html") || str_iequals(tag_name, "body")) skip_content = false;
                else if (str_iequals(tag_name, "head")) skip_content = true;
                else if (str_iequals(tag_name, "title")) { skip_content = false; inside_title = true; page_title[0] = 0; }

                else if (tag_name[0] == 'h' && tag_name[1] >= '1' && tag_name[1] <= '6') { 
                    emit_br(); emit_br(); is_bold = true; 
                    if (tag_name[1] == '1') base_scale = 32.0f;
                    else if (tag_name[1] == '2') base_scale = 24.0f;
                    else if (tag_name[1] == '3') base_scale = 20.0f;
                    else base_scale = 18.0f;
                    current_scale = base_scale;
                }
                else if (str_iequals(tag_name, "font")) {
                    if (font_ptr < MAX_FONT_STACK) {
                        font_stack[font_ptr].color = current_color;
                        font_stack[font_ptr].scale = current_scale;
                        font_ptr++;
                    }
                    char *color_str = str_istrstr(attr_buf, "color=\"");
                    if (color_str) {
                        current_color = parse_html_color(color_str + 7);
                    } else {
                        color_str = str_istrstr(attr_buf, "color=");
                        if (color_str) current_color = parse_html_color(color_str + 6);
                    }
                    
                    char *size_str = str_istrstr(attr_buf, "size=\"");
                    int offset = 0;
                    if (size_str) {
                        offset = 6;
                    } else {
                        size_str = str_istrstr(attr_buf, "size=");
                        if (size_str) offset = 5;
                    }
                    if (size_str) {
                        char s_char = size_str[offset];
                        if (s_char == '+') {
                            int inc = size_str[offset+1] - '0';
                            int new_sz = 3 + inc;
                            if (new_sz > 7) new_sz = 7;
                            if (new_sz < 1) new_sz = 1;
                            s_char = '0' + new_sz;
                        } else if (s_char == '-') {
                            int dec = size_str[offset+1] - '0';
                            int new_sz = 3 - dec;
                            if (new_sz > 7) new_sz = 7;
                            if (new_sz < 1) new_sz = 1;
                            s_char = '0' + new_sz;
                        }
                        if (s_char == '1') current_scale = 10.0f;
                        else if (s_char == '2') current_scale = 13.0f;
                        else if (s_char == '3') current_scale = 15.0f;
                        else if (s_char == '4') current_scale = 18.0f;
                        else if (s_char == '5') current_scale = 24.0f;
                        else if (s_char == '6') current_scale = 32.0f;
                        else if (s_char >= '7' && s_char <= '9') current_scale = 48.0f;
                    }
                }
                else if (str_iequals(tag_name, "br")) emit_br();
                else if (str_iequals(tag_name, "p") || str_iequals(tag_name, "div")) {
                    emit_br();
                }
                else if (str_iequals(tag_name, "pre")) { emit_br(); is_pre = true; current_scale = 14.0f; }
                else if (str_iequals(tag_name, "li")) {
                    emit_br();
                    RenderElement *el = &elements[element_count++];
                    memset(el, 0, sizeof(RenderElement));
                    el->tag = TAG_NONE;
                    
                    if (list_depth > 0 && list_type[list_depth - 1] == 1) { // OL
                        char num[16];
                        itoa(list_index[list_depth - 1]++, num);
                        int l=0; while(num[l]) { el->content[l] = num[l]; l++; }
                        el->content[l++] = '.'; el->content[l++] = ' '; el->content[l] = 0;
                    } else if (list_depth > 0 && list_type[list_depth - 1] == 2) { // DL
                        // Inside DL, li shouldn't really be used but we treat it as space.
                        el->content[0] = ' '; el->content[1] = 0;
                    } else { // UL
                        el->content[0] = (char)130; el->content[1] = ' '; el->content[2] = 0;
                    }
                    
                    el->w = ui_get_string_width_scaled(el->content, current_scale);
                    el->h = ui_get_font_height_scaled(current_scale);
                    el->color = current_color;
                    el->centered = EFF_CENTER;
                    el->bold = is_bold;
                    el->scale = current_scale;
                    el->list_depth = list_depth;
                    el->blockquote_depth = blockquote_depth; // Set blockquote depth
                }
                else if (str_iequals(tag_name, "form")) {
                    emit_br();
                    current_form_id = next_form_id++;
                    char *action = str_istrstr(attr_buf, "action=\"");
                    if (action) {
                        action += 8; int l = 0;
                        while(action[l] && action[l] != '\"' && l < 255) { current_form_action[l] = action[l]; l++; }
                        current_form_action[l] = 0;
                    } else current_form_action[0] = 0;
                }
                else if (str_iequals(tag_name, "head") || str_iequals(tag_name, "script") || str_iequals(tag_name, "style") || str_iequals(tag_name, "title") || str_iequals(tag_name, "noscript")) skip_content = true;
                else if (str_iequals(tag_name, "body")) skip_content = false;
                else if (str_iequals(tag_name, "a")) {
                    char *href = str_istrstr(attr_buf, "href=\"");
                    if (href) {
                        href += 6; int l = 0;
                        while(href[l] && href[l] != '\"' && l < 255) { current_link[l] = href[l]; l++; }
                        current_link[l] = 0;
                    }
                } else if (str_iequals(tag_name, "hr")) {
                    emit_br();
                    RenderElement *el = &elements[element_count++];
                    memset(el, 0, sizeof(RenderElement));
                    el->tag = TAG_HR; 
                    el->list_depth = list_depth; 
                    el->blockquote_depth = blockquote_depth; // Set blockquote depth
                    el->h = 10; // Extra padding
                    el->centered = true;
                    
                    emit_br();
                } else if (str_iequals(tag_name, "img")) {
                    RenderElement *el = &elements[element_count++];
                    memset(el, 0, sizeof(RenderElement));
                    el->tag = TAG_IMG; el->w = 100; el->h = 80; el->centered = EFF_CENTER;
                    char *width_str = str_istrstr(attr_buf, "width=\"");
                    if (width_str) { int w = atoi(width_str + 7); if (w > 0) el->attr_w = w; }
                    char *src = str_istrstr(attr_buf, "src=\"");
                    if (src) {
                        src += 5; int l = 0;
                        while(src[l] && src[l] != '\"' && l < 255) { el->attr_value[l] = src[l]; l++; }
                        el->attr_value[l] = 0; el->img_loading = true; // Deferred load
                    }
                    if (el->img_pixels) { el->w = el->img_w; el->h = el->img_h; }
                    el->blockquote_depth = blockquote_depth; // Set blockquote depth
                    } else if (str_iequals(tag_name, "input")) {
                    RenderElement *el = &elements[element_count++];
                    memset(el, 0, sizeof(RenderElement));
                    el->tag = TAG_INPUT; el->w = 160; el->h = 20; el->centered = EFF_CENTER;
                    char *size_str = str_istrstr(attr_buf, "size=\"");
                    if (size_str) { int sz = atoi(size_str + 6); if (sz > 0) el->w = sz * 8; }
                    char *val = str_istrstr(attr_buf, "value=\"");
                    char *ph = str_istrstr(attr_buf, "placeholder=\"");
                    char *type = str_istrstr(attr_buf, "type=\"");
                    char *name = str_istrstr(attr_buf, "name=\"");
                    
                    el->form_id = current_form_id;
                    el->input_cursor = 0;
                    el->input_scroll = 0;
                    int l;
                    l = 0; while(current_form_action[l]) { el->form_action[l] = current_form_action[l]; l++; } el->form_action[l] = 0;
                    
                    if (name) {
                        name += 6; l = 0;
                        while(name[l] && name[l] != '\"' && l < 63) { el->input_name[l] = name[l]; l++; }
                        el->input_name[l] = 0;
                    } else {
                        l = 0; const char *dn = "q"; while(dn[l]) { el->input_name[l] = dn[l]; l++; } el->input_name[l] = 0;
                    }
                    
                    if (type) {
                        if (str_istarts_with(type+6, "submit")) el->tag = TAG_BUTTON;
                        else if (str_istarts_with(type+6, "radio")) { el->tag = TAG_RADIO; el->w = 16; el->h = 16; }
                        else if (str_istarts_with(type+6, "checkbox")) { el->tag = TAG_CHECKBOX; el->w = 16; el->h = 16; }
                    }
                    if (str_istrstr(attr_buf, "checked")) el->checked = true;
                    
                    if (val) {
                        val += 7; int l = 0;
                        while(val[l] && val[l] != '\"' && l < 255) { el->attr_value[l] = val[l]; l++; }
                        el->attr_value[l] = 0;
                    } else if (ph) {
                        ph += 13; int l = 0;
                        while(ph[l] && ph[l] != '\"' && l < 255) { el->attr_value[l] = ph[l]; l++; }
                        el->attr_value[l] = 0;
                    } else el->attr_value[0] = 0;
                    if (el->tag == TAG_BUTTON) {
                        el->w = ui_get_string_width(el->attr_value) + 20;
                    }
                    el->blockquote_depth = blockquote_depth;
                    }
            }
        } else {
            if (!skip_content) {
                if (is_pre) {
                    char word[256]; int w_idx = 0;
                    while (html[i] && html[i] != '<') {
                        if (html[i] == '\n' || html[i] == '\r') {
                            if (w_idx > 0) {
                                word[w_idx] = 0; decode_html_entities(word);
                                if (inside_title) {
                                    int current_len = 0; while (page_title[current_len]) current_len++;
                                    if (current_len > 0 && current_len < 254) { page_title[current_len++] = ' '; page_title[current_len] = 0; }
                                    int k = 0; while (word[k] && current_len < 254) { page_title[current_len++] = word[k++]; }
                                    page_title[current_len] = 0;
                                } else {
                                    int word_w = ui_get_string_width_scaled(word, current_scale);
                                    RenderElement *el = &elements[element_count++];
                                    memset(el, 0, sizeof(RenderElement));
                                    int k=0; while(word[k]) { el->content[k] = word[k]; k++; } el->content[k] = 0;
                                    el->w = word_w; el->h = ui_get_font_height_scaled(current_scale);
                                    el->tag = TAG_NONE; el->color = current_link[0] ? COLOR_LINK : current_color;
                                    el->centered = EFF_CENTER; el->bold = is_bold;
                                    el->italic = is_italic; el->underline = is_underline;
                                    el->scale = current_scale; el->list_depth = list_depth; el->blockquote_depth = blockquote_depth;
                                    if (current_link[0]) { int k=0; while(current_link[k]) { el->link_url[k] = current_link[k]; k++; } el->link_url[k] = 0; }
                                }
                                w_idx = 0;
                            }
                            emit_br(); i++; if (html[i] == '\n' && html[i-1] == '\r') i++;
                        } else {
                            if (w_idx < 254) word[w_idx++] = html[i++];
                        }
                    }
                    if (w_idx > 0) {
                        word[w_idx] = 0; decode_html_entities(word);
                        if (inside_title) {
                            int current_len = 0; while (page_title[current_len]) current_len++;
                            if (current_len > 0 && current_len < 254) { page_title[current_len++] = ' '; page_title[current_len] = 0; }
                            int k = 0; while (word[k] && current_len < 254) { page_title[current_len++] = word[k++]; }
                            page_title[current_len] = 0;
                        } else {
                            int word_w = ui_get_string_width_scaled(word, current_scale);
                            RenderElement *el = &elements[element_count++];
                            memset(el, 0, sizeof(RenderElement));
                            int k=0; while(word[k]) { el->content[k] = word[k]; k++; } el->content[k] = 0;
                            el->w = word_w; el->h = ui_get_font_height_scaled(current_scale);
                            el->tag = TAG_NONE; el->color = current_link[0] ? COLOR_LINK : current_color;
                            el->centered = EFF_CENTER; el->bold = is_bold;
                            el->italic = is_italic; el->underline = is_underline;
                            el->scale = current_scale; el->list_depth = list_depth; el->blockquote_depth = blockquote_depth;
                            if (current_link[0]) { int k=0; while(current_link[k]) { el->link_url[k] = current_link[k]; k++; } el->link_url[k] = 0; }
                        }
                    }
                } else {
                    while (html[i] && (html[i] == ' ' || html[i] == '\n' || html[i] == '\r')) { is_space_pending = true; i++; }
                    while (html[i] && html[i] != '<') {
                        char word[256]; int w_idx = 0;
                        if (is_space_pending) {
                            is_space_pending = false;
                            if (element_count < MAX_ELEMENTS && !inside_title) {
                                RenderElement *el = &elements[element_count++];
                                memset(el, 0, sizeof(RenderElement));
                                el->tag = TAG_NONE; el->content[0] = ' '; el->content[1] = 0;
                                el->w = ui_get_string_width_scaled(" ", current_scale);
                                el->h = ui_get_font_height_scaled(current_scale);
                                el->color = current_color; el->centered = EFF_CENTER; el->bold = is_bold;
                                el->italic = is_italic; el->underline = is_underline;
                                el->scale = current_scale; el->list_depth = list_depth; el->blockquote_depth = blockquote_depth;
                            }
                        }
                        while (html[i] && html[i] != '<' && html[i] != ' ' && html[i] != '\n' && html[i] != '\r' && w_idx < 254) word[w_idx++] = html[i++];
                        if (html[i] == ' ' || html[i] == '\n' || html[i] == '\r') {
                            is_space_pending = true;
                            while (html[i] && (html[i] == ' ' || html[i] == '\n' || html[i] == '\r')) i++;
                        }
                        word[w_idx] = 0; decode_html_entities(word);
                        if (inside_title) {
                            int current_len = 0; while (page_title[current_len]) current_len++;
                            if (current_len > 0 && current_len < 254) { page_title[current_len++] = ' '; page_title[current_len] = 0; }
                            int k = 0; while (word[k] && current_len < 254) { page_title[current_len++] = word[k++]; }
                            page_title[current_len] = 0;
                        } else if (w_idx > 0) {
                            if (element_count < MAX_ELEMENTS) {
                                int word_w = ui_get_string_width_scaled(word, current_scale);
                                RenderElement *el = &elements[element_count++];
                                memset(el, 0, sizeof(RenderElement));
                                int k=0; while(word[k]) { el->content[k] = word[k]; k++; } el->content[k] = 0;
                                el->w = word_w; el->h = ui_get_font_height_scaled(current_scale);
                                el->tag = TAG_NONE; el->color = current_link[0] ? COLOR_LINK : current_color;
                                el->centered = EFF_CENTER; el->bold = is_bold;
                                el->italic = is_italic; el->underline = is_underline;
                                el->scale = current_scale; el->list_depth = list_depth; el->blockquote_depth = blockquote_depth;
                                if (current_link[0]) { int k=0; while(current_link[k]) { el->link_url[k] = current_link[k]; k++; } el->link_url[k] = 0; }
                            }
                        }
                    }
                }
            } else {
                while (html[i] && html[i] != '<') i++;
            }
        }
    }
    emit_br();
}

static void parse_html_incremental(const char *html, int safe_len) {
    if (inc_parse_offset == 0) {
        browser_clear();
        list_depth = 0;
        cur_line_y = 10; cur_line_x = 10; line_element_count = 0;
        inc_center_depth = 0; inc_table_depth = 0; inc_table_float_depth = 0; inc_blockquote_depth = 0; 
        inc_is_bold = false; inc_is_italic = false; inc_is_underline = false;
        inc_current_color = COLOR_TEXT; inc_current_link[0] = 0;
        inc_current_scale = 15.0f; inc_base_scale = 15.0f;
        inc_is_space_pending = false;
        inc_form_action[0] = 0; inc_form_id = 0;
        inc_skip_content = false;
        inc_is_pre = false;
        for (int k=0; k<16; k++) { inc_list_type[k] = 0; inc_list_index[k] = 0; }
        next_form_id = 1;
        inc_inside_title = false;
        current_page_title[0] = 0;
    }

    int i = inc_parse_offset;
    int center_depth = inc_center_depth;
    int table_depth = inc_table_depth;
    int table_float_depth = inc_table_float_depth;
    int blockquote_depth = inc_blockquote_depth;
    bool is_bold = inc_is_bold;
    bool is_italic = inc_is_italic;
    bool is_underline = inc_is_underline;
    uint32_t current_color = inc_current_color;
    char current_link[256]; { int k=0; while(inc_current_link[k]) { current_link[k] = inc_current_link[k]; k++; } current_link[k] = 0; }
    float current_scale = inc_current_scale;
    float base_scale = inc_base_scale;
    bool is_space_pending = inc_is_space_pending;
    int list_type[16]; for (int k=0; k<16; k++) list_type[k] = inc_list_type[k];
    int list_index[16]; for (int k=0; k<16; k++) list_index[k] = inc_list_index[k];
    char current_form_action[256]; { int k=0; while(inc_form_action[k]) { current_form_action[k] = inc_form_action[k]; k++; } current_form_action[k] = 0; }
    int current_form_id = inc_form_id;
    bool skip_content = inc_skip_content;
    bool is_pre = inc_is_pre;
    bool is_plaintext = false; // We don't persist plaintext across incremental chunks easily
    int table_col = 0; 
    bool inside_title = inc_inside_title;
    char page_title[256]; { int k=0; while(current_page_title[k]) { page_title[k] = current_page_title[k]; k++; } page_title[k] = 0; }

    #undef EFF_CENTER
    #define EFF_CENTER ((center_depth > 0) && (table_depth == 0))

    while (i < safe_len && html[i] && element_count < MAX_ELEMENTS) {
        if (html[i] == '<' && !is_plaintext) {
            if (i + 3 < safe_len && html[i+1] == '!' && html[i+2] == '-' && html[i+3] == '-') {
                i += 4;
                while (i < safe_len && html[i] && !(i + 2 < safe_len && html[i] == '-' && html[i+1] == '-' && html[i+2] == '>')) i++;
                if (i + 2 < safe_len && html[i] == '-' && html[i+1] == '-' && html[i+2] == '>') i += 3;
                continue;
            }
            i++; char tag_name[64]; int tag_idx = 0;
            while (i < safe_len && html[i] && html[i] != '>' && html[i] != ' ' && tag_idx < 63) tag_name[tag_idx++] = html[i++];
            tag_name[tag_idx] = 0;
            char attr_buf[1024] = "";
            if (i < safe_len && html[i] == ' ') {
                i++; int a_idx = 0;
                while (i < safe_len && html[i] && html[i] != '>' && a_idx < 1023) attr_buf[a_idx++] = html[i++];
                attr_buf[a_idx] = 0;
            }
            if (i < safe_len && html[i] == '>') i++;
            decode_html_entities(attr_buf);

            if (tag_name[0] == '/') {
                if (str_iequals(tag_name+1, "center")) { emit_br(); if (center_depth > 0) center_depth--; }
                else if (str_iequals(tag_name+1, "table")) { 
                    emit_br();
                    if (table_depth > 0 && table_depth == table_float_depth) {
                        table_float_depth = 0;
                        if (element_count < MAX_ELEMENTS) { RenderElement *el = &elements[element_count++]; memset(el, 0, sizeof(RenderElement)); el->tag = 9; }
                    }
                    if (table_depth > 0) table_depth--; table_col = 0; 
                }
                else if (str_iequals(tag_name+1, "tr")) { emit_br(); table_col = 0; }
                else if (str_iequals(tag_name+1, "td") || str_iequals(tag_name+1, "th")) {
                    table_col++;
                    if (table_col == 1) {
                         RenderElement *el = &elements[element_count++];
                         memset(el, 0, sizeof(RenderElement));
                         el->tag = TAG_NONE; el->content[0] = ' '; el->content[1] = 0;
                         int current_x = cur_line_x;
                         for (int k=0; k<line_element_count; k++) current_x += elements[line_elements[k]].w;
                         int target_x = 160 + (blockquote_depth * 20) + (list_depth * 20);
                         if (current_x < target_x) el->w = target_x - current_x; else el->w = 10;
                         el->scale = current_scale; el->list_depth = list_depth; el->blockquote_depth = blockquote_depth;
                    }
                    if (str_iequals(tag_name+1, "th")) is_bold = false;
                }
                else if (str_iequals(tag_name+1, "caption")) { emit_br(); is_bold = false; if (center_depth > 0) center_depth--; }
                else if (str_iequals(tag_name+1, "blockquote")) { emit_br(); if (blockquote_depth > 0) blockquote_depth--; }
                else if (str_iequals(tag_name+1, "ul") || str_iequals(tag_name+1, "ol") || str_iequals(tag_name+1, "dl") || str_iequals(tag_name+1, "dir") || str_iequals(tag_name+1, "menu")) { emit_br(); if (list_depth > 0) list_depth--; }
                else if (str_iequals(tag_name+1, "dt")) { emit_br(); is_bold = false; }
                else if (str_iequals(tag_name+1, "dd")) { emit_br(); }
                else if (str_iequals(tag_name+1, "b") || str_iequals(tag_name+1, "strong")) is_bold = false;
                else if (str_iequals(tag_name+1, "i") || str_iequals(tag_name+1, "em") || str_iequals(tag_name+1, "cite") || str_iequals(tag_name+1, "var")) is_italic = false;
                else if (str_iequals(tag_name+1, "u")) is_underline = false;
                else if (str_iequals(tag_name+1, "address")) emit_br();
                else if (tag_name[1] == 'h' && tag_name[2] >= '1' && tag_name[2] <= '6') { emit_br(); emit_br(); is_bold = false; is_italic = false; is_underline = false; base_scale = 15.0f; current_scale = 15.0f; }
                else if (str_iequals(tag_name+1, "form")) { emit_br(); current_form_id = 0; current_form_action[0] = 0; }
                else if (str_iequals(tag_name+1, "a")) current_link[0] = 0;
                else if (str_iequals(tag_name+1, "p") || str_iequals(tag_name+1, "li") || str_iequals(tag_name+1, "div")) { emit_br(); }
                else if (str_iequals(tag_name+1, "pre") || str_iequals(tag_name+1, "xmp") || str_iequals(tag_name+1, "listing")) { emit_br(); is_pre = false; }
                else if (str_iequals(tag_name+1, "font") || str_iequals(tag_name+1, "tt") || str_iequals(tag_name+1, "code") || str_iequals(tag_name+1, "samp") || str_iequals(tag_name+1, "kbd")) {
                    if (inc_font_ptr > 0) {
                        inc_font_ptr--;
                        current_color = inc_font_stack[inc_font_ptr].color;
                        current_scale = inc_font_stack[inc_font_ptr].scale;
                    } else {
                        current_color = COLOR_TEXT;
                        current_scale = base_scale;
                    }
                }
                else if (str_iequals(tag_name+1, "head") || str_iequals(tag_name+1, "script") || str_iequals(tag_name+1, "style") || str_iequals(tag_name+1, "noscript")) skip_content = false;
                else if (str_iequals(tag_name+1, "title")) {
                    inside_title = false;
                    ui_window_set_title(win_browser, page_title);
                    skip_content = true;
                }
                } else {
                if (str_iequals(tag_name, "center")) { emit_br(); center_depth++; }
                else if (str_iequals(tag_name, "table")) { 
                    emit_br(); table_depth++; table_col = 0; 
                    if (str_istrstr(attr_buf, "align=\"right\"") && table_float_depth == 0) {
                        table_float_depth = table_depth;
                        if (element_count < MAX_ELEMENTS) { 
                            RenderElement *el = &elements[element_count++]; memset(el, 0, sizeof(RenderElement)); el->tag = 8;
                            char *bg_str = str_istrstr(attr_buf, "bgcolor=\"");
                            if (bg_str) el->color = parse_html_color(bg_str + 9);
                        }
                    }
                }
                else if (str_iequals(tag_name, "tr")) { emit_br(); table_col = 0; }
                else if (str_iequals(tag_name, "td") || str_iequals(tag_name, "th")) {
                    if (table_col > 0) {
                        RenderElement *el = &elements[element_count++];
                        memset(el, 0, sizeof(RenderElement));
                        el->tag = TAG_NONE; el->content[0] = ' '; el->content[1] = 0; el->w = 10;
                        el->h = ui_get_font_height_scaled(current_scale);
                        el->scale = current_scale; el->list_depth = list_depth; el->blockquote_depth = blockquote_depth;
                    }
                    if (str_iequals(tag_name, "th")) is_bold = true;
                }
                else if (str_iequals(tag_name, "caption")) { emit_br(); center_depth++; is_bold = true; }
                else if (str_iequals(tag_name, "blockquote")) { emit_br(); blockquote_depth++; }
                else if (str_iequals(tag_name, "ul") || str_iequals(tag_name, "dir") || str_iequals(tag_name, "menu")) { emit_br(); list_type[list_depth] = 0; list_depth++; }
                else if (str_iequals(tag_name, "ol")) { emit_br(); list_type[list_depth] = 1; list_index[list_depth] = 1; list_depth++; }
                else if (str_iequals(tag_name, "dl")) { emit_br(); list_type[list_depth] = 2; list_depth++; }
                else if (str_iequals(tag_name, "dt")) { emit_br(); is_bold = true; }
                else if (str_iequals(tag_name, "dd")) { 
                    emit_br(); RenderElement *el = &elements[element_count++]; memset(el, 0, sizeof(RenderElement));
                    el->tag = TAG_NONE; el->content[0] = ' '; el->content[1] = ' '; el->content[2] = ' '; el->content[3] = ' '; el->content[4] = 0;
                    el->w = ui_get_string_width_scaled(el->content, current_scale); el->h = ui_get_font_height_scaled(current_scale); el->color = current_color; el->centered = EFF_CENTER; el->bold = is_bold; el->italic = is_italic; el->underline = is_underline; el->scale = current_scale; el->list_depth = list_depth; el->blockquote_depth = blockquote_depth;
                }
                else if (str_iequals(tag_name, "b") || str_iequals(tag_name, "strong")) is_bold = true;
                else if (str_iequals(tag_name, "i") || str_iequals(tag_name, "em") || str_iequals(tag_name, "cite") || str_iequals(tag_name, "var") || str_iequals(tag_name, "dfn")) is_italic = true;
                else if (str_iequals(tag_name, "u") || str_iequals(tag_name, "s") || str_iequals(tag_name, "strike")) is_underline = true;
                else if (str_iequals(tag_name, "address")) emit_br();
                else if (str_iequals(tag_name, "tt") || str_iequals(tag_name, "code") || str_iequals(tag_name, "samp") || str_iequals(tag_name, "kbd") || str_iequals(tag_name, "xmp") || str_iequals(tag_name, "listing")) {
                    if (inc_font_ptr < MAX_FONT_STACK) {
                        inc_font_stack[inc_font_ptr].color = current_color;
                        inc_font_stack[inc_font_ptr].scale = current_scale;
                        inc_font_ptr++;
                    }
                    current_scale = 14.0f;
                    if (str_iequals(tag_name, "xmp") || str_iequals(tag_name, "listing")) { emit_br(); is_pre = true; }
                }
                else if (str_iequals(tag_name, "plaintext")) { emit_br(); is_plaintext = true; is_pre = true; current_scale = 14.0f; }
                else if (tag_name[0] == 'h' && tag_name[1] >= '1' && tag_name[1] <= '6') {
                    emit_br(); emit_br(); is_bold = true;
                    if (tag_name[1] == '1') base_scale = 32.0f; else if (tag_name[1] == '2') base_scale = 24.0f; else if (tag_name[1] == '3') base_scale = 20.0f; else base_scale = 18.0f;
                    current_scale = base_scale;
                }
                else if (str_iequals(tag_name, "font")) {
                    if (inc_font_ptr < MAX_FONT_STACK) {
                        inc_font_stack[inc_font_ptr].color = current_color;
                        inc_font_stack[inc_font_ptr].scale = current_scale;
                        inc_font_ptr++;
                    }
                    char *color_str = str_istrstr(attr_buf, "color=\"");
                    if (color_str) current_color = parse_html_color(color_str + 7); else { color_str = str_istrstr(attr_buf, "color="); if (color_str) current_color = parse_html_color(color_str + 6); }
                    char *size_str = str_istrstr(attr_buf, "size=\""); int offset = 0; if (size_str) offset = 6; else { size_str = str_istrstr(attr_buf, "size="); if (size_str) offset = 5; }
                    if (size_str) {
                        char s_char = size_str[offset];
                        if (s_char == '+') { int inc = size_str[offset+1] - '0'; int new_sz = 3 + inc; if (new_sz > 7) new_sz = 7; s_char = '0' + new_sz; }
                        else if (s_char == '-') { int dec = size_str[offset+1] - '0'; int new_sz = 3 - dec; if (new_sz < 1) new_sz = 1; s_char = '0' + new_sz; }
                        if (s_char == '1') current_scale = 10.0f; else if (s_char == '2') current_scale = 13.0f; else if (s_char == '3') current_scale = 15.0f; else if (s_char == '4') current_scale = 18.0f; else if (s_char == '5') current_scale = 24.0f; else if (s_char == '6') current_scale = 32.0f; else if (s_char >= '7' && s_char <= '9') current_scale = 48.0f;
                    }
                }
                else if (str_iequals(tag_name, "br")) emit_br();
                else if (str_iequals(tag_name, "p") || str_iequals(tag_name, "div")) { 
                    emit_br();
                }
                else if (str_iequals(tag_name, "pre")) { emit_br(); is_pre = true; current_scale = 14.0f; }
                else if (str_iequals(tag_name, "li")) {
                    emit_br();
                    RenderElement *el = &elements[element_count++]; memset(el, 0, sizeof(RenderElement));
                    el->tag = TAG_NONE; if (list_depth > 0 && list_type[list_depth - 1] == 1) { char num[16]; itoa(list_index[list_depth - 1]++, num); int l=0; while(num[l]) { el->content[l] = num[l]; l++; } el->content[l++] = '.'; el->content[l++] = ' '; el->content[l] = 0; }
                    else if (list_depth > 0 && list_type[list_depth - 1] == 2) { el->content[0] = ' '; el->content[1] = 0; } else { el->content[0] = (char)130; el->content[1] = ' '; el->content[2] = 0; }
                    el->w = ui_get_string_width_scaled(el->content, current_scale); el->h = ui_get_font_height_scaled(current_scale); el->color = current_color; el->centered = EFF_CENTER; el->bold = is_bold; el->scale = current_scale; el->list_depth = list_depth; el->blockquote_depth = blockquote_depth;
                }
                else if (str_iequals(tag_name, "form")) { emit_br(); current_form_id = next_form_id++; char *action = str_istrstr(attr_buf, "action=\""); if (action) { action += 8; int l = 0; while(action[l] && action[l] != '"' && l < 255) { current_form_action[l] = action[l]; l++; } current_form_action[l] = 0; } else current_form_action[0] = 0; }
                else if (str_iequals(tag_name, "head") || str_iequals(tag_name, "script") || str_iequals(tag_name, "style") || str_iequals(tag_name, "noscript")) skip_content = true;
                else if (str_iequals(tag_name, "title")) { skip_content = false; inside_title = true; page_title[0] = 0; }
                else if (str_iequals(tag_name, "body")) skip_content = false;
                else if (str_iequals(tag_name, "a")) { char *href = str_istrstr(attr_buf, "href=\""); if (href) { href += 6; int l = 0; while(href[l] && href[l] != '"' && l < 255) { current_link[l] = href[l]; l++; } current_link[l] = 0; } }
                else if (str_iequals(tag_name, "hr")) { emit_br(); RenderElement *el = &elements[element_count++]; for (int k=0; k<(int)sizeof(RenderElement); k++) ((char*)el)[k] = 0; el->tag = TAG_HR; el->list_depth = list_depth; el->blockquote_depth = blockquote_depth; el->h = 10; el->centered = true; emit_br(); }
                else if (str_iequals(tag_name, "img")) {
                    RenderElement *el = &elements[element_count++]; for (int k=0; k<(int)sizeof(RenderElement); k++) ((char*)el)[k] = 0; el->tag = TAG_IMG; el->w = 100; el->h = 80; el->centered = EFF_CENTER;
                    char *width_str = str_istrstr(attr_buf, "width=\""); if (width_str) { int w = atoi(width_str + 7); if (w > 0) el->attr_w = w; }
                    char *src = str_istrstr(attr_buf, "src=\""); if (src) { src += 5; int l = 0; while(src[l] && src[l] != '"' && l < 255) { el->attr_value[l] = src[l]; l++; } el->attr_value[l] = 0; el->img_loading = true; }
                    el->blockquote_depth = blockquote_depth;
                }
                else if (str_iequals(tag_name, "input")) {
                    RenderElement *el = &elements[element_count++]; for (int k=0; k<(int)sizeof(RenderElement); k++) ((char*)el)[k] = 0; el->tag = TAG_INPUT; el->w = 160; el->h = 20; el->centered = EFF_CENTER;
                    char *size_str = str_istrstr(attr_buf, "size=\""); if (size_str) { int sz = atoi(size_str + 6); if (sz > 0) el->w = sz * 8; }
                    char *val = str_istrstr(attr_buf, "value=\""); char *ph = str_istrstr(attr_buf, "placeholder=\""); char *type = str_istrstr(attr_buf, "type=\""); char *name = str_istrstr(attr_buf, "name=\"");
                    el->form_id = current_form_id; el->input_cursor = 0; el->input_scroll = 0; int l; l = 0; while(current_form_action[l]) { el->form_action[l] = current_form_action[l]; l++; } el->form_action[l] = 0;
                    if (name) { name += 6; l = 0; while(name[l] && name[l] != '"' && l < 63) { el->input_name[l] = name[l]; l++; } el->input_name[l] = 0; } else { l = 0; const char *dn = "q"; while(dn[l]) { el->input_name[l] = dn[l]; l++; } el->input_name[l] = 0; }
                    if (type) {
                        if (str_istarts_with(type+6, "submit")) el->tag = TAG_BUTTON;
                        else if (str_istarts_with(type+6, "radio")) { el->tag = TAG_RADIO; el->w = 16; el->h = 16; }
                        else if (str_istarts_with(type+6, "checkbox")) { el->tag = TAG_CHECKBOX; el->w = 16; el->h = 16; }
                    }
                    if (str_istrstr(attr_buf, "checked")) el->checked = true;
                    if (val) { val += 7; int l = 0; while(val[l] && val[l] != '"' && l < 255) { el->attr_value[l] = val[l]; l++; } el->attr_value[l] = 0; } else if (ph) { ph += 13; int l = 0; while(ph[l] && ph[l] != '"' && l < 255) { el->attr_value[l] = ph[l]; l++; } el->attr_value[l] = 0; } else el->attr_value[0] = 0;
                    if (el->tag == TAG_BUTTON) {
                        el->w = ui_get_string_width(el->attr_value) + 20;
                    }
                    el->blockquote_depth = blockquote_depth;
                }
            }
        } else {
            if (!skip_content) {
                if (is_pre) {
                    char word[256]; int w_idx = 0;
                    while (i < safe_len && html[i] && html[i] != '<') {
                        if (html[i] == '\n' || html[i] == '\r') {
                            if (w_idx > 0) {
                                word[w_idx] = 0; decode_html_entities(word); int word_w = ui_get_string_width_scaled(word, current_scale);
                                RenderElement *el = &elements[element_count++]; memset(el, 0, sizeof(RenderElement));
                                int k=0; while(word[k]) { el->content[k] = word[k]; k++; } el->content[k] = 0;
                                el->w = word_w; el->h = ui_get_font_height_scaled(current_scale); el->tag = TAG_NONE; el->color = current_link[0] ? COLOR_LINK : current_color; el->centered = EFF_CENTER; el->bold = is_bold; el->italic = is_italic; el->underline = is_underline; el->scale = current_scale; el->list_depth = list_depth; el->blockquote_depth = blockquote_depth;
                                if (current_link[0]) { int k=0; while(current_link[k]) { el->link_url[k] = current_link[k]; k++; } el->link_url[k] = 0; }
                                w_idx = 0;
                            }
                            emit_br(); i++; if (i < safe_len && html[i] == '\n' && html[i-1] == '\r') i++;
                        } else { if (w_idx < 254) word[w_idx++] = html[i]; i++; }
                    }
                    if (w_idx > 0) {
                        word[w_idx] = 0; decode_html_entities(word); int word_w = ui_get_string_width_scaled(word, current_scale);
                        RenderElement *el = &elements[element_count++]; memset(el, 0, sizeof(RenderElement));
                        int k=0; while(word[k]) { el->content[k] = word[k]; k++; } el->content[k] = 0;
                        el->w = word_w; el->h = ui_get_font_height_scaled(current_scale); el->tag = TAG_NONE; el->color = current_link[0] ? COLOR_LINK : current_color; el->centered = EFF_CENTER; el->bold = is_bold; el->italic = is_italic; el->underline = is_underline; el->scale = current_scale; el->list_depth = list_depth; el->blockquote_depth = blockquote_depth;
                        if (current_link[0]) { int k=0; while(current_link[k]) { el->link_url[k] = current_link[k]; k++; } el->link_url[k] = 0; }
                    }
                } else {
                    while (i < safe_len && html[i] && (html[i] == ' ' || html[i] == '\n' || html[i] == '\r')) { is_space_pending = true; i++; }
                    while (i < safe_len && html[i] && html[i] != '<') {
                        char word[256]; int w_idx = 0;
                        if (is_space_pending) {
                            is_space_pending = false;
                            if (element_count < MAX_ELEMENTS) {
                                RenderElement *el = &elements[element_count++]; memset(el, 0, sizeof(RenderElement));
                                el->tag = TAG_NONE; el->content[0] = ' '; el->content[1] = 0; el->w = ui_get_string_width_scaled(" ", current_scale); el->h = ui_get_font_height_scaled(current_scale); el->color = current_color; el->centered = EFF_CENTER; el->bold = is_bold; el->italic = is_italic; el->underline = is_underline; el->scale = current_scale; el->list_depth = list_depth; el->blockquote_depth = blockquote_depth;
                            }
                        }
                        while (i < safe_len && html[i] && html[i] != '<' && html[i] != ' ' && html[i] != '\n' && html[i] != '\r' && w_idx < 254) { word[w_idx++] = html[i++]; }
                        if (i < safe_len && (html[i] == ' ' || html[i] == '\n' || html[i] == '\r')) { is_space_pending = true; while (i < safe_len && (html[i] == ' ' || html[i] == '\n' || html[i] == '\r')) i++; }
                        word[w_idx] = 0; decode_html_entities(word); w_idx = 0; while (word[w_idx]) w_idx++;
                        if (inside_title) {
                            int current_len = 0; while (page_title[current_len]) current_len++;
                            if (current_len > 0 && current_len < 254) { page_title[current_len++] = ' '; page_title[current_len] = 0; }
                            int k = 0; while (word[k] && current_len < 254) { page_title[current_len++] = word[k++]; }
                            page_title[current_len] = 0;
                            w_idx = 0; continue;
                        }

                        if (w_idx > 0) {
                            if (element_count < MAX_ELEMENTS) {
                                int word_w = ui_get_string_width_scaled(word, current_scale);
                                RenderElement *el = &elements[element_count++];
                                for (int k=0; k<(int)sizeof(RenderElement); k++) ((char*)el)[k] = 0;
                                int k=0; while(word[k]) { el->content[k] = word[k]; k++; } el->content[k] = 0;
                                el->w = word_w; el->h = ui_get_font_height_scaled(current_scale);
                                el->tag = TAG_NONE; el->color = current_link[0] ? COLOR_LINK : current_color;
                                el->centered = EFF_CENTER; el->bold = is_bold; el->italic = is_italic; el->underline = is_underline;
                                el->scale = current_scale; el->list_depth = list_depth; el->blockquote_depth = blockquote_depth;
                                if (current_link[0]) { int k=0; while(current_link[k]) { el->link_url[k] = current_link[k]; k++; } el->link_url[k] = 0; }
                                w_idx = 0;
                            }
                        }
                    }
                }
            } else {
                while (i < safe_len && html[i] && html[i] != '<') i++;
            }
        }
    }
    emit_br();

    inc_parse_offset = i; inc_center_depth = center_depth; inc_table_depth = table_depth; inc_table_float_depth = table_float_depth; inc_blockquote_depth = blockquote_depth;
    inc_is_bold = is_bold; inc_is_italic = is_italic; inc_is_underline = is_underline; inc_current_color = current_color;
    { int k=0; while(current_link[k]) { inc_current_link[k] = current_link[k]; k++; } inc_current_link[k] = 0; }
    inc_current_scale = current_scale; inc_base_scale = base_scale; inc_is_space_pending = is_space_pending;
    for (int k=0; k<16; k++) { inc_list_type[k] = list_type[k]; inc_list_index[k] = list_index[k]; }
    { int k=0; while(current_form_action[k]) { inc_form_action[k] = current_form_action[k]; k++; } inc_form_action[k] = 0; }
    inc_form_id = current_form_id; inc_skip_content = skip_content; inc_is_pre = is_pre; inc_inside_title = inside_title;
    { int k=0; while(page_title[k]) { current_page_title[k] = page_title[k]; k++; } current_page_title[k] = 0; }
}

static void browser_paint(void) {
    browser_ctx.user_data = (void *)win_browser;
    ui_draw_rect(win_browser, 0, 0, win_w, win_h, COLOR_BG);
    
    for (int i = 0; i < element_count; i++) {
        RenderElement *el = &elements[i];
        int draw_y = el->y - scroll_y + URL_BAR_H;
        int el_h = el->h;
        if (el->tag == TAG_IMG && el->img_h > el_h) el_h = el->img_h;
        if (draw_y + el_h < URL_BAR_H || draw_y > win_h) continue;
        
        if (el->tag == 8) {
            if (el->color != 0 && el->color != COLOR_BG) {
                ui_draw_rect(win_browser, el->x, draw_y, el->w, el->h + 10, el->color);
            }
            continue;
        }
        
        if (el->tag == TAG_IMG) {
            uint32_t *pixels = el->img_pixels;
            if (el->img_frames) pixels = el->img_frames[el->img_current_frame];
            if (pixels) ui_draw_image(win_browser, el->x, draw_y, el->img_w, el->img_h, pixels);
            else ui_draw_rect(win_browser, el->x, draw_y, 100, 80, 0xFFCCCCCC);
        } else if (el->tag == TAG_INPUT) {
            browser_ctx.use_light_theme = true;
            char visible[128];
            int v_len = 0;
            int max_v = (el->w - 10) / 8;
            if (max_v > 127) max_v = 127;
            for (int k = el->input_scroll; el->attr_value[k] && v_len < max_v; k++) {
                visible[v_len++] = el->attr_value[k];
            }
            visible[v_len] = 0;

            widget_textbox_t tb;
            widget_textbox_init(&tb, el->x, draw_y, el->w, el->h, visible, max_v);
            tb.cursor_pos = el->input_cursor - el->input_scroll;
            if (tb.cursor_pos < 0) tb.cursor_pos = 0;
            tb.focused = (focused_element == i);
            widget_textbox_draw(&browser_ctx, &tb);
            browser_ctx.use_light_theme = false;
        } else if (el->tag == TAG_BUTTON) {
            browser_ctx.use_light_theme = true;
            widget_button_t btn;
            widget_button_init(&btn, el->x, draw_y, el->w, el->h, el->attr_value);
            widget_button_draw(&browser_ctx, &btn);
            browser_ctx.use_light_theme = false;
        } else if (el->tag == TAG_RADIO) {
            browser_ctx.use_light_theme = true;
            widget_checkbox_t cb;
            widget_checkbox_init(&cb, el->x, draw_y, el->w, el->h, "", true);
            cb.checked = el->checked;
            widget_checkbox_draw(&browser_ctx, &cb);
            browser_ctx.use_light_theme = false;
        } else if (el->tag == TAG_CHECKBOX) {
            browser_ctx.use_light_theme = true;
            widget_checkbox_t cb;
            widget_checkbox_init(&cb, el->x, draw_y, el->w, el->h, "", false);
            cb.checked = el->checked;
            widget_checkbox_draw(&browser_ctx, &cb);
            browser_ctx.use_light_theme = false;
        } else if (el->tag == TAG_HR) {
            ui_draw_rect(win_browser, el->x, draw_y + el->h / 2, el->w, 2, 0xFF888888);
            ui_draw_rect(win_browser, el->x, draw_y + (el->h / 2) + 2, el->w, 1, 0xFFFFFFFF);
        } else if (el->tag == TAG_NONE) {
            if (el->content[0] != ' ' || el->content[1] != 0) {
                ui_draw_string_scaled(win_browser, el->x, draw_y, el->content, el->color, el->scale);
            }
            if (el->bold) {
                ui_draw_string_scaled(win_browser, el->x + 1, draw_y, el->content, el->color, el->scale);
            }
            if (el->italic) {
                ui_draw_string_scaled(win_browser, el->x + 1, draw_y - 1, el->content, el->color, el->scale);
            }
            if (el->underline) {
                int fh = el->h;
                ui_draw_rect(win_browser, el->x, draw_y + fh - 1, el->w, 1, el->color);
            }
        }
    }

    ui_draw_rect(win_browser, 0, 0, win_w, URL_BAR_H, COLOR_URL_BAR);
    
    widget_textbox_init(&url_tb, 10, 5, win_w - SCROLL_BAR_W - BTN_W*2 - BTN_PAD*2 - 20, 20, url_input_buffer, 511);
    url_tb.cursor_pos = url_cursor;
    url_tb.focused = (focused_element == -1);
    widget_textbox_draw(&browser_ctx, &url_tb);

    // Back button
    int btn_y = (URL_BAR_H - BTN_H) / 2;
    widget_button_init(&btn_back, BACK_BTN_X, btn_y, BTN_W, BTN_H, "<");
    widget_button_draw(&browser_ctx, &btn_back);

    // Home button
    widget_button_init(&btn_home, HOME_BTN_X, btn_y, BTN_W, BTN_H, "H");
    widget_button_draw(&browser_ctx, &btn_home);
    
    // Scroll bar
    int viewport_h = win_h - URL_BAR_H;
    browser_scrollbar.x = win_w - SCROLL_BAR_W;
    browser_scrollbar.y = URL_BAR_H;
    browser_scrollbar.w = SCROLL_BAR_W;
    browser_scrollbar.h = viewport_h;
    browser_scrollbar.on_scroll = browser_on_scroll;
    widget_scrollbar_update(&browser_scrollbar, total_content_height, scroll_y);
    widget_scrollbar_draw(&browser_ctx, &browser_scrollbar);
}

static void navigate(const char *url) {
    static char main_resp[RESP_BUF_SIZE];
    int resp_len = fetch_content(url, main_resp, sizeof(main_resp), true);
    if (resp_len <= 0) return;
    char *body = strstr(main_resp, "\r\n\r\n");
    if (body) {
        body += 4;
        int hdr_len = body - main_resp;
        int body_len = resp_len - hdr_len;
        if (strstr(main_resp, "Transfer-Encoding: chunked")) {
            body_len = decode_chunked_bin(body, body_len);
            parse_html(body);
        } else {
            // For non-chunked: finish incremental parse instead of re-parsing from scratch
            parse_html_incremental(body, body_len);
        }
    }
}

static void net_init_if_needed(void) {
    if (!sys_network_is_initialized()) sys_network_init();
    if (!sys_network_has_ip()) sys_network_dhcp_acquire();
}

int main(int argc, char **argv) {
    win_browser = ui_window_create("Bored Web", 50, 50, win_w, win_h);
    ui_window_set_resizable(win_browser, true);
    ui_set_font(win_browser, "/Library/Fonts/times.ttf");
    net_init_if_needed();
    if (argc > 1) { int k=0; while(argv[1][k]) { url_input_buffer[k] = argv[1][k]; k++; } url_input_buffer[k] = 0; url_cursor = k; }
    navigate(url_input_buffer);
    browser_reflow(); browser_paint(); ui_mark_dirty(win_browser, 0, 0, win_w, win_h);
    gui_event_t ev;
    bool needs_repaint = false;
    while (1) {
        while (ui_get_event(win_browser, &ev)) {
            if (ev.type == GUI_EVENT_PAINT) { needs_repaint = true; }

            if (ev.type == 11) { // GUI_EVENT_RESIZE
                win_w = ev.arg1;
                win_h = ev.arg2;
                browser_reflow();
                needs_repaint = true;
                continue;
            }

            else if (ev.type == GUI_EVENT_CLICK || ev.type == GUI_EVENT_MOUSE_DOWN || ev.type == GUI_EVENT_MOUSE_UP || ev.type == GUI_EVENT_MOUSE_MOVE) {
                int mx = ev.arg1;
                int my = ev.arg2;
                bool is_down = (ev.type == GUI_EVENT_MOUSE_DOWN || (ev.type == GUI_EVENT_MOUSE_MOVE && browser_scrollbar.is_dragging));
                bool is_click = (ev.type == GUI_EVENT_CLICK);
                
                int old_scroll = scroll_y;
                bool was_dragging = browser_scrollbar.is_dragging;
                if (widget_scrollbar_handle_mouse(&browser_scrollbar, mx, my, is_down, &browser_ctx)) {
                    if (scroll_y != old_scroll || browser_scrollbar.is_dragging || was_dragging) {
                        needs_repaint = true;
                    }
                    if (ev.type == GUI_EVENT_MOUSE_MOVE) continue;
                    if (is_down || is_click) continue;
                }
                
                if (ev.type != GUI_EVENT_CLICK && ev.type != GUI_EVENT_MOUSE_DOWN) continue;

                if (my < URL_BAR_H) {
                    if (widget_textbox_handle_mouse(&url_tb, mx, my, is_click, NULL)) {
                        focused_element = -1;
                        needs_repaint = true;
                        continue;
                    }
                    if (widget_button_handle_mouse(&btn_back, mx, my, is_down, is_click, NULL)) {
                        if (is_click && history_count > 0) {
                            history_count--;
                            int j=0; while(history_stack[history_count][j]) { url_input_buffer[j] = history_stack[history_count][j]; j++; } url_input_buffer[j] = 0; url_cursor = j;
                            navigate(url_input_buffer); scroll_y = 0; focused_element = -1;
                        }
                        needs_repaint = true; continue;
                    }
                    if (widget_button_handle_mouse(&btn_home, mx, my, is_down, is_click, NULL)) {
                        if (is_click) {
                            if (history_count < HISTORY_MAX) { int j=0; while(url_input_buffer[j]) { history_stack[history_count][j] = url_input_buffer[j]; j++; } history_stack[history_count][j] = 0; history_count++; }
                            const char *home = "http://find.boreddev.nl";
                            int j=0; while(home[j]) { url_input_buffer[j] = home[j]; j++; } url_input_buffer[j] = 0; url_cursor = j;
                            navigate(url_input_buffer); scroll_y = 0; focused_element = -1;
                        }
                        needs_repaint = true; continue;
                    }
                    if (is_click) {
                        focused_element = -1; needs_repaint = true; 
                    }
                    continue;
                }
                my = ev.arg2 - URL_BAR_H + scroll_y;
                bool found = false;
                for (int i = 0; i < element_count; i++) {
                    RenderElement *el = &elements[i];
                    if (mx >= el->x && mx < el->x + el->w && my >= el->y && my < el->y + el->h) {
                        if (el->tag == TAG_INPUT) {
                            focused_element = i;
                            int len = 0; while(el->attr_value[len]) len++;
                            el->input_cursor = len;
                            int max_v = (el->w - 10) / 8;
                            if (el->input_cursor < el->input_scroll) el->input_scroll = el->input_cursor;
                            if (el->input_cursor >= el->input_scroll + max_v) el->input_scroll = el->input_cursor - max_v + 1;
                            found = true; needs_repaint = true; break;
                        }
                        if (el->tag == TAG_RADIO) {
                            for (int k = 0; k < element_count; k++) {
                                if (elements[k].tag == TAG_RADIO && elements[k].form_id == el->form_id && str_iequals(elements[k].input_name, el->input_name)) {
                                    if (elements[k].checked) {
                                        elements[k].checked = false;
                                        widget_checkbox_t cb;
                                        widget_checkbox_init(&cb, elements[k].x, elements[k].y - scroll_y + URL_BAR_H, elements[k].w, elements[k].h, "", true);
                                        cb.checked = false;
                                        browser_ctx.use_light_theme = true;
                                        widget_checkbox_draw(&browser_ctx, &cb);
                                        browser_ctx.use_light_theme = false;
                                        ui_mark_dirty(win_browser, cb.x, cb.y, cb.w, cb.h);
                                    }
                                }
                            }
                            el->checked = true;
                            widget_checkbox_t cb;
                            widget_checkbox_init(&cb, el->x, el->y - scroll_y + URL_BAR_H, el->w, el->h, "", true);
                            cb.checked = true;
                            browser_ctx.use_light_theme = true;
                            widget_checkbox_draw(&browser_ctx, &cb);
                            browser_ctx.use_light_theme = false;
                            ui_mark_dirty(win_browser, cb.x, cb.y, cb.w, cb.h);
                            found = true; break;
                        }
                        if (el->tag == TAG_CHECKBOX) {
                            el->checked = !el->checked;
                            widget_checkbox_t cb;
                            widget_checkbox_init(&cb, el->x, el->y - scroll_y + URL_BAR_H, el->w, el->h, "", false);
                            cb.checked = el->checked;
                            browser_ctx.use_light_theme = true;
                            widget_checkbox_draw(&browser_ctx, &cb);
                            browser_ctx.use_light_theme = false;
                            ui_mark_dirty(win_browser, cb.x, cb.y, cb.w, cb.h);
                            found = true; break;
                        }
                        if (el->tag == TAG_BUTTON) {
                            int fid = el->form_id;
                            if (fid > 0) {
                                char search_url[1024];
                                char *u = search_url;
                                const char *s;
                                if (el->form_action[0] == '/') {
                                    s = "http://"; while(*s) *u++ = *s++;
                                    s = current_host; while(*s) *u++ = *s++;
                                    if (current_port != 80) {
                                        *u++ = ':';
                                        char pbuf[10]; itoa(current_port, pbuf);
                                        const char* ps = pbuf; while(*ps) *u++ = *ps++;
                                    }
                                    s = el->form_action; while(*s) *u++ = *s++;
                                } else if (str_istarts_with(el->form_action, "http")) {
                                    s = el->form_action; while(*s) *u++ = *s++;
                                } else {
                                    s = "http://"; while(*s) *u++ = *s++;
                                    s = current_host; while(*s) *u++ = *s++;
                                    if (current_port != 80) {
                                        *u++ = ':';
                                        char pbuf[10]; itoa(current_port, pbuf);
                                        const char* ps = pbuf; while(*ps) *u++ = *ps++;
                                    }
                                    if (current_host[0] && current_host[0] != '/') *u++ = '/';
                                    if (el->form_action[0]) { s = el->form_action; while(*s) *u++ = *s++; }
                                }
                                
                                bool first_param = true;
                                for (int k = 0; k < element_count; k++) {
                                    RenderElement *rel = &elements[k];
                                    if (rel->form_id != fid) continue;
                                    
                                    bool include = false;
                                    const char *val_ptr = NULL;
                                    if (rel->tag == TAG_INPUT) {
                                        include = true;
                                        val_ptr = rel->attr_value;
                                    } else if ((rel->tag == TAG_RADIO || rel->tag == TAG_CHECKBOX) && rel->checked) {
                                        include = true;
                                        val_ptr = rel->attr_value;
                                        if (!val_ptr[0]) val_ptr = "on";
                                    }
                                    
                                    if (include && rel->input_name[0]) {
                                        if (first_param) {
                                            s = (strstr(search_url, "?") ? "&" : "?");
                                            while(*s) *u++ = *s++;
                                            first_param = false;
                                        } else {
                                            *u++ = '&';
                                        }
                                        
                                        s = rel->input_name; while(*s) *u++ = *s++;
                                        *u++ = '=';
                                        
                                        for (int m = 0; val_ptr[m] && (u - search_url) < 1020; m++) {
                                            char sc = val_ptr[m];
                                            if (sc == ' ') *u++ = '+';
                                            else *u++ = sc;
                                        }
                                    }
                                }
                                *u = 0;
                                if (history_count < HISTORY_MAX) { int j=0; while(url_input_buffer[j]) { history_stack[history_count][j] = url_input_buffer[j]; j++; } history_stack[history_count][j] = 0; history_count++; }
                                int j=0; while(search_url[j]) { url_input_buffer[j] = search_url[j]; j++; } url_input_buffer[j] = 0; url_cursor = j;
                                navigate(url_input_buffer); scroll_y = 0; focused_element = -1;
                                needs_repaint = true;
                                found = true; break;
                            }
                        }
                        if (el->link_url[0]) {
                            char new_url[512];
                            if (el->link_url[0] == '/') {
                                char *u = new_url; const char *s = "http://"; while(*s) *u++ = *s++;
                                s = current_host; while(*s) *u++ = *s++;
                                if (current_port != 80) {
                                    *u++ = ':';
                                    char pbuf[10]; itoa(current_port, pbuf);
                                    const char* ps = pbuf; while(*ps) *u++ = *ps++;
                                }
                                s = el->link_url; while(*s) *u++ = *s++; *u = 0;
                            } else if (str_istarts_with(el->link_url, "http")) {
                                int k=0; while(el->link_url[k]) { new_url[k] = el->link_url[k]; k++; } new_url[k] = 0;
                            } else {
                                char *u = new_url; const char *s = "http://"; while(*s) *u++ = *s++;
                                s = current_host; while(*s) *u++ = *s++; 
                                if (current_port != 80) {
                                    *u++ = ':';
                                    char pbuf[10]; itoa(current_port, pbuf);
                                    const char* ps = pbuf; while(*ps) *u++ = *ps++;
                                }
                                if (current_host[0] && current_host[0] != '/') *u++ = '/';
                                s = el->link_url; while(*s) *u++ = *s++; *u = 0;
                            }
                            if (history_count < HISTORY_MAX) { int j=0; while(url_input_buffer[j]) { history_stack[history_count][j] = url_input_buffer[j]; j++; } history_stack[history_count][j] = 0; history_count++; }
                            int j=0; while(new_url[j]) { url_input_buffer[j] = new_url[j]; j++; } url_input_buffer[j] = 0; url_cursor = j;
                            navigate(url_input_buffer); scroll_y = 0; focused_element = -1;
                            needs_repaint = true;
                            found = true; break;
                        }
                    }
                }
                if (!found) { focused_element = -1; needs_repaint = true; }
            } else if (ev.type == GUI_EVENT_KEY || ev.type == GUI_EVENT_KEYUP) {
                if (ev.type == GUI_EVENT_KEYUP) continue;
                char c = (char)ev.arg1;
                if (focused_element == -1) {
                    if (c == 13 || c == 10) {
                        if (history_count < HISTORY_MAX) { int j=0; while(url_input_buffer[j]) { history_stack[history_count][j] = url_input_buffer[j]; j++; } history_stack[history_count][j] = 0; history_count++; }
                        navigate(url_input_buffer); scroll_y = 0;
                        needs_repaint = true;
                    }
                    else if (c == 19) { if (url_cursor > 0) url_cursor--; }
                    else if (c == 20) { int len = 0; while(url_input_buffer[len]) len++; if (url_cursor < len) url_cursor++; }
                    else if (c == 127 || c == 8) { 
                        if (url_cursor > 0) {
                            int len = 0; while(url_input_buffer[len]) len++;
                            for (int k=url_cursor-1; k<len; k++) url_input_buffer[k] = url_input_buffer[k+1];
                            url_cursor--;
                        }
                    }
                    else if (c >= 32 && c <= 126 && url_cursor < 511) { 
                        int len = 0; while(url_input_buffer[len]) len++;
                        for (int k=len; k>=url_cursor; k--) url_input_buffer[k+1] = url_input_buffer[k];
                        url_input_buffer[url_cursor++] = c;
                    }
                } else {
                    RenderElement *el = &elements[focused_element];
                    int len = 0; while(el->attr_value[len]) len++;
                    if (c == 13 || c == 10) { 
                        char search_url[1024];
                        char *u = search_url;
                        const char *s;
                        if (el->form_action[0] == '/') {
                            s = "http://"; while(*s) *u++ = *s++;
                            s = current_host; while(*s) *u++ = *s++;
                            if (current_port != 80) {
                                *u++ = ':';
                                char pbuf[10]; itoa(current_port, pbuf);
                                const char* ps = pbuf; while(*ps) *u++ = *ps++;
                            }
                            s = el->form_action; while(*s) *u++ = *s++;
                        } else if (str_istarts_with(el->form_action, "http")) {
                            s = el->form_action; while(*s) *u++ = *s++;
                        } else {
                            s = "http://"; while(*s) *u++ = *s++;
                            s = current_host; while(*s) *u++ = *s++;
                            if (current_port != 80) {
                                *u++ = ':';
                                char pbuf[10]; itoa(current_port, pbuf);
                                const char* ps = pbuf; while(*ps) *u++ = *ps++;
                            }
                            if (current_host[0] && current_host[0] != '/') *u++ = '/';
                            if (el->form_action[0]) { s = el->form_action; while(*s) *u++ = *s++; }
                        }
                        
                        s = (strstr(search_url, "?") ? "&" : "?");
                        while(*s) *u++ = *s++;
                        s = el->input_name; while(*s) *u++ = *s++;
                        *u++ = '=';
                        
                        for (int m=0; el->attr_value[m] && (u - search_url) < 1020; m++) {
                            char sc = el->attr_value[m];
                            if (sc == ' ') *u++ = '+';
                            else *u++ = sc;
                        }
                        *u = 0;
                        int j=0; while(search_url[j]) { url_input_buffer[j] = search_url[j]; j++; } url_input_buffer[j] = 0; url_cursor = j;
                        navigate(url_input_buffer); scroll_y = 0; focused_element = -1;
                        needs_repaint = true;
                    }
                    else if (c == 19) { if (el->input_cursor > 0) el->input_cursor--; }
                    else if (c == 20) { if (el->input_cursor < len) el->input_cursor++; }
                    else if (c == 127 || c == 8) { 
                        if (el->input_cursor > 0) {
                            for (int k=el->input_cursor-1; k<len; k++) el->attr_value[k] = el->attr_value[k+1];
                            el->input_cursor--;
                        }
                    }
                    else if (c >= 32 && c <= 126 && len < 255) { 
                        for (int k=len; k>=el->input_cursor; k--) el->attr_value[k+1] = el->attr_value[k];
                        el->attr_value[el->input_cursor++] = c;
                    }

                    int max_v = (el->w - 10) / 8;
                    if (el->input_cursor < el->input_scroll) el->input_scroll = el->input_cursor;
                    if (el->input_cursor >= el->input_scroll + max_v) el->input_scroll = el->input_cursor - max_v + 1;
                }
                
                if (c == 17) { scroll_y -= 40; }
                else if (c == 18) { scroll_y += 40; }
                else if (c == 19) { scroll_y -= 200; } // Page Up
                else if (c == 20) { scroll_y += 200; } // Page Down

                int max_scroll = total_content_height - (win_h - URL_BAR_H);
                if (max_scroll < 0) max_scroll = 0;
                if (scroll_y > max_scroll) scroll_y = max_scroll;
                if (scroll_y < 0) scroll_y = 0;

                needs_repaint = true;
            } else if (ev.type == 9) { // GUI_EVENT_MOUSE_WHEEL
                scroll_y += ev.arg1 * 20;
                int max_scroll = total_content_height - (win_h - URL_BAR_H);
                if (max_scroll < 0) max_scroll = 0;
                if (scroll_y > max_scroll) scroll_y = max_scroll;
                if (scroll_y < 0) scroll_y = 0;
                needs_repaint = true;
            } else if (ev.type == GUI_EVENT_CLOSE) sys_exit(0);
        }
        if (needs_repaint) {
            browser_reflow(); browser_paint(); ui_mark_dirty(win_browser, 0, 0, win_w, win_h);
            needs_repaint = false;
        }

        // Background image loading
        bool loaded_any = false;
        for (int i = 0; i < element_count; i++) {
            if (elements[i].tag == TAG_IMG && elements[i].img_loading && !elements[i].img_pixels && !elements[i].img_failed) {
                load_image(&elements[i]);
                loaded_any = true;
                break; // Load one at a time to stay responsive
            }
        }
        if (loaded_any) {
            browser_reflow(); browser_paint(); ui_mark_dirty(win_browser, 0, 0, win_w, win_h);
        }

        // Animated GIF progress
        bool gif_updated = false;
        long long now = sys_system(SYSTEM_CMD_GET_TICKS, 0, 0, 0, 0);
        for (int i = 0; i < element_count; i++) {
            if (elements[i].tag == TAG_IMG && elements[i].img_frames && elements[i].img_frame_count > 1) {
                if (now >= elements[i].next_frame_tick) {
                    elements[i].img_current_frame = (elements[i].img_current_frame + 1) % elements[i].img_frame_count;
                    elements[i].next_frame_tick = now + (elements[i].img_delays[elements[i].img_current_frame] * 60 / 1000);
                    if (elements[i].next_frame_tick <= now) elements[i].next_frame_tick = now + 1;
                    gif_updated = true;
                }
            }
        }
        if (gif_updated) {
            browser_paint(); ui_mark_dirty(win_browser, 0, 0, win_w, win_h);
        } else {
            sleep(10);
        }
    }
    return 0;
}
