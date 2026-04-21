// Copyright (c) 2023-2026 Chris (boreddevnl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
#include "explorer.h"
#include "graphics.h"
#include "font_manager.h"
#include "fat32.h"
#include "vfs.h"
#include "disk.h"
#include "wm.h"
#include "memory_manager.h"
#include "process.h"
#include "app_metadata.h"
#define EXPLORER_ITEM_HEIGHT 80
#define EXPLORER_ITEM_WIDTH 120
#define EXPLORER_COLS 4
#define EXPLORER_ROWS 4
#define EXPLORER_PADDING 15

Window win_explorer;

#define DIALOG_NONE 0
#define DIALOG_CREATE_FILE 1
#define DIALOG_CREATE_FOLDER 2
#define DIALOG_DELETE_CONFIRM 3
#define DIALOG_REPLACE_CONFIRM 4
#define DIALOG_REPLACE_MOVE_CONFIRM 5
#define DIALOG_CREATE_REPLACE_CONFIRM 6
#define DIALOG_INPUT_MAX 256
#define DIALOG_RENAME 8
#define ACTION_RESTORE 108
#define DIALOG_ERROR 7
#define ACTION_CREATE_SHORTCUT 107

static Window* explorer_wins[10];
static int explorer_win_count = 0;

static int dropdown_menu_item_height = 25;
#define DROPDOWN_MENU_WIDTH 120
#define DROPDOWN_MENU_ITEMS 3

#define FILE_CONTEXT_MENU_WIDTH 180
#define FILE_CONTEXT_MENU_HEIGHT 50
#define CONTEXT_MENU_ITEM_HEIGHT 25

static void wm_draw_rect(void *user_data, int x, int y, int w, int h, uint32_t color) {
    (void)user_data; draw_rect(x, y, w, h, color);
}
static void wm_draw_rounded_rect_filled(void *user_data, int x, int y, int w, int h, int r, uint32_t color) {
    (void)user_data; draw_rounded_rect_filled(x, y, w, h, r, color);
}
static void wm_draw_string(void *user_data, int x, int y, const char *str, uint32_t color) {
    (void)user_data; draw_string(x, y, str, color);
}
static int wm_measure_string_width(void *user_data, const char *str) {
    (void)user_data;
    return font_manager_get_string_width(graphics_get_current_ttf(), str);
}
static widget_context_t wm_widget_ctx = {
    .user_data = NULL,
    .draw_rect = wm_draw_rect,
    .draw_rounded_rect_filled = wm_draw_rounded_rect_filled,
    .draw_string = wm_draw_string,
    .measure_string_width = wm_measure_string_width,
    .mark_dirty = NULL
};

static char clipboard_path[FAT32_MAX_PATH] = "";
static int clipboard_action = 0; 
#define FILE_CONTEXT_ITEMS 2 

typedef struct {
    const char *label;
    int action_id; 
    bool enabled;
    uint32_t color;
} ExplorerContextItem;


size_t explorer_strlen(const char *str);
void explorer_strcpy(char *dest, const char *src);
static int explorer_strcmp(const char *s1, const char *s2);
void explorer_strcat(char *dest, const char *src);
static void explorer_load_directory(Window *win, const char *path);
static void explorer_handle_right_click(Window *win, int x, int y);
static void explorer_handle_file_context_menu_click(Window *win, int x, int y);
static void explorer_perform_paste(Window *win, const char *dest_dir);
static void explorer_perform_move_internal(Window *win, const char *source_path, const char *dest_dir);
static bool explorer_copy_recursive(const char *src_path, const char *dest_path);
Window* explorer_create_window(const char *path);

extern bool is_dragging_file;

size_t explorer_strlen(const char *str) {
    size_t len = 0;
    while (str[len]) len++;
    return len;
}

void explorer_strcpy(char *dest, const char *src) {
    while (*src) *dest++ = *src++;
    *dest = 0;
}

static int explorer_strcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

static int explorer_strcasecmp(const char *s1, const char *s2) {
    while (*s1 && *s2) {
        char c1 = *s1;
        char c2 = *s2;
        if (c1 >= 'A' && c1 <= 'Z') c1 += 32;
        if (c2 >= 'A' && c2 <= 'Z') c2 += 32;
        if (c1 != c2) {
            return (unsigned char)c1 - (unsigned char)c2;
        }
        s1++;
        s2++;
    }
    char c1 = *s1;
    char c2 = *s2;
    if (c1 >= 'A' && c1 <= 'Z') c1 += 32;
    if (c2 >= 'A' && c2 <= 'Z') c2 += 32;
    return (unsigned char)c1 - (unsigned char)c2;
}

void explorer_strcat(char *dest, const char *src) {
    while (*dest) dest++;
    explorer_strcpy(dest, src);
}

static const char* explorer_get_extension(const char *filename) {
    const char *dot = filename;
    const char *ext = "";
    
    while (*dot) {
        if (*dot == '.') {
            ext = dot + 1;
        }
        dot++;
    }
    
    return ext;
}

static bool explorer_is_markdown_file(const char *filename) {
    const char *ext = explorer_get_extension(filename);
    return explorer_strcmp(ext, "md") == 0;
}

static bool explorer_str_starts_with(const char *str, const char *prefix) {
    while(*prefix) {
        if (*prefix++ != *str++) return false;
    }
    return true;
}

static bool explorer_str_ends_with(const char *str, const char *suffix) {
    int str_len = explorer_strlen(str);
    int suf_len = explorer_strlen(suffix);
    if (suf_len > str_len) return false;
    return explorer_strcmp(str + str_len - suf_len, suffix) == 0;
}

static bool explorer_is_image_file(const char *filename) {
    return explorer_str_ends_with(filename, ".jpg") || explorer_str_ends_with(filename, ".JPG") ||
           explorer_str_ends_with(filename, ".png") || explorer_str_ends_with(filename, ".PNG") ||
           explorer_str_ends_with(filename, ".gif") || explorer_str_ends_with(filename, ".GIF") ||
           explorer_str_ends_with(filename, ".bmp") || explorer_str_ends_with(filename, ".BMP") ||
           explorer_str_ends_with(filename, ".tga") || explorer_str_ends_with(filename, ".TGA");
}

