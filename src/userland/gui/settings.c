// Copyright (c) 2023-2026 Chris (boreddevnl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
// BOREDOS_APP_DESC: System configuration and preferences.
// BOREDOS_APP_ICONS: /Library/images/icons/colloid/preferences-system.png;/Library/images/icons/colloid/preferences-system-services.png
#include "libc/syscall.h"
#include "libc/libui.h"
#include "libc/stdlib.h"
#include "stb_image.h"
#include <stddef.h>
#include <stdint.h>
#include "../../wm/libwidget.h"
#include "utf-8.h"

extern void sys_parallel_run(void (*fn)(void*), void **args, int count);

#define COLOR_COFFEE    0xFF6B4423
#define COLOR_TEAL      0xFF008080
#define COLOR_GREEN     0xFF008000
#define COLOR_BLUE_BG   0xFF000080
#define COLOR_PURPLE    0xFF800080
#define COLOR_GREY      0xFF454545
#define COLOR_BLACK     0xFF000000

#define COLOR_DARK_PANEL  0xFF2D2D2D
#define COLOR_DARK_TEXT   0xFFE0E0E0

static void settings_draw_rect(void *user_data, int x, int y, int w, int h, uint32_t color) {
    ui_draw_rect((ui_window_t)user_data, x, y, w, h, color);
}

static void settings_draw_rounded_rect_filled(void *user_data, int x, int y, int w, int h, int r, uint32_t color) {
    ui_draw_rounded_rect_filled((ui_window_t)user_data, x, y, w, h, r, color);
}

static void settings_draw_string(void *user_data, int x, int y, const char *str, uint32_t color) {
    ui_draw_string((ui_window_t)user_data, x, y, str, color);
}

static int settings_measure_string_width(void *user_data, const char *str) {
    (void)user_data;
    return (int)ui_get_string_width(str);
}

static void settings_mark_dirty(void *user_data, int x, int y, int w, int h) {
    ui_mark_dirty((ui_window_t)user_data, x, y, w, h);
}

static widget_context_t settings_ctx = {
    .draw_rect = settings_draw_rect,
    .draw_rounded_rect_filled = settings_draw_rounded_rect_filled,
    .draw_string = settings_draw_string,
    .measure_string_width = settings_measure_string_width,
    .mark_dirty = settings_mark_dirty
};

static widget_checkbox_t chk_snap;
static widget_checkbox_t chk_align;
static widget_dropdown_t drop_res;
static widget_dropdown_t drop_color;
static widget_dropdown_t drop_keyboard;
static widget_textbox_t tb_r, tb_g, tb_b;
static widget_textbox_t tb_ip, tb_dns;
static widget_button_t btn_apply, btn_back;
static widget_slider_t slider_mouse;
static widget_slider_t slider_cursor_size;

#define MAX_WALLPAPERS 10

static widget_button_t btn_main_wallpaper, btn_main_network, btn_main_desktop, btn_main_mouse, btn_main_fonts, btn_main_display, btn_main_keyboard;
static widget_button_t btn_wp_colors[6];
static widget_button_t btn_wp_patterns[2];
static widget_button_t btn_wp_apply;
static widget_button_t btn_wp_thumbs[MAX_WALLPAPERS];

static widget_button_t btn_net_init;
static widget_button_t btn_net_set_ip, btn_net_set_dns;

static widget_button_t btn_dt_cols_minus, btn_dt_cols_plus;
static widget_button_t btn_dt_rows_minus, btn_dt_rows_plus;

typedef struct {
    char path[128];
    char name[48];
} font_entry_t;

static widget_button_t *btn_fonts = NULL;
static font_entry_t *fonts = NULL;
static int font_capacity = 0;
static widget_scrollbar_t font_scrollbar;
static int font_scroll_y = 0;
static widget_textbox_t tb_custom_w, tb_custom_h;

#define SETTINGS_ICON_MAIN_SIZE 32
#define SETTINGS_ICON_LIST_SIZE 18

#define SETTINGS_ICON_UNTRIED 0
#define SETTINGS_ICON_LOADED  1
#define SETTINGS_ICON_FAILED  2

enum settings_icon_id {
    SETTINGS_ICON_WALLPAPER = 0,
    SETTINGS_ICON_NETWORK,
    SETTINGS_ICON_DESKTOP,
    SETTINGS_ICON_MOUSE,
    SETTINGS_ICON_FONTS,
    SETTINGS_ICON_DISPLAY,
    SETTINGS_ICON_KEYBOARD,
    SETTINGS_ICON_COUNT,// must be last
};

static const char *settings_icon_names[SETTINGS_ICON_COUNT] = {
    "preferences-desktop-wallpaper.png",
    "preferences-system-network-ethernet.png",
    "desktop.png",
    "input-mouse.png",
    "fonts.png",
    "preferences-desktop-display.png",
    "input-keyboard.png"
};

static int settings_icon_main_state[SETTINGS_ICON_COUNT];
static int settings_icon_list_state[SETTINGS_ICON_COUNT];
static uint32_t settings_icon_main_pixels[SETTINGS_ICON_COUNT][SETTINGS_ICON_MAIN_SIZE * SETTINGS_ICON_MAIN_SIZE];
static uint32_t settings_icon_list_pixels[SETTINGS_ICON_COUNT][SETTINGS_ICON_LIST_SIZE * SETTINGS_ICON_LIST_SIZE];

#define COLOR_PURPLE    0xFF800080
#define COLOR_GREY      0xFF454545
#define COLOR_BLACK     0xFF000000

#define COLOR_DARK_PANEL  0xFF2D2D2D
#define COLOR_DARK_TEXT   0xFFE0E0E0
#define COLOR_DARK_BORDER 0xFF404040
#define COLOR_DKGRAY      0xFFAAAAAA
#define COLOR_DARK_BG     0xFF1E1E1E

// Control panel state
#define VIEW_MAIN 0
#define VIEW_WALLPAPER 1
#define VIEW_NETWORK 2
#define VIEW_DESKTOP 3
#define VIEW_MOUSE 4
#define VIEW_FONTS 5
#define VIEW_DISPLAY 6
#define VIEW_KEYBOARD 7

static int disp_sel_res = 2;
static int disp_sel_color = 0;

static int current_view = VIEW_MAIN;
static char rgb_r[4] = "";
static char rgb_g[4] = "";
static char rgb_b[4] = "";
static char custom_res_w[6] = "";
static char custom_res_h[6] = "";
static char net_ip[16] = "";
static char net_dns[16] = "";
static int focused_field = -1;
static int input_cursor = 0;
static int keyboard_layout = 0;

static int dyn_res_w[3];
static int dyn_res_h[3];
static char dyn_res_str[3][32];

static void init_dynamic_resolutions(void) {
    uint64_t phys_w = 1920, phys_h = 1080;
    ui_get_screen_size(&phys_w, &phys_h);
    
    dyn_res_w[2] = (int)phys_w; dyn_res_h[2] = (int)phys_h;
    dyn_res_w[1] = (int)((phys_w * 3) / 4); dyn_res_h[1] = (int)((phys_h * 3) / 4);
    dyn_res_w[0] = (int)(phys_w / 2); dyn_res_h[0] = (int)(phys_h / 2);
    
    for (int i = 0; i < 2; i++) {
        dyn_res_w[i] &= ~1;
        dyn_res_h[i] &= ~1;
    }

    for (int i = 0; i < 3; i++) {
        char bw[16], bh[16];
        itoa(dyn_res_w[i], bw);
        itoa(dyn_res_h[i], bh);
        strcpy(dyn_res_str[i], bw);
        strcat(dyn_res_str[i], "x");
        strcat(dyn_res_str[i], bh);
    }
}

static char net_status[64] = "";

// Pattern buffers (128x128)
#define PATTERN_SIZE 128
static uint32_t pattern_lumberjack[PATTERN_SIZE * PATTERN_SIZE];
static uint32_t pattern_blue_diamond[PATTERN_SIZE * PATTERN_SIZE];

#define WALLPAPER_THUMB_W 80
#define WALLPAPER_THUMB_H 50

typedef struct {
    char path[128];
    char name[64];
    uint32_t thumb[WALLPAPER_THUMB_W * WALLPAPER_THUMB_H];
    _Bool valid;
} wallpaper_entry_t;

static wallpaper_entry_t wallpapers[MAX_WALLPAPERS];
static int wallpaper_count = 0;

static _Bool desktop_snap_to_grid = 1;
static _Bool desktop_auto_align = 1;
static int desktop_max_rows_per_col = 10;
static int desktop_max_cols = 10;
static int mouse_speed = 10;
static int mouse_cursor_scale_tenths = 10;

static int font_count = 0;
static int selected_font = -1;

