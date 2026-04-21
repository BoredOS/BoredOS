// Copyright (c) 2023-2026 Chris (boreddevnl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
#include "wm.h"
#include "graphics.h"
#include "io.h"
#include "process.h"
#include "syscall.h"
#include "kutils.h"
#include "explorer.h"
#include <stdbool.h>
#include <stddef.h>
#include "wallpaper.h"
#include "fat32.h"
#include "file_index.h"
#include "../dev/ps2.h"
#define STBI_NO_STDIO
#include "userland/stb_image.h"
#include "memory_manager.h"
#include "disk.h"
#include "app_metadata.h"
#include "../sys/work_queue.h"
#include "../sys/smp.h"
#include "../core/kconsole.h"


// Hello developer,
// i advise you to just not read this code and live on with your life.
// It's not worth it.
// TRUST ME.
// If you do decide to hate yourself for some dumb reason,
// add a few hours to the counter of despair:
// hours wasted: 57
// send help

#include "../sys/spinlock.h"
static spinlock_t wm_lock = SPINLOCK_INIT;

uint64_t wm_lock_acquire(void) {
    return spinlock_acquire_irqsave(&wm_lock);
}

void wm_lock_release(uint64_t flags) {
    spinlock_release_irqrestore(&wm_lock, flags);
}

extern void serial_write(const char *str);
extern void log_ok(const char *msg);
extern void log_fail(const char *msg);

static bool str_eq(const char *s1, const char *s2) {
    if (!s1 || !s2) return false;
    while (*s1 && *s2) {
        if (*s1 != *s2) return false;
        s1++; s2++;
    }
    return (*s1 == *s2);
}


// --- State ---
static int mx = 400, my = 300; 
static int prev_mx = 400, prev_my = 300; 
static bool start_menu_open = false;
static int pending_dock_click_index = -1;
static int dock_drag_source_index = -1;
static bool dock_drag_active = false;
static int pending_desktop_icon_click = -1; 

// Desktop Context Menu
static bool desktop_menu_visible = false;
static int desktop_menu_x = 0;
static int desktop_menu_y = 0;
static int desktop_menu_target_icon = -1; 

// Dock Context Menu
static bool dock_menu_visible = false;
static int dock_menu_x = 0;
static int dock_menu_y = 0;
static int dock_menu_target_index = -1;

// Desktop Dialog State
static int desktop_dialog_state = 0; 
static char desktop_dialog_input[64];
static int desktop_dialog_cursor = 0;
static int desktop_dialog_target = -1;

// Message Box
static bool msg_box_visible = false;
static char msg_box_title[64];
static char msg_box_text[64];

// Hook definition
void (*wm_custom_paint_hook)(void) = NULL;

// Notification state
static char notif_text[256] = {0};
static int notif_timer = 0;
static int notif_x_offset = 420; // Starts offscreen
static bool notif_active = false;
extern bool ps2_ctrl_pressed;

static lumos_state_t lumos_state = {0};
static bool lumos_index_built = false;  

static bool force_redraw = true;
static bool menubar_dirty_pending = false;

static void lumos_update_search(void) {

    
    int query_hash = 0;
    for (int i = 0; lumos_state.search_query[i] && i < 256; i++) {
        query_hash = (query_hash * 31) + lumos_state.search_query[i];
    }
    
    if (query_hash == lumos_state.last_query_hash) {
        return; 
    }
    
    lumos_state.last_query_hash = query_hash;
    lumos_state.result_count = 0;
    lumos_state.selected_index = 0;
    
    if (lumos_state.search_len == 0) {
        return; 
    }
    
    file_index_result_t results[LUMOS_MAX_RESULTS];
    int count = file_index_find_fuzzy(lumos_state.search_query, results, LUMOS_MAX_RESULTS);
    
    lumos_state.result_count = count;
    for (int i = 0; i < count && i < LUMOS_MAX_RESULTS; i++) {
        lumos_state.results[i] = results[i];
    }
    
    int sw = get_screen_width();
    int sh = get_screen_height();
    int modal_height = LUMOS_SEARCH_HEIGHT + (lumos_state.result_count * LUMOS_RESULT_HEIGHT) + 10;
    int modal_y = (sh * 2 / 5) - (modal_height / 2);
    
    graphics_mark_dirty(0, 0, sw, sh);
    force_redraw = true;
}

static void wm_lumos_handle_key(char c) {
    if (c == 27) {  
        lumos_state.visible = false;
        force_redraw = true;
        return;
    }
    
    if (c == '\n') {  
        if (lumos_state.result_count > 0 && lumos_state.selected_index < lumos_state.result_count) {
            const char *file_path = lumos_state.results[lumos_state.selected_index].entry.path;
            explorer_open_target(file_path);
            lumos_state.visible = false;
            force_redraw = true;
        }
        return;
    }
    
    if (c == 17) {  
        if (lumos_state.selected_index > 0) {
            lumos_state.selected_index--;
            force_redraw = true;
        }
        return;
    }
    
    if (c == 18) { 
        if (lumos_state.selected_index < lumos_state.result_count - 1) {
            lumos_state.selected_index++;
            force_redraw = true;
        }
        return;
    }
    
    if (c == '\b' || c == 127) {  
        if (lumos_state.cursor_pos > 0) {
            for (int i = lumos_state.cursor_pos - 1; i < lumos_state.search_len; i++) {
                lumos_state.search_query[i] = lumos_state.search_query[i + 1];
            }
            lumos_state.search_len--;
            lumos_state.cursor_pos--;
            lumos_state.search_query[lumos_state.search_len] = 0;
            lumos_update_search();
            force_redraw = true;
        }
        return;
    }
    
    if (c == 19) {  
        if (lumos_state.cursor_pos > 0) {
            lumos_state.cursor_pos--;
            force_redraw = true;
        }
        return;
    }
    
    if (c == 20) {  
        if (lumos_state.cursor_pos < lumos_state.search_len) {
            lumos_state.cursor_pos++;
            force_redraw = true;
        }
        return;
    }
    
    if (c >= 32 && c <= 126 && lumos_state.search_len < 255) {
        for (int i = lumos_state.search_len; i >= lumos_state.cursor_pos; i--) {
            lumos_state.search_query[i + 1] = lumos_state.search_query[i];
        }
        lumos_state.search_query[lumos_state.cursor_pos] = c;
        lumos_state.search_len++;
        lumos_state.cursor_pos++;
        lumos_state.search_query[lumos_state.search_len] = 0;
        lumos_update_search();
        force_redraw = true;
    }
}

// Dragging State
static bool is_dragging = false;
static bool is_resizing = false;
static int drag_start_w = 0;
static int drag_start_h = 0;
static Window *drag_window = NULL;
static int drag_offset_x = 0;
static int drag_offset_y = 0;

// File Dragging State
bool is_dragging_file = false;
static char drag_file_path[FAT32_MAX_PATH];
static int drag_icon_type = 0; 

typedef struct {
    int y_start;
    int y_end;
    DirtyRect dirty;
    volatile int *completion_counter;
    int pass;
} wm_strip_job_t;
static int drag_start_x = 0;
static int drag_start_y = 0;
static int drag_icon_orig_x = 0;
static int drag_icon_orig_y = 0;
static Window *drag_src_win = NULL;

static Window *all_windows[32];
static int window_count = 0;


static uint32_t timer_ticks = 0;

static int last_cursor_x = 400;
static int last_cursor_y = 300;

#define CURSOR_W 18
#define CURSOR_H 18

static bool periodic_refresh_pending = false;

// --- Desktop State ---
#define MAX_DESKTOP_ICONS 32
typedef struct {
    char name[64];
    int x, y;
    int type; 
} DesktopIcon;

static DesktopIcon desktop_icons[MAX_DESKTOP_ICONS];
static int desktop_icon_count = 0;

// Desktop Settings
bool desktop_snap_to_grid = true;
bool desktop_auto_align = true;
int desktop_max_rows_per_col = 13;
int desktop_max_cols = 23;

// Mouse Settings
int mouse_speed = 10;       
static int mouse_accum_x = 0;
static int mouse_accum_y = 0;
Window *active_mouse_capture_win = NULL;

// Helper to check if string ends with suffix
static bool str_ends_with(const char *str, const char *suffix) {
    int str_len = 0; while(str[str_len]) str_len++;
    int suf_len = 0; while(suffix[suf_len]) suf_len++;
    if (suf_len > str_len) return false;
    
    for (int i = 0; i < suf_len; i++) {
        if (str[str_len - suf_len + i] != suffix[i]) return false;
    }
    return true;
}

static bool is_image_file(const char *filename) {
    if (!filename) return false;
    return str_ends_with(filename, ".jpg") || str_ends_with(filename, ".JPG") ||
           str_ends_with(filename, ".png") || str_ends_with(filename, ".PNG") ||
           str_ends_with(filename, ".gif") || str_ends_with(filename, ".GIF") ||
           str_ends_with(filename, ".bmp") || str_ends_with(filename, ".BMP") ||
           str_ends_with(filename, ".tga") || str_ends_with(filename, ".TGA");
}

// Helper to check if string starts with prefix
static bool str_starts_with(const char *str, const char *prefix) {
    while(*prefix) {
        if (*prefix++ != *str++) return false;
    }
    return true;
}


static void refresh_desktop_icons(void) {
    // Update limit in FS
    fat32_set_desktop_limit(desktop_max_cols * desktop_max_rows_per_col);

    FAT32_FileInfo *files = (FAT32_FileInfo*)kmalloc(MAX_DESKTOP_ICONS * sizeof(FAT32_FileInfo));
    if (!files) return;

    int file_count = fat32_list_directory("/root/Desktop", files, MAX_DESKTOP_ICONS);
    
    // Temp array to hold new state
    DesktopIcon new_icons[MAX_DESKTOP_ICONS];
    int new_count = 0;
    bool file_processed[MAX_DESKTOP_ICONS];
    for(int i=0; i<MAX_DESKTOP_ICONS; i++) file_processed[i] = false;

    for (int i = 0; i < desktop_icon_count; i++) {
        int found_idx = -1;
        for (int j = 0; j < file_count; j++) {
            if (!file_processed[j] && str_eq(desktop_icons[i].name, files[j].name) != 0) {
                found_idx = j;
                break;
            }
        }
        
        if (found_idx != -1) {
            // Keep it
            if (new_count < MAX_DESKTOP_ICONS) {
                new_icons[new_count] = desktop_icons[i];
                new_count++;
                file_processed[found_idx] = true;
            }
        }
    }

    for (int i = 0; i < file_count; i++) {
        if (!file_processed[i]) {
            if (files[i].name[0] == '.') continue; 
            if (new_count >= MAX_DESKTOP_ICONS) break;

            DesktopIcon *dest = &new_icons[new_count];
            int k = 0; while(files[i].name[k] && k < 63) { dest->name[k] = files[i].name[k]; k++; }
            dest->name[k] = 0;

            if (files[i].is_directory) dest->type = 1;
            else if (str_ends_with(dest->name, ".shortcut")) dest->type = 2;
            else dest->type = 0;
            dest->x = -1; // Mark as new for layout
            dest->y = -1;
            
            new_count++;
        }
    }
    
    desktop_icon_count = new_count;
    for(int i=0; i<new_count; i++) desktop_icons[i] = new_icons[i];
    kfree(files);
    
    if (desktop_auto_align) {
        int start_x = 20;
        int start_y = 30;
        int grid_x = 0;
        int grid_y = 0;
        
        int recycle_idx = -1;
        for (int i = 0; i < desktop_icon_count; i++) {
            if (str_starts_with(desktop_icons[i].name, "Recycle Bin")) {
                recycle_idx = i;
                break;
            }
        }
        
        // Place Recycle Bin at bottom-right of grid
        if (recycle_idx != -1) {
            desktop_icons[recycle_idx].x = start_x + (desktop_max_cols - 1) * 80;
            desktop_icons[recycle_idx].y = start_y + (desktop_max_rows_per_col - 1) * 80;
        }
        
        for (int i = 0; i < desktop_icon_count; i++) {
            if (i == recycle_idx) continue;
            
            desktop_icons[i].x = start_x + (grid_x * 80);
            desktop_icons[i].y = start_y + (grid_y * 80);
            
            grid_y++;
            if (grid_y >= desktop_max_rows_per_col) {
                grid_y = 0;
                grid_x++;

            }
        }
    } else {
        // Place new icons in first available spot
        bool occupied[16][16] = {false};
        for (int i = 0; i < desktop_icon_count; i++) {
            if (desktop_icons[i].x != -1) {
                int col = (desktop_icons[i].x - 20) / 80;
                int row = (desktop_icons[i].y - 20) / 80;
                if (col >= 0 && col < 16 && row >= 0 && row < 16) occupied[col][row] = true;
            }
        }
        
        for (int i = 0; i < desktop_icon_count; i++) {
            if (desktop_icons[i].x == -1) {
                int found_col = -1, found_row = -1;
                for (int c = 0; c < 16; c++) {
                    for (int r = 0; r < desktop_max_rows_per_col; r++) {
                        if (!occupied[c][r]) {
                            found_col = c; found_row = r;
                            goto found;
                        }
                    }
                }
                found:
                if (found_col != -1) {
                    desktop_icons[i].x = 20 + found_col * 80;
                    desktop_icons[i].y = 20 + found_row * 80;
                    occupied[found_col][found_row] = true;
                }
            }
        }
    }
}

void wm_refresh_desktop(void) {
    refresh_desktop_icons();
    force_redraw = true;
}

int wm_get_desktop_icon_count(void) {
    return desktop_icon_count;
}

void wm_show_message(const char *title, const char *message) {
    int i=0; while(title[i] && i<63) { msg_box_title[i] = title[i]; i++; } msg_box_title[i] = 0;
    i=0; while(message[i] && i<63) { msg_box_text[i] = message[i]; i++; } msg_box_text[i] = 0;
    msg_box_visible = true;
    force_redraw = true;
}

static void draw_icon_label(int x, int y, const char *label) {
    char line1[11] = {0}; // 10 chars + null
    char line2[11] = {0}; // 10 chars + null
    int len = 0; while(label[len]) len++;
    
    if (len <= 10) {
        for (int i = 0; i < len; i++) line1[i] = label[i];
    } else {
        // Dot-based wrap: keep extension together if prefix fits
        int dot_pos = -1;
        for (int i = len - 1; i >= 0; i--) {
            if (label[i] == '.') { dot_pos = i; break; }
        }

        int split = -1;
        if (dot_pos != -1 && dot_pos > 0 && dot_pos <= 10) {
            split = dot_pos;
        } else {
            // Word-based wrap: look for space in the first 11 characters
            for (int i = 10; i >= 0; i--) {
                if (label[i] == ' ') {
                    split = i;
                    break;
                }
            }
        }

        if (split != -1) {
            for (int i = 0; i < split; i++) line1[i] = label[i];
            int start2 = (label[split] == ' ') ? split + 1 : split;
            int j = 0;
            while (label[start2 + j] && j < 10) {
                line2[j] = label[start2 + j];
                j++;
            }
            if (label[start2 + j] != 0) {
                int t = (j > 8) ? 8 : j;
                line2[t] = '.'; line2[t+1] = '.'; line2[t+2] = 0;
            }
        } else {
            for (int i = 0; i < 10; i++) line1[i] = label[i];
            int j = 0;
            while (label[10 + j] && j < 10) {
                line2[j] = label[10 + j];
                j++;
            }
            if (label[10 + j] != 0) {
                int t = (j > 8) ? 8 : j;
                line2[t] = '.'; line2[t+1] = '.'; line2[t+2] = 0;
            }
        }
    }
    
    // Draw Line 1 Centered in 80px cell
    int l1_len = 0; while(line1[l1_len]) l1_len++;
    int l1_w = l1_len * 8;
    // x passed is cell left. Center is x + 40. Text start is x + 40 - w/2
    draw_string(x + (80 - l1_w)/2, y + 48, line1, COLOR_WHITE);
    
    // Draw Line 2 Centered
    if (line2[0]) {
        int l2_len = 0; while(line2[l2_len]) l2_len++;
        int l2_w = l2_len * 8;
        draw_string(x + (80 - l2_w)/2, y + 58, line2, COLOR_WHITE);
    }
}

// --- Drawing Helpers ---

void draw_bevel_rect(int x, int y, int w, int h, bool sunken) {
    draw_rect(x, y, w, h, COLOR_GRAY);
    
    uint32_t top_left = sunken ? COLOR_DKGRAY : COLOR_WHITE;
    uint32_t bot_right = sunken ? COLOR_WHITE : COLOR_DKGRAY;
    
    // Top
    draw_rect(x, y, w, 1, top_left);
    // Left
    draw_rect(x, y, 1, h, top_left);
    // Bottom
    draw_rect(x, y + h - 1, w, 1, bot_right);
    // Right
    draw_rect(x + w - 1, y, 1, h, bot_right);
}

void draw_button(int x, int y, int w, int h, const char *text, bool pressed) {
    draw_bevel_rect(x, y, w, h, pressed);
    // Center Text
    int len = 0; while(text[len]) len++;
    int tx = x + (w - (len * 8)) / 2;
    int ty = y + (h - 8) / 2;
    if (pressed) { tx++; ty++; }
    draw_string(tx, ty, text, COLOR_BLACK);
}

// Forward declarations for dock icons
static void draw_dock_files(int x, int y);
static void draw_dock_settings(int x, int y);
static void draw_dock_notepad(int x, int y);
static void draw_dock_calculator(int x, int y);
static void draw_dock_grapher(int x, int y);
static void draw_dock_terminal(int x, int y);
static void draw_dock_minesweeper(int x, int y);
static void draw_dock_paint(int x, int y);
static void draw_dock_clock(int x, int y);
static void draw_dock_taskman(int x, int y);
static void draw_dock_word(int x, int y);
static void draw_dock_browser(int x, int y);
static void draw_filled_circle(int cx, int cy, int r, uint32_t color);
static bool thumb_cache_is_failed(const char *path);
static uint32_t* thumb_cache_lookup(const char *path);
static uint32_t* thumb_cache_decode(const char *path);

#define DOCK_ITEM_SIZE 48
#define DOCK_ITEM_SPACING 10
#define DOCK_HEIGHT 60
#define DOCK_VERTICAL_MARGIN 6
#define DOCK_BG_PADDING 12
#define DOCK_ICON_COUNT 12
#define DOCK_ICON_SIZE 48
#define DOCK_ICON_PIXELS (DOCK_ICON_SIZE * DOCK_ICON_SIZE)
#define DOCK_ICON_BASE_PATH "/Library/images/icons/colloid/"
#define MAX_DOCK_ITEMS 32
#define DOCK_LABEL_MAX 64
#define DOCK_CONFIG_PATH "/Library/Dock/dock.cfg"

enum {
    DOCK_SLOT_FILES = 0,
    DOCK_SLOT_SETTINGS = 1,
    DOCK_SLOT_NOTEPAD = 2,
    DOCK_SLOT_CALCULATOR = 3,
    DOCK_SLOT_GRAPHER = 4,
    DOCK_SLOT_TERMINAL = 5,
    DOCK_SLOT_MINESWEEPER = 6,
    DOCK_SLOT_PAINT = 7,
    DOCK_SLOT_BROWSER = 8,
    DOCK_SLOT_TASKMAN = 9,
    DOCK_SLOT_CLOCK = 10,
    DOCK_SLOT_WORD = 11,
};

typedef struct {
    char label[DOCK_LABEL_MAX];
    char target[FAT32_MAX_PATH];
    int icon_slot;
} dock_item_t;