static void explorer_draw_icon_label(int x, int y, const char *label, uint32_t color) {
    char line1[11] = {0}; 
    char line2[11] = {0}; 
    int len = 0; while(label[len]) len++;
    
    if (len <= 10) {
        for (int i = 0; i < len; i++) line1[i] = label[i];
    } else {
        int dot_pos = -1;
        for (int i = len - 1; i >= 0; i--) {
            if (label[i] == '.') { dot_pos = i; break; }
        }

        int split = -1;
        if (dot_pos != -1 && dot_pos > 0 && dot_pos <= 10) {
            split = dot_pos;
        } else {
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
    
    ttf_font_t *font = graphics_get_current_ttf();

    int l1_w = font_manager_get_string_width(font, line1);
    draw_string(x + (EXPLORER_ITEM_WIDTH - l1_w)/2, y + 56, line1, color);
    
    if (line2[0]) {
        int l2_w = font_manager_get_string_width(font, line2);
        draw_string(x + (EXPLORER_ITEM_WIDTH - l2_w)/2, y + 66, line2, color);
    }
}


static bool check_desktop_limit_explorer(Window *win) {
    ExplorerState *state = (ExplorerState*)win->data;
    if (explorer_str_starts_with(state->current_path, "/root/Desktop")) {
        if (explorer_strcmp(state->current_path, "/root/Desktop") == 0 || explorer_strcmp(state->current_path, "/root/Desktop/") == 0) { // Check if root desktop
             if (state->item_count >= desktop_max_cols * (desktop_max_rows_per_col > 1 ? desktop_max_rows_per_col - 1 : 0)) {
                 state->dialog_state = DIALOG_ERROR;
                 explorer_strcpy(state->dialog_input, "Desktop is full!");
                 return false;
             }
        }
    }
    return true;
}

static void dialog_open_create_file(Window *win, const char *path) {
    ExplorerState *state = (ExplorerState*)win->data;
    state->dialog_state = DIALOG_CREATE_FILE;
    state->dialog_input[0] = 0;
    state->dialog_input_cursor = 0;
    explorer_strcpy(state->dialog_creation_path, path);
}

static void dialog_open_create_folder(Window *win, const char *path) {
    ExplorerState *state = (ExplorerState*)win->data;
    state->dialog_state = DIALOG_CREATE_FOLDER;
    state->dialog_input[0] = 0;
    state->dialog_input_cursor = 0;
    explorer_strcpy(state->dialog_creation_path, path);
}

static void dialog_open_delete_confirm(Window *win, int item_idx) {
    ExplorerState *state = (ExplorerState*)win->data;
    if (item_idx < 0 || item_idx >= state->item_count) return;
    
    state->dialog_state = DIALOG_DELETE_CONFIRM;
    state->dialog_target_is_dir = state->items[item_idx].is_directory;
    
    explorer_strcpy(state->dialog_target_path, state->current_path);
    if (state->dialog_target_path[explorer_strlen(state->dialog_target_path) - 1] != '/') {
        explorer_strcat(state->dialog_target_path, "/");
    }
    explorer_strcat(state->dialog_target_path, state->items[item_idx].name);
}

static void dialog_close(Window *win) {
    ExplorerState *state = (ExplorerState*)win->data;
    state->dialog_state = DIALOG_NONE;
    state->dialog_input[0] = 0;
    state->dialog_input_cursor = 0;
    state->dialog_target_path[0] = 0;
}

static void dialog_confirm_create_file(Window *win) {
    ExplorerState *state = (ExplorerState*)win->data;
    if (state->dialog_input[0] == 0) return;
    
    if (!check_desktop_limit_explorer(win)) return;
    
    char full_path[FAT32_MAX_PATH];
    explorer_strcpy(full_path, state->dialog_creation_path);
    if (full_path[explorer_strlen(full_path) - 1] != '/') {
        explorer_strcat(full_path, "/");
    }
    explorer_strcat(full_path, state->dialog_input);
    
    if (vfs_exists(full_path)) {
        state->dialog_state = DIALOG_CREATE_REPLACE_CONFIRM;
        return;
    }
    
    vfs_file_t *file = vfs_open(full_path, "w");
    if (file) {
        vfs_close(file);
        explorer_refresh_all();
    }
    
    dialog_close(win);
}

static void dialog_force_create_file(Window *win) {
    ExplorerState *state = (ExplorerState*)win->data;
    char full_path[FAT32_MAX_PATH];
    explorer_strcpy(full_path, state->dialog_creation_path);
    if (full_path[explorer_strlen(full_path) - 1] != '/') {
        explorer_strcat(full_path, "/");
    }
    explorer_strcat(full_path, state->dialog_input);
    
    vfs_file_t *file = vfs_open(full_path, "w");
    if (file) {
        vfs_close(file);
        explorer_refresh_all();
    }
    dialog_close(win);
}

static void dialog_confirm_create_folder(Window *win) {
    ExplorerState *state = (ExplorerState*)win->data;
    if (state->dialog_input[0] == 0) return;
    
    if (!check_desktop_limit_explorer(win)) return;
    
    char full_path[FAT32_MAX_PATH];
    explorer_strcpy(full_path, state->dialog_creation_path);
    if (full_path[explorer_strlen(full_path) - 1] != '/') {
        explorer_strcat(full_path, "/");
    }
    explorer_strcat(full_path, state->dialog_input);
    
    if (vfs_mkdir(full_path)) {
        explorer_refresh_all();
    }
    
    dialog_close(win);
}

bool explorer_delete_permanently(const char *path) {
    if (vfs_is_directory(path)) {
        int capacity = 64;
        vfs_dirent_t *entries = (vfs_dirent_t*)kmalloc(capacity * sizeof(vfs_dirent_t));
        if (!entries) return false;

        int count = vfs_list_directory(path, entries, capacity);
        while (count == capacity) {
            capacity *= 2;
            vfs_dirent_t *new_entries = (vfs_dirent_t*)krealloc(entries, capacity * sizeof(vfs_dirent_t));
            if (!new_entries) { kfree(entries); return false; }
            entries = new_entries;
            count = vfs_list_directory(path, entries, capacity);
        }
        
        for (int i = 0; i < count; i++) {
            if (explorer_strcmp(entries[i].name, ".") == 0 || explorer_strcmp(entries[i].name, "..") == 0) continue;

            char child_path[FAT32_MAX_PATH];
            explorer_strcpy(child_path, path);
            if (child_path[explorer_strlen(child_path) - 1] != '/') {
                explorer_strcat(child_path, "/");
            }
            explorer_strcat(child_path, entries[i].name);
            
            if (entries[i].is_directory) {
                explorer_delete_permanently(child_path);
            } else {
                vfs_delete(child_path);
            }
        }
        kfree(entries);
        return vfs_rmdir(path);
    } else {
        return vfs_delete(path);
    }
}

bool explorer_delete_recursive(const char *path) {
    bool is_external = false;
    if (path[0] && path[1] == ':' && path[0] != 'A' && path[0] != 'a') {
        is_external = true;
    }
    
    if (is_external || explorer_str_starts_with(path, "/RecycleBin")) {
        return explorer_delete_permanently(path);
    } else {
        char filename[256];
        int len = explorer_strlen(path);
        int i = len - 1;
        while (i >= 0 && path[i] != '/') i--;
        int j = 0;
        for (int k = i + 1; k < len; k++) filename[j++] = path[k];
        filename[j] = 0;
        
        char drive_prefix[3] = "/";
        if (path[0] && path[1] == ':') {
            drive_prefix[0] = path[0];
        }
        
        char dest_path[FAT32_MAX_PATH];
        explorer_strcpy(dest_path, drive_prefix);
        explorer_strcat(dest_path, "/RecycleBin/");
        explorer_strcat(dest_path, filename);
        
        char origin_path[FAT32_MAX_PATH];
        explorer_strcpy(origin_path, dest_path);
        explorer_strcat(origin_path, ".origin");
        vfs_file_t *fh = vfs_open(origin_path, "w");
        if (fh) {
            vfs_write(fh, path, explorer_strlen(path));
            vfs_close(fh);
        }
        
        explorer_copy_recursive(path, dest_path);
        explorer_delete_permanently(path);
        return true;
    }
}

static void dialog_confirm_delete(Window *win) {
    ExplorerState *state = (ExplorerState*)win->data;
    explorer_delete_recursive(state->dialog_target_path);
    explorer_refresh_all();
    dialog_close(win);
}

static void dialog_confirm_replace(Window *win) {
    ExplorerState *state = (ExplorerState*)win->data;
    explorer_perform_paste(win, state->dialog_dest_dir);
    dialog_close(win);
}

static void dialog_confirm_replace_move(Window *win) {
    ExplorerState *state = (ExplorerState*)win->data;
    explorer_perform_move_internal(win, state->dialog_move_src, state->dialog_dest_dir);
    dialog_close(win);
}

void explorer_clipboard_copy(const char *path) {
    explorer_strcpy(clipboard_path, path);
    clipboard_action = 1; // Copy
}

void explorer_clipboard_cut(const char *path) {
    explorer_strcpy(clipboard_path, path);
    clipboard_action = 2; // Cut
}

bool explorer_clipboard_has_content(void) {
    return clipboard_action != 0 && clipboard_path[0] != 0;
}

static bool explorer_copy_recursive(const char *src_path, const char *dest_path) {
    if (vfs_is_directory(src_path)) {
        if (!vfs_mkdir(dest_path) && !vfs_is_directory(dest_path)) return false;
        int capacity = 64;
        vfs_dirent_t *files = (vfs_dirent_t*)kmalloc(capacity * sizeof(vfs_dirent_t));
        if (!files) return false;
        
        int count = vfs_list_directory(src_path, files, capacity);
        while (count == capacity) {
            capacity *= 2;
            vfs_dirent_t *new_files = (vfs_dirent_t*)krealloc(files, capacity * sizeof(vfs_dirent_t));
            if (!new_files) { kfree(files); return false; }
            files = new_files;
            count = vfs_list_directory(src_path, files, capacity);
        }
        for (int i = 0; i < count; i++) {
            if (explorer_strcmp(files[i].name, ".") == 0 || explorer_strcmp(files[i].name, "..") == 0) continue;
            
            char s_sub[FAT32_MAX_PATH], d_sub[FAT32_MAX_PATH];
            explorer_strcpy(s_sub, src_path);
            if (s_sub[explorer_strlen(s_sub)-1] != '/') explorer_strcat(s_sub, "/");
            explorer_strcat(s_sub, files[i].name);
            
            explorer_strcpy(d_sub, dest_path);
            if (d_sub[explorer_strlen(d_sub)-1] != '/') explorer_strcat(d_sub, "/");
            explorer_strcat(d_sub, files[i].name);
            
            if (!explorer_copy_recursive(s_sub, d_sub)) { kfree(files); return false; }
        }
        kfree(files);
        return true;
    } else {
        vfs_file_t *src = vfs_open(src_path, "r");
        vfs_file_t *dst = vfs_open(dest_path, "w");
        bool success = false;
        if (src && dst) {
            uint8_t *buf = (uint8_t*)kmalloc(4096);
            if (buf) {
                int bytes;
                success = true;
                while ((bytes = vfs_read(src, buf, 4096)) > 0) {
                    if (vfs_write(dst, buf, bytes) != bytes) { success = false; break; }
                }
                kfree(buf);
            }
        }
        if (src) vfs_close(src);
        if (dst) vfs_close(dst);
        return success;
    }
}

static void explorer_copy_file_internal(const char *src_path, const char *dest_dir) {
    char filename[FAT32_MAX_FILENAME];
    int len = explorer_strlen(src_path);
    int i = len - 1;
    while (i >= 0 && src_path[i] != '/') i--;
    int j = 0;
    for (int k = i + 1; k < len; k++) filename[j++] = src_path[k];
    filename[j] = 0;
    
    char dest_path[FAT32_MAX_PATH];
    explorer_strcpy(dest_path, dest_dir);
    if (dest_path[explorer_strlen(dest_path) - 1] != '/') {
        explorer_strcat(dest_path, "/");
    }
    explorer_strcat(dest_path, filename);
    
    if (explorer_strcmp(src_path, dest_path) == 0) return;
    
    explorer_copy_recursive(src_path, dest_path);
}

static void explorer_perform_paste(Window *win, const char *dest_dir) {
    (void)win;
    explorer_copy_file_internal(clipboard_path, dest_dir);
    
    if (clipboard_action == 2) { 
        if (vfs_is_directory(clipboard_path)) {
            explorer_delete_permanently(clipboard_path);
        } else {
            vfs_delete(clipboard_path);
        }
        clipboard_action = 0;
    }
    explorer_refresh_all();
}

void explorer_clipboard_paste(Window *win, const char *dest_dir) {
    ExplorerState *state = (ExplorerState*)win->data;
    if (!explorer_clipboard_has_content()) return;
    
    if (explorer_str_starts_with(dest_dir, clipboard_path)) {
        int src_len = explorer_strlen(clipboard_path);
        if (dest_dir[src_len] == '\0' || dest_dir[src_len] == '/') {
            return;
        }
    }

    char filename[FAT32_MAX_FILENAME];
    int len = explorer_strlen(clipboard_path);
    int i = len - 1;
    while (i >= 0 && clipboard_path[i] != '/') i--;
    int j = 0;
    for (int k = i + 1; k < len; k++) filename[j++] = clipboard_path[k];
    filename[j] = 0;
    
    char dest_path[FAT32_MAX_PATH];
    explorer_strcpy(dest_path, dest_dir);
    if (dest_path[explorer_strlen(dest_path) - 1] != '/') {
        explorer_strcat(dest_path, "/");
    }
    explorer_strcat(dest_path, filename);
    
    if (vfs_exists(dest_path)) {
        state->dialog_state = DIALOG_REPLACE_CONFIRM;
        explorer_strcpy(state->dialog_dest_dir, dest_dir);
        return;
    }
    
    explorer_perform_paste(win, dest_dir);
}

void explorer_create_shortcut(Window *win, const char *target_path) {
    ExplorerState *state = (ExplorerState*)win->data;
    char filename[256];
    int len = explorer_strlen(target_path);
    int i = len - 1;
    while (i >= 0 && target_path[i] != '/') i--;
    int j = 0;
    for (int k = i + 1; k < len; k++) filename[j++] = target_path[k];
    filename[j] = 0;
    
    char shortcut_path[FAT32_MAX_PATH];
    explorer_strcpy(shortcut_path, state->current_path);
    if (shortcut_path[explorer_strlen(shortcut_path)-1] != '/') explorer_strcat(shortcut_path, "/");
    explorer_strcat(shortcut_path, filename);
    explorer_strcat(shortcut_path, ".shortcut");
    
    vfs_file_t *fh = vfs_open(shortcut_path, "w");
    if (fh) {
        vfs_write(fh, target_path, explorer_strlen(target_path));
        vfs_close(fh);
        explorer_refresh_all();
    }
}

static void dropdown_menu_toggle(Window *win) {
    ExplorerState *state = (ExplorerState*)win->data;
    state->dropdown_menu_visible = !state->dropdown_menu_visible;
}

static int explorer_build_context_menu(Window *win, ExplorerContextItem *items_out) {
    ExplorerState *state = (ExplorerState*)win->data;
    int count = 0;
    if (state->file_context_menu_item == -1) {
        if (explorer_str_starts_with(state->current_path, "/RecycleBin")) {
            return 0;
        }
        items_out[count++] = (ExplorerContextItem){"New File", 101, true, COLOR_WHITE};
        items_out[count++] = (ExplorerContextItem){"New Folder", 102, true, COLOR_WHITE};
        items_out[count++] = (ExplorerContextItem){"Paste", 103, explorer_clipboard_has_content(), explorer_clipboard_has_content() ? COLOR_WHITE : COLOR_DKGRAY};
    } else {
        if (explorer_str_starts_with(state->current_path, "/RecycleBin")) {
            items_out[count++] = (ExplorerContextItem){"Restore", ACTION_RESTORE, true, COLOR_BLACK};
            items_out[count++] = (ExplorerContextItem){"Delete Forever", 106, true, COLOR_RED};
            return count;
        }

        bool is_dir = state->items[state->file_context_menu_item].is_directory;
        
        if (!is_dir) {
             items_out[count++] = (ExplorerContextItem){"Open", 100, true, COLOR_WHITE};
             items_out[count++] = (ExplorerContextItem){"Open w/ textedit", 110, true, COLOR_WHITE};
             if (explorer_is_markdown_file(state->items[state->file_context_menu_item].name)) {
        items_out[count++] = (ExplorerContextItem){"Open w/ Markdown", 109, true, COLOR_WHITE};
             }
        }
        
        items_out[count++] = (ExplorerContextItem){"Cut", 104, true, COLOR_WHITE};
        items_out[count++] = (ExplorerContextItem){"Copy", 105, true, COLOR_WHITE};
        
        if (is_dir) {
            items_out[count++] = (ExplorerContextItem){"Paste", 103, explorer_clipboard_has_content(), explorer_clipboard_has_content() ? COLOR_WHITE : COLOR_DKGRAY};
            items_out[count++] = (ExplorerContextItem){"Open in new window", 112, true, COLOR_WHITE};
        }
        
        items_out[count++] = (ExplorerContextItem){"Delete", 106, true, COLOR_RED};
        items_out[count++] = (ExplorerContextItem){"Rename", 111, true, COLOR_WHITE};
        items_out[count++] = (ExplorerContextItem){"Create Shortcut", ACTION_CREATE_SHORTCUT, true, COLOR_WHITE};
        
        if (is_dir) {
            items_out[count++] = (ExplorerContextItem){"New File", 101, true, COLOR_WHITE};
            items_out[count++] = (ExplorerContextItem){"New Folder", 102, true, COLOR_WHITE};
        }
    }
    return count;
}


static void explorer_restore_file(Window *win, int item_idx) {
    ExplorerState *state = (ExplorerState*)win->data;
    if (item_idx < 0 || item_idx >= state->item_count) return;
    
    char recycle_path[FAT32_MAX_PATH];
    explorer_strcpy(recycle_path, state->current_path);
    if (recycle_path[explorer_strlen(recycle_path) - 1] != '/') explorer_strcat(recycle_path, "/");
    explorer_strcat(recycle_path, state->items[item_idx].name);
    
    char origin_file_path[FAT32_MAX_PATH];
    explorer_strcpy(origin_file_path, recycle_path);
    explorer_strcat(origin_file_path, ".origin");
    
    char original_path[FAT32_MAX_PATH] = {0};
    vfs_file_t *fh = vfs_open(origin_file_path, "r");
    if (fh) {
        int len = vfs_read(fh, original_path, FAT32_MAX_PATH - 1);
        if (len > 0) original_path[len] = 0;
        vfs_close(fh);
    }
    
    if (original_path[0] == 0) return; 
    
    explorer_copy_recursive(recycle_path, original_path);
    explorer_delete_permanently(recycle_path);
    vfs_delete(origin_file_path);
    
    explorer_refresh_all();
}

static void explorer_load_directory(Window *win, const char *path) {
    ExplorerState *state = (ExplorerState*)win->data;
    bool path_changed = (explorer_strcmp(state->current_path, path) != 0);
    explorer_strcpy(state->current_path, path);

    state->item_count = 0;
    
    int capacity = EXPLORER_INITIAL_CAPACITY;
    vfs_dirent_t *entries = (vfs_dirent_t*)kmalloc(capacity * sizeof(vfs_dirent_t));
    if (!entries) return;

    int count = vfs_list_directory(path, entries, capacity);
    
    while (count == capacity) {
        capacity *= 2;
        vfs_dirent_t *new_entries = (vfs_dirent_t*)krealloc(entries, capacity * sizeof(vfs_dirent_t));
        if (!new_entries) { kfree(entries); return; }
        entries = new_entries;
        count = vfs_list_directory(path, entries, capacity);
    }
    
    if (state->items_capacity < count) {
        int new_cap = count < EXPLORER_INITIAL_CAPACITY ? EXPLORER_INITIAL_CAPACITY : count;
        ExplorerItem *new_items = (ExplorerItem*)krealloc(state->items, new_cap * sizeof(ExplorerItem));
        if (!new_items) { kfree(entries); return; }
        state->items = new_items;
        state->items_capacity = new_cap;
    }
    
    int temp_count = 0;
    for (int i = 0; i < count; i++) {
        if (explorer_strcmp(entries[i].name, ".color") == 0) {
            continue;
        }
        
        if (explorer_str_ends_with(entries[i].name, ".origin")) {
            continue;
        }

        explorer_strcpy(state->items[temp_count].name, entries[i].name);
        state->items[temp_count].is_directory = entries[i].is_directory;
        state->items[temp_count].size = entries[i].size;
        state->items[temp_count].color = entries[i].is_directory ? COLOR_BLUE : COLOR_YELLOW;
        temp_count++;
    }
    
    state->item_count = temp_count;
    
    for (int i = 0; i < temp_count - 1; i++) {
        for (int j = 0; j < temp_count - i - 1; j++) {
            bool swap = false;
            if (state->items[j].is_directory == state->items[j+1].is_directory) {
                if (explorer_strcasecmp(state->items[j].name, state->items[j+1].name) > 0) {
                    swap = true;
                }
            } else if (!state->items[j].is_directory && state->items[j+1].is_directory) {
                swap = true;
            }
            if (swap) {
                ExplorerItem temp = state->items[j];
                state->items[j] = state->items[j+1];
                state->items[j+1] = temp;
            }
        }
    }

    kfree(entries);
    if (path_changed) {
        state->selected_item = -1;
        state->explorer_scroll_row = 0;
    }
}

static void explorer_navigate_to(Window *win, const char *dirname) {
    ExplorerState *state = (ExplorerState*)win->data;
    char new_path[FAT32_MAX_PATH];
    
    if (explorer_strcmp(dirname, "..") == 0) {
        int len = explorer_strlen(state->current_path);
        int i = len - 1;
        
        while (i > 0 && state->current_path[i] == '/') i--;
        
        while (i > 0 && state->current_path[i] != '/') i--;
        
        if (i == 0) {
            explorer_strcpy(new_path, "/");
        } else {
            for (int j = 0; j < i; j++) {
                new_path[j] = state->current_path[j];
            }
            new_path[i] = 0;
        }
    } else {
        explorer_strcpy(new_path, state->current_path);
        if (new_path[explorer_strlen(new_path) - 1] != '/') {
            explorer_strcat(new_path, "/");
        }
        explorer_strcat(new_path, dirname);
    }
    
    explorer_load_directory(win, new_path);
}

void explorer_open_directory(const char *path) {
    explorer_create_window(path);
}

void explorer_open_target(const char *path) {
    if (vfs_is_directory(path)) {
        explorer_open_directory(path);
    } else {
        if (explorer_str_ends_with(path, ".elf")) {
            process_create_elf(path, NULL, false, -1);
        } else if (explorer_str_ends_with(path, ".pdf")) {
            process_create_elf("/bin/boredword.elf", path, false, -1);
        } else if (explorer_is_markdown_file(path)) {
            process_create_elf("/bin/markdown.elf", path, false, -1);
        } else if (explorer_str_ends_with(path, ".pnt")) {
            process_create_elf("/bin/paint.elf", path, false, -1);
        } else if (explorer_is_image_file(path)) {
            process_create_elf("/bin/viewer.elf", path, false, -1);
        } else {
            process_create_elf("/bin/txtedit.elf", path, false, -1);
        }
    }
}

static void explorer_open_item(Window *win, int index) {
    ExplorerState *state = (ExplorerState*)win->data;
    if (index < 0 || index >= state->item_count) return;

    if (state->items[index].is_directory) {
        explorer_navigate_to(win, state->items[index].name);
        return;
    }

    char full_path[FAT32_MAX_PATH];
    explorer_strcpy(full_path, state->current_path);
    if (full_path[explorer_strlen(full_path) - 1] != '/') {
        explorer_strcat(full_path, "/");
    }
    explorer_strcat(full_path, state->items[index].name);

    if (explorer_str_ends_with(state->items[index].name, ".shortcut")) {
        Window *target = NULL;
        if (explorer_strcmp(state->items[index].name, "Notepad.shortcut") == 0) {
            process_create_elf("/bin/notepad.elf", NULL, false, -1); return;
        } else if (explorer_strcmp(state->items[index].name, "Calculator.shortcut") == 0) {
            process_create_elf("/bin/calculator.elf", NULL, false, -1); return;
        } else if (explorer_strcmp(state->items[index].name, "Terminal.shortcut") == 0) {
            process_create_elf("/bin/terminal.elf", NULL, false, -1); return;
        } else if (explorer_strcmp(state->items[index].name, "Minesweeper.shortcut") == 0) {
            process_create_elf("/bin/minesweeper.elf", NULL, false, -1); return;
        } else if (explorer_strcmp(state->items[index].name, "Control Panel.shortcut") == 0 || explorer_strcmp(state->items[index].name, "Settings.shortcut") == 0) {
            process_create_elf("/bin/settings.elf", NULL, false, -1); return;
        } else if (explorer_strcmp(state->items[index].name, "About.shortcut") == 0) {
            process_create_elf("/bin/about.elf", NULL, false, -1); return;
        } else if (explorer_strcmp(state->items[index].name, "Explorer.shortcut") == 0) {
            explorer_open_directory("/"); return;
        } else if (explorer_strcmp(state->items[index].name, "Recycle Bin.shortcut") == 0) {
            explorer_open_directory("/RecycleBin"); return;
        }

        if (target) {
            wm_bring_to_front(target);
            return;
        }

        vfs_file_t *fh = vfs_open(full_path, "r");
        if (fh) {
            char buf[FAT32_MAX_PATH];
            int len = vfs_read(fh, buf, 255);
            vfs_close(fh);
            if (len > 0) {
                buf[len] = 0;
                explorer_open_target(buf);
                return;
            }
        }
    }

    explorer_open_target(full_path);
}

enum {
    EXPLORER_DOCK_SLOT_FILES = 0,
    EXPLORER_DOCK_SLOT_SETTINGS = 1,
    EXPLORER_DOCK_SLOT_NOTEPAD = 2,
    EXPLORER_DOCK_SLOT_CALCULATOR = 3,
    EXPLORER_DOCK_SLOT_GRAPHER = 4,
    EXPLORER_DOCK_SLOT_TERMINAL = 5,
    EXPLORER_DOCK_SLOT_MINESWEEPER = 6,
    EXPLORER_DOCK_SLOT_PAINT = 7,
    EXPLORER_DOCK_SLOT_BROWSER = 8,
    EXPLORER_DOCK_SLOT_TASKMAN = 9,
    EXPLORER_DOCK_SLOT_CLOCK = 10,
    EXPLORER_DOCK_SLOT_WORD = 11,
};

static void explorer_draw_colloid_slot_icon(int x, int y, int slot_index) {
    (void)wm_draw_dock_icon_scaled(x + 24, y + 12, 32, slot_index);
}

static void explorer_draw_file_icon(int x, int y, bool is_dir, const char *filename, const char *current_path) {
    if (is_dir) {
        explorer_draw_colloid_slot_icon(x + 5, y + 5, EXPLORER_DOCK_SLOT_FILES);
    } else if (explorer_str_ends_with(filename, ".shortcut")) {
        if (explorer_strcmp(filename, "Notepad.shortcut") == 0) {
            explorer_draw_colloid_slot_icon(x + 5, y + 5, EXPLORER_DOCK_SLOT_NOTEPAD);
        } else if (explorer_strcmp(filename, "Calculator.shortcut") == 0) {
            explorer_draw_colloid_slot_icon(x + 5, y + 5, EXPLORER_DOCK_SLOT_CALCULATOR);
        } else if (explorer_strcmp(filename, "Terminal.shortcut") == 0) {
            explorer_draw_colloid_slot_icon(x + 5, y + 5, EXPLORER_DOCK_SLOT_TERMINAL);
        } else if (explorer_strcmp(filename, "Minesweeper.shortcut") == 0) {
            explorer_draw_colloid_slot_icon(x + 5, y + 5, EXPLORER_DOCK_SLOT_MINESWEEPER);
        } else if (explorer_strcmp(filename, "Control Panel.shortcut") == 0 || explorer_strcmp(filename, "Settings.shortcut") == 0) {
            explorer_draw_colloid_slot_icon(x + 5, y + 5, EXPLORER_DOCK_SLOT_SETTINGS);
        } else if (explorer_strcmp(filename, "About.shortcut") == 0) {
            explorer_draw_colloid_slot_icon(x + 5, y + 5, EXPLORER_DOCK_SLOT_SETTINGS);
        } else if (explorer_strcmp(filename, "Explorer.shortcut") == 0) {
            explorer_draw_colloid_slot_icon(x + 5, y + 5, EXPLORER_DOCK_SLOT_FILES);
        } else if (explorer_strcmp(filename, "Recycle Bin.shortcut") == 0) {
            explorer_draw_colloid_slot_icon(x + 5, y + 5, EXPLORER_DOCK_SLOT_FILES);
        } else if (explorer_strcmp(filename, "Paint.shortcut") == 0) {
            explorer_draw_colloid_slot_icon(x + 5, y + 5, EXPLORER_DOCK_SLOT_PAINT);
        } else if (explorer_strcmp(filename, "Grapher.shortcut") == 0) {
            explorer_draw_colloid_slot_icon(x + 5, y + 5, EXPLORER_DOCK_SLOT_GRAPHER);
        } else if (explorer_strcmp(filename, "Clock.shortcut") == 0) {
            explorer_draw_colloid_slot_icon(x + 5, y + 5, EXPLORER_DOCK_SLOT_CLOCK);
        } else if (explorer_strcmp(filename, "Browser.shortcut") == 0) {
            explorer_draw_colloid_slot_icon(x + 5, y + 5, EXPLORER_DOCK_SLOT_BROWSER);
        } else if (explorer_strcmp(filename, "Task Manager.shortcut") == 0) {
            explorer_draw_colloid_slot_icon(x + 5, y + 5, EXPLORER_DOCK_SLOT_TASKMAN);
        } else if (explorer_strcmp(filename, "Word Processor.shortcut") == 0) {
            explorer_draw_colloid_slot_icon(x + 5, y + 5, EXPLORER_DOCK_SLOT_WORD);
        } else {
            explorer_draw_colloid_slot_icon(x + 5, y + 5, EXPLORER_DOCK_SLOT_NOTEPAD);
        }
    } else if (explorer_str_ends_with(filename, ".pnt")) {
        explorer_draw_colloid_slot_icon(x + 5, y + 5, EXPLORER_DOCK_SLOT_PAINT);
    } else if (explorer_is_image_file(filename)) {
        char full_path[FAT32_MAX_PATH];
        explorer_strcpy(full_path, current_path);
        if (full_path[explorer_strlen(full_path) - 1] != '/') explorer_strcat(full_path, "/");
        explorer_strcat(full_path, filename);
        draw_image_icon(x + 5, y + 5, full_path);
    } else if (explorer_str_ends_with(filename, ".pdf")) {
        explorer_draw_colloid_slot_icon(x + 5, y + 5, EXPLORER_DOCK_SLOT_WORD);
    } else if (explorer_str_ends_with(filename, ".elf")) {
        char app_path[FAT32_MAX_PATH];
        char icon_path[BOREDOS_APP_METADATA_MAX_IMAGE_PATH];

        explorer_strcpy(app_path, current_path);
        if (app_path[explorer_strlen(app_path) - 1] != '/') explorer_strcat(app_path, "/");
        explorer_strcat(app_path, filename);

        if (!(app_metadata_get_primary_image(app_path, icon_path, sizeof(icon_path)) &&
              draw_icon_path(x + 5, y + 5, icon_path))) {
            explorer_draw_colloid_slot_icon(x + 5, y + 5, EXPLORER_DOCK_SLOT_TERMINAL);
        }
    } else {
        explorer_draw_colloid_slot_icon(x + 5, y + 5, EXPLORER_DOCK_SLOT_NOTEPAD);
    }
}


static void explorer_paint(Window *win) {
    ExplorerState *state = (ExplorerState*)win->data;
    int offset_x = win->x + 4;
    int offset_y = win->y + 20;
    DirtyRect dirty = graphics_get_dirty_rect();
    

    graphics_push_clipping(offset_x, offset_y, win->w - 8, win->h - 28);
    
    draw_rect(offset_x, offset_y, win->w - 8, win->h - 28, COLOR_DARK_BG);
    
    ttf_font_t *ttf_ = graphics_get_current_ttf();

    int path_height = 22;
    int path_x = offset_x;
    int path_w = win->w - 16 - 8;
    draw_rounded_rect_filled(path_x, offset_y + 3, path_w, path_height, 5, COLOR_DARK_PANEL);
    draw_string(path_x + 6, offset_y + 8, "Path:", COLOR_DARK_TEXT);
    int path_label_w = ttf_ ? font_manager_get_string_width(ttf_, "Path:") : 40;
    draw_string(path_x + 6 + path_label_w + 6, offset_y + 8, state->current_path, COLOR_DARK_TEXT);
    
    int dropdown_btn_x = win->x + win->w - 90;
    widget_button_init(&state->btn_dropdown, dropdown_btn_x, offset_y + 3, 35, 22, "...");
    widget_button_init(&state->btn_back, win->x + win->w - 40, offset_y + 3, 30, 22, "<");
    widget_button_init(&state->btn_up, win->x + win->w - 160, offset_y + 3, 30, 22, "^");
    widget_button_init(&state->btn_fwd, win->x + win->w - 125, offset_y + 3, 30, 22, "v");
    
    widget_button_draw(&wm_widget_ctx, &state->btn_dropdown);
    widget_button_draw(&wm_widget_ctx, &state->btn_back);
    widget_button_draw(&wm_widget_ctx, &state->btn_up);
    widget_button_draw(&wm_widget_ctx, &state->btn_fwd);
    
    int content_start_y = offset_y + 30;
    
    graphics_push_clipping(win->x + 4, content_start_y, win->w - 8, win->h - 54 - 4);
    
    for (int i = 0; i < state->item_count; i++) {
        int row = i / EXPLORER_COLS;
        int col = i % EXPLORER_COLS;
        
        if (row < state->explorer_scroll_row) continue;
        if (row >= state->explorer_scroll_row + EXPLORER_ROWS) break;
        
        int item_x = offset_x + 10 + (col * (EXPLORER_ITEM_WIDTH + EXPLORER_PADDING));
        int item_y = content_start_y + ((row - state->explorer_scroll_row) * (EXPLORER_ITEM_HEIGHT + EXPLORER_PADDING));
        
        uint32_t bg_color = (i == state->selected_item) ? 0xFF4A90E2 : COLOR_DARK_PANEL;
        uint32_t text_color = (i == state->selected_item) ? COLOR_WHITE : COLOR_DARK_TEXT;
        draw_rounded_rect_filled(item_x, item_y, EXPLORER_ITEM_WIDTH, EXPLORER_ITEM_HEIGHT, 6, bg_color);
        
        explorer_draw_file_icon(item_x + 5, item_y + 5, state->items[i].is_directory, state->items[i].name, state->current_path);
        
        const char *display_name = state->items[i].name;
        if (explorer_strcmp(state->items[i].name, "RecycleBin") == 0) {
            display_name = "Recycle Bin";
        }
        explorer_draw_icon_label(item_x, item_y, display_name, text_color);
    }
    

    graphics_pop_clipping(); // Pop content clipping
    graphics_pop_clipping(); // Pop main window clipping
    
    if (state->dropdown_menu_visible) {
        int menu_x = dropdown_btn_x;
        int menu_y = offset_y + 26;
        
        draw_rounded_rect_filled(menu_x, menu_y, DROPDOWN_MENU_WIDTH, dropdown_menu_item_height * DROPDOWN_MENU_ITEMS, 6, COLOR_DARK_PANEL);
        
        draw_string(menu_x + 8, menu_y + 5, "New File", COLOR_WHITE);
        draw_string(menu_x + 8, menu_y + dropdown_menu_item_height + 5, "New Folder", COLOR_WHITE);
        draw_string(menu_x + 8, menu_y + dropdown_menu_item_height * 2 + 5, "Delete", COLOR_RED);
    }

    if (state->dialog_state == DIALOG_CREATE_FILE) {
        int dlg_x = win->x + win->w / 2 - 150;
        int dlg_y = win->y + win->h / 2 - 60;
        
        draw_rounded_rect_filled(dlg_x, dlg_y, 300, 110, 8, COLOR_DARK_PANEL);
        
        draw_string(dlg_x + 10, dlg_y + 10, "Create New File", COLOR_WHITE);
        
        widget_textbox_init(&state->dialog_textbox, dlg_x + 10, dlg_y + 35, 280, 25, state->dialog_input, DIALOG_INPUT_MAX);
        state->dialog_textbox.focused = true;
        state->dialog_textbox.cursor_pos = state->dialog_input_cursor;
        widget_textbox_draw(&wm_widget_ctx, &state->dialog_textbox);
        
        widget_button_init(&state->btn_primary, dlg_x + 50, dlg_y + 70, 80, 25, "Create");
        widget_button_init(&state->btn_secondary, dlg_x + 170, dlg_y + 70, 80, 25, "Cancel");
        widget_button_draw(&wm_widget_ctx, &state->btn_primary);
        widget_button_draw(&wm_widget_ctx, &state->btn_secondary);
    } else if (state->dialog_state == DIALOG_CREATE_FOLDER) {
        int dlg_x = win->x + win->w / 2 - 150;
        int dlg_y = win->y + win->h / 2 - 60;
        
        draw_rounded_rect_filled(dlg_x, dlg_y, 300, 110, 8, COLOR_DARK_PANEL);
        
        draw_string(dlg_x + 10, dlg_y + 10, "Create New Folder", COLOR_WHITE);
        
        widget_textbox_init(&state->dialog_textbox, dlg_x + 10, dlg_y + 35, 280, 25, state->dialog_input, DIALOG_INPUT_MAX);
        state->dialog_textbox.focused = true;
        state->dialog_textbox.cursor_pos = state->dialog_input_cursor;
        widget_textbox_draw(&wm_widget_ctx, &state->dialog_textbox);
        
        widget_button_init(&state->btn_primary, dlg_x + 50, dlg_y + 70, 80, 25, "Create");
        widget_button_init(&state->btn_secondary, dlg_x + 170, dlg_y + 70, 80, 25, "Cancel");
        widget_button_draw(&wm_widget_ctx, &state->btn_primary);
        widget_button_draw(&wm_widget_ctx, &state->btn_secondary);
    } else if (state->dialog_state == DIALOG_DELETE_CONFIRM) {
        int dlg_x = win->x + win->w / 2 - 150;
        int dlg_y = win->y + win->h / 2 - 60;
        
        draw_rounded_rect_filled(dlg_x, dlg_y, 300, 110, 8, COLOR_DARK_PANEL);
        
        const char *title = state->dialog_target_is_dir ? "Delete Folder?" : "Delete File?";
        draw_string(dlg_x + 10, dlg_y + 10, title, COLOR_WHITE);
        
        if (explorer_str_starts_with(state->current_path, "/RecycleBin")) {
            draw_string(dlg_x + 10, dlg_y + 35, "This action cannot be undone.", 0xFFAAAAAA);
            draw_string(dlg_x + 10, dlg_y + 48, "Delete forever?", 0xFFAAAAAA);
        } else {
            draw_string(dlg_x + 10, dlg_y + 35, "This file will be moved to", 0xFFAAAAAA);
            draw_string(dlg_x + 10, dlg_y + 45, "the recycle bin.", 0xFFAAAAAA);
        }
        draw_rounded_rect_filled(dlg_x + 50, dlg_y + 65, 80, 25, 6, 0xFF8B2020);
        draw_string(dlg_x + 68, dlg_y + 72, "Delete", COLOR_WHITE);
        
        // Use libwidget only for the cancel button, or do we want to use libwidget for the delete button too?
        // Let's use libwidget but the delete button needs red styling so maybe just keep it manual or make it secondary.
        // Actually wait, I will use libwidget for both and let the text dictate the action, we can't style individual buttons yet.
        widget_button_init(&state->btn_primary, dlg_x + 50, dlg_y + 70, 80, 25, "Delete");
        widget_button_init(&state->btn_secondary, dlg_x + 170, dlg_y + 70, 80, 25, "Cancel");
        widget_button_draw(&wm_widget_ctx, &state->btn_primary);
        widget_button_draw(&wm_widget_ctx, &state->btn_secondary);
    } else if (state->dialog_state == DIALOG_REPLACE_CONFIRM) {
        int dlg_x = win->x + win->w / 2 - 150;
        int dlg_y = win->y + win->h / 2 - 60;
        
        draw_rounded_rect_filled(dlg_x, dlg_y, 300, 110, 8, COLOR_DARK_PANEL);
        
        draw_string(dlg_x + 10, dlg_y + 10, "File Exists", COLOR_WHITE);
        
        draw_string(dlg_x + 10, dlg_y + 35, "Replace existing file?", 0xFFAAAAAA);
        draw_string(dlg_x + 10, dlg_y + 48, "This cannot be undone.", 0xFFAAAAAA);
        
        widget_button_init(&state->btn_primary, dlg_x + 50, dlg_y + 70, 80, 25, "Replace");
        widget_button_init(&state->btn_secondary, dlg_x + 170, dlg_y + 70, 80, 25, "Cancel");
        widget_button_draw(&wm_widget_ctx, &state->btn_primary);
        widget_button_draw(&wm_widget_ctx, &state->btn_secondary);
    } else if (state->dialog_state == DIALOG_REPLACE_MOVE_CONFIRM) {
        int dlg_x = win->x + win->w / 2 - 150;
        int dlg_y = win->y + win->h / 2 - 60;
        
        draw_rounded_rect_filled(dlg_x, dlg_y, 300, 110, 8, COLOR_DARK_PANEL);
        
        draw_string(dlg_x + 10, dlg_y + 10, "File Exists", COLOR_WHITE);
        
        draw_string(dlg_x + 10, dlg_y + 35, "Replace existing file?", 0xFFAAAAAA);
        draw_string(dlg_x + 10, dlg_y + 48, "This cannot be undone.", 0xFFAAAAAA);
        
        draw_rounded_rect_filled(dlg_x + 50, dlg_y + 70, 80, 25, 6, COLOR_DARK_BORDER);
        draw_string(dlg_x + 63, dlg_y + 77, "Replace", COLOR_WHITE);
        draw_rounded_rect_filled(dlg_x + 170, dlg_y + 70, 80, 25, 6, COLOR_DARK_BORDER);
        draw_string(dlg_x + 185, dlg_y + 77, "Cancel", COLOR_WHITE);
    } else if (state->dialog_state == DIALOG_CREATE_REPLACE_CONFIRM) {
        int dlg_x = win->x + win->w / 2 - 150;
        int dlg_y = win->y + win->h / 2 - 60;
        
        draw_rounded_rect_filled(dlg_x, dlg_y, 300, 110, 8, COLOR_DARK_PANEL);
        
        draw_string(dlg_x + 10, dlg_y + 10, "File Exists", COLOR_WHITE);
        
        draw_string(dlg_x + 10, dlg_y + 35, "Overwrite existing file?", 0xFFAAAAAA);
        draw_string(dlg_x + 10, dlg_y + 48, "This cannot be undone.", 0xFFAAAAAA);
        
        widget_button_init(&state->btn_primary, dlg_x + 50, dlg_y + 70, 80, 25, "Overwrite");
        widget_button_init(&state->btn_secondary, dlg_x + 170, dlg_y + 70, 80, 25, "Cancel");
        widget_button_draw(&wm_widget_ctx, &state->btn_primary);
        widget_button_draw(&wm_widget_ctx, &state->btn_secondary);
    } else if (state->dialog_state == DIALOG_ERROR) {
        int dlg_x = win->x + win->w / 2 - 150;
        int dlg_y = win->y + win->h / 2 - 60;
        
        draw_rounded_rect_filled(dlg_x, dlg_y, 300, 110, 8, COLOR_DARK_PANEL);
        
        draw_string(dlg_x + 10, dlg_y + 10, "Error", 0xFFFF6B6B);
        draw_string(dlg_x + 10, dlg_y + 40, state->dialog_input, 0xFFAAAAAA);
        
        widget_button_init(&state->btn_primary, dlg_x + 110, dlg_y + 70, 80, 25, "OK");
        widget_button_draw(&wm_widget_ctx, &state->btn_primary);
    } else if (state->dialog_state == DIALOG_RENAME) {
        int dlg_x = win->x + win->w / 2 - 150;
        int dlg_y = win->y + win->h / 2 - 60;
        
        draw_rounded_rect_filled(dlg_x, dlg_y, 300, 110, 8, COLOR_DARK_PANEL);
        draw_string(dlg_x + 10, dlg_y + 10, "Rename", COLOR_WHITE);
        widget_textbox_init(&state->dialog_textbox, dlg_x + 10, dlg_y + 35, 280, 25, state->dialog_input, DIALOG_INPUT_MAX);
        state->dialog_textbox.focused = true;
        state->dialog_textbox.cursor_pos = state->dialog_input_cursor;
        widget_textbox_draw(&wm_widget_ctx, &state->dialog_textbox);
        
        widget_button_init(&state->btn_primary, dlg_x + 50, dlg_y + 70, 80, 25, "Rename");
        widget_button_init(&state->btn_secondary, dlg_x + 170, dlg_y + 70, 80, 25, "Cancel");
        widget_button_draw(&wm_widget_ctx, &state->btn_primary);
        widget_button_draw(&wm_widget_ctx, &state->btn_secondary);
    }
    
    if (state->file_context_menu_visible) {
        int menu_screen_x = win->x + state->file_context_menu_x;
        int menu_screen_y = win->y + state->file_context_menu_y + 20;
        
        ExplorerContextItem menu_items[20];
        int count = explorer_build_context_menu(win, menu_items);
        
        int menu_height = 0;
        for (int i = 0; i < count; i++) {
            if (menu_items[i].action_id == 0) menu_height += 5; 
            else menu_height += CONTEXT_MENU_ITEM_HEIGHT;
        }
        
        draw_rounded_rect_filled(menu_screen_x, menu_screen_y, FILE_CONTEXT_MENU_WIDTH, menu_height, 8, COLOR_DARK_PANEL);
        
        int y_offset = 0;
        for (int i = 0; i < count; i++) {
            if (menu_items[i].action_id == 0) {
                draw_rect(menu_screen_x + 8, menu_screen_y + y_offset + 3, FILE_CONTEXT_MENU_WIDTH - 16, 1, COLOR_DARK_BORDER);
                y_offset += 5;
            } else {
                draw_string(menu_screen_x + 10, menu_screen_y + y_offset + 6, menu_items[i].label, menu_items[i].color);
                y_offset += CONTEXT_MENU_ITEM_HEIGHT;
            }
        }
    }
}


#define WIDGET_CLICKED(btn, cx, cy) ((cx) >= (btn)->x && (cx) < (btn)->x + (btn)->w && (cy) >= (btn)->y && (cy) < (btn)->y + (btn)->h)
#define TEXTBOX_CLICKED(tb, cx, cy) ((cx) >= (tb)->x && (cx) < (tb)->x + (tb)->w && (cy) >= (tb)->y && (cy) < (tb)->y + (tb)->h)

static void explorer_handle_click(Window *win, int x, int y) {
    ExplorerState *state = (ExplorerState*)win->data;
    if (state->file_context_menu_visible) {
        explorer_handle_file_context_menu_click(win, x, y);
        return;
    }
    
    if (state->dialog_state == DIALOG_CREATE_FILE || state->dialog_state == DIALOG_CREATE_FOLDER) {
        if (WIDGET_CLICKED(&state->btn_primary, win->x + x, win->y + y + 20)) {
            state->btn_primary.pressed = false;
            if (state->dialog_state == DIALOG_CREATE_FILE) dialog_confirm_create_file(win);
            else dialog_confirm_create_folder(win);
            return;
        }
        
        if (WIDGET_CLICKED(&state->btn_secondary, win->x + x, win->y + y)) {
            state->btn_secondary.pressed = false;
            dialog_close(win);
            return;
        }
        
        if (TEXTBOX_CLICKED(&state->dialog_textbox, win->x + x, win->y + y)) {
            state->dialog_input_cursor = (win->x + x - state->dialog_textbox.x - 5) / 8;
            if (state->dialog_input_cursor > (int)explorer_strlen(state->dialog_input)) {
                state->dialog_input_cursor = explorer_strlen(state->dialog_input);
            }
            if (state->dialog_input_cursor < 0) state->dialog_input_cursor = 0;
            return;
        }
        return; 
    } else if (state->dialog_state == DIALOG_DELETE_CONFIRM) {
        if (WIDGET_CLICKED(&state->btn_primary, win->x + x, win->y + y + 20)) {
            state->btn_primary.pressed = false;
            dialog_confirm_delete(win);
            return;
        }
        
        if (WIDGET_CLICKED(&state->btn_secondary, win->x + x, win->y + y)) {
            state->btn_secondary.pressed = false;
            dialog_close(win);
            return;
        }
        return; 
    } else if (state->dialog_state == DIALOG_REPLACE_CONFIRM) {
        if (WIDGET_CLICKED(&state->btn_primary, win->x + x, win->y + y + 20)) {
            state->btn_primary.pressed = false;
            dialog_confirm_replace(win);
            return;
        }
        
        if (WIDGET_CLICKED(&state->btn_secondary, win->x + x, win->y + y + 20)) {
            state->btn_secondary.pressed = false;
            dialog_close(win);
            return;
        }
        return;
    } else if (state->dialog_state == DIALOG_REPLACE_MOVE_CONFIRM) {
        if (WIDGET_CLICKED(&state->btn_primary, win->x + x, win->y + y + 20)) {
            state->btn_primary.pressed = false;
            dialog_confirm_replace_move(win);
            return;
        }
        
        if (WIDGET_CLICKED(&state->btn_secondary, win->x + x, win->y + y + 20)) {
            state->btn_secondary.pressed = false;
            dialog_close(win);
            return;
        }
        return;
    } else if (state->dialog_state == DIALOG_CREATE_REPLACE_CONFIRM) {
        if (WIDGET_CLICKED(&state->btn_primary, win->x + x, win->y + y + 20)) {
            state->btn_primary.pressed = false;
            dialog_force_create_file(win);
            return;
        }
        
        if (WIDGET_CLICKED(&state->btn_secondary, win->x + x, win->y + y + 20)) {
            state->btn_secondary.pressed = false;
            dialog_close(win);
            return;
        }
        return;
    } else if (state->dialog_state == DIALOG_ERROR) {
        if (WIDGET_CLICKED(&state->btn_primary, win->x + x, win->y + y + 20)) {
            state->btn_primary.pressed = false;
            dialog_close(win);
            return;
        }
        return;
    } else if (state->dialog_state == DIALOG_RENAME) {
        if (WIDGET_CLICKED(&state->btn_primary, win->x + x, win->y + y + 20)) {
            state->btn_primary.pressed = false;
            char new_path[FAT32_MAX_PATH];
            explorer_strcpy(new_path, state->current_path);
            if (new_path[explorer_strlen(new_path)-1] != '/') explorer_strcat(new_path, "/");
            explorer_strcat(new_path, state->dialog_input);
            
            if (vfs_rename(state->dialog_target_path, new_path)) explorer_refresh_all();
            dialog_close(win);
            return;
        }
        
        if (WIDGET_CLICKED(&state->btn_secondary, win->x + x, win->y + y + 20)) {
            state->btn_secondary.pressed = false;
            dialog_close(win);
            return;
        }
        if (TEXTBOX_CLICKED(&state->dialog_textbox, win->x + x, win->y + y + 20)) {
            state->dialog_input_cursor = (win->x + x - state->dialog_textbox.x - 5) / 8;
            if (state->dialog_input_cursor > (int)explorer_strlen(state->dialog_input)) {
                state->dialog_input_cursor = explorer_strlen(state->dialog_input);
            }
            if (state->dialog_input_cursor < 0) state->dialog_input_cursor = 0;
            return;
        }
        return; 
    }
    

    
    if (state->dropdown_menu_visible) {
        int dropdown_btn_x = win->w - 90;  
        int menu_y = 26;  
        
        if (x >= dropdown_btn_x && x < dropdown_btn_x + DROPDOWN_MENU_WIDTH &&
            y >= menu_y && y < menu_y + dropdown_menu_item_height) {
            dropdown_menu_toggle(win);
            dialog_open_create_file(win, state->current_path);
            return;
        }
        
        if (x >= dropdown_btn_x && x < dropdown_btn_x + DROPDOWN_MENU_WIDTH &&
            y >= menu_y + dropdown_menu_item_height && 
            y < menu_y + dropdown_menu_item_height * 2) {
            dropdown_menu_toggle(win);
            dialog_open_create_folder(win, state->current_path);
            return;
        }
        
        if (x >= dropdown_btn_x && x < dropdown_btn_x + DROPDOWN_MENU_WIDTH &&
            y >= menu_y + dropdown_menu_item_height * 2 && 
            y < menu_y + dropdown_menu_item_height * 3) {
            dropdown_menu_toggle(win);
            if (state->selected_item >= 0) {
                dialog_open_delete_confirm(win, state->selected_item);
            }
            return;
        }
        
        dropdown_menu_toggle(win);
        return;
    }
    
    

    
    if (WIDGET_CLICKED(&state->btn_dropdown, win->x + x, win->y + y + 20)) {
        state->btn_dropdown.pressed = false;
        dropdown_menu_toggle(win);
        return;
    }
    
    if (WIDGET_CLICKED(&state->btn_back, win->x + x, win->y + y + 20)) {
        state->btn_back.pressed = false;
        explorer_navigate_to(win, "..");
        return;
    }
    
    if (WIDGET_CLICKED(&state->btn_up, win->x + x, win->y + y + 20)) {
        state->btn_up.pressed = false;
        if (state->explorer_scroll_row > 0) state->explorer_scroll_row--;
        return;
    }
    
    if (WIDGET_CLICKED(&state->btn_fwd, win->x + x, win->y + y + 20)) {
        state->btn_fwd.pressed = false;
        int total_rows = (state->item_count + EXPLORER_COLS - 1) / EXPLORER_COLS;
        if (total_rows == 0) total_rows = 1;
        if (state->explorer_scroll_row < total_rows - (EXPLORER_ROWS - 1)) state->explorer_scroll_row++;
        return;
    }
    
    int content_start_y = 30;
    int offset_x = 4;
    
    for (int i = 0; i < state->item_count; i++) {
        int row = i / EXPLORER_COLS;
        int col = i % EXPLORER_COLS;
        
        if (row < state->explorer_scroll_row) continue;
        if (row >= state->explorer_scroll_row + EXPLORER_ROWS) break;
        
        int item_x = offset_x + 10 + (col * (EXPLORER_ITEM_WIDTH + EXPLORER_PADDING));
        int item_y = content_start_y + ((row - state->explorer_scroll_row) * (EXPLORER_ITEM_HEIGHT + EXPLORER_PADDING));
        
        if (x >= item_x && x < item_x + EXPLORER_ITEM_WIDTH &&
            y >= item_y && y < item_y + EXPLORER_ITEM_HEIGHT) {
            
            if (state->last_clicked_item == i) {
                explorer_open_item(win, i);
                state->last_clicked_item = -1;
            } else {
                state->selected_item = i;
                state->last_clicked_item = i;
                state->last_click_time = 0;  
            }
            return;
        }
    }
}

static void explorer_handle_key(Window *win, char c, bool pressed) {
    if (!pressed) return;
    ExplorerState *state = (ExplorerState*)win->data;
    
    if (state->dialog_state == DIALOG_CREATE_FILE || state->dialog_state == DIALOG_CREATE_FOLDER || state->dialog_state == DIALOG_RENAME) {
        if (c == 27) {
            dialog_close(win);
            return;
        } else if (c == '\n') {
            if (state->dialog_state == DIALOG_CREATE_FILE) {
                dialog_confirm_create_file(win);
            } else if (state->dialog_state == DIALOG_CREATE_FOLDER) {
                dialog_confirm_create_folder(win);
            } else if (state->dialog_state == DIALOG_RENAME) {
                char new_path[FAT32_MAX_PATH];
                explorer_strcpy(new_path, state->current_path);
                if (new_path[explorer_strlen(new_path)-1] != '/') explorer_strcat(new_path, "/");
                explorer_strcat(new_path, state->dialog_input);
                if (vfs_rename(state->dialog_target_path, new_path)) explorer_refresh(win);
                dialog_close(win);
            }
        } else if (c == 19) {
            if (state->dialog_input_cursor > 0) state->dialog_input_cursor--;
        } else if (c == 20) {
            if (state->dialog_input_cursor < (int)explorer_strlen(state->dialog_input)) state->dialog_input_cursor++;
        } else if (c == 8 || c == 127) {
            if (state->dialog_input_cursor > 0) {
                state->dialog_input_cursor--;
                for (int i = state->dialog_input_cursor; i < (int)explorer_strlen(state->dialog_input); i++) {
                    state->dialog_input[i] = state->dialog_input[i + 1];
                }
            }
        } else if (c >= 32 && c < 127) {
            int len = explorer_strlen(state->dialog_input);
            if (len < DIALOG_INPUT_MAX - 1) {
                for (int i = len; i >= state->dialog_input_cursor; i--) {
                    state->dialog_input[i + 1] = state->dialog_input[i];
                }
                state->dialog_input[state->dialog_input_cursor] = c;
                state->dialog_input_cursor++;
            }
        }
        wm_mark_dirty(win->x, win->y, win->w, win->h);
        return;
    }
    
    if (state->dialog_state == DIALOG_DELETE_CONFIRM) {
        if (c == 27) {  // ESC
            dialog_close(win);
            return;
        }
        return;
    } else if (state->dialog_state == DIALOG_REPLACE_CONFIRM) {
        if (c == 27) { // ESC
            dialog_close(win);
        } else if (c == '\n') { // Enter
            dialog_confirm_replace(win);
        }
        return;
    } else if (state->dialog_state == DIALOG_REPLACE_MOVE_CONFIRM) {
        if (c == 27) { // ESC
            dialog_close(win);
        } else if (c == '\n') { // Enter
            dialog_confirm_replace_move(win);
        }
        return;
    } else if (state->dialog_state == DIALOG_CREATE_REPLACE_CONFIRM) {
        if (c == 27) { // ESC
            dialog_close(win);
        } else if (c == '\n') { // Enter
            dialog_force_create_file(win);
        }
        return;
    } else if (state->dialog_state == DIALOG_ERROR) {
        if (c == 27 || c == '\n') {
            dialog_close(win);
        }
        return;
    }
    
    if (state->dropdown_menu_visible && c == 27) {
        dropdown_menu_toggle(win);
        return;
    }
    
    if (c == 17) {
        if (state->selected_item > 0) {
            state->selected_item -= EXPLORER_COLS;
            if (state->selected_item < 0) state->selected_item = 0;
            // Scroll if needed
            int row = state->selected_item / EXPLORER_COLS;
            if (row < state->explorer_scroll_row) state->explorer_scroll_row = row;
        }
    } else if (c == 18) {  // DOWN
        if (state->selected_item < state->item_count - 1) {
            state->selected_item += EXPLORER_COLS;
            if (state->selected_item >= state->item_count) state->selected_item = state->item_count - 1;
            int row = state->selected_item / EXPLORER_COLS;
            if (row >= state->explorer_scroll_row + (EXPLORER_ROWS - 1)) state->explorer_scroll_row = row - (EXPLORER_ROWS - 1) + 1;
        }
    } else if (c == 19) {  // LEFT
        if (state->selected_item > 0) {
            state->selected_item--;
        }
    } else if (c == 20) {  // RIGHT
        if (state->selected_item < state->item_count - 1) {
            state->selected_item++;
        }
    } else if (c == '\n') {  // ENTER
        if (state->selected_item >= 0 && state->selected_item < state->item_count) {
            if (state->items[state->selected_item].is_directory) {
                explorer_open_item(win, state->selected_item);
            }
        }
    } else if (c == 'd' || c == 'D') {  
        if (state->selected_item >= 0) {
            dialog_open_delete_confirm(win, state->selected_item);
        }
    } else if (c == 'n' || c == 'N') {  
        dialog_open_create_file(win, state->current_path);
    } else if (c == 'f' || c == 'F') {   // heya coder, i made these shortcuts.. idk if they're any good, just make a pull request to change them
                                        // if you have a better idea lmao
        dialog_open_create_folder(win, state->current_path);
    }
}


static void explorer_handle_right_click(Window *win, int x, int y) {
    ExplorerState *state = (ExplorerState*)win->data;
    int content_start_y = 30;
    int offset_x = 4;
    
    for (int i = 0; i < state->item_count; i++) {
        int row = i / EXPLORER_COLS;
        int col = i % EXPLORER_COLS;
        
        if (row < state->explorer_scroll_row) continue;
        if (row >= state->explorer_scroll_row + EXPLORER_ROWS) break;
        
        int item_x = offset_x + 10 + (col * (EXPLORER_ITEM_WIDTH + EXPLORER_PADDING));
        int item_y = content_start_y + ((row - state->explorer_scroll_row) * (EXPLORER_ITEM_HEIGHT + EXPLORER_PADDING));
        
        if (x >= item_x && x < item_x + EXPLORER_ITEM_WIDTH &&
            y >= item_y && y < item_y + EXPLORER_ITEM_HEIGHT) {
            
            state->file_context_menu_visible = true;
            state->file_context_menu_item = i;
            state->file_context_menu_x = x;
            state->file_context_menu_y = y;
            return;
        }
    }
    
    state->file_context_menu_visible = true;
    state->file_context_menu_item = -1; 
    state->file_context_menu_x = x;
    state->file_context_menu_y = y;
}

static void explorer_handle_file_context_menu_click(Window *win, int x, int y) {
    ExplorerState *state = (ExplorerState*)win->data;
    
    if (!state->file_context_menu_visible) {
        return;
    }
    
    int relative_x = x - state->file_context_menu_x;
    int relative_y = y - state->file_context_menu_y;
    
    ExplorerContextItem menu_items[20];
    int count = explorer_build_context_menu(win, menu_items);
    int menu_height = 0;
    for (int i = 0; i < count; i++) {
        if (menu_items[i].action_id == 0) menu_height += 5; else menu_height += CONTEXT_MENU_ITEM_HEIGHT;
    }
    
    if (relative_x < 0 || relative_x > FILE_CONTEXT_MENU_WIDTH ||
        relative_y < 0 || relative_y > menu_height) {
        state->file_context_menu_visible = false;
        state->file_context_menu_item = -1;
        return;
    }
    
    int current_y = 0;
    int clicked_action = 0;
    
    for (int i = 0; i < count; i++) {
        int h = (menu_items[i].action_id == 0) ? 5 : CONTEXT_MENU_ITEM_HEIGHT;
        if (relative_y >= current_y && relative_y < current_y + h) {
            if (menu_items[i].enabled && menu_items[i].action_id != 0) {
                clicked_action = menu_items[i].action_id;
            }
            break;
        }
        current_y += h;
    }
    
    if (clicked_action == 0) return;
    
    char full_path[FAT32_MAX_PATH];
    if (state->file_context_menu_item >= 0) {
        explorer_strcpy(full_path, state->current_path);
        if (full_path[explorer_strlen(full_path) - 1] != '/') explorer_strcat(full_path, "/");
        explorer_strcat(full_path, state->items[state->file_context_menu_item].name);
    }
    
    if (clicked_action == 100) { // Open
        explorer_open_item(win, state->file_context_menu_item);
    } else if (clicked_action == 109) { // Open MD
        explorer_open_item(win, state->file_context_menu_item);
    } else if (clicked_action == 101) { // New File
        if (state->file_context_menu_item >= 0 && state->items[state->file_context_menu_item].is_directory) {
            dialog_open_create_file(win, full_path);
        } else {
            dialog_open_create_file(win, state->current_path);
        }
    } else if (clicked_action == 102) { // New Folder
        if (state->file_context_menu_item >= 0 && state->items[state->file_context_menu_item].is_directory) {
            dialog_open_create_folder(win, full_path);
        } else {
            dialog_open_create_folder(win, state->current_path);
        }
    } else if (clicked_action == 103) { // Paste
        if (state->file_context_menu_item >= 0 && state->items[state->file_context_menu_item].is_directory) {
            explorer_clipboard_paste(win, full_path);
        } else {
            explorer_clipboard_paste(win, state->current_path);
        }
    } else if (clicked_action == 104) { // Cut
        explorer_clipboard_cut(full_path);
    } else if (clicked_action == 105) { // Copy
        explorer_clipboard_copy(full_path);
    } else if (clicked_action == 106) { // Delete
        dialog_open_delete_confirm(win, state->file_context_menu_item);
    } else if (clicked_action == 111) { // Rename
        state->dialog_state = DIALOG_RENAME;
        explorer_strcpy(state->dialog_input, state->items[state->file_context_menu_item].name);
        state->dialog_input_cursor = explorer_strlen(state->dialog_input);
        explorer_strcpy(state->dialog_target_path, full_path);
    } else if (clicked_action == 110) { // Open with Text Editor
        process_create_elf("/bin/txtedit.elf", full_path, false, -1);
    } else if (clicked_action == ACTION_RESTORE) {
        explorer_restore_file(win, state->file_context_menu_item);
    } else if (clicked_action == ACTION_CREATE_SHORTCUT) {
        explorer_create_shortcut(win, full_path);
    } else if (clicked_action == 112) { // Open in new window
        explorer_create_window(full_path);
    }
    
    state->file_context_menu_visible = false;
    state->file_context_menu_item = -1;
}


bool explorer_get_file_at(int screen_x, int screen_y, char *out_path, bool *is_dir) {
    for (int w = 0; w < explorer_win_count; w++) {
        Window *win = explorer_wins[w];
        if (!win->visible) continue;
        
        ExplorerState *state = (ExplorerState*)win->data;
        int rel_x = screen_x - win->x;
        int rel_y = screen_y - win->y;
        
        if (rel_x < 4 || rel_x > win->w - 4 || rel_y < 54 || rel_y > win->h - 4) continue;
        
        int content_start_y = 54;
        int offset_x = 4;
        
        for (int i = 0; i < state->item_count; i++) {
            int row = i / EXPLORER_COLS;
            int col = i % EXPLORER_COLS;
            if (row < state->explorer_scroll_row) continue;
            if (row >= state->explorer_scroll_row + EXPLORER_ROWS) break;
            
            int item_x = offset_x + 10 + (col * (EXPLORER_ITEM_WIDTH + EXPLORER_PADDING));
            int item_y = content_start_y + ((row - state->explorer_scroll_row) * (EXPLORER_ITEM_HEIGHT + EXPLORER_PADDING));
            
            if (rel_x >= item_x && rel_x < item_x + EXPLORER_ITEM_WIDTH &&
                rel_y >= item_y && rel_y < item_y + EXPLORER_ITEM_HEIGHT) {
                explorer_strcpy(out_path, state->current_path);
                if (out_path[explorer_strlen(out_path) - 1] != '/') explorer_strcat(out_path, "/");
                explorer_strcat(out_path, state->items[i].name);
                *is_dir = state->items[i].is_directory;
                return true;
            }
        }
    }
    return false;
}

void explorer_clear_click_state(Window *win) {
    ExplorerState *state = (ExplorerState*)win->data;
    state->last_clicked_item = -1;
}

void explorer_refresh(Window *win) {
    if (!win) return;
    ExplorerState *state = (ExplorerState*)win->data;
    explorer_load_directory(win, state->current_path);
    wm_mark_dirty(win->x, win->y, win->w, win->h);
}

void explorer_refresh_all(void) {
    for (int i = 0; i < explorer_win_count; i++) {
        explorer_refresh(explorer_wins[i]);
    }
    wm_refresh_desktop();
}

static void explorer_perform_move_internal(Window *win, const char *source_path, const char *dest_dir) {
    (void)win;
    char filename[FAT32_MAX_FILENAME];
    int len = explorer_strlen(source_path);
    int i = len - 1;
    while (i >= 0 && source_path[i] != '/') i--;
    int j = 0;
    for (int k = i + 1; k < len; k++) filename[j++] = source_path[k];
    filename[j] = 0;
    
    char dest_path[FAT32_MAX_PATH];
    explorer_strcpy(dest_path, dest_dir);
    if (dest_path[explorer_strlen(dest_path) - 1] != '/') {
        explorer_strcat(dest_path, "/");
    }
    explorer_strcat(dest_path, filename);
    
    if (explorer_strcmp(source_path, dest_path) == 0) {
        return;
    }
    
    if (explorer_str_starts_with(dest_path, "/RecycleBin") && !explorer_str_starts_with(source_path, "/RecycleBin")) {
        char origin_path[FAT32_MAX_PATH];
        explorer_strcpy(origin_path, dest_path);
        explorer_strcat(origin_path, ".origin");
        vfs_file_t *fh = vfs_open(origin_path, "w");
        if (fh) {
            vfs_write(fh, source_path, explorer_strlen(source_path));
            vfs_close(fh);
        }
    }

    if (!explorer_str_starts_with(dest_path, "/RecycleBin") && explorer_str_starts_with(source_path, "/RecycleBin")) {
        char origin_path[FAT32_MAX_PATH];
        explorer_strcpy(origin_path, source_path);
        explorer_strcat(origin_path, ".origin");
        vfs_delete(origin_path);
    }
    if (!vfs_rename(source_path, dest_path)) {
        if (explorer_copy_recursive(source_path, dest_path)) {
            explorer_delete_permanently(source_path);
        }
    }
        
    explorer_refresh_all();
}

void explorer_import_file_to(Window *win, const char *source_path, const char *dest_dir) {
    ExplorerState *state = (ExplorerState*)win->data;

    // Prevent moving a directory into itself or its subdirectories
    if (explorer_str_starts_with(dest_dir, source_path)) {
        int src_len = explorer_strlen(source_path);
        if (dest_dir[src_len] == '\0' || dest_dir[src_len] == '/') {
            return;
        }
    }

    char filename[FAT32_MAX_FILENAME];
    int len = explorer_strlen(source_path);
    int i = len - 1;
    while (i >= 0 && source_path[i] != '/') i--;
    int j = 0;
    for (int k = i + 1; k < len; k++) filename[j++] = source_path[k];
    filename[j] = 0;
    
    char dest_path[FAT32_MAX_PATH];
    explorer_strcpy(dest_path, dest_dir);
    if (dest_path[explorer_strlen(dest_path) - 1] != '/') explorer_strcat(dest_path, "/");
    explorer_strcat(dest_path, filename);
    
    if (vfs_exists(dest_path) && explorer_strcmp(source_path, dest_path) != 0) {
        explorer_strcpy(state->dialog_move_src, source_path);
        explorer_strcpy(state->dialog_dest_dir, dest_dir);
        state->dialog_state = DIALOG_REPLACE_MOVE_CONFIRM;
        return;
    }
    
    explorer_perform_move_internal(win, source_path, dest_dir);
}

void explorer_import_file(Window *win, const char *source_path) {
    ExplorerState *state = (ExplorerState*)win->data;
    explorer_import_file_to(win, source_path, state->current_path);
}


Window* explorer_create_window(const char *path) {
    if (explorer_win_count >= 10) return NULL;
    
    Window *win = (Window*)kmalloc(sizeof(Window));
    ExplorerState *state = (ExplorerState*)kmalloc(sizeof(ExplorerState));
    if (!state || !win) return NULL;
    mem_memset(state, 0, sizeof(ExplorerState));
    
    win->title = "Files";
    win->x = 300 + (explorer_win_count * 30);
    win->y = 100 + (explorer_win_count * 30);
    win->w = 600;
    win->h = 400;
    win->visible = false;
    win->focused = false;
    win->z_index = 10;
    win->paint = explorer_paint;
    win->handle_key = explorer_handle_key;
    win->handle_click = explorer_handle_click;
    win->handle_right_click = explorer_handle_right_click;
    win->data = state;
    
    state->items = NULL;
    state->items_capacity = 0;
    state->selected_item = -1;
    state->last_clicked_item = -1;
    state->explorer_scroll_row = 0;
    state->dialog_state = DIALOG_NONE;
    
    if (explorer_strcmp(path, "/") == 0) explorer_load_directory(win, "/");
    else explorer_load_directory(win, path);
    
    explorer_wins[explorer_win_count++] = win;
    wm_add_window_locked(win);
    // wm_add_window_locked already calls wm_bring_to_front_locked!
    
    return win;
}

void explorer_init(void) {
    ExplorerState *state = (ExplorerState*)kmalloc(sizeof(ExplorerState));
    if (!state) return;
    mem_memset(state, 0, sizeof(ExplorerState));

    win_explorer.title = "Files";
    win_explorer.x = 300;
    win_explorer.y = 100;
    win_explorer.w = 600;
    win_explorer.h = 400;
    win_explorer.visible = false;
    win_explorer.focused = false;
    win_explorer.z_index = 0;
    win_explorer.paint = explorer_paint;
    win_explorer.handle_key = explorer_handle_key;
    win_explorer.handle_click = explorer_handle_click;
    win_explorer.handle_right_click = explorer_handle_right_click;
    win_explorer.data = state;
    
    state->items = NULL;
    state->items_capacity = 0;
    state->selected_item = -1;
    state->last_clicked_item = -1;
    state->explorer_scroll_row = 0;
    state->dialog_state = DIALOG_NONE;

    explorer_wins[explorer_win_count++] = &win_explorer;
    explorer_load_directory(&win_explorer, "/");
}
void explorer_reset(void) {
    ExplorerState *state = (ExplorerState*)win_explorer.data;
    explorer_load_directory(&win_explorer, "/");
    state->explorer_scroll_row = 0;
    win_explorer.focused = false;
}