static void cli_itoa(int num, char *str) {
    if (num == 0) {
        str[0] = '0';
        str[1] = '\0';
        return;
    }
    int t = num;
    int len = 0;
    while (t > 0) { len++; t /= 10; }
    str[len] = '\0';
    for (int i = len - 1; i >= 0; i--) {
        str[i] = (num % 10) + '0';
        num /= 10;
    }
}

static void format_scale_tenths(int scale_tenths, char *str) {
    if (scale_tenths < 10) scale_tenths = 10;
    if (scale_tenths > 40) scale_tenths = 40;

    char whole[4];
    cli_itoa(scale_tenths / 10, whole);
    strcpy(str, whole);

    int len = 0;
    while (str[len]) len++;
    str[len++] = '.';
    str[len++] = (char)('0' + (scale_tenths % 10));
    str[len++] = 'x';
    str[len] = 0;
}

static void generate_lumberjack_pattern(void) {
    uint32_t red = 0xFFDC143C;
    uint32_t dark_grey = 0xFF404040;
    uint32_t black = 0xFF000000;
    int scale = 5;
    
    for (int y = 0; y < PATTERN_SIZE; y++) {
        for (int x = 0; x < PATTERN_SIZE; x++) {
            int cell_x = (x / scale) % 3;
            int cell_y = (y / scale) % 3;
            uint32_t color;
            if (cell_x == 1 && cell_y == 1) {
                color = black;
            } else if (cell_x == 1 || cell_y == 1) {
                color = dark_grey;
            } else {
                color = red;
            }
            pattern_lumberjack[y * PATTERN_SIZE + x] = color;
        }
    }
}

static void itoa_hex(uint64_t num, char* str) {
    if (num == 0) {
        str[0] = '0';
        str[1] = '\0';
        return;
    }
    char buf[64];
    int len = 0;
    while (num > 0) {
        int rem = num % 16;
        if (rem < 10) buf[len++] = rem + '0';
        else buf[len++] = rem - 10 + 'a';
        num /= 16;
    }
    for (int i = 0; i < len; i++) {
        str[i] = buf[len - 1 - i];
    }
    str[len] = '\0';
}

static void scale_rgba_to_argb(const unsigned char *rgba, int src_w, int src_h, uint32_t *dst, int dst_w, int dst_h) {
    for (int y = 0; y < dst_h; y++) {
        int src_y = (dst_h <= 1 || src_h <= 1) ? 0 : (y * (src_h - 1)) / (dst_h - 1);
        for (int x = 0; x < dst_w; x++) {
            int src_x = (dst_w <= 1 || src_w <= 1) ? 0 : (x * (src_w - 1)) / (dst_w - 1);
            int idx = (src_y * src_w + src_x) * 4;
            uint8_t r = rgba[idx];
            uint8_t g = rgba[idx + 1];
            uint8_t b = rgba[idx + 2];
            uint8_t a = rgba[idx + 3];
            dst[y * dst_w + x] = ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
        }
    }
}

static void settings_try_load_icon_variant(int icon_id, int dst_w, int dst_h, uint32_t *dst_pixels, int *state) {
    if (!state || !dst_pixels || icon_id < 0 || icon_id >= SETTINGS_ICON_COUNT) return;
    if (*state != SETTINGS_ICON_UNTRIED) return;

    *state = SETTINGS_ICON_FAILED;

    char path[160];
    strcpy(path, "/Library/images/icons/colloid/");
    strcat(path, settings_icon_names[icon_id]);

    int fd = sys_open(path, "r");
    if (fd < 0) return;

    int size = sys_seek(fd, 0, 2);
    sys_seek(fd, 0, 0);
    if (size <= 0 || size > 4 * 1024 * 1024) {
        sys_close(fd);
        return;
    }

    unsigned char *buf = (unsigned char *)malloc((size_t)size);
    if (!buf) {
        sys_close(fd);
        return;
    }

    int bytes_read = sys_read(fd, buf, size);
    sys_close(fd);
    if (bytes_read <= 0) {
        free(buf);
        return;
    }

    int img_w, img_h, channels;
    unsigned char *img = stbi_load_from_memory(buf, bytes_read, &img_w, &img_h, &channels, 4);
    free(buf);
    if (!img || img_w <= 0 || img_h <= 0) {
        if (img) stbi_image_free(img);
        return;
    }

    scale_rgba_to_argb(img, img_w, img_h, dst_pixels, dst_w, dst_h);
    stbi_image_free(img);
    *state = SETTINGS_ICON_LOADED;
}

static void settings_draw_icon(ui_window_t win, int icon_id, int x, int y, bool list_variant) {
    if (icon_id < 0 || icon_id >= SETTINGS_ICON_COUNT) return;

    if (list_variant) {
        settings_try_load_icon_variant(
            icon_id,
            SETTINGS_ICON_LIST_SIZE,
            SETTINGS_ICON_LIST_SIZE,
            settings_icon_list_pixels[icon_id],
            &settings_icon_list_state[icon_id]
        );
        if (settings_icon_list_state[icon_id] == SETTINGS_ICON_LOADED) {
            ui_draw_image(win, x, y, SETTINGS_ICON_LIST_SIZE, SETTINGS_ICON_LIST_SIZE, settings_icon_list_pixels[icon_id]);
            return;
        }
        ui_draw_rounded_rect_filled(win, x, y, SETTINGS_ICON_LIST_SIZE, SETTINGS_ICON_LIST_SIZE, 4, 0xFF3A3A3A);
        return;
    }

    settings_try_load_icon_variant(
        icon_id,
        SETTINGS_ICON_MAIN_SIZE,
        SETTINGS_ICON_MAIN_SIZE,
        settings_icon_main_pixels[icon_id],
        &settings_icon_main_state[icon_id]
    );
    if (settings_icon_main_state[icon_id] == SETTINGS_ICON_LOADED) {
        ui_draw_image(win, x, y, SETTINGS_ICON_MAIN_SIZE, SETTINGS_ICON_MAIN_SIZE, settings_icon_main_pixels[icon_id]);
        return;
    }
    ui_draw_rounded_rect_filled(win, x, y, SETTINGS_ICON_MAIN_SIZE, SETTINGS_ICON_MAIN_SIZE, 6, 0xFF3A3A3A);
}

static void load_settings_icons(void) {
    for (int i = 0; i < SETTINGS_ICON_COUNT; i++) {
        settings_try_load_icon_variant(
            i,
            SETTINGS_ICON_MAIN_SIZE,
            SETTINGS_ICON_MAIN_SIZE,
            settings_icon_main_pixels[i],
            &settings_icon_main_state[i]
        );
    }
    settings_try_load_icon_variant(
        SETTINGS_ICON_FONTS,
        SETTINGS_ICON_LIST_SIZE,
        SETTINGS_ICON_LIST_SIZE,
        settings_icon_list_pixels[SETTINGS_ICON_FONTS],
        &settings_icon_list_state[SETTINGS_ICON_FONTS]
    );
}

static int is_supported_image(const char *name) {
    int len = 0; while (name[len]) len++;
    if (len < 4) return 0;
    
    const char *last_dot = NULL;
    for (int i = 0; name[i]; i++) if (name[i] == '.') last_dot = &name[i];
    if (!last_dot) return 0;
    
    char ext[8];
    int elen = 0;
    for (int i = 1; last_dot[i] && elen < 7; i++) {
        char c = last_dot[i];
        if (c >= 'A' && c <= 'Z') c += 32;
        ext[elen++] = c;
    }
    ext[elen] = 0;
    
    if (strcmp(ext, "jpg") == 0 || strcmp(ext, "jpeg") == 0 || 
        strcmp(ext, "png") == 0 || strcmp(ext, "bmp") == 0 ||
        strcmp(ext, "tga") == 0 || strcmp(ext, "psd") == 0 ||
        strcmp(ext, "hdr") == 0 || strcmp(ext, "pic") == 0 ||
        strcmp(ext, "pnm") == 0 || strcmp(ext, "ppm") == 0 ||
        strcmp(ext, "pgm") == 0) {
        return 1;
    }
    return 0;
}

