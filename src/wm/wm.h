// Copyright (c) 2023-2026 Chris (boreddevnl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
#ifndef WM_H
#define WM_H

#include <stdint.h>
#include <stdbool.h>
#include "../sys/spinlock.h"

uint64_t wm_lock_acquire(void);
void wm_lock_release(uint64_t flags);

// --- Constants ---
#define COLOR_TEAL      0xFF008080
#define COLOR_GRAY      0xFFC0C0C0
#define COLOR_WHITE     0xFFFFFFFF
#define COLOR_BLACK     0xFF000000
#define COLOR_BLUE      0xFF000080
#define COLOR_LTGRAY    0xFFDFDFDF
#define COLOR_DKGRAY    0xFF808080
#define COLOR_PURPLE    0xFF800080
#define COLOR_COFFEE    0xFF6B4423
#define COLOR_RED    0xFFFF0000
#define COLOR_ORANGE 0xFFFF7F00
#define COLOR_YELLOW 0xFFFFFF00
#define COLOR_GREEN  0xFF00FF00
#define COLOR_LIGHTBLUE 0xFF0000FF
#define COLOR_APPLE_INDIGO 0xFF4B0082
#define COLOR_APPLE_VIOLET 0xFF9400D3

#define COLOR_NOTEPAD_BG    0xFFF5F5DC       
#define COLOR_DARK_BG       0xFF1E1E1E  // Main dark background
#define COLOR_DARK_PANEL    0xFF2D2D2D  // Slightly lighter panel background
#define COLOR_DARK_TITLEBAR 0xFF282828  // Darker for title bar
#define COLOR_DARK_TEXT     0xFFF0F0F0  // Light gray text
#define COLOR_DARK_BORDER   0xFF3A3A3A  // Border color
#define COLOR_DOCK_BG       0xFF3A3A3A  // Dock background
#define COLOR_MENUBAR_BG     0xFF1A1A1A  // Top bar background
#define COLOR_TRAFFIC_RED   0xFFED6158  // Close button red
#define COLOR_TRAFFIC_YELLOW 0xFFFCC02E // Minimize button (not used for now)
#define COLOR_TRAFFIC_GREEN 0xFF5FC038  // Zoom button (not used for now)

#define DESKTOP_TOP_DEADSPACE_HEIGHT 80 // stops files from being rendered under menu bar
typedef struct Window Window;
struct Window {
    char *title;
    int x, y, w, h;
    bool visible;
    char buffer[1024];
    int buf_len;
    int cursor_pos;
    bool focused;
    int z_index;  
    void *data;   
    uint32_t *pixels; 
    uint32_t *comp_pixels; 
    void *font;
    spinlock_t lock;
    
    // Callbacks
    void (*paint)(Window *win);
    void (*handle_key)(Window *win, char c, bool pressed);
    void (*handle_click)(Window *win, int x, int y);
    void (*handle_right_click)(Window *win, int x, int y);
    void (*handle_close)(Window *win);
    void (*handle_resize)(Window *win, int w, int h);
    bool resizable;
};

#define LUMOS_MAX_RESULTS 6
#define LUMOS_MODAL_WIDTH 520
#define LUMOS_RESULT_HEIGHT 40
#define LUMOS_SEARCH_HEIGHT 48

#include "../sys/file_index.h"

typedef struct {
    bool visible;
    char search_query[256];
    int search_len;
    int cursor_pos;
    file_index_result_t results[LUMOS_MAX_RESULTS];
    int result_count;
    int selected_index;
    int last_query_hash;
} lumos_state_t;

void wm_init(void);
void wm_handle_mouse(int dx, int dy, uint8_t buttons, int dz);
void wm_handle_key(char c, bool pressed);
void wm_handle_click(int x, int y);
void wm_handle_right_click(int x, int y);
void wm_process_input(void);
void wm_process_deferred_thumbs(void);
void wm_add_window_locked(Window *win);
void wm_add_window(Window *win);
void wm_remove_window(Window *win);
void wm_bring_to_front_locked(Window *win);
void wm_bring_to_front(Window *win);
Window* wm_find_window_by_title(const char *title);

// Redraw system
void wm_mark_dirty(int x, int y, int w, int h);
void wm_refresh(void);
void wm_paint(void);
void wm_refresh_desktop(void);
void wm_timer_tick(void);
uint32_t wm_get_ticks(void);
int wm_get_desktop_icon_count(void);
void wm_show_message(const char *title, const char *message);
void wm_notify_fs_change(void);
bool wm_draw_dock_icon_scaled(int x, int y, int size, int slot_index);

// Hook for external rendering (e.g. VM overlay)
extern void (*wm_custom_paint_hook)(void);

// Drawing helpers
void draw_bevel_rect(int x, int y, int w, int h, bool sunken);
void draw_button(int x, int y, int w, int h, const char *text, bool pressed);
void draw_rounded_rect(int x, int y, int w, int h, int radius, uint32_t color);
void draw_rounded_rect_filled(int x, int y, int w, int h, int radius, uint32_t color);
void draw_traffic_light(int x, int y); 
void draw_icon(int x, int y, const char *label);
void draw_folder_icon(int x, int y, const char *label);
void draw_document_icon(int x, int y, const char *label);
void draw_pdf_icon(int x, int y, const char *label);
void draw_elf_icon(int x, int y, const char *label);
void draw_image_icon(int x, int y, const char *label);
bool draw_icon_path(int x, int y, const char *path);
void draw_notepad_icon(int x, int y, const char *label);
void draw_calculator_icon(int x, int y, const char *label);
void draw_terminal_icon(int x, int y, const char *label);
void draw_minesweeper_icon(int x, int y, const char *label);
void draw_control_panel_icon(int x, int y, const char *label);
void draw_about_icon(int x, int y, const char *label);
void draw_recycle_bin_icon(int x, int y, const char *label);
void draw_paint_icon(int x, int y, const char *label);
void draw_squircle_icon(int x, int y, const char *label, uint32_t bg_color);
void draw_files_icon(int x, int y, const char *label);
void draw_settings_icon(int x, int y, const char *label);

// Desktop Settings
extern bool desktop_snap_to_grid;
extern bool desktop_auto_align;
extern int desktop_max_rows_per_col;
extern int desktop_max_cols;

// Mouse Settings
extern int mouse_speed;

#endif