typedef struct {
    const char *label;
    const char *target;
    int icon_slot;
} dock_default_item_t;

static dock_item_t dock_items[MAX_DOCK_ITEMS];
static int dock_item_count = 0;

static const dock_default_item_t dock_default_items[] = {
    {"Files", "/root", DOCK_SLOT_FILES},
    {"Browser", "/bin/browser.elf", DOCK_SLOT_BROWSER},
    {"Settings", "/bin/settings.elf", DOCK_SLOT_SETTINGS},
    {"Notepad", "/bin/notepad.elf", DOCK_SLOT_NOTEPAD},
    {"Calculator", "/bin/calculator.elf", DOCK_SLOT_CALCULATOR},
    {"Grapher", "/bin/grapher.elf", DOCK_SLOT_GRAPHER},
    {"Terminal", "/bin/terminal.elf", DOCK_SLOT_TERMINAL},
    {"Minesweeper", "/bin/minesweeper.elf", DOCK_SLOT_MINESWEEPER},
    {"Paint", "/bin/paint.elf", DOCK_SLOT_PAINT},
    {"Task Manager", "/bin/taskman.elf", DOCK_SLOT_TASKMAN},
    {"Clock", "/bin/clock.elf", DOCK_SLOT_CLOCK},
    {"BoredWord", "/bin/boredword.elf", DOCK_SLOT_WORD},
};

typedef enum {
    DOCK_ICON_UNTRIED = 0,
    DOCK_ICON_LOADING = 1,
    DOCK_ICON_READY = 2,
    DOCK_ICON_FAILED = 3,
} dock_icon_state_t;

typedef struct {
    const char *filename;
    volatile int state;
    uint32_t pixels[DOCK_ICON_PIXELS];
} dock_icon_entry_t;

static dock_icon_entry_t dock_icons[DOCK_ICON_COUNT] = {
    {"file-manager.png", DOCK_ICON_UNTRIED, {0}},
    {"preferences-system.png", DOCK_ICON_UNTRIED, {0}},
    {"text-editor.png", DOCK_ICON_UNTRIED, {0}},
    {"calc.png", DOCK_ICON_UNTRIED, {0}},
    {"se.sjoerd.Graphs.png", DOCK_ICON_UNTRIED, {0}},
    {"xterm.png", DOCK_ICON_UNTRIED, {0}},
    {"gnome-mines.png", DOCK_ICON_UNTRIED, {0}},
    {"gnome-paint.png", DOCK_ICON_UNTRIED, {0}},
    {"web-browser.png", DOCK_ICON_UNTRIED, {0}},
    {"utilities-system-monitor.png", DOCK_ICON_UNTRIED, {0}},
    {"preferences-system-time.png", DOCK_ICON_UNTRIED, {0}},
    {"libreoffice-writer.png", DOCK_ICON_UNTRIED, {0}},
};

static uint32_t blend_src_over_dst(uint32_t dst, uint32_t src) {
    uint32_t sa = (src >> 24) & 0xFF;
    if (sa == 0) return dst;
    if (sa == 255) return 0xFF000000 | (src & 0x00FFFFFF);

    uint32_t sr = (src >> 16) & 0xFF;
    uint32_t sg = (src >> 8) & 0xFF;
    uint32_t sb = src & 0xFF;

    uint32_t dr = (dst >> 16) & 0xFF;
    uint32_t dg = (dst >> 8) & 0xFF;
    uint32_t db = dst & 0xFF;

    uint32_t inv = 255 - sa;
    uint32_t out_r = (sr * sa + dr * inv) / 255;
    uint32_t out_g = (sg * sa + dg * inv) / 255;
    uint32_t out_b = (sb * sa + db * inv) / 255;

    return 0xFF000000 | (out_r << 16) | (out_g << 8) | out_b;
}

static bool dock_icon_decode_into_entry(dock_icon_entry_t *entry) {
    if (!entry || !entry->filename) return false;

    char full_path[192];
    k_strcpy(full_path, DOCK_ICON_BASE_PATH);
    k_strcpy(full_path + k_strlen(full_path), entry->filename);

    FAT32_FileHandle *fh = fat32_open(full_path, "r");
    if (!fh) return false;

    uint32_t file_size = fh->size;
    if (file_size == 0 || file_size > 8 * 1024 * 1024) {
        fat32_close(fh);
        return false;
    }

    unsigned char *encoded = (unsigned char*)kmalloc(file_size);
    if (!encoded) {
        fat32_close(fh);
        return false;
    }

    int total = 0;
    while (total < (int)file_size) {
        int chunk = fat32_read(fh, encoded + total, (int)file_size - total);
        if (chunk <= 0) break;
        total += chunk;
    }
    fat32_close(fh);

    if (total <= 0) {
        kfree(encoded);
        return false;
    }

    int img_w = 0, img_h = 0, channels = 0;
    unsigned char *rgba = stbi_load_from_memory(encoded, total, &img_w, &img_h, &channels, 4);
    kfree(encoded);
    if (!rgba || img_w <= 0 || img_h <= 0) {
        if (rgba) stbi_image_free(rgba);
        return false;
    }

    int img_max_x = img_w - 1;
    int img_max_y = img_h - 1;

    k_memset(entry->pixels, 0, sizeof(entry->pixels));
    for (int ty = 0; ty < DOCK_ICON_SIZE; ty++) {
        for (int tx = 0; tx < DOCK_ICON_SIZE; tx++) {
            int sx = (DOCK_ICON_SIZE > 1) ? (tx * img_max_x) / (DOCK_ICON_SIZE - 1) : 0;
            int sy = (DOCK_ICON_SIZE > 1) ? (ty * img_max_y) / (DOCK_ICON_SIZE - 1) : 0;
            if (sx < 0) sx = 0;
            if (sy < 0) sy = 0;
            if (sx > img_max_x) sx = img_max_x;
            if (sy > img_max_y) sy = img_max_y;

            int idx = (sy * img_w + sx) * 4;
            uint32_t r = rgba[idx];
            uint32_t g = rgba[idx + 1];
            uint32_t b = rgba[idx + 2];
            uint32_t a = rgba[idx + 3];
            entry->pixels[ty * DOCK_ICON_SIZE + tx] = (a << 24) | (r << 16) | (g << 8) | b;
        }
    }

    stbi_image_free(rgba);
    return true;
}