static void decode_wallpapers_task(void *arg) {
    (void)arg;
    wallpaper_count = 0;
    FAT32_FileInfo info[MAX_WALLPAPERS];
    int count = sys_list("/Library/images/Wallpapers", info, MAX_WALLPAPERS);
    if (count < 0) return;

    for (int i = 0; i < count && wallpaper_count < MAX_WALLPAPERS; i++) {
        if (info[i].is_directory) continue; // Skip directories
        
        if (!is_supported_image(info[i].name)) continue;

        wallpaper_entry_t *wp = &wallpapers[wallpaper_count];
        // Set path
        char *pref = "/Library/images/Wallpapers/";
        int pl = 0; while (pref[pl]) { wp->path[pl] = pref[pl]; pl++; }
        int nl = 0; while (info[i].name[nl]) { wp->path[pl+nl] = info[i].name[nl]; nl++; }
        wp->path[pl+nl] = 0;

        // Set name (strip extension)
        int dot_idx = -1;
        for (int j = 0; info[i].name[j]; j++) if (info[i].name[j] == '.') dot_idx = j;
        int name_len = (dot_idx != -1) ? dot_idx : nl;
        for (int j = 0; j < name_len && j < 63; j++) wp->name[j] = info[i].name[j];
        wp->name[(name_len < 63) ? name_len : 63] = 0;

        char cache_path[256];
        int cp = 0;
        char *cpref = "/Library/Caches/Thumbnails/";
        while (cpref[cp]) { cache_path[cp] = cpref[cp]; cp++; }
        int cn = 0;
        while (info[i].name[cn]) { cache_path[cp+cn] = info[i].name[cn]; cn++; }
        char *csuf = ".bin";
        int cs = 0;
        while (csuf[cs]) { cache_path[cp+cn+cs] = csuf[cs]; cs++; }
        cache_path[cp+cn+cs] = 0;

        int cfd = sys_open(cache_path, "r");
        if (cfd >= 0) {
            sys_read(cfd, wp->thumb, WALLPAPER_THUMB_W * WALLPAPER_THUMB_H * 4);
            sys_close(cfd);
            wp->valid = 1;
        } else {
            int fd = sys_open(wp->path, "r");
            if (fd >= 0) {
                int size = sys_seek(fd, 0, 2); // SEEK_END
                sys_seek(fd, 0, 0); // SEEK_SET
                if (size > 0 && size < 8 * 1024 * 1024) {
                        unsigned char *buf = (unsigned char *)malloc(size);
                        if (buf) {
                            sys_read(fd, buf, size);
                            int img_w, img_h, channels;
                            unsigned char *img = stbi_load_from_memory(buf, size, &img_w, &img_h, &channels, 4);
                            if (img && img_w > 0 && img_h > 0) {
                                scale_rgba_to_argb(img, img_w, img_h, wp->thumb, WALLPAPER_THUMB_W, WALLPAPER_THUMB_H);
                                wp->valid = 1;
                                stbi_image_free(img);

                                sys_mkdir("/Library/Caches");
                                sys_mkdir("/Library/Caches/Thumbnails");
                                int swfd = sys_open(cache_path, "w");
                                if (swfd >= 0) {
                                    sys_write_fs(swfd, wp->thumb, WALLPAPER_THUMB_W * WALLPAPER_THUMB_H * 4);
                                    sys_close(swfd);
                                }
                            }
                            free(buf);
                        }
                }
                sys_close(fd);
            }
        }

        wallpaper_count++;
    }
}

static void load_wallpapers(void) {
    void *job_args[1] = { NULL };
    sys_parallel_run(decode_wallpapers_task, job_args, 1);

    for (int i = 0; i < wallpaper_count; i++) {
        int tx = (i % 3) * (WALLPAPER_THUMB_W + 15);
        int ty = (i / 3) * (WALLPAPER_THUMB_H + 30);
        widget_button_init(&btn_wp_thumbs[i], 8 + tx, 306 + ty, WALLPAPER_THUMB_W + 8, WALLPAPER_THUMB_H + 20, "");
    }
}

static uint32_t parse_rgb_separate(const char *r, const char *g, const char *b) {
    int rv = 0, gv = 0, bv = 0;
    for (int i = 0; r[i] && i < 3; i++) {
        if (r[i] >= '0' && r[i] <= '9') rv = rv * 10 + (r[i] - '0');
    }
    for (int i = 0; g[i] && i < 3; i++) {
        if (g[i] >= '0' && g[i] <= '9') gv = gv * 10 + (g[i] - '0');
    }
    for (int i = 0; b[i] && i < 3; i++) {
        if (b[i] >= '0' && b[i] <= '9') bv = bv * 10 + (b[i] - '0');
    }
    if (rv > 255) rv = 255;
    if (gv > 255) gv = 255;
    if (bv > 255) bv = 255;
    return 0xFF000000 | (rv << 16) | (gv << 8) | bv;
}

static void control_panel_paint_main(ui_window_t win) {
    int offset_x = 8;
    int offset_y = 6;
    
    int item_y = 0;
    int item_h = 60;
    int item_spacing = 10;
    
    // Wallpaper
    widget_button_draw(&settings_ctx, &btn_main_wallpaper);
    settings_draw_icon(win, SETTINGS_ICON_WALLPAPER, offset_x + 16, offset_y + item_y + 14, false);
    ui_draw_string(win, offset_x + 60, offset_y + item_y + 15, "Wallpaper", COLOR_DARK_TEXT);
    ui_draw_string(win, offset_x + 60, offset_y + item_y + 35, "Choose wallpaper", COLOR_DKGRAY);
    
    // Network
    item_y += item_h + item_spacing;
    widget_button_draw(&settings_ctx, &btn_main_network);
    settings_draw_icon(win, SETTINGS_ICON_NETWORK, offset_x + 16, offset_y + item_y + 14, false);
    ui_draw_string(win, offset_x + 60, offset_y + item_y + 15, "Network", COLOR_DARK_TEXT);
    ui_draw_string(win, offset_x + 60, offset_y + item_y + 35, "Internet and connectivity", COLOR_DKGRAY);
    
    // Desktop
    item_y += item_h + item_spacing;
    widget_button_draw(&settings_ctx, &btn_main_desktop);
    settings_draw_icon(win, SETTINGS_ICON_DESKTOP, offset_x + 16, offset_y + item_y + 14, false);
    ui_draw_string(win, offset_x + 60, offset_y + item_y + 15, "Desktop", COLOR_DARK_TEXT);
    ui_draw_string(win, offset_x + 60, offset_y + item_y + 35, "Desktop alignment", COLOR_DKGRAY);
    
    // Mouse
    item_y += item_h + item_spacing;
    widget_button_draw(&settings_ctx, &btn_main_mouse);
    settings_draw_icon(win, SETTINGS_ICON_MOUSE, offset_x + 16, offset_y + item_y + 14, false);
    ui_draw_string(win, offset_x + 60, offset_y + item_y + 15, "Mouse", COLOR_DARK_TEXT);
    ui_draw_string(win, offset_x + 60, offset_y + item_y + 35, "Pointer settings", COLOR_DKGRAY);
    
    // Fonts
    item_y += item_h + item_spacing;
    widget_button_draw(&settings_ctx, &btn_main_fonts);
    settings_draw_icon(win, SETTINGS_ICON_FONTS, offset_x + 16, offset_y + item_y + 14, false);
    ui_draw_string(win, offset_x + 60, offset_y + item_y + 15, "Fonts", COLOR_DARK_TEXT);
    ui_draw_string(win, offset_x + 60, offset_y + item_y + 35, "Choose system font", COLOR_DKGRAY);

    // Display
    item_y += item_h + item_spacing;
    widget_button_draw(&settings_ctx, &btn_main_display);
    settings_draw_icon(win, SETTINGS_ICON_DISPLAY, offset_x + 16, offset_y + item_y + 14, false);
    ui_draw_string(win, offset_x + 60, offset_y + item_y + 15, "Display", COLOR_DARK_TEXT);
    ui_draw_string(win, offset_x + 60, offset_y + item_y + 35, "Screen resolution & color", COLOR_DKGRAY);

    // Keyboard
    item_y += item_h + item_spacing;
    widget_button_draw(&settings_ctx, &btn_main_keyboard);
    settings_draw_icon(win, SETTINGS_ICON_KEYBOARD, offset_x + 16, offset_y + item_y + 14, false);
    ui_draw_string(win, offset_x + 60, offset_y + item_y + 15, "Keyboard", COLOR_DARK_TEXT);
    ui_draw_string(win, offset_x + 60, offset_y + item_y + 35, "Keyboard layout", COLOR_DKGRAY);
}

static void control_panel_paint_wallpaper(ui_window_t win) {
    int offset_x = 8;
    int offset_y = 6;
    
    widget_button_draw(&settings_ctx, &btn_back);
    
    ui_draw_string(win, offset_x, offset_y + 40, "Presets:", COLOR_DARK_TEXT);
    
    int button_y = offset_y + 65;
    int button_x = offset_x;
    
    // Colors
    widget_button_draw(&settings_ctx, &btn_wp_colors[0]);
    ui_draw_rect(win, button_x + 8, button_y + 6, 18, 13, COLOR_COFFEE);
    ui_draw_string(win, button_x + 35, button_y + 8, "Coffee", COLOR_DARK_TEXT);
    
    widget_button_draw(&settings_ctx, &btn_wp_colors[1]);
    ui_draw_rect(win, button_x + 108, button_y + 6, 18, 13, COLOR_TEAL);
    ui_draw_string(win, button_x + 135, button_y + 8, "Teal", COLOR_DARK_TEXT);
    
    widget_button_draw(&settings_ctx, &btn_wp_colors[2]);
    ui_draw_rect(win, button_x + 208, button_y + 6, 18, 13, COLOR_GREEN);
    ui_draw_string(win, button_x + 235, button_y + 8, "Green", COLOR_DARK_TEXT);
    
    button_y += 35;
    widget_button_draw(&settings_ctx, &btn_wp_colors[3]);
    ui_draw_rect(win, button_x + 8, button_y + 6, 18, 13, COLOR_BLUE_BG);
    ui_draw_string(win, button_x + 35, button_y + 8, "Blue", COLOR_DARK_TEXT);
    
    widget_button_draw(&settings_ctx, &btn_wp_colors[4]);
    ui_draw_rect(win, button_x + 108, button_y + 6, 18, 13, COLOR_PURPLE);
    ui_draw_string(win, button_x + 132, button_y + 8, "Purple", COLOR_DARK_TEXT);
    
    widget_button_draw(&settings_ctx, &btn_wp_colors[5]);
    ui_draw_rect(win, button_x + 208, button_y + 6, 18, 13, COLOR_GREY);
    ui_draw_string(win, button_x + 235, button_y + 8, "Grey", COLOR_DARK_TEXT);
    
    // Patterns
    button_y += 40;
    ui_draw_string(win, offset_x, button_y, "Patterns:", COLOR_DARK_TEXT);
    
    button_y += 20;
    widget_button_draw(&settings_ctx, &btn_wp_patterns[0]);
    for (int py = 0; py < 10; py++) {
        for (int px = 0; px < 12; px++) {
            int cell_x = px % 3;
            int cell_y = py % 3;
            uint32_t color = (cell_x == 1 && cell_y == 1) ? 0xFF000000 : 
                           (cell_x == 1 || cell_y == 1) ? 0xFF404040 : 0xFFDC143C;
            ui_draw_rect(win, button_x + 8 + px, button_y + 7 + py, 1, 1, color);
        }
    }
    ui_draw_string(win, button_x + 28, button_y + 8, "Lumberjack", COLOR_DARK_TEXT);
    
    widget_button_draw(&settings_ctx, &btn_wp_patterns[1]);
    for (int py = 0; py < 8; py++) {
        for (int px = 0; px < 10; px++) {
            int cx = px - 5;
            int cy = py - 4;
            int abs_cx = cx < 0 ? -cx : cx;
            int abs_cy = cy < 0 ? -cy : cy;
            uint32_t color = (abs_cx + abs_cy <= 3) ? 0xFF0000CD : 0xFFADD8E6;
            ui_draw_rect(win, button_x + 153 + px, button_y + 8 + py, 1, 1, color);
        }
    }
    ui_draw_string(win, button_x + 165, button_y + 8, "Blue Diamond", COLOR_DARK_TEXT);
    
    // Custom color
    button_y += 40;
    ui_draw_string(win, offset_x, button_y, "Custom color:", COLOR_DARK_TEXT);
    button_y += 20;
    
    ui_draw_string(win, button_x, button_y + 4, "R:", COLOR_DARK_TEXT);
    tb_r.focused = (focused_field == 0);
    widget_textbox_draw(&settings_ctx, &tb_r);
    
    ui_draw_string(win, button_x + 90, button_y + 4, "G:", COLOR_DARK_TEXT);
    tb_g.focused = (focused_field == 1);
    widget_textbox_draw(&settings_ctx, &tb_g);
    
    ui_draw_string(win, button_x + 180, button_y + 4, "B:", COLOR_DARK_TEXT);
    tb_b.focused = (focused_field == 2);
    widget_textbox_draw(&settings_ctx, &tb_b);
    
    widget_button_draw(&settings_ctx, &btn_wp_apply);
    ui_draw_string(win, button_x + 18, button_y + 33, "Apply", COLOR_DARK_TEXT);
    
    // Wallpapers section
    button_y += 60;
    ui_draw_string(win, offset_x, button_y, "Wallpapers:", COLOR_DARK_TEXT);
    button_y += 20;
    
    for (int i = 0; i < wallpaper_count; i++) {
        int tx = (i % 3) * (WALLPAPER_THUMB_W + 15);
        int ty = (i / 3) * (WALLPAPER_THUMB_H + 30);
        
        widget_button_draw(&settings_ctx, &btn_wp_thumbs[i]);
        if (wallpapers[i].valid) {
            ui_draw_image(win, button_x + tx + 4, button_y + ty + 4, WALLPAPER_THUMB_W, WALLPAPER_THUMB_H, wallpapers[i].thumb);
        }
        ui_draw_string(win, button_x + tx + 8, button_y + ty + WALLPAPER_THUMB_H + 8, wallpapers[i].name, 0xFFFFFFFF);
    }
}

static void control_panel_paint_network(ui_window_t win) {
    int offset_x = 8;
    int offset_y = 6;
    
    widget_button_draw(&settings_ctx, &btn_back);
    
    ui_draw_string(win, offset_x, offset_y + 40, "Network Adapter:", COLOR_DARK_TEXT);
    widget_button_draw(&settings_ctx, &btn_net_init);
    ui_draw_string(win, offset_x + 30, offset_y + 63, "Init Network", COLOR_DARK_TEXT);
    
    if (net_status[0] != '\0') {
        ui_draw_string(win, offset_x + 150, offset_y + 63, net_status, 0xFF90EE90);
    }

    int info_y = offset_y + 90;
    
    // Adapter Info
    char nic_name[64];
    ui_draw_string(win, offset_x, info_y, "NIC:", COLOR_DARK_TEXT);
    if (sys_network_is_initialized() && sys_network_get_nic_name(nic_name) == 0) {
        ui_draw_string(win, offset_x + 40, info_y, nic_name, COLOR_DKGRAY);
    } else {
        ui_draw_string(win, offset_x + 40, info_y, "NOT INITIALIZED", 0xFFFF6B6B);
    }
    info_y += 20;

    ui_draw_string(win, offset_x, info_y, "MAC:", COLOR_DARK_TEXT);
    net_mac_address_t mac;
    if (sys_network_is_initialized() && sys_network_get_mac(&mac) == 0) {
        char mac_str[32];
        char b[4];
        mac_str[0] = 0;
        for (int i=0; i<6; i++) {
            itoa_hex(mac.bytes[i], b);
            if (b[1] == 0) { b[1] = b[0]; b[0] = '0'; b[2] = 0; } // zero pad
            strcat(mac_str, b);
            if (i < 5) strcat(mac_str, ":");
        }
        ui_draw_string(win, offset_x + 40, info_y, mac_str, COLOR_DKGRAY);
    } else {
        ui_draw_string(win, offset_x + 40, info_y, "NOT INITIALIZED", 0xFFFF6B6B);
    }
    info_y += 30;

    // Current IP Address
    ui_draw_string(win, offset_x, info_y, "IP:", COLOR_DARK_TEXT);
    if (!sys_network_has_ip()) {
        ui_draw_string(win, offset_x + 40, info_y, "NOT INITIALIZED", 0xFFFF6B6B);
    } else {
        net_ipv4_address_t ip;
        if (sys_network_get_ip(&ip) == 0) {
            char ip_str[32];
            char b[4];
            ip_str[0] = 0;
            for (int i=0; i<4; i++) {
                cli_itoa(ip.bytes[i], b);
                strcat(ip_str, b);
                if (i < 3) strcat(ip_str, ".");
            }
            ui_draw_string(win, offset_x + 40, info_y, ip_str, COLOR_DKGRAY);
        }
    }
    info_y += 20;

    // Current DNS Address
    ui_draw_string(win, offset_x, info_y, "DNS:", COLOR_DARK_TEXT);
    if (!sys_network_has_ip()) {
        ui_draw_string(win, offset_x + 40, info_y, "NOT INITIALIZED", 0xFFFF6B6B);
    } else {
        net_ipv4_address_t dns;
        if (sys_get_dns_server(&dns) == 0) {
            char dns_str[32];
            char b[4];
            dns_str[0] = 0;
            for (int i=0; i<4; i++) {
                cli_itoa(dns.bytes[i], b);
                strcat(dns_str, b);
                if (i < 3) strcat(dns_str, ".");
            }
            ui_draw_string(win, offset_x + 40, info_y, dns_str, COLOR_DKGRAY);
        }
    }
    info_y += 30;

    // IP SET
    ui_draw_string(win, offset_x, info_y + 4, "IPSET:", COLOR_DARK_TEXT);
    tb_ip.focused = (focused_field == 5);
    widget_textbox_draw(&settings_ctx, &tb_ip);
    
    widget_button_draw(&settings_ctx, &btn_net_set_ip);
    ui_draw_string(win, offset_x + 225, info_y + 4, "SET", COLOR_DARK_TEXT);
    
    info_y += 30;

    // DNS SET
    ui_draw_string(win, offset_x, info_y + 4, "DNSSET:", COLOR_DARK_TEXT);
    tb_dns.focused = (focused_field == 6);
    widget_textbox_draw(&settings_ctx, &tb_dns);
    
    widget_button_draw(&settings_ctx, &btn_net_set_dns);
    ui_draw_string(win, offset_x + 225, info_y + 4, "SET", COLOR_DARK_TEXT);
}