static dock_icon_entry_t *dock_icon_get_entry(int slot_index) {
    if (slot_index < 0 || slot_index >= DOCK_ICON_COUNT) return NULL;

    dock_icon_entry_t *entry = &dock_icons[slot_index];
    int state = __atomic_load_n(&entry->state, __ATOMIC_ACQUIRE);
    if (state == DOCK_ICON_READY || state == DOCK_ICON_FAILED) return entry;

    if (state == DOCK_ICON_UNTRIED) {
        int expected = DOCK_ICON_UNTRIED;
        if (__atomic_compare_exchange_n(&entry->state, &expected, DOCK_ICON_LOADING, false,
                                        __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
            bool ok = dock_icon_decode_into_entry(entry);
            __atomic_store_n(&entry->state, ok ? DOCK_ICON_READY : DOCK_ICON_FAILED, __ATOMIC_RELEASE);
            return entry;
        }
        state = __atomic_load_n(&entry->state, __ATOMIC_ACQUIRE);
    }

    if (state == DOCK_ICON_LOADING) return NULL;
    return entry;
}

bool wm_draw_dock_icon_scaled(int x, int y, int size, int slot_index) {
    if (size <= 0) return false;

    dock_icon_entry_t *entry = dock_icon_get_entry(slot_index);
    if (!entry) return false;
    if (__atomic_load_n(&entry->state, __ATOMIC_ACQUIRE) != DOCK_ICON_READY) return false;

    int src_max = DOCK_ICON_SIZE - 1;

    for (int ty = 0; ty < size; ty++) {
        int sy = (size > 1) ? (ty * src_max) / (size - 1) : 0;
        if (sy < 0) sy = 0;
        if (sy > src_max) sy = src_max;

        for (int tx = 0; tx < size; tx++) {
            int sx = (size > 1) ? (tx * src_max) / (size - 1) : 0;
            if (sx < 0) sx = 0;
            if (sx > src_max) sx = src_max;

            uint32_t src = entry->pixels[sy * DOCK_ICON_SIZE + sx];
            uint32_t a = (src >> 24) & 0xFF;
            if (a == 0) continue;

            if (a == 255) {
                put_pixel(x + tx, y + ty, 0xFF000000 | (src & 0x00FFFFFF));
            } else {
                uint32_t dst = graphics_get_pixel(x + tx, y + ty);
                put_pixel(x + tx, y + ty, blend_src_over_dst(dst, src));
            }
        }
    }

    return true;
}

static void draw_dock_icon_slot_png(int x, int y, int slot_index) {
    (void)wm_draw_dock_icon_scaled(x, y, DOCK_ICON_SIZE, slot_index);
}

static bool draw_icon_path_scaled(int x, int y, int size, const char *path) {
    uint32_t *icon = NULL;
    if (size <= 0 || !path || !path[0]) return false;

    icon = thumb_cache_lookup(path);
    if (!icon && !thumb_cache_is_failed(path)) {
        icon = thumb_cache_decode(path);
    }
    if (!icon) return false;

    int src_max = 47;
    for (int ty = 0; ty < size; ty++) {
        int sy = (size > 1) ? (ty * src_max) / (size - 1) : 0;
        if (sy < 0) sy = 0;
        if (sy > src_max) sy = src_max;

        for (int tx = 0; tx < size; tx++) {
            int sx = (size > 1) ? (tx * src_max) / (size - 1) : 0;
            if (sx < 0) sx = 0;
            if (sx > src_max) sx = src_max;

            uint32_t src = icon[sy * 48 + sx];
            uint32_t a = (src >> 24) & 0xFF;
            if (a == 0) continue;

            if (a == 255) {
                put_pixel(x + tx, y + ty, 0xFF000000 | (src & 0x00FFFFFF));
            } else {
                uint32_t dst = graphics_get_pixel(x + tx, y + ty);
                put_pixel(x + tx, y + ty, blend_src_over_dst(dst, src));
            }
        }
    }

    return true;
}

static bool dock_resolve_shortcut_target(const char *shortcut_path, char *out_target, int out_target_size) {
    if (!shortcut_path || !out_target || out_target_size <= 1) return false;

    FAT32_FileHandle *fh = fat32_open(shortcut_path, "r");
    if (!fh) return false;

    int len = fat32_read(fh, out_target, out_target_size - 1);
    fat32_close(fh);
    if (len <= 0) return false;

    while (len > 0 && (out_target[len - 1] == '\n' || out_target[len - 1] == '\r')) {
        len--;
    }
    out_target[len] = 0;
    return len > 0;
}

static bool dock_draw_metadata_icon(int x, int y, const dock_item_t *item) {
    if (!item || !item->target[0]) return false;

    char elf_target[FAT32_MAX_PATH];
    const char *metadata_target = NULL;

    if (str_ends_with(item->target, ".elf")) {
        metadata_target = item->target;
    } else if (str_ends_with(item->target, ".shortcut")) {
        if (dock_resolve_shortcut_target(item->target, elf_target, sizeof(elf_target)) &&
            str_ends_with(elf_target, ".elf")) {
            metadata_target = elf_target;
        }
    }

    if (!metadata_target) return false;

    char icon_path[BOREDOS_APP_METADATA_MAX_IMAGE_PATH];
    if (!app_metadata_get_primary_image(metadata_target, icon_path, sizeof(icon_path))) {
        return false;
    }

    return draw_icon_path_scaled(x, y, DOCK_ICON_SIZE, icon_path);
}

static void dock_copy_text(char *dst, int dst_size, const char *src) {
    if (!dst || dst_size <= 0) return;
    int i = 0;
    if (src) {
        while (src[i] && i < dst_size - 1) {
            dst[i] = src[i];
            i++;
        }
    }
    dst[i] = 0;
}

static int dock_parse_int(const char *s) {
    if (!s) return 0;
    int v = 0;
    while (*s >= '0' && *s <= '9') {
        v = (v * 10) + (*s - '0');
        s++;
    }
    return v;
}

static void dock_write_int(FAT32_FileHandle *fh, int v) {
    char tmp[16];
    int n = 0;
    if (v == 0) {
        tmp[n++] = '0';
    } else {
        char rev[16];
        int r = 0;
        while (v > 0 && r < 15) {
            rev[r++] = '0' + (v % 10);
            v /= 10;
        }
        while (r > 0) tmp[n++] = rev[--r];
    }
    if (n > 0) fat32_write(fh, tmp, n);
}

static void dock_label_from_target(const char *target, char *out_label, int out_size) {
    const char *name = target;
    if (!target || !target[0]) {
        dock_copy_text(out_label, out_size, "Item");
        return;
    }

    int i = 0;
    while (target[i]) {
        if (target[i] == '/') name = &target[i + 1];
        i++;
    }

    if (!name || !name[0]) {
        dock_copy_text(out_label, out_size, "Files");
        return;
    }

    dock_copy_text(out_label, out_size, name);
    int len = (int)k_strlen(out_label);
    if (len > 4 && str_ends_with(out_label, ".elf")) {
        out_label[len - 4] = 0;
    } else if (len > 9 && str_ends_with(out_label, ".shortcut")) {
        out_label[len - 9] = 0;
    }
}

static int dock_icon_slot_for_target(const char *target, const char *label) {
    (void)label;
    if (!target || !target[0]) return DOCK_SLOT_FILES;

    if (str_eq(target, "/") != 0 || str_eq(target, "/root") != 0 || fat32_is_directory(target)) {
        return DOCK_SLOT_FILES;
    }
    if (str_ends_with(target, ".elf")) return DOCK_SLOT_TERMINAL;
    if (str_ends_with(target, ".shortcut")) {
        char shortcut_target[FAT32_MAX_PATH];
        if (dock_resolve_shortcut_target(target, shortcut_target, sizeof(shortcut_target)) &&
            str_ends_with(shortcut_target, ".elf")) {
            return DOCK_SLOT_TERMINAL;
        }
        return DOCK_SLOT_NOTEPAD;
    }

    return DOCK_SLOT_NOTEPAD;
}

static bool dock_insert_item(int index, const char *label, const char *target, int icon_slot) {
    if (dock_item_count >= MAX_DOCK_ITEMS) return false;
    if (index < 0) index = 0;
    if (index > dock_item_count) index = dock_item_count;
    if (icon_slot < 0 || icon_slot >= DOCK_ICON_COUNT) icon_slot = DOCK_SLOT_FILES;

    for (int i = dock_item_count; i > index; i--) dock_items[i] = dock_items[i - 1];

    dock_copy_text(dock_items[index].label, DOCK_LABEL_MAX, label);
    dock_copy_text(dock_items[index].target, FAT32_MAX_PATH, target);
    dock_items[index].icon_slot = icon_slot;
    dock_item_count++;
    return true;
}

static void dock_remove_item(int index) {
    if (index < 0 || index >= dock_item_count) return;
    for (int i = index; i < dock_item_count - 1; i++) dock_items[i] = dock_items[i + 1];
    dock_item_count--;
}

static void dock_move_item(int from_index, int to_index) {
    if (from_index < 0 || from_index >= dock_item_count) return;
    if (to_index < 0) to_index = 0;
    if (to_index > dock_item_count) to_index = dock_item_count;
    if (to_index > from_index) to_index--;
    if (to_index == from_index) return;

    dock_item_t temp = dock_items[from_index];
    if (to_index > from_index) {
        for (int i = from_index; i < to_index; i++) dock_items[i] = dock_items[i + 1];
    } else {
        for (int i = from_index; i > to_index; i--) dock_items[i] = dock_items[i - 1];
    }
    dock_items[to_index] = temp;
}

static int dock_find_item_by_target(const char *target) {
    if (!target) return -1;
    for (int i = 0; i < dock_item_count; i++) {
        if (str_eq(dock_items[i].target, target) != 0) return i;
    }
    return -1;
}

static int dock_total_width(void) {
    if (dock_item_count <= 0) return 0;
    return (dock_item_count * DOCK_ITEM_SIZE) + ((dock_item_count - 1) * DOCK_ITEM_SPACING);
}

static bool dock_rect_contains(int x, int y, int w, int h, int px, int py) {
    return px >= x && px < x + w && py >= y && py < y + h;
}

static void dock_get_geometry(int sw, int sh, int *dock_x, int *dock_y, int *dock_bg_x, int *dock_bg_w) {
    int total_w = dock_total_width();
    if (dock_x) *dock_x = (sw - total_w) / 2;
    if (dock_y) *dock_y = sh - DOCK_HEIGHT - DOCK_VERTICAL_MARGIN;
    if (dock_bg_x) *dock_bg_x = (sw - total_w) / 2 - DOCK_BG_PADDING;
    if (dock_bg_w) *dock_bg_w = total_w + (DOCK_BG_PADDING * 2);
}

static bool dock_point_in_bounds(int x, int y, int sw, int sh) {
    int dock_y, dock_bg_x, dock_bg_w;
    dock_get_geometry(sw, sh, NULL, &dock_y, &dock_bg_x, &dock_bg_w);
    return dock_item_count > 0 && dock_rect_contains(dock_bg_x, dock_y, dock_bg_w, DOCK_HEIGHT, x, y);
}

static int dock_item_index_at_point(int x, int y, int sw, int sh) {
    if (!dock_point_in_bounds(x, y, sw, sh)) return -1;

    int dock_x;
    dock_get_geometry(sw, sh, &dock_x, NULL, NULL, NULL);
    for (int i = 0; i < dock_item_count; i++) {
        int ix = dock_x + i * (DOCK_ITEM_SIZE + DOCK_ITEM_SPACING);
        if (dock_rect_contains(ix, sh - DOCK_HEIGHT - DOCK_VERTICAL_MARGIN + 6, DOCK_ITEM_SIZE, DOCK_ITEM_SIZE, x, y)) {
            return i;
        }
    }
    return -1;
}

static int dock_insertion_index_from_x(int x, int y, int sw, int sh) {
    if (!dock_point_in_bounds(x, y, sw, sh)) return -1;
    int dock_x;
    dock_get_geometry(sw, sh, &dock_x, NULL, NULL, NULL);

    for (int i = 0; i < dock_item_count; i++) {
        int ix = dock_x + i * (DOCK_ITEM_SIZE + DOCK_ITEM_SPACING);
        if (x < ix + (DOCK_ITEM_SIZE / 2)) return i;
    }
    return dock_item_count;
}

static void dock_save_config(void) {
    fat32_mkdir("/root");
    fat32_mkdir("/Library/Dock/dock.cfg");

    FAT32_FileHandle *fh = fat32_open(DOCK_CONFIG_PATH, "w");
    if (!fh) return;

    const char *header = "v1\n";
    fat32_write(fh, header, (int)k_strlen(header));

    for (int i = 0; i < dock_item_count; i++) {
        dock_write_int(fh, dock_items[i].icon_slot);
        fat32_write(fh, "|", 1);
        fat32_write(fh, dock_items[i].label, (int)k_strlen(dock_items[i].label));
        fat32_write(fh, "|", 1);
        fat32_write(fh, dock_items[i].target, (int)k_strlen(dock_items[i].target));
        fat32_write(fh, "\n", 1);
    }

    fat32_close(fh);
}

static void dock_seed_defaults(void) {
    dock_item_count = 0;
    int default_count = (int)(sizeof(dock_default_items) / sizeof(dock_default_items[0]));
    for (int i = 0; i < default_count; i++) {
        dock_insert_item(dock_item_count, dock_default_items[i].label,
                         dock_default_items[i].target, dock_default_items[i].icon_slot);
    }
}

static void dock_load_config(void) {
    dock_item_count = 0;

    FAT32_FileHandle *fh = fat32_open(DOCK_CONFIG_PATH, "r");
    if (!fh) {
        dock_seed_defaults();
        dock_save_config();
        return;
    }

    uint32_t size = fh->size;
    if (size == 0 || size > 16384) {
        fat32_close(fh);
        dock_seed_defaults();
        dock_save_config();
        return;
    }

    char *buffer = (char*)kmalloc(size + 1);
    if (!buffer) {
        fat32_close(fh);
        dock_seed_defaults();
        return;
    }

    int total = 0;
    while (total < (int)size) {
        int chunk = fat32_read(fh, buffer + total, (int)size - total);
        if (chunk <= 0) break;
        total += chunk;
    }
    fat32_close(fh);
    buffer[total] = 0;

    char *cursor = buffer;
    while (*cursor) {
        char *line = cursor;
        while (*cursor && *cursor != '\n' && *cursor != '\r') cursor++;
        if (*cursor) {
            *cursor = 0;
            cursor++;
            while (*cursor == '\n' || *cursor == '\r') cursor++;
        }

        if (!line[0] || str_eq(line, "v1") != 0 || line[0] == '#') continue;

        char *sep1 = line;
        while (*sep1 && *sep1 != '|') sep1++;
        if (*sep1 != '|') continue;
        *sep1 = 0;

        char *label = sep1 + 1;
        char *sep2 = label;
        while (*sep2 && *sep2 != '|') sep2++;
        if (*sep2 != '|') continue;
        *sep2 = 0;
        char *target = sep2 + 1;

        if (!label[0] || !target[0]) continue;

        int icon_slot = dock_parse_int(line);
        if (icon_slot < 0 || icon_slot >= DOCK_ICON_COUNT) {
            icon_slot = dock_icon_slot_for_target(target, label);
        }

        if (!dock_insert_item(dock_item_count, label, target, icon_slot)) break;
    }

    kfree(buffer);

    if (dock_item_count == 0) {
        dock_seed_defaults();
        dock_save_config();
    }
}

static bool dock_can_pin_path(const char *path) {
    if (!path || !path[0]) return false;
    if (path[0] != '/') return false;
    if (fat32_is_directory(path)) return true;
    if (str_ends_with(path, ".elf")) return true;
    if (str_ends_with(path, ".shortcut")) return true;
    return false;
}

static bool dock_launch_shortcut_path(const char *shortcut_path) {
    FAT32_FileHandle *fh = fat32_open(shortcut_path, "r");
    if (!fh) return false;
    char buf[FAT32_MAX_PATH];
    int len = fat32_read(fh, buf, FAT32_MAX_PATH - 1);
    fat32_close(fh);
    if (len <= 0) return false;
    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r')) len--;
    buf[len] = 0;
    if (!buf[0]) return false;
    explorer_open_target(buf);
    return true;
}

static void dock_launch_item(int index) {
    if (index < 0 || index >= dock_item_count) return;
    const char *target = dock_items[index].target;
    if (!target || !target[0]) return;

    if (str_ends_with(target, ".shortcut")) {
        if (dock_launch_shortcut_path(target)) return;
    }

    explorer_open_target(target);
}

static bool dock_item_is_protected(int index) {
    if (index < 0 || index >= dock_item_count) return false;
    const dock_item_t *item = &dock_items[index];
    return (str_eq(item->label, "Files") != 0) && (str_eq(item->target, "/root") != 0);
}

static void draw_scaled_icon(int x, int y, void (*draw_fn)(int, int)) {
    uint32_t icon_buf[48 * 48];
    for (int i = 0; i < 48 * 48; i++) icon_buf[i] = 0xFFFF00FF;
    
    graphics_set_render_target(icon_buf, 48, 48);
    draw_fn(0, 0);
    graphics_set_render_target(NULL, 0, 0);
    int dx = x + 24, dy = y + 12;
    
    for (int ty = 0; ty < 32; ty++) {
        for (int tx = 0; tx < 32; tx++) {
            int src_x = tx * 48 / 32;
            int src_y = ty * 48 / 32;
            uint32_t c1 = icon_buf[src_y * 48 + src_x];
            if (c1 != 0xFFFF00FF) {
                put_pixel(dx + tx, dy + ty, c1);
            }
        }
    }
}

void draw_icon(int x, int y, const char *label) {
    uint32_t icon_buf[48 * 48];
    for (int i = 0; i < 48 * 48; i++) icon_buf[i] = 0xFFFF00FF;
    graphics_set_render_target(icon_buf, 48, 48);
    
    draw_rounded_rect_filled(0, 0, 48, 48, 10, 0xFFE0E0E0);
    draw_rounded_rect_filled(5, 5, 38, 38, 4, 0xFFFFFFFF);
    draw_rect(12, 15, 24, 2, 0xFFCCCCCC);
    draw_rect(12, 25, 24, 2, 0xFFCCCCCC);
    draw_rect(12, 35, 16, 2, 0xFFCCCCCC);
    
    graphics_set_render_target(NULL, 0, 0);
    
    int dx = x + 24, dy = y + 12;
    for (int ty = 0; ty < 32; ty++) {
        for (int tx = 0; tx < 32; tx++) {
            int src_x = tx * 48 / 32;
            int src_y = ty * 48 / 32;
            uint32_t c1 = icon_buf[src_y * 48 + src_x];
            if (c1 != 0xFFFF00FF) put_pixel(dx + tx, dy + ty, c1);
        }
    }
    
    draw_icon_label(x, y, label);
}

void draw_folder_icon(int x, int y, const char *label) {
    draw_scaled_icon(x, y, draw_dock_files);
    draw_icon_label(x, y, label);
}

void draw_document_icon(int x, int y, const char *label) {
    uint32_t icon_buf[48 * 48];
    for (int i = 0; i < 48 * 48; i++) icon_buf[i] = 0xFFFF00FF;
    graphics_set_render_target(icon_buf, 48, 48);
    
    // Document dock style (making the source drawing slightly smaller to reduce final size)
    draw_rounded_rect_filled(4, 4, 40, 40, 8, 0xFFFFFFFF);
    draw_rounded_rect_filled(8, 8, 32, 32, 4, 0xFFF5F5F5);
    draw_rect(14, 17, 20, 2, 0xFFBBBBBB);
    draw_rect(14, 25, 20, 2, 0xFFBBBBBB);
    draw_rect(14, 33, 14, 2, 0xFFBBBBBB);
    
    graphics_set_render_target(NULL, 0, 0);
    int dx = x + 24, dy = y + 12;
    for (int ty = 0; ty < 32; ty++) {
        for (int tx = 0; tx < 32; tx++) {
            int src_x = tx * 48 / 32;
            int src_y = ty * 48 / 32;
            uint32_t c1 = icon_buf[src_y * 48 + src_x];
            if (c1 != 0xFFFF00FF) put_pixel(dx + tx, dy + ty, c1);
        }
    }
    
    draw_icon_label(x, y, label);
}

void draw_pdf_icon(int x, int y, const char *label) {
    uint32_t icon_buf[48 * 48];
    for (int i = 0; i < 48 * 48; i++) icon_buf[i] = 0xFFFF00FF;
    graphics_set_render_target(icon_buf, 48, 48);
    
    // Document shape
    draw_rounded_rect_filled(4, 4, 40, 40, 8, 0xFFFFFFFF);
    // Red banner
    draw_rounded_rect_filled(8, 8, 32, 14, 4, 0xFFDF2020);
    // PDF text roughly (simplified to lines for now)
    draw_rect(14, 25, 20, 2, 0xFFBBBBBB);
    draw_rect(14, 33, 14, 2, 0xFFBBBBBB);
    
    graphics_set_render_target(NULL, 0, 0);
    int dx = x + 24, dy = y + 12;
    for (int ty = 0; ty < 32; ty++) {
        for (int tx = 0; tx < 32; tx++) {
            int src_x = tx * 48 / 32;
            int src_y = ty * 48 / 32;
            uint32_t c1 = icon_buf[src_y * 48 + src_x];
            if (c1 != 0xFFFF00FF) put_pixel(dx + tx, dy + ty, c1);
        }
    }
    
    draw_icon_label(x, y, label);
}

void draw_elf_icon(int x, int y, const char *label) {
    uint32_t icon_buf[48 * 48];
    for (int i = 0; i < 48 * 48; i++) icon_buf[i] = 0xFFFF00FF;
    graphics_set_render_target(icon_buf, 48, 48);
    
    // Grey squircle (macOS detailed style)
    draw_rounded_rect_filled(2, 2, 44, 44, 12, 0xFF353535); // Subtle shadow border
    draw_rounded_rect_filled(4, 4, 40, 40, 10, 0xFF4A4A4A); // Main grey body
    
    // Glossy top highlight
    draw_rect(10, 5, 28, 1, 0xFF5A5A5A);
    
    // Green "exec" text (fixed font 8x12)
    draw_string(8, 12, "exec", 0xFF00FF00);
    
    // Minor details to look "premium"
    draw_rect(10, 28, 28, 1, 0xFF3D3D3D);
    draw_rect(10, 34, 20, 1, 0xFF3D3D3D);
    
    graphics_set_render_target(NULL, 0, 0);
    
    int dx = x + 24, dy = y + 12;
    for (int ty = 0; ty < 32; ty++) {
        for (int tx = 0; tx < 32; tx++) {
            int src_x = tx * 48 / 32;
            int src_y = ty * 48 / 32;
            uint32_t c1 = icon_buf[src_y * 48 + src_x];
            if (c1 != 0xFFFF00FF) put_pixel(dx + tx, dy + ty, c1);
        }
    }
    
    draw_icon_label(x, y, label);
}

#define THUMB_CACHE_SIZE 128
#define THUMB_PIXELS (48 * 48)
static struct {
    char path[FAT32_MAX_PATH];
    uint32_t pixels[THUMB_PIXELS];
    bool valid;
    bool failed; // Mark as failed so we don't retry
} thumb_cache[THUMB_CACHE_SIZE];
static int thumb_cache_next = 0; // Round-robin eviction

// Deferred Thumbnail Request Queue
#define THUMB_QUEUE_SIZE 128
static char thumb_request_queue[THUMB_QUEUE_SIZE][FAT32_MAX_PATH];
static int thumb_queue_head = 0;
static int thumb_queue_tail = 0;

static void thumb_request_push(const char *path) {
    if (!path) return;
    
    // Check if already in queue
    int curr = thumb_queue_head;
    while (curr != thumb_queue_tail) {
        if (str_eq(thumb_request_queue[curr], path) != 0) return;
        curr = (curr + 1) % THUMB_QUEUE_SIZE;
    }
    
    // Push if space
    int next_tail = (thumb_queue_tail + 1) % THUMB_QUEUE_SIZE;
    if (next_tail != thumb_queue_head) {
        int i = 0;
        while (path[i] && i < 255) {
            thumb_request_queue[thumb_queue_tail][i] = path[i];
            i++;
        }
        thumb_request_queue[thumb_queue_tail][i] = 0;
        thumb_queue_tail = next_tail;
    }
}

static bool thumb_cache_is_failed(const char *path) {
    for (int i = 0; i < THUMB_CACHE_SIZE; i++) {
        if (thumb_cache[i].failed && str_eq(thumb_cache[i].path, path) != 0) return true;
    }
    return false;
}

static uint32_t* thumb_cache_lookup(const char *path) {
    for (int i = 0; i < THUMB_CACHE_SIZE; i++) {
        if (thumb_cache[i].valid && str_eq(thumb_cache[i].path, path) != 0) {
            return thumb_cache[i].pixels;
        }
    }
    return NULL;
}

static void thumb_cache_mark_failed(const char *path) {
    if (!path || !path[0]) return;

    int slot = thumb_cache_next;
    thumb_cache_next = (thumb_cache_next + 1) % THUMB_CACHE_SIZE;

    int p = 0;
    while (path[p] && p < FAT32_MAX_PATH - 1) {
        thumb_cache[slot].path[p] = path[p];
        p++;
    }
    thumb_cache[slot].path[p] = 0;
    thumb_cache[slot].valid = false;
    thumb_cache[slot].failed = true;
}



static uint32_t* thumb_cache_decode(const char *path) {
    uint32_t *cached = thumb_cache_lookup(path);
    if (cached) return cached;
    if (thumb_cache_is_failed(path)) return NULL;

    // Open and read the JPG file
    FAT32_FileHandle *fh = fat32_open(path, "r");
    if (!fh) {
        thumb_cache_mark_failed(path);
        return NULL;
    }
    
    uint32_t file_size = fh->size;
    if (file_size == 0 || file_size > 8 * 1024 * 1024) {
        fat32_close(fh);
        thumb_cache_mark_failed(path);
        return NULL;
    }
    
    unsigned char *buf = (unsigned char*)kmalloc(file_size);
    if (!buf) {
        fat32_close(fh);
        thumb_cache_mark_failed(path);
        return NULL;
    }
    
    int total = 0;
    while (total < (int)file_size) {
        int chunk = fat32_read(fh, buf + total, (int)file_size - total);
        if (chunk <= 0) break;
        total += chunk;
    }
    fat32_close(fh);
    
    if (total <= 0) {
        kfree(buf);
        thumb_cache_mark_failed(path);
        return NULL;
    }
    
    // Decode image
    int img_w, img_h, channels;
    unsigned char *img = stbi_load_from_memory(buf, total, &img_w, &img_h, &channels, 4);
    if (!img || img_w <= 0 || img_h <= 0) {
        if (img) stbi_image_free(img);
        kfree(buf);
        thumb_cache_mark_failed(path);
        return NULL;
    }
    
    // Store in cache — downscale to 48x48
    int slot = thumb_cache_next;
    thumb_cache_next = (thumb_cache_next + 1) % THUMB_CACHE_SIZE;
    
    // Copy path
    int p = 0;
    while (path[p] && p < 255) { thumb_cache[slot].path[p] = path[p]; p++; }
    thumb_cache[slot].path[p] = 0;
    
    // Downscale image to 48x48 with aspect-fill
    for (int ty = 0; ty < 48; ty++) {
        for (int tx = 0; tx < 48; tx++) {
            int sx = tx * img_w / 48;
            int sy = ty * img_h / 48;
            if (sx >= img_w) sx = img_w - 1;
            if (sy >= img_h) sy = img_h - 1;
            int idx = (sy * img_w + sx) * 4;
            uint32_t r = img[idx], g = img[idx+1], b = img[idx+2], a = img[idx+3];
            thumb_cache[slot].pixels[ty * 48 + tx] = (a << 24) | (r << 16) | (g << 8) | b;
        }
    }
    
    thumb_cache[slot].valid = true;
    thumb_cache[slot].failed = false;
    
    stbi_image_free(img);
    kfree(buf);
    
    return thumb_cache[slot].pixels;
}

void draw_image_icon(int x, int y, const char *label) {
    uint32_t icon_buf[48 * 48];
    for (int i = 0; i < 48 * 48; i++) icon_buf[i] = 0xFFFF00FF;
    graphics_set_render_target(icon_buf, 48, 48);
    
    uint32_t *thumb = NULL;
    
    // Dynamic path: try thumbnail cache for any JPG file path
    if (!thumb_cache_is_failed(label)) {
        thumb = thumb_cache_lookup(label);
        if (!thumb) {
            // Queue for background decoding
            thumb_request_push(label);
        }
    }
    
    if (thumb) {
        // White border
        draw_rounded_rect_filled(0, 0, 48, 48, 4, 0xFFFFFFFF);
        // Draw thumbnail into icon - handle 48x48 dynamic thumbs
        int dst_w = 44, dst_h = 44;
        for (int ty = 0; ty < dst_h; ty++) {
            for (int tx = 0; tx < dst_w; tx++) {
                int sx = tx * 48 / dst_w;
                int sy = ty * 48 / dst_h;
                uint32_t pixel = thumb[sy * 48 + sx];
                put_pixel(2 + tx, 2 + ty, pixel);
            }
        }
    } else {
        // Fallback photo
        draw_rounded_rect_filled(0, 0, 48, 48, 10, 0xFFE0E0E0);
        draw_rounded_rect_filled(5, 5, 38, 38, 4, 0xFF87CEEB); // Sky
        draw_rect(5, 25, 38, 18, 0xFF90EE90); // Grass
        draw_filled_circle(15, 15, 6, 0xFFFFFF00); // Sun
    }
    
    graphics_set_render_target(NULL, 0, 0);
    int dx = x + 24, dy = y + 12;
    for (int ty = 0; ty < 32; ty++) {
        for (int tx = 0; tx < 32; tx++) {
            int src_x = tx * 48 / 32;
            int src_y = ty * 48 / 32;
            uint32_t c1 = icon_buf[src_y * 48 + src_x];
            if (c1 != 0xFFFF00FF) put_pixel(dx + tx, dy + ty, c1);
        }
    }
    
    // Removing the explicit `draw_icon_label` call here to prevent double-text since `wm.c` or Explorer manually draws it as well inside their draw block
}

bool draw_icon_path(int x, int y, const char *path) {
    uint32_t *icon = NULL;

    if (!path || !path[0]) return false;

    icon = thumb_cache_lookup(path);
    if (!icon && !thumb_cache_is_failed(path)) {
        icon = thumb_cache_decode(path);
    }
    if (!icon) return false;

    int dx = x + 24;
    int dy = y + 12;
    for (int ty = 0; ty < 32; ty++) {
        for (int tx = 0; tx < 32; tx++) {
            int src_x = tx * 48 / 32;
            int src_y = ty * 48 / 32;
            uint32_t src = icon[src_y * 48 + src_x];
            uint32_t a = (src >> 24) & 0xFF;
            if (a == 0) continue;

            if (a == 255) {
                put_pixel(dx + tx, dy + ty, 0xFF000000 | (src & 0x00FFFFFF));
            } else {
                uint32_t dst = graphics_get_pixel(dx + tx, dy + ty);
                put_pixel(dx + tx, dy + ty, blend_src_over_dst(dst, src));
            }
        }
    }

    return true;
}

void draw_notepad_icon(int x, int y, const char *label) {
    draw_scaled_icon(x, y, draw_dock_notepad);
    draw_icon_label(x, y, label);
}

void draw_calculator_icon(int x, int y, const char *label) {
    draw_scaled_icon(x, y, draw_dock_calculator);
    draw_icon_label(x, y, label);
}

void draw_grapher_icon(int x, int y, const char *label) {
    draw_scaled_icon(x, y, draw_dock_grapher);
    draw_icon_label(x, y, label);
}

void draw_terminal_icon(int x, int y, const char *label) {
    draw_scaled_icon(x, y, draw_dock_terminal);
    draw_icon_label(x, y, label);
}

void draw_minesweeper_icon(int x, int y, const char *label) {
    draw_scaled_icon(x, y, draw_dock_minesweeper);
    draw_icon_label(x, y, label);
}

void draw_control_panel_icon(int x, int y, const char *label) {
    draw_scaled_icon(x, y, draw_dock_settings);
    draw_icon_label(x, y, label);
}

void draw_clock_icon(int x, int y, const char *label) {
    draw_scaled_icon(x, y, draw_dock_clock);
    draw_icon_label(x, y, label);
}

void draw_about_icon(int x, int y, const char *label) {
    uint32_t icon_buf[48 * 48];
    for (int i = 0; i < 48 * 48; i++) icon_buf[i] = 0xFFFF00FF;
    graphics_set_render_target(icon_buf, 48, 48);
    
    // About dock style
    draw_rounded_rect_filled(0, 0, 48, 48, 10, 0xFF4285F4);
    draw_rounded_rect_filled(5, 5, 38, 38, 4, 0xFFE8F0FE);
    draw_rect(22, 15, 4, 4, 0xFF4285F4); // Dot
    draw_rect(22, 23, 4, 16, 0xFF4285F4); // Body
    
    graphics_set_render_target(NULL, 0, 0);
    int dx = x + 24, dy = y + 12;
    for (int ty = 0; ty < 32; ty++) {
        for (int tx = 0; tx < 32; tx++) {
            int src_x = tx * 48 / 32;
            int src_y = ty * 48 / 32;
            uint32_t c1 = icon_buf[src_y * 48 + src_x];
            if (c1 != 0xFFFF00FF) put_pixel(dx + tx, dy + ty, c1);
        }
    }
    
    draw_icon_label(x, y, label);
}

void draw_recycle_bin_icon(int x, int y, const char *label) {
    uint32_t icon_buf[48 * 48];
    for (int i = 0; i < 48 * 48; i++) icon_buf[i] = 0xFFFF00FF;
    graphics_set_render_target(icon_buf, 48, 48);
    
    // Recycle bin dock style
    draw_rounded_rect_filled(0, 0, 48, 48, 10, 0xFFECEFF1);
    draw_rounded_rect_filled(5, 5, 38, 38, 4, 0xFFCFD8DC);
    draw_rect(16, 18, 16, 20, 0xFF90A4AE); // Bin body
    draw_rect(12, 15, 24, 3, 0xFF78909C); // Bin lid
    draw_rect(20, 13, 8, 2, 0xFF78909C); // Handle
    
    graphics_set_render_target(NULL, 0, 0);
    int dx = x + 24, dy = y + 12;
    for (int ty = 0; ty < 32; ty++) {
        for (int tx = 0; tx < 32; tx++) {
            int src_x = tx * 48 / 32;
            int src_y = ty * 48 / 32;
            uint32_t c1 = icon_buf[src_y * 48 + src_x];
            if (c1 != 0xFFFF00FF) put_pixel(dx + tx, dy + ty, c1);
        }
    }
    
    draw_icon_label(x, y, label);
}

void draw_paint_icon(int x, int y, const char *label) {
    draw_scaled_icon(x, y, draw_dock_paint);
    draw_icon_label(x, y, label);
}

static void draw_dock_taskman(int x, int y) {
    draw_rounded_rect_filled(x, y, 48, 48, 12, 0xFF37474F); // Dark blue-grey
    draw_rounded_rect_filled(x+4, y+4, 40, 40, 8, 0xFF455A64);
    
    // Draw "Activity" lines
    draw_rect(x+8, y+24, 6, 12, 0xFF4FC3F7); // Light blue bar
    draw_rect(x+16, y+16, 6, 20, 0xFF81C784); // Green bar
    draw_rect(x+24, y+20, 6, 16, 0xFFFFB74D); // Orange bar
    draw_rect(x+32, y+10, 6, 26, 0xFFE57373); // Red bar
}

void draw_taskman_icon(int x, int y, const char *label) {
    draw_scaled_icon(x, y, draw_dock_taskman);
    draw_icon_label(x, y, label);
}

static void draw_filled_circle(int cx, int cy, int r, uint32_t color);

// Draw traffic light (close button - red)
void draw_traffic_light(int x, int y) {
    draw_filled_circle(x + 6, y + 6, 6, COLOR_TRAFFIC_RED);
    draw_filled_circle(x + 6, y + 6, 2, COLOR_WHITE);
}

// Draw a squircle-style app icon
void draw_squircle_icon(int x, int y, const char *label, uint32_t bg_color) {
    // Simplified squircle using rounded rectangle
    draw_rounded_rect_filled(x + 12, y, 56, 56, 12, bg_color);
    draw_icon_label(x, y + 60, label);
}

//  Files icon 
void draw_files_icon(int x, int y, const char *label) {
    draw_rounded_rect_filled(x + 27, y + 6, 25, 15, 3, 0xFF4A90E2);  // Blue color
    draw_squircle_icon(x, y, label, 0xFF4A90E2);
}

//  Settings/Gear icon
void draw_settings_icon(int x, int y, const char *label) {
    // Gear icon with dark background
    draw_squircle_icon(x, y, label, 0xFF666666);
    // Simple gear shape in the middle of squircle
    int cx = x + 12 + 28;
    int cy = y + 28;
    draw_rect(cx - 2, cy - 2, 4, 4, COLOR_WHITE);  // Center
}

static int isqrt_local(int n) {
    if (n <= 0) return 0;
    int x = n;
    int y = (x + 1) / 2;
    while (y < x) {
        x = y;
        y = (x + n / x) / 2;
    }
    return x;
}

static void draw_filled_circle(int cx, int cy, int r, uint32_t color) {
    for (int dy = -r; dy <= r; dy++) {
        int dx = isqrt_local(r * r - dy * dy);
        draw_rect(cx - dx, cy + dy, dx * 2 + 1, 1, color);
    }
}

static void draw_dock_files(int x, int y) {
    draw_rounded_rect_filled(x, y, 48, 48, 10, 0xFF1D5FAA);
    draw_rounded_rect_filled(x + 1, y + 1, 46, 28, 9, 0xFF4A90E2);
    draw_rounded_rect_filled(x + 1, y + 24, 46, 23, 9, 0xFF2E72C9);
    draw_rounded_rect_filled(x + 7, y + 13, 15, 7, 3, 0xFF62A8F5);
    draw_rounded_rect_filled(x + 8, y + 12, 13, 5, 3, 0xFF7DB8F8);
    draw_rounded_rect_filled(x + 5, y + 17, 38, 23, 5, 0xFFCDE4FA);
    draw_rounded_rect_filled(x + 7, y + 19, 34, 19, 4, 0xFFEAF5FF);
    draw_rect(x + 12, y + 23, 24, 2, 0xFF88B8D8);
    draw_rect(x + 12, y + 27, 17, 2, 0xFF88B8D8);
    draw_rect(x + 12, y + 31, 21, 2, 0xFF88B8D8);
}

static void draw_dock_settings(int x, int y) {
    draw_rounded_rect_filled(x, y, 48, 48, 10, 0xFF3A3A3A);
    draw_rounded_rect_filled(x + 1, y + 1, 46, 28, 9, 0xFF6E6E6E);
    draw_rounded_rect_filled(x + 1, y + 24, 46, 23, 9, 0xFF4A4A4A);
    int cx = x + 24, cy = y + 25;
    draw_rounded_rect_filled(cx - 4, cy - 18, 8, 7, 2, 0xFFCCCCCC);
    draw_rounded_rect_filled(cx - 4, cy + 11, 8, 7, 2, 0xFFCCCCCC);
    draw_rounded_rect_filled(cx - 18, cy - 4, 7, 8, 2, 0xFFCCCCCC);
    draw_rounded_rect_filled(cx + 11, cy - 4, 7, 8, 2, 0xFFCCCCCC);
    draw_rounded_rect_filled(cx - 14, cy - 14, 6, 6, 2, 0xFFCCCCCC);
    draw_rounded_rect_filled(cx + 8, cy - 14, 6, 6, 2, 0xFFCCCCCC);
    draw_rounded_rect_filled(cx - 14, cy + 8, 6, 6, 2, 0xFFCCCCCC);
    draw_rounded_rect_filled(cx + 8, cy + 8, 6, 6, 2, 0xFFCCCCCC);
    draw_filled_circle(cx, cy, 13, 0xFFCCCCCC);
    draw_filled_circle(cx, cy, 6, 0xFF4A4A4A);
    draw_filled_circle(cx, cy, 4, 0xFF3A3A3A);
}

static long long isqrt(long long n) {
    if (n < 0) return -1;
    if (n == 0) return 0;
    long long x = n;
    long long y = 1;
    while (x > y) {
        x = (x + y) / 2;
        y = n / x;
    }
    return x;
}

static void __attribute__((unused)) draw_dock_word(int x, int y) {
    // Rich blue document style
    draw_rounded_rect_filled(x, y, 48, 48, 10, 0xFF4A90E2);
    draw_rounded_rect_filled(x + 1, y + 1, 46, 28, 9, 0xFF5D9CE6);
    draw_rounded_rect_filled(x + 1, y + 24, 46, 23, 9, 0xFF3A80D2);
    
    // White document page in center
    draw_rounded_rect_filled(x + 8, y + 8, 32, 32, 4, 0xFFFFFFFF);
    // Blue header edge
    draw_rounded_rect_filled(x + 8, y + 8, 32, 8, 4, 0xFF2868B8);
    
    // Text lines using dark grey
    draw_rect(x + 14, y + 22, 20, 2, 0xFF666666);
    draw_rect(x + 14, y + 27, 20, 2, 0xFF666666);
    draw_rect(x + 14, y + 32, 14, 2, 0xFF666666);
}

static void draw_dock_notepad(int x, int y) {
    draw_rounded_rect_filled(x, y, 48, 48, 10, 0xFFCC9A00);
    draw_rounded_rect_filled(x + 1, y + 1, 46, 28, 9, 0xFFFFD700);
    draw_rounded_rect_filled(x + 1, y + 24, 46, 23, 9, 0xFFE8BC00);
    draw_rounded_rect_filled(x + 7, y + 9, 34, 32, 3, 0xFFFFFDE7);
    draw_rounded_rect_filled(x + 7, y + 9, 34, 8, 3, 0xFFFFCA28);
    draw_rect(x + 7, y + 13, 34, 4, 0xFFFFCA28);
    draw_rect(x + 11, y + 21, 18, 2, 0xFFBBAA70);
    draw_rect(x + 11, y + 25, 26, 1, 0xFFCCBB88);
    draw_rect(x + 11, y + 28, 22, 1, 0xFFCCBB88);
    draw_rect(x + 11, y + 31, 24, 1, 0xFFCCBB88);
    draw_rect(x + 11, y + 34, 18, 1, 0xFFCCBB88);
    draw_rect(x + 32, y + 11, 3, 13, 0xFFF5DEB3);
    draw_rect(x + 32, y + 9, 3, 4, 0xFFFF9800);
    draw_rect(x + 33, y + 24, 1, 2, 0xFF555555);
}

static void draw_dock_grapher(int x, int y) {
    // Dark background with a panel look
    draw_rounded_rect_filled(x, y, 48, 48, 10, 0xFF121212);
    draw_rounded_rect_filled(x + 1, y + 1, 46, 28, 9, 0xFF1E1E1E);
    draw_rounded_rect_filled(x + 1, y + 24, 46, 23, 9, 0xFF161616);
    
    // Subtle grid (matches Grapher's theme)
    uint32_t grid_color = 0xFF2A2A2A;
    for (int i = 8; i < 40; i += 8) {
        draw_rect(x + i, y + 6, 1, 36, grid_color);
        draw_rect(x + 6, y + i + 6, 36, 1, grid_color);
    }
    
    // Axis line
    draw_rect(x + 24, y + 10, 1, 28, 0xFF444444);
    draw_rect(x + 10, y + 24, 28, 1, 0xFF444444);
    
    // Vibrant Sine Wave (Neon Cyan)
    uint32_t curve_color = 0xFF00E5FF;
    int curve_y[] = {24, 23, 21, 19, 17, 16, 15, 15, 16, 17, 19, 21, 23, 24, 26, 28, 30, 32, 33, 33, 32, 30, 28, 26, 24, 23, 21, 19, 17, 16, 15, 15, 16, 17, 19, 21};
    for (int i = 0; i < 35; i++) {
        int x1 = x + 6 + i;
        int y1 = y + curve_y[i];
        int y2 = y + curve_y[i+1];
        
        // Anti-aliased look with multi-point vertical connector
        if (y1 < y2) for (int j = y1; j <= y2; j++) put_pixel(x1, j, curve_color);
        else for (int j = y2; j <= y1; j++) put_pixel(x1, j, curve_color);
    }
    
    // Add white indicator "nodes" at the peaks
    draw_filled_circle(x + 6 + 7, y + 15, 2, 0xFFFFFFFF);
    draw_filled_circle(x + 6 + 18, y + 33, 2, 0xFFFFFFFF);
    draw_filled_circle(x + 6 + 30, y + 15, 2, 0xFFFFFFFF);
}

static void draw_dock_calculator(int x, int y) {
    draw_rounded_rect_filled(x, y, 48, 48, 10, 0xFF111111);
    draw_rounded_rect_filled(x + 1, y + 1, 46, 28, 9, 0xFF222222);
    draw_rounded_rect_filled(x + 1, y + 24, 46, 23, 9, 0xFF151515);
    draw_rounded_rect_filled(x + 6, y + 6, 36, 11, 3, 0xFF1A3A2A);
    draw_rect(x + 25, y + 9, 14, 5, 0xFF33FF88);
    // 3x3 button grid
    uint32_t btn_clr[3][3] = {
        {0xFF555555, 0xFF555555, 0xFF555555},
        {0xFF444444, 0xFF444444, 0xFF444444},
        {0xFF444444, 0xFF444444, 0xFFFF9500},
    };
    for (int row = 0; row < 3; row++) {
        for (int col = 0; col < 3; col++) {
            draw_rounded_rect_filled(x + 10 + col * 9, y + 22 + row * 6, 7, 5, 2, btn_clr[row][col]);
        }
    }
}

static void draw_dock_terminal(int x, int y) {
    draw_rounded_rect_filled(x, y, 48, 48, 10, 0xFF0A0A0A);
    draw_rounded_rect_filled(x + 1, y + 1, 46, 28, 9, 0xFF161616);
    draw_rounded_rect_filled(x + 1, y + 24, 46, 23, 9, 0xFF0D0D0D);
    int px = x + 12;
    int py = y + 24;
    for (int i = 0; i < 6; i++) {
        draw_rect(px + i, py - 4 + i, 2, 1, 0xFF33DD33);
    }
    for (int i = 0; i < 6; i++) {
        draw_rect(px + i, py + 4 - i, 2, 1, 0xFF33DD33);
    }
    draw_rect(px + 10, py + 7, 8, 1, 0xFF33DD33);
}

static void draw_dock_minesweeper(int x, int y) {
    draw_rounded_rect_filled(x, y, 48, 48, 10, 0xFF1B5E20);
    draw_rounded_rect_filled(x + 1, y + 1, 46, 28, 9, 0xFF4CAF50);
    draw_rounded_rect_filled(x + 1, y + 24, 46, 23, 9, 0xFF388E3C);
    for (int row = 0; row < 5; row++) {
        for (int col = 0; col < 5; col++) {
            int bx = x + 6 + col * 8, by = y + 7 + row * 8;
            if ((row == 2 && col == 2)) {
                draw_rounded_rect_filled(bx, by, 6, 6, 1, 0xFFC8E6C9);
            } else if ((row + col) % 2 == 0) {
                draw_rounded_rect_filled(bx, by, 6, 6, 1, 0xFF81C784);
                draw_rect(bx, by, 6, 1, 0xFFA5D6A7);
                draw_rect(bx, by, 1, 6, 0xFFA5D6A7);
                draw_rect(bx + 5, by + 1, 1, 5, 0xFF2E7D32);
                draw_rect(bx + 1, by + 5, 5, 1, 0xFF2E7D32);
            } else {
                draw_rounded_rect_filled(bx, by, 6, 6, 1, 0xFF66BB6A);
                draw_rect(bx, by, 6, 1, 0xFF81C784);
                draw_rect(bx, by, 1, 6, 0xFF81C784);
            }
        }
    }
    int mx = x + 6 + 2 * 8 + 3, my = y + 7 + 2 * 8 + 3;
    draw_filled_circle(mx, my, 4, 0xFF111111);
    draw_rect(mx - 5, my - 1, 11, 2, 0xFF111111);
    draw_rect(mx - 1, my - 5, 2, 11, 0xFF111111);
    draw_rect(mx - 3, my - 3, 2, 2, 0xFF111111);
    draw_rect(mx + 1, my - 3, 2, 2, 0xFF111111);
    draw_rect(mx - 3, my + 1, 2, 2, 0xFF111111);
    draw_rect(mx + 1, my + 1, 2, 2, 0xFF111111);
    draw_rect(mx - 1, my - 2, 2, 2, 0xFFFFFFFF);
    draw_rect(x + 7, y + 40, 1, 6, 0xFF333333);
    draw_rect(x + 8, y + 40, 4, 3, 0xFFFF3333);
}

static void draw_dock_paint(int x, int y) {
    draw_rounded_rect_filled(x, y, 48, 48, 10, 0xFFBBBBBB);
    draw_rounded_rect_filled(x + 1, y + 1, 46, 28, 9, 0xFFFFFFFF);
    draw_rounded_rect_filled(x + 1, y + 24, 46, 23, 9, 0xFFEEEEEE);
    draw_rounded_rect_filled(x + 6, y + 9, 36, 30, 10, 0xFFEEDDB0);
    draw_rounded_rect_filled(x + 8, y + 11, 32, 26, 8, 0xFFF5E8C0);
    draw_filled_circle(x + 35, y + 32, 5, 0xFFEEDDB0);
    draw_filled_circle(x + 35, y + 32, 3, 0xFFFFFFFF);
    draw_filled_circle(x + 15, y + 18, 5, 0xFFFF3333);
    draw_filled_circle(x + 23, y + 14, 5, 0xFF3399FF);
    draw_filled_circle(x + 31, y + 18, 5, 0xFFFFCC00);
    draw_filled_circle(x + 28, y + 27, 5, 0xFF33CC33);
    draw_filled_circle(x + 16, y + 27, 5, 0xFFFF6600);
    draw_rect(x + 30, y + 30, 3, 14, 0xFF8B6914);
    draw_rounded_rect_filled(x + 29, y + 27, 5, 5, 2, 0xFFBBBBBB);
    draw_rounded_rect_filled(x + 30, y + 22, 3, 7, 1, 0xFF1A1A1A);
}

static void __attribute__((unused)) draw_dock_browser(int x, int y) {
    draw_rounded_rect_filled(x, y, 48, 48, 10, 0xFF0D47A1);
    draw_rounded_rect_filled(x + 1, y + 1, 46, 28, 9, 0xFF1976D2);
    draw_rounded_rect_filled(x + 1, y + 24, 46, 23, 9, 0xFF1565C0);
    
    int cx = x + 24, cy = y + 24;
    draw_filled_circle(cx, cy, 18, 0xFF64B5F6);
    draw_filled_circle(cx, cy, 16, 0xFF2196F3);
    
    // Simple globe lines
    draw_rect(cx - 16, cy, 32, 1, 0xFFBBDEFB);
    draw_rect(cx, cy - 16, 1, 32, 0xFFBBDEFB);
    
    for(int i=0; i<32; i++) {
        int r = (i-16);
        if (r*r > 16*16) continue;
        int w = isqrt(16*16 - r*r);
        put_pixel(cx - w, cy + r, 0xFFBBDEFB);
        put_pixel(cx + w, cy + r, 0xFFBBDEFB);
    }
}

static void draw_dock_clock(int x, int y) {
    draw_rounded_rect_filled(x, y, 48, 48, 10, 0xFF4A4A4A);
    draw_rounded_rect_filled(x + 1, y + 1, 46, 28, 9, 0xFF6E6E6E);
    draw_rounded_rect_filled(x + 1, y + 24, 46, 23, 9, 0xFF5A5A5A);
    
    int cx = x + 24, cy = y + 24;
    draw_filled_circle(cx, cy, 18, 0xFFF0F0F0);
    draw_filled_circle(cx, cy, 1, 0xFF333333);
    
    // Hour hand
    draw_rect(cx - 1, cy - 8, 2, 8, 0xFF333333);
    // Minute hand
    draw_rect(cx, cy - 1, 10, 2, 0xFF333333);
}


void draw_window(Window *win) {
    if (!win->visible) return;
    
    // Dark mode window with rounded corners
    // Border/Shadow effect
    draw_rounded_rect_filled(win->x - 1, win->y - 1, win->w + 2, win->h + 2, 8, 0xFF000000);
    
    // Main window body (fully rounded)
    draw_rounded_rect_filled(win->x, win->y, win->w, win->h, 8, COLOR_DARK_PANEL);
    
    // Title Bar (rounded at top only - overdraw bottom to hide rounding)
    draw_rounded_rect_filled(win->x, win->y, win->w, 20, 8, COLOR_DARK_TITLEBAR);
    draw_rect(win->x, win->y + 12, win->w, 8, COLOR_DARK_TITLEBAR);  // Cover bottom rounded corners
    draw_string(win->x + 28, win->y + 4, win->title, COLOR_DARK_TEXT);
    
    // Traffic Light (close button - red)
    draw_traffic_light(win->x + 8, win->y + 2);
    
    // Client Area with dark background, rounded only at bottom
    draw_rounded_rect_filled(win->x, win->y + 20, win->w, win->h - 20, 8, COLOR_DARK_BG);
    draw_rect(win->x, win->y + 20, win->w, 8, COLOR_DARK_BG);
    
    if (win->comp_pixels) {
        graphics_blit_buffer(win->comp_pixels, win->x, win->y + 20, win->w, win->h - 20);
    } else if (win->pixels) {
        graphics_blit_buffer(win->pixels, win->x, win->y + 20, win->w, win->h - 20);
    }
    
    // Mask bottom corners: clear pixels outside the rounded boundary
    {
        int radius = 8;
        int bx = win->x;
        int by = win->y + win->h - radius;
        for (int dy = 0; dy < radius; dy++) {
            int dx = isqrt(radius*radius - dy*dy);
            int fill_w = radius - dx;
            if (fill_w > 0) {
                // Bottom-left corner
                draw_rect(bx, by + dy, fill_w, 1, 0xFF000000);
                // Bottom-right corner
                draw_rect(bx + win->w - fill_w, by + dy, fill_w, 1, 0xFF000000);
            }
        }
    }
    
    if (win->paint) {
        win->paint(win);
    }
    
    // Draw Resize Handle for resizable windows (MacOS 9 style)
    if (win->resizable) {
        int hx = win->x + win->w - 16;
        int hy = win->y + win->h - 16;
        for (int i = 0; i < 3; i++) {
            for (int j = 0; j <= i; j++) {
                // Draw a small 2x2 "dot" for the knurling
                draw_rect(hx + 12 - i*4 + j*4, hy + 12 - j*4, 2, 2, 0xFF888888);
            }
        }
    }
}

void draw_cursor(int x, int y) {
    // '.' transparent, 'w' white outline, 'b' black fill
    static const char cursor_bitmap[CURSOR_H][CURSOR_W + 1] = {
        "w.................",
        "ww................",
        "wbw...............",
        "wbbw..............",
        "wbbbw.............",
        "wbbbbw............",
        "wbbbbbw...........",
        "wbbbbbbw..........",
        "wbbbbbbbw.........",
        "wbbbbbbbbw........",
        "wbbbbbbbbw........",
        "wbbbbbbbw.........",
        "wbbbbbbbw.........",
        "wbbbbbbw..........",
        "wwwwbbbw..........",
        "....wbbw..........",
        ".....wbw..........",
        "......ww..........."
    };

    for (int r = 0; r < CURSOR_H; r++) {
        for (int c = 0; c < CURSOR_W; c++) {
            char p = cursor_bitmap[r][c];
            if (p == 'w') {
                put_pixel(x + c, y + r, COLOR_WHITE);
            } else if (p == 'b') {
                put_pixel(x + c, y + r, COLOR_BLACK);
            }
        }
    }
}

// Erase cursor by redrawing the background in that area
static void erase_cursor(int x, int y) {
    int sw = get_screen_width();
    int sh = get_screen_height();
    
    // Clamp to screen
    int x1 = x < 0 ? 0 : x;
    int y1 = y < 0 ? 0 : y;
    int x2 = x + CURSOR_W > sw ? sw : x + CURSOR_W;
    int y2 = y + CURSOR_H > sh ? sh : y + CURSOR_H;
    int w = x2 - x1;
    int h = y2 - y1;
    
    if (y1 < sh - 28) {
        draw_rect(x1, y1, w, h, COLOR_TEAL);
    } else {
        draw_rect(x1, y1, w, h, COLOR_GRAY);
    }
}

// --- Clock ---

static uint8_t rtc_read(uint8_t reg) {
    outb(0x70, reg);
    return inb(0x71);
}

static void draw_clock(int x, int y) {
    while (rtc_read(0x0A) & 0x80);

    uint8_t s = rtc_read(0x00);
    uint8_t m = rtc_read(0x02);
    uint8_t h = rtc_read(0x04);
    uint8_t b = rtc_read(0x0B);

    if (!(b & 0x04)) {
        s = (s & 0x0F) + ((s >> 4) * 10);
        m = (m & 0x0F) + ((m >> 4) * 10);
        h = (h & 0x0F) + ((h >> 4) * 10);
    }

    char buf[9];
    buf[0] = '0' + (h / 10);
    buf[1] = '0' + (h % 10);
    buf[2] = ':';
    buf[3] = '0' + (m / 10);
    buf[4] = '0' + (m % 10);
    buf[5] = ':';
    buf[6] = '0' + (s / 10);
    buf[7] = '0' + (s % 10);
    buf[8] = 0;

    draw_string(x, y, buf, COLOR_WHITE);
}

// --- Main Paint Function ---
bool rect_contains(int x, int y, int w, int h, int px, int py) {
    return px >= x && px < x + w && py >= y && py < y + h;
}

static void wm_render_lumos(int y_start, int y_end, DirtyRect dirty) {
    if (!lumos_state.visible) {
        return;
    }
    
    int sw = get_screen_width();
    int sh = get_screen_height();
    
    int modal_width = LUMOS_MODAL_WIDTH;
    int modal_height = LUMOS_SEARCH_HEIGHT + (lumos_state.result_count * LUMOS_RESULT_HEIGHT) + 10;
    
    if (lumos_state.result_count == 0 && lumos_state.search_len > 0) {
        modal_height = LUMOS_SEARCH_HEIGHT + LUMOS_RESULT_HEIGHT + 20; 
    }
    
    int modal_x = (sw - modal_width) / 2;
    int modal_y = (sh * 2 / 5) - (modal_height / 2);
    
    if (modal_y + modal_height <= y_start || modal_y >= y_end) {
        return;
    }
    
    // Draw modal background - with subtle blur effect
    draw_rounded_rect_blurred(modal_x, modal_y, modal_width, modal_height, 12, COLOR_DARK_PANEL, 1, 220);
    
    int search_x = modal_x + 8;
    int search_y = modal_y + 8;
    int search_width = modal_width - 16;
    int search_height = 32;
    
    draw_rounded_rect_filled(search_x, search_y, search_width, search_height, 6, COLOR_DARK_BG);
    
    if (lumos_state.search_len > 0) {
        draw_string(search_x + 8, search_y + 8, lumos_state.search_query, COLOR_DARK_TEXT);
    } else {
        draw_string(search_x + 8, search_y + 8, "Search files...", COLOR_DKGRAY);
    }
    
    if (lumos_state.cursor_pos <= lumos_state.search_len && lumos_state.search_len > 0) {
        ttf_font_t *ttf = graphics_get_current_ttf();
        char temp_query[256];
        for (int i = 0; i < lumos_state.cursor_pos && i < 255; i++) {
            temp_query[i] = lumos_state.search_query[i];
        }
        temp_query[lumos_state.cursor_pos] = 0;
        int cursor_x_offset = (ttf) ? font_manager_get_string_width(ttf, temp_query) : (lumos_state.cursor_pos * 6);
        draw_rect(search_x + 8 + cursor_x_offset, search_y + 8, 1, 16, COLOR_DARK_TEXT);
    }
    
    for (int i = 0; i < lumos_state.result_count && i < LUMOS_MAX_RESULTS; i++) {
        if (i < 0 || i >= LUMOS_MAX_RESULTS) {
            break;
        }
        
        int result_x = modal_x + 4;
        int result_y = modal_y + LUMOS_SEARCH_HEIGHT + 4 + (i * LUMOS_RESULT_HEIGHT);
        int result_width = modal_width - 8;
        
        if (i == lumos_state.selected_index) {
            draw_rounded_rect_filled(result_x, result_y, result_width, LUMOS_RESULT_HEIGHT - 2, 6, COLOR_DARK_BORDER);
        }
        
        const char *full_path = lumos_state.results[i].entry.path;
        if (!full_path || full_path[0] == 0) {
            continue;  
        }
        
        const char *filename = full_path;
        
        for (int j = 0; full_path[j]; j++) {
            if (full_path[j] == '/') {
                filename = &full_path[j + 1];
            }
        }
        
        if (!filename || filename[0] == 0) {
            continue;
        }
        
        draw_string(result_x + 8, result_y + 8, filename, COLOR_DARK_TEXT);
        
        if (!lumos_state.results[i].entry.is_directory) {
            char size_str[32];
            uint32_t size = lumos_state.results[i].entry.size;
            
            if (size < 1024) {
                // Bytes
                size_str[0] = '0' + ((size / 1) % 10);
                size_str[1] = 'B';
                size_str[2] = 0;
            } else if (size < 1024 * 1024) {
                // Kilobytes - properly format for values up to 1023 KB
                int kb = size / 1024;
                if (kb >= 100) {
                    size_str[0] = '0' + (kb / 100);
                    size_str[1] = '0' + ((kb / 10) % 10);
                    size_str[2] = '0' + (kb % 10);
                    size_str[3] = 'K';
                    size_str[4] = 'B';
                    size_str[5] = 0;
                } else {
                    size_str[0] = '0' + (kb / 10);
                    size_str[1] = '0' + (kb % 10);
                    size_str[2] = 'K';
                    size_str[3] = 'B';
                    size_str[4] = 0;
                }
            } else {
                // Megabytes - properly format for any MB value
                int mb = size / (1024 * 1024);
                if (mb >= 100) {
                    size_str[0] = '0' + (mb / 100);
                    size_str[1] = '0' + ((mb / 10) % 10);
                    size_str[2] = '0' + (mb % 10);
                    size_str[3] = 'M';
                    size_str[4] = 'B';
                    size_str[5] = 0;
                } else {
                    size_str[0] = '0' + (mb / 10);
                    size_str[1] = '0' + (mb % 10);
                    size_str[2] = 'M';
                    size_str[3] = 'B';
                    size_str[4] = 0;
                }
            }
            
            int size_x = result_x + result_width - 8 - 32;  // Account for wider size strings
            draw_string(size_x, result_y + 8, size_str, COLOR_DKGRAY);
        }
    }
    
    // Draw "No results" message if needed
    if (lumos_state.search_len > 0 && lumos_state.result_count == 0) {
        int msg_y = modal_y + LUMOS_SEARCH_HEIGHT + 10;
        draw_string(modal_x + 20, msg_y, "No results found", COLOR_DKGRAY);
    }
}

static Window *sorted_windows_cache[32];
static int sorted_window_count_cache = 0;

static void wm_paint_region(int y_start, int y_end, DirtyRect dirty, int pass) {
    int sw = get_screen_width();
    int sh = get_screen_height();
    
    int cx = 0, cy = y_start, cw = sw, ch = y_end - y_start;
    if (dirty.active) {
        if (cx < dirty.x) { cw -= (dirty.x - cx); cx = dirty.x; }
        if (cy < dirty.y) { ch -= (dirty.y - cy); cy = dirty.y; }
        if (cx + cw > dirty.x + dirty.w) cw = dirty.x + dirty.w - cx;
        if (cy + ch > dirty.y + dirty.h) ch = dirty.y + dirty.h - cy;
    }
    
    if (cw <= 0 || ch <= 0) return;

    graphics_set_clipping(cx, cy, cw, ch);

    if (pass == 1) {
        draw_desktop_background();
        
        for (int i = 0; i < desktop_icon_count; i++) {
            DesktopIcon *icon = &desktop_icons[i];
            if (icon->y + 85 <= cy || icon->y >= cy + ch) continue;
            if (dirty.active && (icon->x + 85 <= dirty.x || icon->x >= dirty.x + dirty.w)) continue;

            if (icon->type == 1) draw_folder_icon(icon->x, icon->y, icon->name);
            else if (icon->type == 2) {
                char label[64]; int len = 0;
                while(icon->name[len] && len < 63) { label[len] = icon->name[len]; len++; }
                label[len] = 0;
                if (len > 9 && str_ends_with(label, ".shortcut")) label[len-9] = 0;
                if (str_starts_with(icon->name, "Notepad")) draw_notepad_icon(icon->x, icon->y, label);
                else if (str_starts_with(icon->name, "Calculator")) draw_calculator_icon(icon->x, icon->y, label);
                else if (str_starts_with(icon->name, "Terminal")) draw_terminal_icon(icon->x, icon->y, label);
                else if (str_starts_with(icon->name, "Minesweeper")) draw_minesweeper_icon(icon->x, icon->y, label);
                else if (str_starts_with(icon->name, "Settings")) draw_control_panel_icon(icon->x, icon->y, label);
                else if (str_starts_with(icon->name, "Clock")) draw_clock_icon(icon->x, icon->y, label);
                else if (str_starts_with(icon->name, "About")) draw_about_icon(icon->x, icon->y, label);
                else if (str_starts_with(icon->name, "Recycle Bin")) draw_recycle_bin_icon(icon->x, icon->y, label);
                else if (str_starts_with(icon->name, "Files")) draw_folder_icon(icon->x, icon->y, label);
                else if (str_starts_with(icon->name, "Paint")) draw_paint_icon(icon->x, icon->y, label);
                else if (str_starts_with(icon->name, "Grapher")) draw_grapher_icon(icon->x, icon->y, label);
                else draw_icon(icon->x, icon->y, label);
            } else {
                if (str_ends_with(icon->name, ".elf")) {
                    char full_path[128] = "/root/Desktop/";
                    char icon_path[BOREDOS_APP_METADATA_MAX_IMAGE_PATH];
                    bool drew_icon = false;
                    int p = 14;
                    int n = 0;

                    while (icon->name[n] && p < 127) full_path[p++] = icon->name[n++];
                    full_path[p] = 0;

                    if (app_metadata_get_primary_image(full_path, icon_path, sizeof(icon_path))) {
                        drew_icon = draw_icon_path(icon->x, icon->y, icon_path);
                    }
                    if (!drew_icon) {
                        drew_icon = draw_icon_path(icon->x, icon->y, "/Library/images/icons/colloid/xterm.png");
                    }

                    if (drew_icon) {
                        draw_icon_label(icon->x, icon->y, icon->name);
                    } else {
                        draw_elf_icon(icon->x, icon->y, icon->name);
                    }
                }
                else if (str_ends_with(icon->name, ".pnt")) draw_paint_icon(icon->x, icon->y, icon->name);
                else if (is_image_file(icon->name)) {
                    char full_path[128] = "/root/Desktop/"; int p=14; int n=0; while(icon->name[n] && p < 127) full_path[p++] = icon->name[n++]; full_path[p]=0;
                    draw_image_icon(icon->x, icon->y, full_path);
                    draw_icon_label(icon->x, icon->y, icon->name);
                }
                else if (str_ends_with(icon->name, ".pdf")) draw_pdf_icon(icon->x, icon->y, icon->name);
                else draw_document_icon(icon->x, icon->y, icon->name);
            }
        }
        
        for (int i = 0; i < sorted_window_count_cache; i++) {
            Window *win = sorted_windows_cache[i];
            if (!win || !win->visible) continue;
            if (win->y + win->h <= cy || win->y >= cy + ch) continue;
            if (dirty.active && !win->focused && (win->x + win->w <= dirty.x || win->x >= dirty.x + dirty.w)) continue;
            draw_window(win);
        }
    } else if (pass == 2) {
        if (0 < cy + ch && 30 > cy) {
            draw_rect(0, 0, sw, 30, COLOR_MENUBAR_BG);
            draw_boredos_logo(8, 8, 1);
            draw_clock(sw - 80, 12);
        }
        
        if (start_menu_open && 40 < cy + ch && 125 > cy) {
            draw_rounded_rect_filled(8, 40, 160, 85, 8, COLOR_DARK_PANEL);
            draw_string(20, 48, "About BoredOS", COLOR_DARK_TEXT);
            draw_string(20, 68, "Settings", COLOR_DARK_TEXT);
            draw_string(20, 88, "Shutdown", COLOR_DARK_TEXT);
            draw_string(20, 108, "Restart", COLOR_DARK_TEXT);
        }
        
        int dock_y, dock_x, dock_bg_x, dock_bg_w;
        dock_get_geometry(sw, sh, &dock_x, &dock_y, &dock_bg_x, &dock_bg_w);
        if (dock_item_count > 0 && dock_y < cy + ch && dock_y + DOCK_HEIGHT > cy) {
            draw_rounded_rect_blurred(dock_bg_x, dock_y, dock_bg_w, DOCK_HEIGHT, 18, COLOR_DOCK_BG, 1, 180);
            int dx = dock_x;
            int dy = dock_y + 6;
            for (int i = 0; i < dock_item_count; i++) {
                if (!dock_draw_metadata_icon(dx, dy, &dock_items[i])) {
                    draw_dock_icon_slot_png(dx, dy, dock_items[i].icon_slot);
                }
                dx += DOCK_ITEM_SIZE + DOCK_ITEM_SPACING;
            }
        }

        if (dock_menu_visible) {
            int d_mw = 140, d_mh = 25;
            if (dock_menu_y < cy + ch && dock_menu_y + d_mh > cy) {
                draw_rounded_rect_filled(dock_menu_x, dock_menu_y, d_mw, d_mh, 8, COLOR_DARK_PANEL);
                draw_string(dock_menu_x + 10, dock_menu_y + 8, "Remove from Dock", COLOR_TRAFFIC_RED);
            }
        }
        
        if (desktop_menu_visible) {
            int d_mw = 140, d_mh = (desktop_menu_target_icon != -1) ? 125 : 75;
            if (desktop_menu_y < cy + ch && desktop_menu_y + d_mh > cy) {
                draw_rounded_rect_filled(desktop_menu_x, desktop_menu_y, d_mw, d_mh, 8, COLOR_DARK_PANEL);
                int item_h = 25;
                if (desktop_menu_target_icon != -1) {
                    bool cp = explorer_clipboard_has_content();
                    draw_string(desktop_menu_x + 10, desktop_menu_y + 5, "Cut", COLOR_WHITE);
                    draw_string(desktop_menu_x + 10, desktop_menu_y + 5 + item_h, "Copy", COLOR_WHITE);
                    draw_string(desktop_menu_x + 10, desktop_menu_y + 5 + item_h * 2, "Paste", cp ? COLOR_WHITE : COLOR_DKGRAY);
                    draw_string(desktop_menu_x + 10, desktop_menu_y + 5 + item_h * 3, "Delete", COLOR_TRAFFIC_RED);
                    draw_string(desktop_menu_x + 10, desktop_menu_y + 5 + item_h * 4, "Rename", COLOR_WHITE);
                } else {
                    bool cp = explorer_clipboard_has_content();
                    draw_string(desktop_menu_x + 10, desktop_menu_y + 5, "New File", COLOR_WHITE);
                    draw_string(desktop_menu_x + 10, desktop_menu_y + 5 + item_h, "New Folder", COLOR_WHITE);
                    draw_string(desktop_menu_x + 10, desktop_menu_y + 5 + item_h * 2, "Paste", cp ? COLOR_WHITE : COLOR_DKGRAY);
                }
            }
        }

        if (desktop_dialog_state != 0) {
            int dlg_w = 300, dlg_h = 110, dg_x = (sw - dlg_w)/2, dg_y = (sh - dlg_h)/2;
            if (dg_y < cy + ch && dg_y + dlg_h > cy) {
                draw_rounded_rect_filled(dg_x, dg_y, dlg_w, dlg_h, 8, COLOR_DARK_PANEL);
                const char *title = (desktop_dialog_state == 1) ? "Create New File" : (desktop_dialog_state == 2 ? "Create New Folder" : "Rename");
                const char *btn = (desktop_dialog_state == 0) ? "Rename" : "Create";
                draw_string(dg_x + 10, dg_y + 10, title, COLOR_WHITE);
                draw_rounded_rect_filled(dg_x + 10, dg_y + 35, 280, 20, 4, COLOR_DARK_BG);
                draw_string(dg_x + 15, dg_y + 40, desktop_dialog_input, COLOR_WHITE);
                char temp_sub[64]; int k; for (k=0; k<desktop_dialog_cursor&&desktop_dialog_input[k]; k++) temp_sub[k]=desktop_dialog_input[k]; temp_sub[k]=0;
                draw_rect(dg_x + 15 + font_manager_get_string_width(graphics_get_current_ttf(), temp_sub), dg_y + 39, 2, 12, COLOR_WHITE);
                draw_rounded_rect_filled(dg_x + 50, dg_y + 65, 80, 25, 4, COLOR_DARK_BORDER); draw_string(dg_x + 70, dg_y + 72, btn, COLOR_WHITE);
                draw_rounded_rect_filled(dg_x + 170, dg_y + 65, 80, 25, 4, COLOR_DARK_BORDER); draw_string(dg_x + 185, dg_y + 72, "Cancel", COLOR_WHITE);
            }
        }

        if (msg_box_visible) {
            ttf_font_t *_ttf = graphics_get_current_ttf();
            int title_w = _ttf ? font_manager_get_string_width(_ttf, msg_box_title) : (int)(k_strlen(msg_box_title) * 8);
            int text_w  = _ttf ? font_manager_get_string_width(_ttf, msg_box_text)  : (int)(k_strlen(msg_box_text)  * 8);
            int content_w = (title_w > text_w) ? title_w : text_w;
            int padding = 30; // horizontal padding on each side
            int mw = content_w + padding * 2;
            if (mw < 160) mw = 160; // minimum width
            int mh = 100;
            int m_x = (sw - mw)/2, m_y = (sh - mh)/2;
            if (m_y < cy + ch && m_y + mh > cy) {
                draw_rounded_rect_filled(m_x, m_y, mw, mh, 8, COLOR_DARK_PANEL);
                draw_string(m_x + 15, m_y + 10, msg_box_title, COLOR_DARK_TEXT);
                draw_string(m_x + 15, m_y + 40, msg_box_text, COLOR_DARK_TEXT);
                draw_rounded_rect_filled(m_x + mw/2 - 30, m_y + 70, 60, 20, 4, COLOR_DARK_BORDER);
                draw_string(m_x + mw/2 - 10, m_y + 75, "OK", COLOR_WHITE);
            }
        }

        if (notif_active) {
            int nx = sw - 400 + notif_x_offset, ny = 40, nw = 380, nh = 50;
            if (ny < cy + ch && ny + nh > cy) {
                draw_rounded_rect_filled(nx, ny, nw, nh, 8, COLOR_DARK_PANEL);
                draw_string(nx + 15, ny + 10, "Screenshot", COLOR_DARK_TEXT);
                draw_string(nx + 15, ny + 30, notif_text, COLOR_DKGRAY);
            }
        }
        
        // Render lumos modal
        wm_render_lumos(cy, cy + ch, dirty);
        
        if (wm_custom_paint_hook) wm_custom_paint_hook();
        
        if (is_dragging_file) {
            if (mx - 20 < cx + cw && mx + 20 > cx && my - 20 < cy + ch && my + 20 > cy) {
                if (drag_icon_type == 1) draw_folder_icon(mx - 20, my - 20, "Moving...");
                else if (drag_icon_type == 2) draw_icon(mx - 20, my - 20, "Moving...");
                else draw_document_icon(mx - 20, my - 20, "Moving...");
            }
        }
    }
}

static void wm_strip_worker_job(void *arg) {
    wm_strip_job_t *job = (wm_strip_job_t *)arg;
    wm_paint_region(job->y_start, job->y_end, job->dirty, job->pass);
    __atomic_sub_fetch(job->completion_counter, 1, __ATOMIC_SEQ_CST);
}

void wm_paint(void) {
    int sw = get_screen_width();
    int sh = get_screen_height();
    uint64_t rflags;
    rflags = wm_lock_acquire();
    wm_mark_dirty(last_cursor_x, last_cursor_y, CURSOR_W, CURSOR_H);
    wm_mark_dirty(mx, my, CURSOR_W, CURSOR_H);

    DirtyRect dirty = graphics_get_dirty_rect();
    if (menubar_dirty_pending) {
        graphics_mark_dirty(0, 0, sw, 30);
        dirty = graphics_get_dirty_rect();
        menubar_dirty_pending = false;
    }
    if (dirty.active && dock_item_count > 0) {
        int d_x, d_y, d_bg_x, d_bg_w;
        dock_get_geometry(sw, sh, &d_x, &d_y, &d_bg_x, &d_bg_w);
        if (!(dirty.x >= d_bg_x + d_bg_w || dirty.x + dirty.w <= d_bg_x ||
              dirty.y >= d_y + DOCK_HEIGHT || dirty.y + dirty.h <= d_y)) {
            graphics_mark_dirty(d_bg_x - 10, d_y - 10, d_bg_w + 20, DOCK_HEIGHT + 20);
            dirty = graphics_get_dirty_rect();
        }
    }

    sorted_window_count_cache = window_count;
    if (sorted_window_count_cache > 32) sorted_window_count_cache = 32;
    for (int i = 0; i < sorted_window_count_cache; i++) sorted_windows_cache[i] = all_windows[i];
    for (int i = 0; i < sorted_window_count_cache - 1; i++) {
        for (int j = 0; j < sorted_window_count_cache - i - 1; j++) {
            if (sorted_windows_cache[j] && sorted_windows_cache[j+1] &&
                sorted_windows_cache[j]->z_index > sorted_windows_cache[j+1]->z_index) {
                Window *tmp = sorted_windows_cache[j];
                sorted_windows_cache[j] = sorted_windows_cache[j+1];
                sorted_windows_cache[j+1] = tmp;
            }
        }
    }
    
    // Memory barrier to ensure APs see the sorted window list correctly
    asm volatile("" ::: "memory");

    uint32_t cpu_count = smp_cpu_count();
    if (cpu_count > 32) cpu_count = 32;
    if (cpu_count < 1) cpu_count = 1;

    volatile int completion_counter = (int)cpu_count;
    wm_strip_job_t jobs[32];
    int rows_per_strip = sh / cpu_count;
    
    // PASS 1: BACKGROUND & WINDOWS
    for (uint32_t i = 0; i < cpu_count; i++) {
        jobs[i].y_start = i * rows_per_strip;
        jobs[i].y_end = (i == cpu_count - 1) ? sh : (i + 1) * rows_per_strip;
        jobs[i].dirty = dirty;
        jobs[i].completion_counter = &completion_counter;
        jobs[i].pass = 1;
        if (i < cpu_count - 1) work_queue_submit(wm_strip_worker_job, &jobs[i]);
    }
    wm_paint_region(jobs[cpu_count-1].y_start, jobs[cpu_count-1].y_end, dirty, 1);
    __atomic_sub_fetch(&completion_counter, 1, __ATOMIC_SEQ_CST);
    while (completion_counter > 0) {
        if (!work_queue_drain_one()) asm volatile("pause");
    }

    // PASS 2: UI OVERLAY (Dock, start menu, menus etc)
    completion_counter = (int)cpu_count;
    for (uint32_t i = 0; i < cpu_count; i++) {
        jobs[i].pass = 2;
        if (i < cpu_count - 1) work_queue_submit(wm_strip_worker_job, &jobs[i]);
    }
    wm_paint_region(jobs[cpu_count-1].y_start, jobs[cpu_count-1].y_end, dirty, 2);
    __atomic_sub_fetch(&completion_counter, 1, __ATOMIC_SEQ_CST);
    while (completion_counter > 0) {
        if (!work_queue_drain_one()) asm volatile("pause");
    }

    graphics_clear_clipping(); 
    draw_cursor(mx, my);
    last_cursor_x = mx;
    last_cursor_y = my;
    graphics_flip_buffer();
    graphics_clear_dirty_no_lock();
    wm_lock_release(rflags);
}

void wm_bring_to_front_locked(Window *win) {
    for (int i = 0; i < window_count; i++) {
        all_windows[i]->focused = false;
    }
    
    int max_z = 0;
    for (int i = 0; i < window_count; i++) {
        if (all_windows[i]->z_index > max_z) max_z = all_windows[i]->z_index;
    }
    
    win->visible = true;
    win->focused = true;
    win->z_index = max_z + 1;
    force_redraw = true;
}

void wm_bring_to_front(Window *win) {
    uint64_t rflags;
    rflags = wm_lock_acquire();
    wm_bring_to_front_locked(win);
    wm_lock_release(rflags);
}

void wm_add_window_locked(Window *win) {
    if (window_count < 32) {
        all_windows[window_count++] = win;
        wm_bring_to_front_locked(win); // Ensure newly added windows are on top
        wm_mark_dirty(0, 0, get_screen_width(), 30);
        menubar_dirty_pending = true;
    }
}

void wm_add_window(Window *win) {
    uint64_t rflags;
    rflags = wm_lock_acquire();
    wm_add_window_locked(win);
    wm_lock_release(rflags);
}

Window* wm_find_window_by_title_locked(const char *title) {
    if (!title) return NULL;
    for (int i = 0; i < window_count; i++) {
        if (all_windows[i] && all_windows[i]->title && str_eq(all_windows[i]->title, title)) {
            return all_windows[i];
        }
    }
    return NULL;
}

Window* wm_find_window_by_title(const char *title) {
    if (!title) return NULL;
    uint64_t rflags = wm_lock_acquire();
    Window *win = wm_find_window_by_title_locked(title);
    wm_lock_release(rflags);
    return win;
}

void wm_remove_window(Window *win) {
    if (!win) return;
    
    serial_write("WM: Removing window '");
    if (win->title) serial_write(win->title);
    else serial_write("unknown");
    serial_write("'\n");
    
    uint64_t rflags;
    rflags = wm_lock_acquire();
    
    int index = -1;
    for (int i = 0; i < window_count; i++) {
        if (all_windows[i] == win) {
            index = i;
            break;
        }
    }
    
    if (index != -1) {
        // Shift remaining windows
        for (int i = index; i < window_count - 1; i++) {
            all_windows[i] = all_windows[i + 1];
        }
        window_count--;
        
        if (active_mouse_capture_win == win) active_mouse_capture_win = NULL;
        
        if (drag_window == win) {
            is_dragging = false;
            is_resizing = false;
            drag_window = NULL;
        }
        
        if (drag_src_win == win) {
            drag_src_win = NULL;
        }
        
        // Mark for redraw while protected
        force_redraw = true;
    } else {
        wm_lock_release(rflags);
        log_fail("Window not found in all_windows list!");
        return;
    }
    
    if (win->pixels) kfree(win->pixels);
    if (win->comp_pixels) kfree(win->comp_pixels);
    if (win->title && win->handle_close) { 
        kfree(win->title);
    }
    kfree(win);
    
    wm_lock_release(rflags);
}

void wm_handle_click(int x, int y) {
    int sh = get_screen_height();
    int sw = get_screen_width();
    
    if (lumos_state.visible) {
        int modal_width = LUMOS_MODAL_WIDTH;
        int modal_height = LUMOS_SEARCH_HEIGHT + (lumos_state.result_count * LUMOS_RESULT_HEIGHT) + 10;
        int modal_x = (sw - modal_width) / 2;
        int modal_y = (sh * 2 / 5) - (modal_height / 2);
        
        if (rect_contains(modal_x, modal_y, modal_width, modal_height, x, y)) {
            int result_click_y = y - (modal_y + LUMOS_SEARCH_HEIGHT + 4);
            if (result_click_y >= 0 && result_click_y < lumos_state.result_count * LUMOS_RESULT_HEIGHT) {
                int result_idx = result_click_y / LUMOS_RESULT_HEIGHT;
                if (result_idx >= 0 && result_idx < lumos_state.result_count) {
                    lumos_state.selected_index = result_idx;
                    const char *file_path = lumos_state.results[result_idx].entry.path;
                    lumos_state.visible = false;
                }
            }
        } else {
            lumos_state.visible = false;
        }
        force_redraw = true;
        return;
    }
    
    if (msg_box_visible) {
        int mw = 320;
        int mh = 100;
        int mx = (sw - mw) / 2;
        int my = (sh - mh) / 2;
        if (rect_contains(mx + mw/2 - 30, my + 70, 60, 20, x, y)) {
            msg_box_visible = false;
            force_redraw = true;
        }
        return;
    }

    if (dock_menu_visible) {
        int menu_w = 140;
        int menu_h = 25;
        if (rect_contains(dock_menu_x, dock_menu_y, menu_w, menu_h, x, y)) {
            if (dock_menu_target_index >= 0 && dock_menu_target_index < dock_item_count) {
                if (dock_item_is_protected(dock_menu_target_index)) {
                    wm_show_message("Dock", "Unable to remove the Files app from the dock.");
                } else {
                    dock_remove_item(dock_menu_target_index);
                    dock_save_config();
                }
            }
        }
        dock_menu_visible = false;
        dock_menu_target_index = -1;
        pending_dock_click_index = -1;
        dock_drag_active = false;
        dock_drag_source_index = -1;
        force_redraw = true;
        return;
    }
    
    // Handle Desktop Context Menu Click
    if (desktop_menu_visible) {
        int menu_w = 140;
        int menu_h = (desktop_menu_target_icon != -1) ? 125 : 75;
        
        if (rect_contains(desktop_menu_x, desktop_menu_y, menu_w, menu_h, x, y)) {
            int rel_y = y - desktop_menu_y;
            int item = rel_y / 25;
            
            if (item == 0 && desktop_menu_target_icon != -1) { // Cut
                DesktopIcon *icon = &desktop_icons[desktop_menu_target_icon];
                char path[128] = "/root/Desktop/";
                int p=14; int n=0; while(icon->name[n]) path[p++] = icon->name[n++]; path[p]=0;
                explorer_clipboard_cut(path);
            } else if (item == 1 && desktop_menu_target_icon != -1) { // Copy
                DesktopIcon *icon = &desktop_icons[desktop_menu_target_icon];
                char path[128] = "/root/Desktop/";
                int p=14; int n=0; while(icon->name[n]) path[p++] = icon->name[n++]; path[p]=0;
                explorer_clipboard_copy(path);
            } else if (item == 0 && desktop_menu_target_icon == -1) { // New File
                desktop_dialog_state = 1;
                desktop_dialog_input[0] = 0;
                desktop_dialog_cursor = 0;
            } else if (item == 1 && desktop_menu_target_icon == -1) { // New Folder
                desktop_dialog_state = 2;
                desktop_dialog_input[0] = 0;
                desktop_dialog_cursor = 0;
            } else if (item == 2) { // Paste
                bool can_paste = explorer_clipboard_has_content();
                if (desktop_menu_target_icon != -1) {
                    DesktopIcon *icon = &desktop_icons[desktop_menu_target_icon];
                    if (icon->type != 1) can_paste = false;
                }
                
                if (can_paste) {
                    int old_count = desktop_icon_count;
                    if (desktop_menu_target_icon != -1 && desktop_icons[desktop_menu_target_icon].type == 1) {
                        // Paste into folder
                        char path[128] = "/root/Desktop/";
                        DesktopIcon *icon = &desktop_icons[desktop_menu_target_icon];
                        int p=14; int n=0; while(icon->name[n]) path[p++] = icon->name[n++]; path[p]=0;
                        explorer_clipboard_paste(&win_explorer, path);
                    } else {
                        // Paste to desktop
                        explorer_clipboard_paste(&win_explorer, "/root/Desktop");
                    }
                    refresh_desktop_icons();

                    if (!desktop_auto_align && desktop_icon_count > old_count && desktop_menu_target_icon == -1) {
                        int new_idx = desktop_icon_count - 1;
                        desktop_icons[new_idx].x = desktop_menu_x - 20;
                        desktop_icons[new_idx].y = desktop_menu_y - 20;
                        if (desktop_snap_to_grid) {
                            int col = (desktop_icons[new_idx].x - 20 + 40) / 80;
                            int row = (desktop_icons[new_idx].y - 20 + 40) / 80;
                            if (col < 0) col = 0;
                            if (row < 0) row = 0;
                            desktop_icons[new_idx].x = 20 + col * 80;
                            desktop_icons[new_idx].y = 20 + row * 80;
                        }
                    }
                }
            }
            else if (item == 3 && desktop_menu_target_icon != -1) { // Delete
                DesktopIcon *icon = &desktop_icons[desktop_menu_target_icon];
                char path[128] = "/root/Desktop/";
                int p=14; int n=0; while(icon->name[n]) path[p++] = icon->name[n++]; path[p]=0;
                explorer_delete_recursive(path);
                refresh_desktop_icons();
            }
            else if (item == 4 && desktop_menu_target_icon != -1) { // Rename
                desktop_dialog_state = 8;
                desktop_dialog_target = desktop_menu_target_icon;
                int k=0; while(desktop_icons[desktop_dialog_target].name[k]) {
                    desktop_dialog_input[k] = desktop_icons[desktop_dialog_target].name[k];
                    k++;
                }
                desktop_dialog_input[k] = 0;
                desktop_dialog_cursor = k;
            }
        }
        desktop_menu_visible = false;
        force_redraw = true;
        return;
    }

    // Handle Desktop Dialog Clicks
    if (desktop_dialog_state != 0) {
        int dlg_x = (sw - 300) / 2; int dlg_y = (sh - 110) / 2;
        if (rect_contains(dlg_x + 50, dlg_y + 65, 80, 25, x, y)) { // Confirm
            if (desktop_dialog_state == 8) { // Rename
                char old_path[128] = "/root/Desktop/";
                char new_path[128] = "/root/Desktop/";
                int p=14; int n=0; while(desktop_icons[desktop_dialog_target].name[n]) old_path[p++] = desktop_icons[desktop_dialog_target].name[n++]; old_path[p]=0;
                p=14; n=0; while(desktop_dialog_input[n]) new_path[p++] = desktop_dialog_input[n++]; new_path[p]=0;
                
                if (fat32_rename(old_path, new_path)) {
                    refresh_desktop_icons();
                    explorer_refresh_all();
                }
            } else if (desktop_dialog_state == 1 || desktop_dialog_state == 2) { // Create File/Folder
                if (desktop_icon_count >= desktop_max_cols * desktop_max_rows_per_col) {
                    wm_show_message("Error", "Desktop is full!");
                } else if (desktop_dialog_input[0] != 0) {
                    char path[128] = "/root/Desktop/";
                    int p=14; int n=0; while(desktop_dialog_input[n]) path[p++] = desktop_dialog_input[n++]; path[p]=0;
                    if (desktop_dialog_state == 1) {
                        FAT32_FileHandle *fh = fat32_open(path, "w");
                        if (fh) fat32_close(fh);
                    } else {
                        fat32_mkdir(path);
                    }
                    refresh_desktop_icons();
                    explorer_refresh_all();
                }
            }
            desktop_dialog_state = 0;
            force_redraw = true;
            return;
        }
        if (rect_contains(dlg_x + 170, dlg_y + 65, 80, 25, x, y)) { // Cancel
            desktop_dialog_state = 0;
            force_redraw = true;
            return;
        }
        if (rect_contains(dlg_x + 10, dlg_y + 35, 280, 20, x, y)) {
            desktop_dialog_cursor = (x - dlg_x - 15) / 8;
            int len = 0; while(desktop_dialog_input[len]) len++;
            if (desktop_dialog_cursor > len) desktop_dialog_cursor = len;
            force_redraw = true;
            return;
        }
    }
    
    // Check Top Bar Logo (toggle dropdown menu)
    if (rect_contains(8, 8, 24, 24, x, y)) {
        start_menu_open = !start_menu_open;
        force_redraw = true;
        pending_desktop_icon_click = -1;
        return;
    }
    
    // Handle top bar dropdown menu items
    if (start_menu_open && rect_contains(8, 40, 160, 120, x, y)) {
        int rel_y = y - 40;
        int item = rel_y / 20;
        
        if (item == 0) {  // About
            process_create_elf("/bin/about.elf", NULL, false, -1);
        } else if (item == 1) {  // Settings
            Window *existing = wm_find_window_by_title_locked("Settings");
            if (existing) wm_bring_to_front_locked(existing);
            else process_create_elf("/bin/settings.elf", NULL, false, -1);
        } else if (item == 2) {  // Shutdown
            k_shutdown();
        } else if (item == 3) {  // Restart
            k_reboot();
        }
        
        start_menu_open = false;
        force_redraw = true;
        return;
    }
    
    // Find topmost window at click location
    Window *topmost = NULL;
    int topmost_z = -1;
    
    for (int i = 0; i < window_count; i++) {
        Window *win = all_windows[i];
        if (win->visible && rect_contains(win->x, win->y, win->w, win->h, x, y)) {
            if (win->z_index > topmost_z) {
                topmost = win;
                topmost_z = win->z_index;
            }
        }
    }
    
    // If a window was clicked
    if (topmost != NULL) {
        wm_bring_to_front_locked(topmost);
        
        // Check traffic light close button (now at top-left)
        if (rect_contains(topmost->x + 8, topmost->y + 2, 12, 12, x, y)) {
            if (topmost->handle_close) {
                topmost->handle_close(topmost);
            } else {
                topmost->visible = false;
            }
            
            // Reset window state on close
            if (topmost == &win_explorer) {
                explorer_reset();
            }
        } else if (topmost->resizable && x >= topmost->x + topmost->w - 20 && y >= topmost->y + topmost->h - 20) {
            // Dragging the resize handle
            is_resizing = true;
            drag_window = topmost;
            drag_offset_x = x - topmost->x;
            drag_offset_y = y - topmost->y;
            drag_start_w = topmost->w;
            drag_start_h = topmost->h;
        } else if (y < topmost->y + 30) {
            // Dragging the title bar
            is_dragging = true;
            drag_window = topmost;
            drag_offset_x = x - topmost->x;
            drag_offset_y = y - topmost->y;
        } else {
            // Content click
            if (topmost->handle_click) {
                topmost->handle_click(topmost, x - topmost->x, y - topmost->y - 20);
            }
        }
        pending_desktop_icon_click = -1;
    } else {
        // No window clicked - check desktop icons
        // Clear focus from all windows first
        for (int w = 0; w < window_count; w++) {
            all_windows[w]->focused = false;
        }
        
        pending_desktop_icon_click = -1;
        
        for (int i = 0; i < desktop_icon_count; i++) {
            DesktopIcon *icon = &desktop_icons[i];
            if (rect_contains(icon->x + 20, icon->y, 40, 40, x, y)) {
                // Handle click - Defer to mouse up to allow dragging
                pending_desktop_icon_click = i;
                return;
            }
        }
    }
    
    // Close dropdown menu if clicked elsewhere
    if (start_menu_open) {
        start_menu_open = false;
    }
    
    force_redraw = true;
}

// Handle right click (context menu or special actions)
void wm_handle_right_click(int x, int y) {
    desktop_menu_visible = false;
    dock_menu_visible = false;
    dock_menu_target_index = -1;

    int sw = get_screen_width();
    int sh = get_screen_height();

    // Find topmost window at click location
    Window *topmost = NULL;
    int topmost_z = -1;
    
    for (int i = 0; i < window_count; i++) {
        Window *win = all_windows[i];
        if (win->visible && rect_contains(win->x, win->y, win->w, win->h, x, y)) {
            if (win->z_index > topmost_z) {
                topmost = win;
                topmost_z = win->z_index;
            }
        }
    }
    
    // If a window was clicked
    if (topmost != NULL) {
        // Don't process close button or title bar for right click
        if (y >= topmost->y + 20) {
            // Content right click
            if (topmost->handle_right_click) {
                topmost->handle_right_click(topmost, x - topmost->x, y - topmost->y - 20);
            }
        }
    } else {
        int dock_item = dock_item_index_at_point(x, y, sw, sh);
        if (dock_item != -1) {
            dock_menu_visible = true;
            dock_menu_x = x;
            dock_menu_y = y;
            dock_menu_target_index = dock_item;
            force_redraw = true;
            return;
        }
        if (dock_point_in_bounds(x, y, sw, sh)) {
            force_redraw = true;
            return;
        }

        // Desktop Right Click
        desktop_menu_visible = true;
        desktop_menu_x = x;
        desktop_menu_y = y;
        desktop_menu_target_icon = -1;
        
        // Check if clicked on an icon
        for (int i = 0; i < desktop_icon_count; i++) {
            if (rect_contains(desktop_icons[i].x + 20, desktop_icons[i].y, 40, 40, x, y)) {
                desktop_menu_target_icon = i;
                break;
            }
        }
    }
    
    force_redraw = true;
}

static void wm_handle_mouse_internal(int dx, int dy, uint8_t buttons, int dz) {
    int sw = get_screen_width();
    int sh = get_screen_height();
    
    prev_mx = mx;
    prev_my = my;
    
    mouse_accum_x += dx * mouse_speed;
    mouse_accum_y += dy * mouse_speed;
    
    int move_x = mouse_accum_x / 10;
    int move_y = mouse_accum_y / 10;
    
    mouse_accum_x -= move_x * 10;
    mouse_accum_y -= move_y * 10;
    
    mx += move_x;
    my += move_y;
    
    if (mx < 0) mx = 0;
    if (my < 0) my = 0;
    if (mx >= sw) mx = sw - 1;
    if (my >= sh) my = sh - 1;
    
    if (move_x != 0 || move_y != 0) {
        wm_mark_dirty(prev_mx, prev_my, CURSOR_W, CURSOR_H);
        wm_mark_dirty(mx, my, CURSOR_W, CURSOR_H);
    }
    
    if (dz != 0) {
        // Find focused window and send wheel event
        for (int w = 0; w < window_count; w++) {
            Window *win = all_windows[w];
            if (win->focused && win->visible) {
                 // Map to userland process
                 process_t* proc = process_get_by_ui_window(win);
                 if (proc) {
                     gui_event_t ev;
                     ev.type = 9; // GUI_EVENT_MOUSE_WHEEL
                     ev.arg1 = dz;
                     process_push_gui_event(proc, &ev);
                 }
                 break;
            }
        }
    }
    
    static bool prev_left = false;
    static bool prev_right = false;
    bool left = buttons & 0x01;
    bool right = buttons & 0x02;

    if (left && !prev_left) {
        drag_start_x = mx;
        drag_start_y = my;

        if (dock_menu_visible) {
            pending_dock_click_index = -1;
            dock_drag_active = false;
            dock_drag_source_index = -1;
            wm_handle_click(mx, my);
            prev_left = left;
            prev_right = right;
            return;
        }

        int dock_item = dock_item_index_at_point(mx, my, sw, sh);
        if (dock_item != -1) {
            pending_dock_click_index = dock_item;
            dock_drag_active = false;
            dock_drag_source_index = -1;
            dock_menu_visible = false;
        } else if (!dock_point_in_bounds(mx, my, sw, sh)) {
            wm_handle_click(mx, my);
        }
    } else if (right && !prev_right) {
        wm_handle_right_click(mx, my);
    } else if (left && is_dragging && drag_window) {
        drag_window->x = mx - drag_offset_x;
        drag_window->y = my - drag_offset_y;
        // Mark for full redraw since window moved
        force_redraw = true;
    } else if (left && is_resizing && drag_window) {
        int new_w = mx - drag_window->x + (drag_start_w - drag_offset_x);
        int new_h = my - drag_window->y + (drag_start_h - drag_offset_y);
        
        if (new_w < 150) new_w = 150;
        if (new_h < 100) new_h = 100;
        
        if (new_w != drag_window->w || new_h != drag_window->h) {
            drag_window->w = new_w;
            drag_window->h = new_h;
            if (drag_window->handle_resize) {
                drag_window->handle_resize(drag_window, new_w, new_h);
            }
            
            // Push resize event to userland process if it has one
            process_t *proc = process_get_by_ui_window(drag_window);
            if (proc) {
                gui_event_t ev;
                ev.type = 11; // GUI_EVENT_RESIZE
                ev.arg1 = new_w;
                ev.arg2 = new_h;
                ev.arg3 = 0;
                process_push_gui_event(proc, &ev);
            }
            
            force_redraw = true;
        }
    } else if (left && !is_dragging && !is_resizing && !is_dragging_file && (dx != 0 || dy != 0)) {
        // Check deadzone
        int dist_x = mx - drag_start_x;
        int dist_y = my - drag_start_y;
        if (dist_x < 0) dist_x = -dist_x;
        if (dist_y < 0) dist_y = -dist_y;
        
        if (dist_x >= 5 || dist_y >= 5) {
            if (pending_dock_click_index != -1 && pending_dock_click_index < dock_item_count) {
                dock_drag_active = true;
                dock_drag_source_index = pending_dock_click_index;
                pending_dock_click_index = -1;
                is_dragging_file = true;
                drag_icon_type = 2;
                drag_file_path[0] = 0;
            }
            
            if (pending_desktop_icon_click != -1) {
                int i = pending_desktop_icon_click;
                DesktopIcon *icon = &desktop_icons[i];
                is_dragging_file = true;
                drag_icon_type = icon->type;
                pending_desktop_icon_click = -1; 
                drag_icon_orig_x = icon->x;
                drag_icon_orig_y = icon->y;
                // Construct path
                char path[128] = "/root/Desktop/";
                int p=14; int n=0; while(icon->name[n]) path[p++] = icon->name[n++]; path[p]=0;
                int k=0; while(path[k]) { drag_file_path[k] = path[k]; k++; } drag_file_path[k]=0;
            }
            // 2. Check Explorer Items
            if (!is_dragging_file) {
                Window *topmost_at_drag = NULL;
                int tops_z = -1;
                for (int w = 0; w < window_count; w++) {
                    Window *w_ptr = all_windows[w];
                    if (w_ptr->visible && rect_contains(w_ptr->x, w_ptr->y, w_ptr->w, w_ptr->h, drag_start_x, drag_start_y)) {
                        if (w_ptr->z_index > tops_z) {
                            topmost_at_drag = w_ptr;
                            tops_z = w_ptr->z_index;
                        }
                    }
                }
                
                if (!topmost_at_drag || str_starts_with(topmost_at_drag->title, "Files")) {
                    bool is_dir;
                    if (explorer_get_file_at(drag_start_x, drag_start_y, drag_file_path, &is_dir)) {
                        is_dragging_file = true;
                        drag_icon_type = is_dir ? 1 : 0;
                        drag_src_win = NULL;
                        
                        // Find which explorer window was clicked to clear its state
                        for (int w = 0; w < window_count; w++) {
                            Window *win = all_windows[w];
                            if (win->visible && rect_contains(win->x, win->y, win->w, win->h, drag_start_x, drag_start_y)) {
                                if (str_starts_with(win->title, "Files")) {
                                    drag_src_win = win;
                                    explorer_clear_click_state(win);
                                }
                            }
                        }
                    }
                }
            }
            
            if (is_dragging_file) force_redraw = true;
        }
        
    } else if (!left) {
        if (is_dragging || is_resizing) {
            is_dragging = false;
            is_resizing = false;
            drag_window = NULL;
            force_redraw = true;
        }
        
        // Handle Dock App Click (mouse up without dragging)
        if (pending_dock_click_index != -1 && !dock_drag_active) {
            dock_launch_item(pending_dock_click_index);
            pending_dock_click_index = -1;
            force_redraw = true;
        }
        
        // Handle Desktop Icon Click (Mouse Up)
        if (pending_desktop_icon_click != -1) {
            int i = pending_desktop_icon_click;
            if (i < desktop_icon_count) {
                DesktopIcon *icon = &desktop_icons[i];
                bool handled = false;
                if (icon->type == 2) { // App Shortcut
                    // Check name to launch app
                    if (str_ends_with(icon->name, "Notepad.shortcut")) {
                        process_create_elf("/bin/notepad.elf", NULL, false, -1); handled = true;
                    } else if (str_ends_with(icon->name, "Calculator.shortcut")) {
                        process_create_elf("/bin/calculator.elf", NULL, false, -1); handled = true;
                    } else if (str_ends_with(icon->name, "Minesweeper.shortcut")) {
                        process_create_elf("/bin/minesweeper.elf", NULL, false, -1); handled = true;
                    } else if (str_ends_with(icon->name, "Settings.shortcut")) {
                        process_create_elf("/bin/settings.elf", NULL, false, -1); handled = true;
                    } else if (str_ends_with(icon->name, "Terminal.shortcut")) {
                        process_create_elf("/bin/terminal.elf", NULL, false, -1); handled = true;
                    } else if (str_ends_with(icon->name, "About.shortcut")) {
                        process_create_elf("/bin/about.elf", NULL, false, -1); handled = true;
                    } else if (str_ends_with(icon->name, "Files.shortcut")) {
                        explorer_open_directory("/"); handled = true;
                    } else if (str_ends_with(icon->name, "Recycle Bin.shortcut")) {
                        explorer_open_directory("/RecycleBin"); handled = true;
                    } else if (str_ends_with(icon->name, "Paint.shortcut")) {
                        process_create_elf("/bin/paint.elf", NULL, false, -1); handled = true;
                    }
                    
                    if (!handled) {
                        // Generic Shortcut Handling
                        char path[128] = "/root/Desktop/";
                        int p=14; int n=0; while(icon->name[n]) path[p++] = icon->name[n++]; path[p]=0;
                        
                        if (str_ends_with(icon->name, ".shortcut") && !str_starts_with(icon->name, "Recycle Bin")) {
                            FAT32_FileHandle *fh = fat32_open(path, "r");
                            if (fh) {
                                char buf[256];
                                int len = fat32_read(fh, buf, 255);
                                fat32_close(fh);
                                if (len > 0) {
                                    buf[len] = 0;
                                    if (fat32_is_directory(buf)) {
                                        explorer_open_directory(buf);
                                    } else {
                                        process_create_elf("/bin/txtedit.elf", buf, false, -1);
                                    }
                                    pending_desktop_icon_click = -1;
                                    force_redraw = true;
                                    return;
                                }
                            }
                        }
                    }
                } else if (icon->type == 1) { // Folder
                    char path[128] = "/root/Desktop/";
                    int p=14; int n=0; while(icon->name[n]) path[p++] = icon->name[n++]; path[p]=0;
                    explorer_open_directory(path);
                } else { // File
                    char path[128] = "/root/Desktop/";
                    int p=14; int n=0; while(icon->name[n]) path[p++] = icon->name[n++]; path[p]=0;
                    
                    if (str_ends_with(icon->name, ".elf")) {
                        process_create_elf(path, NULL, false, -1);
                    } else if (str_ends_with(icon->name, ".pnt")) {
                        process_create_elf("/bin/paint.elf", path, false, -1);
                    } else if (str_ends_with(icon->name, ".md")) {
                        process_create_elf("/bin/markdown.elf", path, false, -1);
                    } else if (str_ends_with(icon->name, ".pdf")) {
                        process_create_elf("/bin/boredword.elf", path, false, -1);
                    } else if (is_image_file(icon->name)) {
                        process_create_elf("/bin/viewer.elf", path, false, -1);
                    } else {
                        process_create_elf("/bin/txtedit.elf", path, false, -1);
                    }
                }
            }
            pending_desktop_icon_click = -1;
        }
        
        if (is_dragging_file) {
            // Drop logic
            bool drop_handled = false;

            if (dock_drag_active && dock_drag_source_index >= 0 && dock_drag_source_index < dock_item_count) {
                int insert_idx = dock_insertion_index_from_x(mx, my, sw, sh);
                if (insert_idx != -1) {
                    dock_move_item(dock_drag_source_index, insert_idx);
                    dock_save_config();
                }
                drop_handled = true;
            }

            if (!drop_handled && dock_point_in_bounds(mx, my, sw, sh)) {
                int insert_idx = dock_insertion_index_from_x(mx, my, sw, sh);
                if (insert_idx == -1) insert_idx = dock_item_count;

                if (dock_can_pin_path(drag_file_path)) {
                    int existing_idx = dock_find_item_by_target(drag_file_path);
                    if (existing_idx != -1) {
                        dock_move_item(existing_idx, insert_idx);
                    } else if (dock_item_count < MAX_DOCK_ITEMS) {
                        char label[DOCK_LABEL_MAX];
                        dock_label_from_target(drag_file_path, label, sizeof(label));
                        int icon_slot = dock_icon_slot_for_target(drag_file_path, label);
                        dock_insert_item(insert_idx, label, drag_file_path, icon_slot);
                    } else {
                        wm_show_message("Dock", "Dock is full.");
                    }
                    dock_save_config();
                } else {
                    wm_show_message("Dock", "Only folders, .elf and .shortcut can be pinned.");
                }
                drop_handled = true;
            }
            
            Window *drop_win = NULL;
            int topmost_z = -1;
            for (int w = 0; w < window_count; w++) {
                Window *win = all_windows[w];
                if (win->visible && rect_contains(win->x, win->y, win->w, win->h, mx, my)) {
                    if (win->z_index > topmost_z && str_starts_with(win->title, "Files")) {
                        drop_win = win;
                        topmost_z = win->z_index;
                    }
                }
            }

            if (!drop_handled && drop_win) {
                char target_path[256];
                bool is_dir;
                // Check if dropped on a folder inside this explorer
                if (explorer_get_file_at(mx, my, target_path, &is_dir) && is_dir) {
                    explorer_import_file_to(drop_win, drag_file_path, target_path);
                } else {
                    // Dropped in current dir of this explorer
                    explorer_import_file(drop_win, drag_file_path);
                }

                if (str_starts_with(drag_file_path, "/root/Desktop/")) {
                    refresh_desktop_icons();
                }
            } else if (!drop_handled) {
                // Dropped on Desktop (or elsewhere)
                {
                    bool from_desktop = (drag_file_path[0]=='/' && drag_file_path[1]=='D' && drag_file_path[2]=='e');
                    bool dropped_on_target = false;
                    for (int i = 0; i < desktop_icon_count; i++) {
                        if (from_desktop) {
                            char path[128] = "/root/Desktop/";
                            int p=14; int n=0; while(desktop_icons[i].name[n]) path[p++] = desktop_icons[i].name[n++]; path[p]=0;
                            if (str_eq(path, drag_file_path) != 0) continue;
                        }

                        if (rect_contains(desktop_icons[i].x + 20, desktop_icons[i].y, 40, 40, mx, my)) {
                            if (desktop_icons[i].type == 1) {
                                char target_path[256] = "/root/Desktop/";
                                int p=14; int n=0; while(desktop_icons[i].name[n]) target_path[p++] = desktop_icons[i].name[n++]; target_path[p]=0;
                                explorer_import_file_to(&win_explorer, drag_file_path, target_path);
                                refresh_desktop_icons();
                                dropped_on_target = true;
                                break;
                            } else if (desktop_icons[i].type == 2 && str_starts_with(desktop_icons[i].name, "Recycle Bin")) {
                                explorer_import_file_to(&win_explorer, drag_file_path, "/RecycleBin");
                                refresh_desktop_icons();
                                dropped_on_target = true;
                                break;
                            } else {
                                dropped_on_target = true;
                                break;
                            }
                        }
                    }

                    if (!dropped_on_target && !from_desktop) {
                        // Dragged from Explorer to Desktop
                        // Check limit first
                        if (desktop_icon_count >= desktop_max_cols * desktop_max_rows_per_col) {
                            wm_show_message("Error", "Desktop is full!");
                        } else {
                            explorer_import_file_to(&win_explorer, drag_file_path, "/root/Desktop");
                        }
                        
                        // Handle insertion at specific position
                        char filename[64];
                        int len = 0; while(drag_file_path[len]) len++;
                        int s = len - 1; while(s >= 0 && drag_file_path[s] != '/') s--;
                        s++;
                        int d = 0; while(drag_file_path[s] && d < 63) filename[d++] = drag_file_path[s++];
                        filename[d] = 0;

                        if (desktop_auto_align && !msg_box_visible) {
                            int new_idx = -1;
                            for(int i=0; i<desktop_icon_count; i++) {
                                if (str_eq(desktop_icons[i].name, filename) != 0) {
                                    new_idx = i;
                                    break;
                                }
                            }
                            
                            if (new_idx != -1) {
                                int target_col = (mx - 20) / 80;
                                int target_row = (my - 20) / 80;
                                if (target_col < 0) target_col = 0;
                                if (target_row < 0) target_row = 0;
                                int target_idx = target_col * desktop_max_rows_per_col + target_row;
                                if (target_idx >= desktop_icon_count) target_idx = desktop_icon_count - 1;
                                
                                // Move new_idx to target_idx
                                DesktopIcon temp = desktop_icons[new_idx];
                                // Shift down to make space
                                for (int i = new_idx; i > target_idx; i--) desktop_icons[i] = desktop_icons[i-1];
                                desktop_icons[target_idx] = temp;
                                
                                refresh_desktop_icons(); // Re-apply layout
                            }
                        } else if (!desktop_auto_align && !msg_box_visible) {
                            for(int i=0; i<desktop_icon_count; i++) {
                                if (str_eq(desktop_icons[i].name, filename) != 0) {
                                    desktop_icons[i].x = mx - 20;
                                    desktop_icons[i].y = my - 20;
                                    if (desktop_snap_to_grid) {
                                        int col = (desktop_icons[i].x - 20 + 40) / 80;
                                        int row = (desktop_icons[i].y - 20 + 40) / 80;
                                        if (col < 0) col = 0;
                            if (row < 0) row = 0;
                                        desktop_icons[i].x = 20 + col * 80;
                                        desktop_icons[i].y = 20 + row * 80;
                                    }
                                    break;
                                }
                            }
                        }
                    } else if (!dropped_on_target) {
                        int dragged_idx = -1;
                        for(int i=0; i<desktop_icon_count; i++) {
                            char path[128] = "/root/Desktop/";
                            int p=14; int n=0; while(desktop_icons[i].name[n]) path[p++] = desktop_icons[i].name[n++]; path[p]=0;
                            if (str_eq(path, drag_file_path) != 0) {
                                dragged_idx = i;
                                break;
                            }
                        }
                        
                        if (dragged_idx != -1) {
                            if (desktop_auto_align) {
                                int cell_h = 80;
                                int rel_y = my - 20;
                                if (rel_y < 0) rel_y = 0;
                                
                                int target_col = (mx - 20) / 80;
                                if (target_col < 0) target_col = 0;
                                
                                int target_row = rel_y / cell_h;
                                int row_offset = rel_y % cell_h;
                                
                                // 20% threshold logic: Only insert "before" if in the top 20%
                                if (row_offset > (int)(cell_h * 0.2)) {
                                    target_row++;
                                }
                                
                                int target_idx = target_col * desktop_max_rows_per_col + target_row;
                                if (target_idx >= desktop_icon_count) target_idx = desktop_icon_count - 1;
                                
                                // Shift items
                                DesktopIcon temp = desktop_icons[dragged_idx];
                                if (target_idx > dragged_idx) {
                                    for (int i = dragged_idx; i < target_idx; i++) desktop_icons[i] = desktop_icons[i+1];
                                } else {
                                    for (int i = dragged_idx; i > target_idx; i--) desktop_icons[i] = desktop_icons[i-1];
                                }
                                desktop_icons[target_idx] = temp;
                                refresh_desktop_icons(); // Re-applies layout
                            } else {
                                desktop_icons[dragged_idx].x = mx - 20;
                                desktop_icons[dragged_idx].y = my - 20;
                                if (desktop_snap_to_grid) {
                                    int col = (desktop_icons[dragged_idx].x - 20 + 40) / 80;
                                    int row = (desktop_icons[dragged_idx].y - 20 + 40) / 80;
                                    if (col < 0) col = 0;
                                    if (row < 0) row = 0;
                                    desktop_icons[dragged_idx].x = 20 + col * 80;
                                    desktop_icons[dragged_idx].y = 20 + row * 80;
                                }
                                
                                for (int i = 0; i < desktop_icon_count; i++) {
                                    if (i == dragged_idx) continue;
                                    int dx = desktop_icons[i].x - desktop_icons[dragged_idx].x;
                                    int dy = desktop_icons[i].y - desktop_icons[dragged_idx].y;
                                    if (dx < 0) dx = -dx;
                                    if (dy < 0) dy = -dy;
                                    if (dx < 35 && dy < 35) {
                                        // Revert position
                                        desktop_icons[dragged_idx].x = drag_icon_orig_x;
                                        desktop_icons[dragged_idx].y = drag_icon_orig_y;
                                        break;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        
        if (is_dragging_file) {
            is_dragging_file = false;
            dock_drag_active = false;
            dock_drag_source_index = -1;
            force_redraw = true;
        }
    }
    
    if (is_dragging_file) {
        force_redraw = true;
    }
    
    // Send mouse events to userland windows
    if (left && !prev_left) {
        // Left button pressed - send MOUSE_DOWN event to topmost window
        Window *topmost = NULL;
        int topmost_z = -1;
        for (int w = 0; w < window_count; w++) {
            Window *win = all_windows[w];
            if (win->visible && rect_contains(win->x, win->y, win->w, win->h, mx, my)) {
                if (win->z_index > topmost_z) {
                    topmost = win;
                    topmost_z = win->z_index;
                }
            }
        }
        if (topmost && topmost->data) {
            active_mouse_capture_win = topmost;
            if (my >= topmost->y + 20)
                syscall_send_mouse_down_event(topmost, mx - topmost->x, my - topmost->y - 20);
        } else {
            active_mouse_capture_win = NULL;
        }
    }
    
    if (!left && prev_left) {
        // Left button released - send MOUSE_UP event to captured or topmost window
        Window *target = active_mouse_capture_win;
        if (!target) {
            int topmost_z = -1;
            for (int w = 0; w < window_count; w++) {
                Window *win = all_windows[w];
                if (win->visible && rect_contains(win->x, win->y, win->w, win->h, mx, my)) {
                    if (win->z_index > topmost_z) {
                        target = win;
                        topmost_z = win->z_index;
                    }
                }
            }
        }
        
        if (target && target->data) {
            int rel_y = my - target->y - 20;
            // Provide coordinates clamped if escaping bounds slightly on UP? Usually raw is fine.
            syscall_send_mouse_up_event(target, mx - target->x, rel_y);
        }
        active_mouse_capture_win = NULL;
    }
    
    if (dx != 0 || dy != 0) {
        // Mouse moved - send MOUSE_MOVE event to captured window (if dragging) or topmost
        Window *target = active_mouse_capture_win;
        if (!target) {
            int topmost_z = -1;
            for (int w = 0; w < window_count; w++) {
                Window *win = all_windows[w];
                if (win->visible && rect_contains(win->x, win->y, win->w, win->h, mx, my)) {
                    if (win->z_index > topmost_z) {
                        target = win;
                        topmost_z = win->z_index;
                    }
                }
            }
        }
        
        if (target && target->data) {
            int rel_y = my - target->y - 20;
            syscall_send_mouse_move_event(target, mx - target->x, rel_y, buttons);
        }
    }
    
    prev_left = left;
    prev_right = right;
    
    if (prev_mx != mx || prev_my != my) {
        // Cursor moved - just mark dirty cursor areas
        wm_mark_dirty(prev_mx, prev_my, CURSOR_W, CURSOR_H);
        wm_mark_dirty(mx, my, CURSOR_W, CURSOR_H);
    }
    
    prev_left = left;
}

void wm_handle_mouse(int dx, int dy, uint8_t buttons, int dz) {
    uint64_t rflags = wm_lock_acquire();
    wm_handle_mouse_internal(dx, dy, buttons, dz);
    wm_lock_release(rflags);
}

// Input Queue
#define INPUT_QUEUE_SIZE 128
typedef struct {
    char c;
    bool pressed;
} key_event_t;
static key_event_t key_queue[INPUT_QUEUE_SIZE];
static volatile int key_head = 0;
static volatile int key_tail = 0;

static void wm_dispatch_key(char c, bool pressed) {
    if (desktop_dialog_state != 0) {
        int len = 0; while(desktop_dialog_input[len]) len++;
        if (c == '\n') {
            if (desktop_dialog_state == 8) { // Rename
                char old_path[128] = "/root/Desktop/";
                char new_path[128] = "/root/Desktop/";
                int p=14; int n=0; while(desktop_icons[desktop_dialog_target].name[n]) old_path[p++] = desktop_icons[desktop_dialog_target].name[n++]; old_path[p]=0;
                p=14; n=0; while(desktop_dialog_input[n]) new_path[p++] = desktop_dialog_input[n++]; new_path[p]=0;
                if (fat32_rename(old_path, new_path)) {
                    refresh_desktop_icons();
                    explorer_refresh_all();
                }
            } else if (desktop_dialog_state == 1 || desktop_dialog_state == 2) { // Create File/Folder
                if (desktop_icon_count >= desktop_max_cols * desktop_max_rows_per_col) {
                    wm_show_message("Error", "Desktop is full!");
                } else if (desktop_dialog_input[0] != 0) {
                    char path[128] = "/root/Desktop/";
                    int p=14; int n=0; while(desktop_dialog_input[n]) path[p++] = desktop_dialog_input[n++]; path[p]=0;
                    if (desktop_dialog_state == 1) {
                        FAT32_FileHandle *fh = fat32_open(path, "w");
                        if (fh) fat32_close(fh);
                    } else {
                        fat32_mkdir(path);
                    }
                    refresh_desktop_icons();
                    explorer_refresh_all();
                }
            }
            desktop_dialog_state = 0;
        } else if (c == 27) {
            desktop_dialog_state = 0;
        } else if (c == '\b' || c == 127) {
            if (desktop_dialog_cursor > 0) {
                for(int i = desktop_dialog_cursor - 1; i < len; i++) desktop_dialog_input[i] = desktop_dialog_input[i+1];
                desktop_dialog_cursor--;
            }
        } else if (c == 19) { // Left
            if (desktop_dialog_cursor > 0) desktop_dialog_cursor--;
        } else if (c == 20) { // Right
            if (desktop_dialog_cursor < len) desktop_dialog_cursor++;
        } else if (c >= 32 && c <= 126 && len < 63) {
            for(int i = len; i >= desktop_dialog_cursor; i--) desktop_dialog_input[i+1] = desktop_dialog_input[i];
            desktop_dialog_input[desktop_dialog_cursor] = c;
            desktop_dialog_cursor++;
        }
        force_redraw = true;
        return;
    }

    Window *target = NULL;
    for (int i = 0; i < window_count; i++) {
        if (all_windows[i]->focused && all_windows[i]->visible) {
            target = all_windows[i];
            break;
        }
    }
    
    if (!target) return;
    
    
    if (target->handle_key) {
        target->handle_key(target, c, pressed);
    }
    
    // Mark window as needing redraw on next timer tick
    wm_mark_dirty(target->x, target->y, target->w, target->h);
}

void wm_show_notification(const char *msg) {
    int i = 0;
    while (msg[i] && i < 255) {
        notif_text[i] = msg[i];
        i++;
    }
    notif_text[i] = 0;
    
    notif_timer = 180; // ~3 seconds at 60Hz
    notif_x_offset = 420;
    notif_active = true;
    force_redraw = true;
}

// Wrapper for work queue - builds file index asynchronously
static void build_file_index_async(void *arg) {
    (void)arg;  // Unused
    file_index_build();
}

void wm_handle_key(char c, bool pressed) {
    if (pressed && c == 'p' && ps2_ctrl_pressed) {
        process_create_elf("/bin/screenshot.elf", NULL, false, -1);
        return;
    }
    
    if (pressed && c == ' ' && ps2_ctrl_pressed && ps2_shift_pressed()) {
        lumos_state.visible = !lumos_state.visible;
        if (lumos_state.visible) {
            // Check current index status - it may still be building in background
            lumos_index_built = file_index_is_valid();
            // Clear search state when opening
            lumos_state.search_len = 0;
            lumos_state.search_query[0] = 0;
            lumos_state.cursor_pos = 0;
            lumos_state.result_count = 0;
            lumos_state.selected_index = 0;
        }
        int sw = get_screen_width();
        int sh = get_screen_height();
        graphics_mark_dirty(0, 0, sw, sh);
        force_redraw = true;
        return;
    }
    
    if (lumos_state.visible && pressed) {
        wm_lumos_handle_key(c);
        return;
    }
    
    int next = (key_head + 1) % INPUT_QUEUE_SIZE;
    if (next != key_tail) {
        key_queue[key_head].c = c;
        key_queue[key_head].pressed = pressed;
        key_head = next;
    }
}

void wm_process_input(void) {
    if (periodic_refresh_pending) {
        if (!is_dragging && !is_dragging_file) {
            refresh_desktop_icons();
            explorer_refresh_all();
            force_redraw = true;
        }
        periodic_refresh_pending = false;
    }

    uint64_t rflags;
    rflags = wm_lock_acquire();
    while (key_head != key_tail) {
        key_event_t ev = key_queue[key_tail];
        key_tail = (key_tail + 1) % INPUT_QUEUE_SIZE;
        wm_dispatch_key(ev.c, ev.pressed);
    }
    wm_lock_release(rflags);

    if (force_redraw) {
        graphics_mark_screen_dirty();
        force_redraw = false;
    }
    
    DirtyRect dirty = graphics_get_dirty_rect();
    if (dirty.active) {
        wm_paint();
    }
}

void wm_mark_dirty(int x, int y, int w, int h) {
    graphics_mark_dirty(x, y, w, h);
}

void wm_refresh(void) {
    force_redraw = true;
}

void wm_process_deferred_thumbs(void) {
    if (thumb_queue_head == thumb_queue_tail) return;
    
    char path[256];
    int i = 0;
    while (thumb_request_queue[thumb_queue_head][i]) {
        path[i] = thumb_request_queue[thumb_queue_head][i];
        i++;
    }
    path[i] = 0;

    // Pop from queue
    thumb_queue_head = (thumb_queue_head + 1) % THUMB_QUEUE_SIZE;

    if (thumb_cache_lookup(path) || thumb_cache_is_failed(path)) return;
    
    if (thumb_cache_decode(path)) {
        force_redraw = true;
    }
}

void wm_init(void) {
    disk_manager_init();
    log_ok("Disk Manager ready");
    
    disk_manager_scan();
    log_ok("Disk scanning complete");

    explorer_init();
    log_ok("Explorer ready");
    
    wallpaper_init();
    log_ok("Wallpaper engine ready");
    
    file_index_init();
    log_ok("File Indexer ready");
    
    if (!file_index_load()) {
        log_ok("No Index cache, background build started");
        work_queue_submit(build_file_index_async, NULL);
    } else {
        log_ok("Index cache loaded");
        lumos_index_built = true;
    }
    
    refresh_desktop_icons();
    log_ok("Desktop icons refreshed");

    dock_load_config();
    log_ok("Dock config loaded");
    
    // Initialize z-indices
    win_explorer.z_index = 1;

    all_windows[0] = &win_explorer;
    window_count = 1;
    
    win_explorer.visible = false;
    win_explorer.focused = false;
    win_explorer.z_index = 10;
    
    force_redraw = true;

    serial_write("[WM] Initialization complete, transitioning to GUI\n");
    kconsole_set_active(false);
    graphics_flip_buffer();
}

uint32_t wm_get_ticks(void) {
    return timer_ticks;
}

// Called by timer interrupt ~60Hz
void wm_timer_tick(void) {
    timer_ticks++;
    
    static uint8_t last_second = 0xFF;
    
    outb(0x70, 0x00);
    uint8_t current_sec = inb(0x71);
    
    if (current_sec != last_second) {
        last_second = current_sec;
        int sw = get_screen_width();
        wm_mark_dirty(sw - 110, 0, 110, 30);
    }
    
    if (notif_active) {
        if (notif_timer > 0) {
            notif_timer--;
            // Slide in
            if (notif_timer > 165 && notif_x_offset > 0) { // First 15 ticks (1/4 sec) slide in
                notif_x_offset -= 28; // Slightly faster slide for larger distance
                if (notif_x_offset < 0) notif_x_offset = 0;
            }
            // Slide out
            else if (notif_timer < 15 && notif_x_offset < 420) { // Last 15 ticks slide out
                notif_x_offset += 28;
            }
        } else {
            notif_active = false;
        }
        
        int sw = get_screen_width();
        wm_mark_dirty(sw - 420, 40, 415, 60); 
    }
}

static volatile bool index_rebuild_queued = false;

static void index_rebuild_wrapper(void *arg) {
    (void)arg;
    file_index_build();
    lumos_index_built = file_index_is_valid();
    index_rebuild_queued = false;
}

void wm_notify_fs_change(void) {
    periodic_refresh_pending = true;
    
    file_index_invalidate_cache();
    lumos_index_built = false;

    // Kick off a background index rebuild so Lumos reflects changes immediately.
    // The flag prevents flooding the work queue when many files change at once
    // (e.g. during a bulk folder move that triggers one notification per file).
    if (!index_rebuild_queued) {
        index_rebuild_queued = true;
        work_queue_submit(index_rebuild_wrapper, NULL);
    }
}