static void control_panel_paint_desktop(ui_window_t win) {
    int offset_x = 8;
    int offset_y = 6;
    
    widget_button_draw(&settings_ctx, &btn_back);
    ui_draw_string(win, offset_x, offset_y + 40, "Desktop Settings:", COLOR_DARK_TEXT);
    
    int section_y = offset_y + 65;
    
    widget_checkbox_draw(&settings_ctx, &chk_snap);
    
    section_y += 25;
    widget_checkbox_draw(&settings_ctx, &chk_align);
    
    section_y += 30;
    ui_draw_string(win, offset_x, section_y + 3, "Apps per column:", COLOR_DARK_TEXT);
    
    widget_button_init(&btn_dt_rows_minus, offset_x + 130, section_y, 20, 20, "");
    widget_button_draw(&settings_ctx, &btn_dt_rows_minus);
    ui_draw_string(win, offset_x + 135, section_y + 4, "-", COLOR_DARK_TEXT);
    
    char num[4];
    cli_itoa(desktop_max_rows_per_col, num);
    ui_draw_string(win, offset_x + 160, section_y + 5, num, COLOR_DARK_TEXT);
    
    widget_button_init(&btn_dt_rows_plus, offset_x + 180, section_y, 20, 20, "");
    widget_button_draw(&settings_ctx, &btn_dt_rows_plus);
    ui_draw_string(win, offset_x + 186, section_y + 4, "+", COLOR_DARK_TEXT);
    
    section_y += 30;
    ui_draw_string(win, offset_x, section_y + 3, "Columns:", COLOR_DARK_TEXT);
    
    widget_button_init(&btn_dt_cols_minus, offset_x + 130, section_y, 20, 20, "");
    widget_button_draw(&settings_ctx, &btn_dt_cols_minus);
    ui_draw_string(win, offset_x + 135, section_y + 4, "-", COLOR_DARK_TEXT);
    
    char num_c[4];
    cli_itoa(desktop_max_cols, num_c);
    ui_draw_string(win, offset_x + 160, section_y + 5, num_c, COLOR_DARK_TEXT);
    
    widget_button_init(&btn_dt_cols_plus, offset_x + 180, section_y, 20, 20, "");
    widget_button_draw(&settings_ctx, &btn_dt_cols_plus);
    ui_draw_string(win, offset_x + 186, section_y + 4, "+", COLOR_DARK_TEXT);
}

static void control_panel_paint_mouse(ui_window_t win) {
    int offset_x = 8;
    int offset_y = 6;
    
    widget_button_draw(&settings_ctx, &btn_back);
    ui_draw_string(win, offset_x, offset_y + 40, "Mouse Settings:", COLOR_DARK_TEXT);
    
    int section_y = offset_y + 65;
    ui_draw_string(win, offset_x, section_y, "Speed:", COLOR_DARK_TEXT);
    
    slider_mouse.value = (float)mouse_speed;
    widget_slider_draw(&settings_ctx, &slider_mouse);
    
    ui_draw_string(win, offset_x + 270, section_y + 4, "x", COLOR_DARK_TEXT);
    char speed_str[4];
    cli_itoa(mouse_speed, speed_str);
    ui_draw_string(win, offset_x + 280, section_y + 4, speed_str, COLOR_DARK_TEXT);

    section_y += 40;
    ui_draw_string(win, offset_x, section_y, "Size:", COLOR_DARK_TEXT);

    slider_cursor_size.value = (float)mouse_cursor_scale_tenths;
    widget_slider_draw(&settings_ctx, &slider_cursor_size);

    char scale_str[8];
    format_scale_tenths(mouse_cursor_scale_tenths, scale_str);
    ui_draw_string(win, offset_x + 270, section_y + 4, scale_str, COLOR_DARK_TEXT);
}

static void on_font_scroll(void *user_data, int new_scroll_y) {
    (void)user_data;
    font_scroll_y = new_scroll_y;
}

static void load_fonts(void) {
    font_count = 0;
    FAT32_FileInfo info[256]; 
    int count = sys_list("/Library/Fonts", info, 256);
    if (count < 0) return;

    if (fonts) free(fonts);
    if (btn_fonts) free(btn_fonts);
    
    fonts = (font_entry_t *)malloc(sizeof(font_entry_t) * count);
    btn_fonts = (widget_button_t *)malloc(sizeof(widget_button_t) * count);
    font_capacity = count;

    for (int i = 0; i < count; i++) {
        if (info[i].is_directory) continue;
        // check if .ttf (case-insensitive)
        int len = 0; while (info[i].name[len]) len++;
        if (len < 4) continue;
        char c1 = info[i].name[len-1]; if (c1 >= 'A' && c1 <= 'Z') c1 += 32;
        char c2 = info[i].name[len-2]; if (c2 >= 'A' && c2 <= 'Z') c2 += 32;
        char c3 = info[i].name[len-3]; if (c3 >= 'A' && c3 <= 'Z') c3 += 32;
        char c4 = info[i].name[len-4]; if (c4 >= 'A' && c4 <= 'Z') c4 += 32;
        if (c4 != '.' || c3 != 't' || c2 != 't' || c1 != 'f') continue;

        font_entry_t *fe = &fonts[font_count];
        // Build full path
        char *pref = "/Library/Fonts/";
        int pl = 0; while (pref[pl]) { fe->path[pl] = pref[pl]; pl++; }
        int nl = 0; while (info[i].name[nl]) { fe->path[pl+nl] = info[i].name[nl]; nl++; }
        fe->path[pl+nl] = 0;
        // Store display name (strip .ttf)
        for (int j = 0; j < nl - 4 && j < 47; j++) fe->name[j] = info[i].name[j];
        fe->name[(nl-4 < 47) ? nl-4 : 47] = 0;

        widget_button_init(&btn_fonts[font_count], 8, 0, 310, 35, "");

        font_count++;
    }

    widget_scrollbar_init(&font_scrollbar, 330, 66, 12, 420);
    font_scrollbar.content_height = font_count * 40;
    font_scrollbar.on_scroll = (void(*)(void*,int))on_font_scroll;
}

static void control_panel_paint_fonts(ui_window_t win) {
    int offset_x = 8;
    int offset_y = 6;
    
    widget_button_draw(&settings_ctx, &btn_back);
    
    ui_draw_string(win, offset_x, offset_y + 40, "System Font:", COLOR_DARK_TEXT);
    
    int list_y = 66;
    int list_h = 420;
    
    // Draw fonts
    for (int i = 0; i < font_count; i++) {
        int item_y = list_y + (i * 40) - font_scroll_y;
        
        // Only draw if visible
        if (item_y + 35 < list_y || item_y > list_y + list_h) continue;
        
        btn_fonts[i].y = item_y;
        widget_button_draw(&settings_ctx, &btn_fonts[i]);
        
        settings_draw_icon(win, SETTINGS_ICON_FONTS, offset_x + 10, item_y + 9, true);
        ui_draw_string(win, offset_x + 40, item_y + 9, fonts[i].name, COLOR_DARK_TEXT);
        if (i == selected_font) {
            ui_draw_string(win, offset_x + 270, item_y + 9, "*", 0xFF90EE90);
        }
    }
        widget_scrollbar_draw(&settings_ctx, &font_scrollbar);
}

static void control_panel_paint_display(ui_window_t win) {
    int offset_x = 8;
    int offset_y = 6;
    int right_x = offset_x + 160;
    
    widget_button_draw(&settings_ctx, &btn_back);
    ui_draw_string(win, offset_x, offset_y + 40, "Resolution:", COLOR_DARK_TEXT);
    
    int custom_y = offset_y + 270;
    ui_draw_string(win, offset_x, custom_y - 20, "Custom:", COLOR_DARK_TEXT);
    
    widget_textbox_init(&tb_custom_w, offset_x, custom_y, 60, 25, custom_res_w, 5);
    tb_custom_w.focused = (focused_field == 3);
    widget_textbox_draw(&settings_ctx, &tb_custom_w);
    
    ui_draw_string(win, offset_x + 65, custom_y + 7, "x", COLOR_DARK_TEXT);
    
    widget_textbox_init(&tb_custom_h, offset_x + 80, custom_y, 60, 25, custom_res_h, 5);
    tb_custom_h.focused = (focused_field == 4);
    widget_textbox_draw(&settings_ctx, &tb_custom_h);

    ui_draw_string(win, right_x, offset_y + 40, "Color Depth:", COLOR_DARK_TEXT);

    widget_button_draw(&settings_ctx, &btn_apply);
    
    // Draw dropdowns last so they render above everything else
    widget_dropdown_draw(&settings_ctx, &drop_res);
    widget_dropdown_draw(&settings_ctx, &drop_color);
}

static void control_panel_paint_keyboard(ui_window_t win) {
    int offset_x = 8;
    int offset_y = 6;

    widget_button_draw(&settings_ctx, &btn_back);

    ui_draw_string(win, offset_x, offset_y + 40, "Keyboard Layout:", COLOR_DARK_TEXT);

    widget_dropdown_draw(&settings_ctx, &drop_keyboard);

    widget_button_draw(&settings_ctx, &btn_apply);
}

static void control_panel_paint(ui_window_t win) {
    // Fill background
    ui_draw_rect(win, 0, 0, 350, 500, COLOR_DARK_BG);
    
    settings_ctx.user_data = (void *)win;

    if (current_view == VIEW_MAIN) {
        control_panel_paint_main(win);
    } else if (current_view == VIEW_WALLPAPER) {
        control_panel_paint_wallpaper(win);
    } else if (current_view == VIEW_NETWORK) {
        control_panel_paint_network(win);
    } else if (current_view == VIEW_DESKTOP) {
        control_panel_paint_desktop(win);
    } else if (current_view == VIEW_MOUSE) {
        control_panel_paint_mouse(win);
    } else if (current_view == VIEW_FONTS) {
        control_panel_paint_fonts(win);
    } else if (current_view == VIEW_DISPLAY) {
        control_panel_paint_display(win);
    } else if (current_view == VIEW_KEYBOARD) {
        control_panel_paint_keyboard(win);
    }
}

static void save_desktop_config(void) {
    sys_system(SYSTEM_CMD_SET_DESKTOP_PROP, 1, desktop_snap_to_grid, 0, 0);
    sys_system(SYSTEM_CMD_SET_DESKTOP_PROP, 2, desktop_auto_align, 0, 0);
    sys_system(SYSTEM_CMD_SET_DESKTOP_PROP, 3, desktop_max_rows_per_col, 0, 0);
    sys_system(SYSTEM_CMD_SET_DESKTOP_PROP, 4, desktop_max_cols, 0, 0);
}

static void save_mouse_config(void) {
    sys_system(SYSTEM_CMD_SET_MOUSE_SPEED, mouse_speed, 0, 0, 0);
    sys_system(SYSTEM_CMD_SET_MOUSE_CURSOR_SCALE, mouse_cursor_scale_tenths, 0, 0, 0);
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

static void control_panel_handle_mouse(int x, int y, bool is_down, bool is_click) {
    if (current_view != VIEW_MAIN && widget_button_handle_mouse(&btn_back, x, y, is_down, is_click, NULL)) {
        if (is_click) {
            current_view = VIEW_MAIN;
            focused_field = -1;
            btn_back.pressed = false;
        }
        return;
    }
    
    if (current_view == VIEW_DESKTOP) {
        if (widget_checkbox_handle_mouse(&chk_snap, x, y, is_click, NULL)) {
            if (is_click) desktop_snap_to_grid = chk_snap.checked; 
            return;
        }
        if (widget_checkbox_handle_mouse(&chk_align, x, y, is_click, NULL)) {
            if (is_click) desktop_auto_align = chk_align.checked;
            return;
        }
    }
    
    if (current_view == VIEW_DISPLAY) {
        if (widget_dropdown_handle_mouse(&drop_res, x, y, is_click, NULL)) {
            disp_sel_res = drop_res.selected_idx;
            return;
        }
        if (widget_dropdown_handle_mouse(&drop_color, x, y, is_click, NULL)) {
            disp_sel_color = drop_color.selected_idx;
            return;
        }
        if (drop_res.is_open || drop_color.is_open) return;
        
        if (widget_button_handle_mouse(&btn_apply, x, y, is_down, is_click, NULL)) {
            if (is_click) {
                btn_apply.pressed = false;
                int w = 1024, h = 768;
                if (disp_sel_res == 0) { w = 640; h = 480; }
                else if (disp_sel_res == 1) { w = 800; h = 600; }
                else if (disp_sel_res >= 2 && disp_sel_res <= 4) {
                    w = dyn_res_w[disp_sel_res - 2];
                    h = dyn_res_h[disp_sel_res - 2];
                } else if (disp_sel_res == 5) {
                    extern int atoi(const char *str);
                    int cw = atoi(custom_res_w);
                    int ch = atoi(custom_res_h);
                    if (cw >= 320 && ch >= 200) { w = cw; h = ch; }
                }
                
                int bpp = 32, mode = 0;
                if (disp_sel_color == 1) { bpp = 16; }
                if (disp_sel_color == 2) { bpp = 8; mode = 0; }
                if (disp_sel_color == 3) { bpp = 8; mode = 1; }
                if (disp_sel_color == 4) { bpp = 8; mode = 2; }
                
                sys_system(SYSTEM_CMD_SET_RESOLUTION, w, h, bpp, mode);
            }
            return;
        }
    }
    
    if (current_view == VIEW_WALLPAPER) {
        if (widget_textbox_handle_mouse(&settings_ctx, &tb_r, x, y, is_click, NULL)) {
            focused_field = 0; input_cursor = tb_r.cursor_pos; return;
        }
        if (widget_textbox_handle_mouse(&settings_ctx, &tb_g, x, y, is_click, NULL)) {
            focused_field = 1; input_cursor = tb_g.cursor_pos; return;
        }
        if (widget_textbox_handle_mouse(&settings_ctx, &tb_b, x, y, is_click, NULL)) {
            focused_field = 2; input_cursor = tb_b.cursor_pos; return;
        }
        
        for (int i=0; i<6; i++) {
            if (widget_button_handle_mouse(&btn_wp_colors[i], x, y, is_down, is_click, NULL)) {
                if (is_click) {
                    uint32_t c = 0;
                    if (i==0) c = COLOR_COFFEE; else if(i==1) c = COLOR_TEAL; else if(i==2) c = COLOR_GREEN;
                    else if(i==3) c = COLOR_BLUE_BG; else if(i==4) c = COLOR_PURPLE; else if(i==5) c = COLOR_GREY;
                    sys_system(SYSTEM_CMD_SET_BG_COLOR, c, 0, 0, 0); btn_wp_colors[i].pressed=false;
                }
                return;
            }
        }
        if (widget_button_handle_mouse(&btn_wp_patterns[0], x, y, is_down, is_click, NULL)) {
            if (is_click) { sys_system(SYSTEM_CMD_SET_BG_PATTERN, (uint64_t)pattern_lumberjack, 0, 0, 0); btn_wp_patterns[0].pressed=false;} return;
        }
        if (widget_button_handle_mouse(&btn_wp_patterns[1], x, y, is_down, is_click, NULL)) {
            if (is_click) { sys_system(SYSTEM_CMD_SET_BG_PATTERN, (uint64_t)pattern_blue_diamond, 0, 0, 0); btn_wp_patterns[1].pressed=false;} return;
        }
        if (widget_button_handle_mouse(&btn_wp_apply, x, y, is_down, is_click, NULL)) {
            if (is_click) {
                uint32_t cust = parse_rgb_separate(rgb_r, rgb_g, rgb_b);
                sys_system(SYSTEM_CMD_SET_BG_COLOR, cust, 0, 0, 0);
                btn_wp_apply.pressed=false;
            }
            return;
        }
        for (int i=0; i<wallpaper_count; i++) {
            if (wallpapers[i].valid && widget_button_handle_mouse(&btn_wp_thumbs[i], x, y, is_down, is_click, NULL)) {
                if (is_click) { sys_system(SYSTEM_CMD_SET_WALLPAPER_PATH, (uint64_t)wallpapers[i].path, 0, 0, 0); btn_wp_thumbs[i].pressed=false;} return;
            }
        }
    }
    
    if (current_view == VIEW_NETWORK) {
        if (widget_textbox_handle_mouse(&settings_ctx, &tb_ip, x, y, is_click, NULL)) {
            focused_field = 5; input_cursor = tb_ip.cursor_pos; return;
        }
        if (widget_textbox_handle_mouse(&settings_ctx, &tb_dns, x, y, is_click, NULL)) {
            focused_field = 6; input_cursor = tb_dns.cursor_pos; return;
        }
        
        if (widget_button_handle_mouse(&btn_net_init, x, y, is_down, is_click, NULL)) {
            if (is_click) {
                if (sys_system(SYSTEM_CMD_NETWORK_INIT, 0, 0, 0, 0) == 0) {
                    net_status[0] = 'I'; net_status[1] = 'n'; net_status[2] = 'i'; 
                    net_status[3] = 't'; net_status[4] = 'e'; net_status[5] = 'd'; net_status[6] = 0;
                } else {
                    net_status[0] = 'F'; net_status[1] = 'a'; net_status[2] = 'i'; 
                    net_status[3] = 'l'; net_status[4] = 'e'; net_status[5] = 'd'; net_status[6] = 0;
                }
                btn_net_init.pressed=false;
            }
            return;
        }
        if (widget_button_handle_mouse(&btn_net_set_ip, x, y, is_down, is_click, NULL)) {
            if (is_click) {
                net_ipv4_address_t ip; if (parse_ip(net_ip, &ip) == 0) sys_network_set_ip(&ip);
                btn_net_set_ip.pressed=false;
            }
            return;
        }
        if (widget_button_handle_mouse(&btn_net_set_dns, x, y, is_down, is_click, NULL)) {
            if (is_click) {
                net_ipv4_address_t ip; if (parse_ip(net_dns, &ip) == 0) sys_set_dns_server(&ip);
                btn_net_set_dns.pressed=false;
            }
            return;
        }
    }

    if (current_view == VIEW_DESKTOP) {
        if (widget_button_handle_mouse(&btn_dt_rows_minus, x, y, is_down, is_click, NULL)) {
            if (is_click) { if (desktop_max_rows_per_col > 1) { desktop_max_rows_per_col--; save_desktop_config(); } btn_dt_rows_minus.pressed=false; } return;
        }
        if (widget_button_handle_mouse(&btn_dt_rows_plus, x, y, is_down, is_click, NULL)) {
            if (is_click) { if (desktop_max_rows_per_col < 15) desktop_max_rows_per_col++; save_desktop_config(); btn_dt_rows_plus.pressed=false;} return;
        }
        if (widget_button_handle_mouse(&btn_dt_cols_minus, x, y, is_down, is_click, NULL)) {
            if (is_click) { if (desktop_max_cols > 1) { desktop_max_cols--; save_desktop_config(); } btn_dt_cols_minus.pressed=false;} return;
        }
        if (widget_button_handle_mouse(&btn_dt_cols_plus, x, y, is_down, is_click, NULL)) {
            if (is_click) { desktop_max_cols++; save_desktop_config(); btn_dt_cols_plus.pressed=false;} return;
        }
    }

    if (current_view == VIEW_FONTS) {
        if (widget_scrollbar_handle_mouse(&font_scrollbar, x, y, is_down, NULL)) {
            return;
        }
        int list_y = 66;
        for (int i=0; i<font_count; i++) {
            int item_y = list_y + (i * 40) - font_scroll_y;
            if (item_y + 35 < list_y || item_y > 486) continue;

            btn_fonts[i].y = item_y;
            if (widget_button_handle_mouse(&btn_fonts[i], x, y, is_down, is_click, NULL)) {
                if (is_click) { 
                    selected_font = i; 
                    sys_system(SYSTEM_CMD_SET_FONT, (uint64_t)fonts[i].path, 0, 0, 0); 
                    btn_fonts[i].pressed=false;
                } 
                return;
            }
        }
    }

    if (current_view == VIEW_MAIN) {
        if (widget_button_handle_mouse(&btn_main_wallpaper, x, y, is_down, is_click, NULL)) {
            if (is_click) { current_view = VIEW_WALLPAPER; focused_field = -1; btn_main_wallpaper.pressed = false; }
            return;
        }
        if (widget_button_handle_mouse(&btn_main_network, x, y, is_down, is_click, NULL)) {
            if (is_click) { current_view = VIEW_NETWORK; focused_field = -1; btn_main_network.pressed = false; }
            return;
        }
        if (widget_button_handle_mouse(&btn_main_desktop, x, y, is_down, is_click, NULL)) {
            if (is_click) { current_view = VIEW_DESKTOP; focused_field = -1; btn_main_desktop.pressed = false; }
            return;
        }
        if (widget_button_handle_mouse(&btn_main_mouse, x, y, is_down, is_click, NULL)) {
            if (is_click) { current_view = VIEW_MOUSE; focused_field = -1; btn_main_mouse.pressed = false; }
            return;
        }
        if (widget_button_handle_mouse(&btn_main_fonts, x, y, is_down, is_click, NULL)) {
            if (is_click) { 
                current_view = VIEW_FONTS; focused_field = -1; btn_main_fonts.pressed = false;
                if (font_count == 0) load_fonts();
            }
            return;
        }
        if (widget_button_handle_mouse(&btn_main_display, x, y, is_down, is_click, NULL)) {
            if (is_click) { current_view = VIEW_DISPLAY; focused_field = -1; btn_main_display.pressed = false; }
            return;
        }
        if (widget_button_handle_mouse(&btn_main_keyboard, x, y, is_down, is_click, NULL)) {
            if (is_click) {
                current_view = VIEW_KEYBOARD;
                focused_field = -1;
                btn_main_keyboard.pressed = false;
            }
            return;
        }
    }

    if (current_view == VIEW_MOUSE) {
        if (widget_slider_handle_mouse(&slider_mouse, x, y, is_down, is_click, NULL)) {
            int new_speed = (int)(slider_mouse.value);
            focused_field = -1; // slider doesn't use textbox, so clear focus on click

            if (new_speed != mouse_speed) {
                mouse_speed = new_speed;
                save_mouse_config();
            }
            return;
        }

        if (widget_slider_handle_mouse(&slider_cursor_size, x, y, is_down, is_click, NULL)) {
            int new_scale = (int)(slider_cursor_size.value);
            focused_field = -1;

            if (new_scale != mouse_cursor_scale_tenths) {
                mouse_cursor_scale_tenths = new_scale;
                save_mouse_config();
            }
            return;
        }
    }
    
    if (current_view == VIEW_DISPLAY) {
        if (widget_textbox_handle_mouse(&settings_ctx, &tb_custom_w, x, y, is_click, NULL)) {
            focused_field = 3; disp_sel_res = 5; input_cursor = tb_custom_w.cursor_pos; return;
        }
        if (widget_textbox_handle_mouse(&settings_ctx, &tb_custom_h, x, y, is_click, NULL)) {
            focused_field = 4; disp_sel_res = 5; input_cursor = tb_custom_h.cursor_pos; return;
        }
    }

    if (current_view == VIEW_KEYBOARD) {

        if (widget_dropdown_handle_mouse(&drop_keyboard, x, y, is_click, NULL)) {
            keyboard_layout = drop_keyboard.selected_idx;
            return;
        }

        if (widget_button_handle_mouse(&btn_apply, x, y, is_down, is_click, NULL)) {
            if (is_click) {
                sys_system(SYSTEM_CMD_SET_KEYBOARD_LAYOUT, keyboard_layout, 0,0,0);
                btn_apply.pressed = false;
            }
            return;
        }
    }
}

static void control_panel_handle_key(uint32_t codepoint, int legacy, bool pressed) {
    if (!pressed) return;
    if (focused_field < 0) return;
    
    if (current_view == VIEW_WALLPAPER) {
        if (focused_field == 0) widget_textbox_handle_key(&tb_r, codepoint, legacy, NULL);
        if (focused_field == 1) widget_textbox_handle_key(&tb_g, codepoint, legacy, NULL);
        if (focused_field == 2) widget_textbox_handle_key(&tb_b, codepoint, legacy, NULL);
        if (legacy == '\t') focused_field = (focused_field + 1) % 3;
    } else if (current_view == VIEW_DISPLAY) {
        char *focused_buffer = (focused_field == 3) ? custom_res_w : custom_res_h;
        if (legacy == '\b' || legacy == 127) {
            if (input_cursor > 0) {
                const char *prev = text_prev_utf8(focused_buffer, focused_buffer + input_cursor);
                input_cursor = (int)(prev - focused_buffer);
                focused_buffer[input_cursor] = '\0';
            }
        } else if (codepoint >= '0' && codepoint <= '9') {
            if (input_cursor < 5) {
                focused_buffer[input_cursor] = (char)codepoint;
                input_cursor++;
                focused_buffer[input_cursor] = '\0';
            }
        }
    } else if (current_view == VIEW_NETWORK) {
        if (focused_field == 5) widget_textbox_handle_key(&tb_ip, codepoint, legacy, NULL);
        if (focused_field == 6) widget_textbox_handle_key(&tb_dns, codepoint, legacy, NULL);
        if (legacy == '\t') focused_field = (focused_field == 5) ? 6 : 5;
    }
}

static void init_settings_widgets(void) {
    widget_checkbox_init(&chk_snap, 8, 71, 150, 20, "Snap to Grid", false);
    widget_checkbox_init(&chk_align, 8, 96, 150, 20, "Auto Align Icons", false);

    static const char *res_opts[] = {"640x480", "800x600", dyn_res_str[0], dyn_res_str[1], dyn_res_str[2], "Custom"};
    widget_dropdown_init(&drop_res, 8, 66, 140, 30, res_opts, 6);
    
    static const char *color_opts[] = {"32-bit", "16-bit", "256 Colors", "Grayscale", "Monochrome"};
    widget_dropdown_init(&drop_color, 168, 66, 140, 30, color_opts, 5);

    static const char *keyboard_opts[] = {"QWERTY", "AZERTY", "QWERTZ", "DVORAK"}; // add more layouts here 
    widget_dropdown_init(&drop_keyboard, 8, 80, 200, 30, keyboard_opts, 4); // increment the last number when adding more layouts

    widget_textbox_init(&tb_r, 33, 226, 50, 18, rgb_r, 4);
    widget_textbox_init(&tb_g, 123, 226, 50, 18, rgb_g, 4);
    widget_textbox_init(&tb_b, 213, 226, 50, 18, rgb_b, 4);

    widget_textbox_init(&tb_ip, 68, 196, 140, 20, net_ip, 16);
    widget_textbox_init(&tb_dns, 68, 226, 140, 20, net_dns, 16);

    widget_button_init(&btn_back, 8, 11, 80, 25, "< Back");
    widget_button_init(&btn_apply, 8, 326, 300, 35, "Apply");

    // Main Menu Buttons
    int item_y = 0;
    widget_button_init(&btn_main_wallpaper, 8, 6 + item_y, 334, 60, ""); item_y += 70;
    widget_button_init(&btn_main_network, 8, 6 + item_y, 334, 60, ""); item_y += 70;
    widget_button_init(&btn_main_desktop, 8, 6 + item_y, 334, 60, ""); item_y += 70;
    widget_button_init(&btn_main_mouse, 8, 6 + item_y, 334, 60, ""); item_y += 70;
    widget_button_init(&btn_main_fonts, 8, 6 + item_y, 334, 60, ""); item_y += 70;
    widget_button_init(&btn_main_display, 8, 6 + item_y, 334, 60, ""); item_y += 70;
    widget_button_init(&btn_main_keyboard, 8, 6 + item_y, 334, 60, "");
    
    // Wallpaper View Buttons
    widget_button_init(&btn_wp_colors[0], 8, 71, 91, 25, "");
    widget_button_init(&btn_wp_colors[1], 108, 71, 91, 25, "");
    widget_button_init(&btn_wp_colors[2], 208, 71, 91, 25, "");
    widget_button_init(&btn_wp_colors[3], 8, 106, 91, 25, "");
    widget_button_init(&btn_wp_colors[4], 108, 106, 91, 25, "");
    widget_button_init(&btn_wp_colors[5], 208, 106, 91, 25, "");
    widget_button_init(&btn_wp_patterns[0], 8, 166, 132, 25, "");
    widget_button_init(&btn_wp_patterns[1], 153, 166, 132, 25, "");
    widget_button_init(&btn_wp_apply, 8, 251, 70, 25, "");

    // Network View Buttons
    widget_button_init(&btn_net_init, 8, 61, 140, 25, "");
    widget_button_init(&btn_net_set_ip, 218, 196, 50, 20, "");
    widget_button_init(&btn_net_set_dns, 218, 226, 50, 20, "");

    // Desktop View Buttons
    widget_button_init(&btn_dt_rows_minus, 138, 121, 20, 20, "");
    widget_button_init(&btn_dt_rows_plus, 188, 121, 20, 20, "");
    widget_button_init(&btn_dt_cols_minus, 138, 151, 20, 20, "");
    widget_button_init(&btn_dt_cols_plus, 188, 151, 20, 20, "");

    // Display View Textboxes
    widget_textbox_init(&tb_custom_w, 8, 276, 60, 25, custom_res_w, 5);
    widget_textbox_init(&tb_custom_h, 88, 276, 60, 25, custom_res_h, 5);

    widget_slider_init(&slider_mouse, 68, 71, 200, 20, 1.0f, 50.0f, (float)mouse_speed);
    slider_mouse.step = 1.0f;

    widget_slider_init(&slider_cursor_size, 68, 111, 200, 20, 10.0f, 40.0f, (float)mouse_cursor_scale_tenths);
    slider_cursor_size.step = 1.0f;
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    ui_window_t win = ui_window_create("Settings", 200, 150, 350, 500);
    if (!win) return 1;

    generate_lumberjack_pattern();
    init_dynamic_resolutions();

    init_settings_widgets();

    desktop_snap_to_grid    = sys_system(SYSTEM_CMD_GET_DESKTOP_PROP, 1, 0, 0, 0);
    desktop_auto_align      = sys_system(SYSTEM_CMD_GET_DESKTOP_PROP, 2, 0, 0, 0);
    desktop_max_rows_per_col = sys_system(SYSTEM_CMD_GET_DESKTOP_PROP, 3, 0, 0, 0);
    desktop_max_cols        = sys_system(SYSTEM_CMD_GET_DESKTOP_PROP, 4, 0, 0, 0);
    mouse_speed             = sys_system(SYSTEM_CMD_GET_MOUSE_SPEED, 0, 0, 0, 0);
    mouse_cursor_scale_tenths = sys_system(SYSTEM_CMD_GET_MOUSE_CURSOR_SCALE, 0, 0, 0, 0);
    if (mouse_cursor_scale_tenths < 10) mouse_cursor_scale_tenths = 10;
    if (mouse_cursor_scale_tenths > 40) mouse_cursor_scale_tenths = 40;
    load_settings_icons();

    // Set initial widget states
    chk_snap.checked        = desktop_snap_to_grid;
    chk_align.checked       = desktop_auto_align;
    drop_res.selected_idx   = disp_sel_res;
    drop_color.selected_idx = disp_sel_color;

    keyboard_layout = sys_system(SYSTEM_CMD_GET_KEYBOARD_LAYOUT, 0, 0, 0, 0);
    drop_keyboard.selected_idx = keyboard_layout;

    control_panel_paint(win);
    ui_mark_dirty(win, 0, 0, 350, 500);

    load_wallpapers(); // load after first paint to avoid startup delay

    gui_event_t ev;
    while (1) {
        bool dirty = false;

        if (ui_get_event(win, &ev)) {
            if (ev.type == GUI_EVENT_PAINT) {
                dirty = true;

            } else if (ev.type == GUI_EVENT_CLICK ||
                       ev.type == GUI_EVENT_MOUSE_DOWN ||
                       ev.type == GUI_EVENT_MOUSE_MOVE ||
                       ev.type == GUI_EVENT_MOUSE_UP) {
                bool down = false;

                if (ev.type == GUI_EVENT_MOUSE_DOWN || ev.type == GUI_EVENT_CLICK) {
                    down = true;
                } else if (ev.type == GUI_EVENT_MOUSE_MOVE) {
                    down = (ev.arg3 & 1);
                } else if (ev.type == GUI_EVENT_MOUSE_UP) {
                    down = false;
                }

                control_panel_handle_mouse(
                    ev.arg1,
                    ev.arg2,
                    down,
                    ev.type == GUI_EVENT_CLICK
                );
                dirty = true;

            } else if (ev.type == GUI_EVENT_MOUSE_WHEEL) {
                if (current_view == VIEW_FONTS) {
                    font_scroll_y -= ev.arg1 * 20;
                    int max_scroll = font_scrollbar.content_height - font_scrollbar.h;
                    if (max_scroll < 0) max_scroll = 0;
                    if (font_scroll_y < 0) font_scroll_y = 0;
                    if (font_scroll_y > max_scroll) font_scroll_y = max_scroll;

                    widget_scrollbar_update(
                        &font_scrollbar,
                        font_scrollbar.content_height,
                        font_scroll_y
                    );
                    dirty = true;
                }

            } else if (ev.type == GUI_EVENT_KEY) {
                control_panel_handle_key((uint32_t)ev.arg4, (int)ev.arg1, true);
                dirty = true;

            } else if (ev.type == GUI_EVENT_KEYUP) {
                control_panel_handle_key((uint32_t)ev.arg4, (int)ev.arg1, false);

            } else if (ev.type == GUI_EVENT_CLOSE) {
                sys_exit(0);
            }

            if (dirty) {
                control_panel_paint(win);
                ui_mark_dirty(win, 0, 0, 350, 500);
            }
        } else {
            sleep(10);
        }
    }

    return 0;
}
