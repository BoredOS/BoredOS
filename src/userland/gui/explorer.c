// Copyright (c) 2026 Lluciocc (https://github.com/lluciocc)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
// BOREDOS_APP_DESC: modern minimal userland file manager
// BOREDOS_APP_ICONS: /Library/images/icons/colloid/file-manager.png;/Library/images/icons/colloid/folder.png

// todo: refresh desktop when the FS moves

#include "stb_image.h"
#include "libc/input.h"
#include "libc/libui.h"
#include "libc/stdint.h"
#include "libc/stdlib.h"
#include "libc/string.h"
#include "libc/syscall.h"
#include "libc/syscall_user.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define EXPLORER_MAX_ITEMS 256
#define EXPLORER_MAX_PATH 256
#define EXPLORER_MAX_NAME 256
#define EXPLORER_MAX_HISTORY 32
#define EXPLORER_MAX_SIDEBAR 32
#define EXPLORER_DIALOG_INPUT_MAX 192

#define WIN_DEFAULT_W 920
#define WIN_DEFAULT_H 620
#define HEADER_H 64
#define SIDEBAR_W 220
#define SIDEBAR_ROW_H 38
#define SIDEBAR_PAD 12
#define GRID_PAD 24
#define CELL_W 132
#define CELL_H 116
#define ITEM_W 112
#define ITEM_H 104
#define ICON_SIZE 48
#define SIDE_ICON_SIZE 24
#define FOLDER_ICON_CACHE_SIZE 16

#define COLOR_BG        0xFF1B1D21
#define COLOR_HEADER    0xFF24262B
#define COLOR_SIDEBAR   0xFF202227
#define COLOR_PANEL     0xFF2A2D33
#define COLOR_FIELD     0xFF191B1F
#define COLOR_HOVER     0xFF303640
#define COLOR_SELECTED  0xFF234963
#define COLOR_ACCENT    0xFF62A0EA
#define COLOR_ACCENT_2  0xFF3584E4
#define COLOR_BORDER    0xFF3A3F47
#define COLOR_TEXT      0xFFEDEFF2
#define COLOR_MUTED     0xFFA7ADB7
#define COLOR_DIM       0xFF777D86
#define COLOR_DANGER    0xFFE57373
#define COLOR_FOLDER    0xFF4A90E2
#define COLOR_DOC       0xFFECEFF4
#define COLOR_MD        0xFF8AB4F8
#define COLOR_IMAGE     0xFF81C995
#define COLOR_PDF       0xFFEF5350
#define COLOR_ELF       0xFF3C4043
#define COLOR_TRASH     0xFF90A4AE

typedef enum {
    ITEM_FILE,
    ITEM_DIR,
    ITEM_IMAGE,
    ITEM_MARKDOWN,
    ITEM_TEXT,
    ITEM_PDF,
    ITEM_ELF,
    ITEM_ARCHIVE,
    ITEM_UNSUPPORTED_ARCHIVE
} item_type_t;

typedef struct {
    char name[EXPLORER_MAX_NAME];
    char path[EXPLORER_MAX_PATH];
    char archive_entry[EXPLORER_MAX_PATH];
    uint32_t size;
    bool is_dir;
    bool is_archive_item;
    bool archive_virtual_dir;
    item_type_t type;
    int folder_icon_index;
    int x;
    int y;
    int w;
    int h;
} ExplorerItem;

typedef enum {
    SIDE_HOME,
    SIDE_DESKTOP,
    SIDE_DOCUMENTS,
    SIDE_DOWNLOADS,
    SIDE_PICTURES,
    SIDE_MUSIC,
    SIDE_VIDEOS,
    SIDE_TRASH,
    SIDE_ROOT,
    SIDE_DEVICE
} sidebar_kind_t;

typedef struct {
    sidebar_kind_t kind;
    char label[48];
    char path[EXPLORER_MAX_PATH];
    int y;
} SidebarEntry;

typedef enum {
    DIALOG_NONE,
    DIALOG_NEW_FOLDER,
    DIALOG_NEW_FILE,
    DIALOG_RENAME,
    DIALOG_DELETE,
    DIALOG_PROPERTIES,
    DIALOG_ERROR
} dialog_type_t;

typedef enum {
    MENU_NONE,
    MENU_FILE,
    MENU_FOLDER,
    MENU_ARCHIVE,
    MENU_EMPTY
} menu_kind_t;

typedef enum {
    ICON_FOLDER,
    ICON_FOLDER_DOCUMENTS,
    ICON_FOLDER_DOWNLOADS,
    ICON_FOLDER_IMAGES,
    ICON_FOLDER_MUSIC,
    ICON_FOLDER_VIDEOS,
    ICON_FOLDER_ROOT,
    ICON_TRASH,
    ICON_DEVICE,
    ICON_ARCHIVE,
    ICON_TEXT,
    ICON_MARKDOWN,
    ICON_PDF,
    ICON_IMAGE,
    ICON_ELF,
    ICON_FILE,
    ICON_COUNT
} icon_id_t;

typedef enum {
    ICON_UNTRIED,
    ICON_LOADED,
    ICON_FAILED
} icon_state_t;

typedef struct {
    const char *filename;
    icon_state_t large_state;
    icon_state_t small_state;
    uint32_t large[ICON_SIZE * ICON_SIZE];
    uint32_t small[SIDE_ICON_SIZE * SIDE_ICON_SIZE];
} IconCacheEntry;

typedef struct {
    const char *key;
    const char *filename;
} FolderIconRule;

typedef struct {
    ui_window_t win;
    int win_w;
    int win_h;

    char current_path[EXPLORER_MAX_PATH];
    char path_input[EXPLORER_MAX_PATH];
    int path_cursor;
    bool path_focused;

    bool archive_mode;
    char archive_path[EXPLORER_MAX_PATH];
    char archive_dir[EXPLORER_MAX_PATH];

    ExplorerItem items[EXPLORER_MAX_ITEMS];
    int item_count;
    int selected_item;
    int hovered_item;
    int scroll_y;
    int content_h;

    SidebarEntry sidebar[EXPLORER_MAX_SIDEBAR];
    int sidebar_count;
    int sidebar_hovered;
    int sidebar_scroll_y;
    int sidebar_content_h;

    char history[EXPLORER_MAX_HISTORY][EXPLORER_MAX_PATH];
    int history_count;

    menu_kind_t menu_kind;
    int menu_x;
    int menu_y;
    int menu_w;
    int menu_h;
    int menu_target;
    int menu_hovered;

    bool mouse_down;
    int mouse_x;
    int mouse_y;

    int last_clicked_item;
    uint64_t last_click_tick;

    dialog_type_t dialog;
    char dialog_input[EXPLORER_DIALOG_INPUT_MAX];
    int dialog_cursor;
    char dialog_target[EXPLORER_MAX_PATH];
    char error_text[160];
    char props_name[EXPLORER_MAX_NAME];
    char props_type[48];
    uint32_t props_size;
    bool props_is_dir;

    bool needs_paint;
    bool repaint_all;
    bool layout_dirty;
    bool sidebar_dirty;
    int dirty_items[8];
    int dirty_item_count;
} ExplorerState;

struct tar_header {
    char name[100];
    char mode[8];
    char uid[8];
    char gid[8];
    char size[12];
    char mtime[12];
    char chksum[8];
    char typeflag;
    char linkname[100];
    char magic[6];
    char version[2];
    char uname[32];
    char gname[32];
    char devmajor[8];
    char devminor[8];
    char prefix[155];
    char pad[12];
} __attribute__((packed));

static ExplorerState g;

// shared fs/tar buffer, avoids malloc spam
static uint8_t g_io_buf[4096];

// shared directory list for non-recursive reads; keeps large buffers off user stack
static FAT32_FileInfo g_dir_entries[EXPLORER_MAX_ITEMS];

// zero blocks for padding and tar end
static uint8_t g_zero_block[512];

static IconCacheEntry g_icons[ICON_COUNT] = {
    [ICON_FOLDER] = {"folder.png", ICON_UNTRIED, ICON_UNTRIED, {0}, {0}},
    [ICON_FOLDER_DOCUMENTS] = {"folder-documents.png", ICON_UNTRIED, ICON_UNTRIED, {0}, {0}},
    [ICON_FOLDER_DOWNLOADS] = {"folder-download.png", ICON_UNTRIED, ICON_UNTRIED, {0}, {0}},
    [ICON_FOLDER_IMAGES] = {"folder-images.png", ICON_UNTRIED, ICON_UNTRIED, {0}, {0}},
    [ICON_FOLDER_MUSIC] = {"folder-music.png", ICON_UNTRIED, ICON_UNTRIED, {0}, {0}},
    [ICON_FOLDER_VIDEOS] = {"folder-videos.png", ICON_UNTRIED, ICON_UNTRIED, {0}, {0}},
    [ICON_FOLDER_ROOT] = {"folder-root.png", ICON_UNTRIED, ICON_UNTRIED, {0}, {0}},
    [ICON_TRASH] = {"user-trash.png", ICON_UNTRIED, ICON_UNTRIED, {0}, {0}},
    [ICON_DEVICE] = {"drive-harddisk.png", ICON_UNTRIED, ICON_UNTRIED, {0}, {0}},
    [ICON_ARCHIVE] = {"file-roller.png", ICON_UNTRIED, ICON_UNTRIED, {0}, {0}},
    [ICON_TEXT] = {"text-editor.png", ICON_UNTRIED, ICON_UNTRIED, {0}, {0}},
    [ICON_MARKDOWN] = {"marktext.png", ICON_UNTRIED, ICON_UNTRIED, {0}, {0}},
    [ICON_PDF] = {"wps-office2023-pdfmain.png", ICON_UNTRIED, ICON_UNTRIED, {0}, {0}},
    [ICON_IMAGE] = {"org.gnome.Loupe.png", ICON_UNTRIED, ICON_UNTRIED, {0}, {0}},
    [ICON_ELF] = {"terminal.png", ICON_UNTRIED, ICON_UNTRIED, {0}, {0}},
    [ICON_FILE] = {"application-default-icon.png", ICON_UNTRIED, ICON_UNTRIED, {0}, {0}},
};

#define FOLDER_ICON_RULE(key_, filename_) { key_, filename_ }

static FolderIconRule g_folder_icon_rules[] = {
    FOLDER_ICON_RULE("activities", "folder-activities.png"),
    FOLDER_ICON_RULE("android", "folder-android.png"),
    FOLDER_ICON_RULE("appimage", "folder-appimage.png"),
    FOLDER_ICON_RULE("black", "folder-black.png"),
    FOLDER_ICON_RULE("blender", "folder-blender.png"),
    FOLDER_ICON_RULE("blue", "folder-blue.png"),
    FOLDER_ICON_RULE("book", "folder-book.png"),
    FOLDER_ICON_RULE("bookmark", "folder-bookmark.png"),
    FOLDER_ICON_RULE("brown", "folder-brown.png"),
    FOLDER_ICON_RULE("build", "folder-build.png"),
    FOLDER_ICON_RULE("calculate", "folder-calculate.png"),
    FOLDER_ICON_RULE("chart", "folder-chart.png"),
    FOLDER_ICON_RULE("cloud", "folder-cloud.png"),
    FOLDER_ICON_RULE("code", "folder-code.png"),
    FOLDER_ICON_RULE("comic", "folder-comic.png"),
    FOLDER_ICON_RULE("crash", "folder-crash.png"),
    FOLDER_ICON_RULE("cyan", "folder-cyan.png"),
    FOLDER_ICON_RULE("database", "folder-database.png"),
    FOLDER_ICON_RULE("deb", "folder-deb.png"),
    FOLDER_ICON_RULE("decrypted", "folder-decrypted.png"),
    FOLDER_ICON_RULE("design", "folder-design.png"),
    FOLDER_ICON_RULE("development", "folder-development.png"),
    FOLDER_ICON_RULE("docker", "folder-docker.png"),
    FOLDER_ICON_RULE("drawing", "folder-drawing.png"),
    FOLDER_ICON_RULE("dropbox", "folder-dropbox.png"),
    FOLDER_ICON_RULE("encrypted", "folder-encrypted.png"),
    FOLDER_ICON_RULE("extension", "folder-extension.png"),
    FOLDER_ICON_RULE("flatpak", "folder-flatpak.png"),
    FOLDER_ICON_RULE("games", "folder-games.png"),
    FOLDER_ICON_RULE("gdrive", "folder-gdrive.png"),
    FOLDER_ICON_RULE("git", "folder-git.png"),
    FOLDER_ICON_RULE("github", "folder-github.png"),
    FOLDER_ICON_RULE("godot", "folder-godot.png"),
    FOLDER_ICON_RULE("html", "folder-html.png"),
    FOLDER_ICON_RULE("important", "folder-important.png"),
    FOLDER_ICON_RULE("java", "folder-java.png"),
    FOLDER_ICON_RULE("language", "folder-language.png"),
    FOLDER_ICON_RULE("library", "folder-library.png"),
    FOLDER_ICON_RULE("locked", "folder-locked.png"),
    FOLDER_ICON_RULE("log", "folder-log.png"),
    FOLDER_ICON_RULE("mac", "folder-mac.png"),
    FOLDER_ICON_RULE("mail", "folder-mail.png"),
    FOLDER_ICON_RULE("notes", "folder-notes.png"),
    FOLDER_ICON_RULE("open", "folder-open.png"),
    FOLDER_ICON_RULE("paint", "folder-paint.png"),
    FOLDER_ICON_RULE("podcast", "folder-podcast.png"),
    FOLDER_ICON_RULE("presentation", "folder-presentation.png"),
    FOLDER_ICON_RULE("print", "folder-print.png"),
    FOLDER_ICON_RULE("projects", "folder-projects.png"),
    FOLDER_ICON_RULE("public", "folder-public.png"),
    FOLDER_ICON_RULE("rpm", "folder-rpm.png"),
    FOLDER_ICON_RULE("script", "folder-script.png"),
    FOLDER_ICON_RULE("sign", "folder-sign.png"),
    FOLDER_ICON_RULE("snap", "folder-snap.png"),
    FOLDER_ICON_RULE("steam", "folder-steam.png"),
    FOLDER_ICON_RULE("table", "folder-table.png"),
    FOLDER_ICON_RULE("tar", "folder-tar.png"),
    FOLDER_ICON_RULE("temp", "folder-temp.png"),
    FOLDER_ICON_RULE("templates", "folder-templates.png"),
    FOLDER_ICON_RULE("torrent", "folder-torrent.png"),
    FOLDER_ICON_RULE("trash", "folder-trash.png"),
    FOLDER_ICON_RULE("unlocked", "folder-unlocked.png"),
    FOLDER_ICON_RULE("vbox", "folder-vbox.png"),
    FOLDER_ICON_RULE("windows", "folder-windows.png"),
    FOLDER_ICON_RULE("wine", "folder-wine.png"),
};

#undef FOLDER_ICON_RULE

static IconCacheEntry g_folder_icon_cache[FOLDER_ICON_CACHE_SIZE];
static int g_folder_icon_cache_next;

static int min_i(int a, int b) { return a < b ? a : b; }
static int max_i(int a, int b) { return a > b ? a : b; }

static bool rect_contains(int x, int y, int w, int h, int px, int py) {
    return px >= x && px < x + w && py >= y && py < y + h;
}

static void str_copy(char *dst, const char *src, int cap) {
    int i = 0;
    if (!dst || cap <= 0) return;
    if (!src) src = "";
    while (src[i] && i < cap - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

static void str_append(char *dst, const char *src, int cap) {
    int len = 0;
    if (!dst || !src || cap <= 0) return;
    while (dst[len] && len < cap - 1) len++;
    int i = 0;
    while (src[i] && len + i < cap - 1) {
        dst[len + i] = src[i];
        i++;
    }
    dst[len + i] = 0;
}

static bool str_eq(const char *a, const char *b) {
    if (!a || !b) return false;
    return strcmp(a, b) == 0;
}

static bool str_eq_ci(const char *a, const char *b) {
    if (!a || !b) return false;
    return strcasecmp(a, b) == 0;
}

static bool starts_with(const char *s, const char *prefix) {
    if (!s || !prefix) return false;
    while (*prefix) {
        if (*s++ != *prefix++) return false;
    }
    return true;
}

static bool ends_with_ci(const char *s, const char *suffix) {
    int slen;
    int tlen;
    if (!s || !suffix) return false;
    slen = (int)strlen(s);
    tlen = (int)strlen(suffix);
    if (tlen > slen) return false;
    return strcasecmp(s + slen - tlen, suffix) == 0;
}

static char lower_ascii(char c) {
    if (c >= 'A' && c <= 'Z') return (char)(c + ('a' - 'A'));
    return c;
}

static bool is_alnum_ascii(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}

static void normalize_folder_key(const char *src, char *out, int cap) {
    int pos = 0;
    if (!out || cap <= 0) return;
    if (!src) src = "";
    for (int i = 0; src[i] && pos < cap - 1; i++) {
        if (is_alnum_ascii(src[i])) out[pos++] = lower_ascii(src[i]);
    }
    out[pos] = 0;
}

static bool folder_key_matches(const char *folder_name, const char *key) {
    char normalized[64];
    int nlen;
    int klen;
    normalize_folder_key(folder_name, normalized, (int)sizeof(normalized));
    if (str_eq(normalized, key)) return true;

    nlen = (int)strlen(normalized);
    klen = (int)strlen(key);
    if (nlen == klen + 1 && normalized[nlen - 1] == 's' &&
        strncmp(normalized, key, (size_t)klen) == 0) {
        return true;
    }
    if (klen > 1 && key[klen - 1] == 'y' && nlen == klen + 2 &&
        normalized[nlen - 3] == 'i' && normalized[nlen - 2] == 'e' &&
        normalized[nlen - 1] == 's' &&
        strncmp(normalized, key, (size_t)(klen - 1)) == 0) {
        return true;
    }
    return false;
}

static int folder_icon_index_for_name(const char *name) {
    if (!name || !name[0]) return -1;

    if (folder_key_matches(name, "pictures") || folder_key_matches(name, "photos") ||
        folder_key_matches(name, "images")) {
        return -1;
    }

    for (int i = 0; i < (int)(sizeof(g_folder_icon_rules) / sizeof(g_folder_icon_rules[0])); i++) {
        if (folder_key_matches(name, g_folder_icon_rules[i].key)) return i;
    }
    return -1;
}

static const char *path_basename(const char *path) {
    const char *last = path;
    if (!path || !path[0]) return "";
    for (int i = 0; path[i]; i++) {
        if (path[i] == '/' && path[i + 1]) last = path + i + 1;
    }
    return last;
}

static void basename_without_tar_ext(const char *path, char *out, int cap) {
    const char *base = path_basename(path);
    str_copy(out, base, cap);
    int len = (int)strlen(out);
    if (len > 6 && ends_with_ci(out, ".ustar")) out[len - 6] = 0;
    else if (len > 4 && ends_with_ci(out, ".tar")) out[len - 4] = 0;
}

static void path_join(char *out, const char *dir, const char *name, int cap) {
    str_copy(out, dir && dir[0] ? dir : "/", cap);
    if (!str_eq(out, "/")) str_append(out, "/", cap);
    str_append(out, name, cap);
}

static void path_parent(const char *path, char *out, int cap) {
    int len;
    if (!path || !path[0] || str_eq(path, "/")) {
        str_copy(out, "/", cap);
        return;
    }
    str_copy(out, path, cap);
    len = (int)strlen(out);
    while (len > 1 && out[len - 1] == '/') {
        out[len - 1] = 0;
        len--;
    }
    while (len > 1 && out[len - 1] != '/') len--;
    if (len <= 1) {
        str_copy(out, "/", cap);
    } else {
        out[len - 1] = 0;
    }
}

static bool is_supported_tar_name(const char *name) {
    return ends_with_ci(name, ".tar") || ends_with_ci(name, ".ustar");
}

static bool is_archive_name(const char *name) {
    return is_supported_tar_name(name) || ends_with_ci(name, ".zip") ||
           ends_with_ci(name, ".gz") || ends_with_ci(name, ".tgz");
}

static bool is_unsupported_archive_name(const char *name) {
    return is_archive_name(name) && !is_supported_tar_name(name);
}

static bool is_image_name(const char *name) {
    return ends_with_ci(name, ".png") || ends_with_ci(name, ".jpg") ||
           ends_with_ci(name, ".jpeg") || ends_with_ci(name, ".gif") ||
           ends_with_ci(name, ".bmp") || ends_with_ci(name, ".tga");
}

static item_type_t item_type_for(const char *name, bool is_dir) {
    if (is_dir) return ITEM_DIR;
    if (is_supported_tar_name(name)) return ITEM_ARCHIVE;
    if (is_unsupported_archive_name(name)) return ITEM_UNSUPPORTED_ARCHIVE;
    if (ends_with_ci(name, ".elf")) return ITEM_ELF;
    if (ends_with_ci(name, ".pdf")) return ITEM_PDF;
    if (ends_with_ci(name, ".md")) return ITEM_MARKDOWN;
    if (ends_with_ci(name, ".txt")) return ITEM_TEXT;
    if (is_image_name(name)) return ITEM_IMAGE;
    return ITEM_FILE;
}

static void make_absolute(const char *input, char *out, int cap) {
    if (!input || !input[0]) {
        str_copy(out, g.archive_mode ? "/" : g.current_path, cap);
    } else if (input[0] == '/') {
        str_copy(out, input, cap);
    } else {
        path_join(out, g.archive_mode ? "/" : g.current_path, input, cap);
    }
}

static bool path_is_dir(const char *path) {
    FAT32_FileInfo info;
    if (!path || !path[0]) return false;
    if (str_eq(path, "/")) return true;
    if (sys_get_file_info(path, &info) == 0) return info.is_directory != 0;
    return false;
}

static uint64_t ticks_now(void) {
    return (uint64_t)sys_system(SYSTEM_CMD_GET_TICKS, 0, 0, 0, 0);
}

static void request_full_repaint(void) {
    g.needs_paint = true;
    g.repaint_all = true;
}

static void request_item_repaint(int index) {
    if (index < 0 || index >= g.item_count) return;
    for (int i = 0; i < g.dirty_item_count; i++) {
        if (g.dirty_items[i] == index) return;
    }
    if (g.dirty_item_count < (int)(sizeof(g.dirty_items) / sizeof(g.dirty_items[0]))) {
        g.dirty_items[g.dirty_item_count++] = index;
        g.needs_paint = true;
    } else {
        request_full_repaint();
    }
}

static void request_sidebar_repaint(void) {
    g.sidebar_dirty = true;
    g.needs_paint = true;
}

static void set_error(const char *msg) {
    str_copy(g.error_text, msg, (int)sizeof(g.error_text));
    g.dialog = DIALOG_ERROR;
    request_full_repaint();
}

static void fit_text(const char *src, char *out, int cap, int max_w) {
    int len;
    if (!out || cap <= 0) return;
    str_copy(out, src ? src : "", cap);
    if ((int)ui_get_string_width(out) <= max_w) return;

    len = (int)strlen(out);
    while (len > 2 && (int)ui_get_string_width(out) > max_w) {
        out[len - 1] = 0;
        len--;
    }
    if (len > 2) {
        out[len - 2] = '.';
        out[len - 1] = '.';
        out[len] = 0;
    }
}

static void fit_text_tail(const char *src, char *out, int cap, int max_w) {
    int len;
    int start = 0;
    if (!out || cap <= 0) return;
    str_copy(out, src ? src : "", cap);
    if ((int)ui_get_string_width(out) <= max_w) return;
    len = (int)strlen(src ? src : "");
    for (start = 0; start < len; start++) {
        out[0] = '.';
        out[1] = '.';
        out[2] = 0;
        str_append(out, (src ? src : "") + start, cap);
        if ((int)ui_get_string_width(out) <= max_w) return;
    }
    str_copy(out, "..", cap);
}

static void draw_round_rect(int x, int y, int w, int h, int radius, uint32_t color) {
    if (w <= 0 || h <= 0) return;
    ui_draw_rounded_rect_filled(g.win, x, y, w, h, radius, color);
}

static void draw_text_fit(int x, int y, const char *text, uint32_t color, int max_w) {
    char buf[EXPLORER_MAX_NAME];
    fit_text(text, buf, (int)sizeof(buf), max_w);
    ui_draw_string(g.win, x, y, buf, color);
}

static void draw_button_rect(int x, int y, int w, int h, const char *label, bool hover, bool active) {
    uint32_t bg = active ? COLOR_SELECTED : (hover ? COLOR_HOVER : COLOR_PANEL);
    uint32_t border = active ? COLOR_ACCENT_2 : COLOR_BORDER;
    draw_round_rect(x, y, w, h, 8, border);
    draw_round_rect(x + 1, y + 1, w - 2, h - 2, 7, bg);
    if (label && label[0]) {
        int tw = (int)ui_get_string_width(label);
        ui_draw_string(g.win, x + (w - tw) / 2, y + (h - 14) / 2, label, COLOR_TEXT);
    }
}

static void scale_rgba_to_argb(const unsigned char *rgba, int src_w, int src_h,
                               uint32_t *dst, int dst_w, int dst_h) {
    if (!rgba || !dst || src_w <= 0 || src_h <= 0 || dst_w <= 0 || dst_h <= 0) return;
    uint32_t step_x = ((uint32_t)src_w << 16) / (uint32_t)dst_w;
    uint32_t step_y = ((uint32_t)src_h << 16) / (uint32_t)dst_h;
    uint32_t cy = 0;
    for (int y = 0; y < dst_h; y++) {
        uint32_t sy = cy >> 16;
        if (sy >= (uint32_t)src_h) sy = (uint32_t)src_h - 1;
        uint32_t cx = 0;
        for (int x = 0; x < dst_w; x++) {
            uint32_t sx = cx >> 16;
            if (sx >= (uint32_t)src_w) sx = (uint32_t)src_w - 1;
            int idx = (int)((sy * (uint32_t)src_w + sx) * 4);
            uint8_t r = rgba[idx];
            uint8_t gr = rgba[idx + 1];
            uint8_t b = rgba[idx + 2];
            uint8_t a = rgba[idx + 3];
            dst[y * dst_w + x] = ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)gr << 8) | b;
            cx += step_x;
        }
        cy += step_y;
    }
}

static bool load_icon_cache_entry(IconCacheEntry *entry, bool small) {
    if (!entry || !entry->filename) return false;
    icon_state_t *state = small ? &entry->small_state : &entry->large_state;
    if (*state == ICON_LOADED) return true;
    if (*state == ICON_FAILED) return false;

    // mark failed before decode, avoids disk spam if decoding aborts
    entry->large_state = ICON_FAILED;
    entry->small_state = ICON_FAILED;

    char path[160];
    str_copy(path, "/Library/images/icons/colloid/", (int)sizeof(path));
    str_append(path, entry->filename, (int)sizeof(path));

    int fd = sys_open(path, "r");
    if (fd < 0) return false;
    int size = sys_seek(fd, 0, 2);
    sys_seek(fd, 0, 0);
    if (size <= 0 || size > 4 * 1024 * 1024) {
        sys_close(fd);
        return false;
    }
    unsigned char *buf = (unsigned char *)malloc((size_t)size);
    if (!buf) {
        sys_close(fd);
        return false;
    }
    int got = sys_read(fd, buf, (uint32_t)size);
    sys_close(fd);
    if (got <= 0) {
        free(buf);
        return false;
    }
    int w = 0;
    int h = 0;
    int channels = 0;
    unsigned char *rgba = stbi_load_from_memory(buf, got, &w, &h, &channels, 4);
    free(buf);
    if (!rgba || w <= 0 || h <= 0) {
        if (rgba) stbi_image_free(rgba);
        return false;
    }
    scale_rgba_to_argb(rgba, w, h, entry->large, ICON_SIZE, ICON_SIZE);
    scale_rgba_to_argb(rgba, w, h, entry->small, SIDE_ICON_SIZE, SIDE_ICON_SIZE);
    stbi_image_free(rgba);
    entry->large_state = ICON_LOADED;
    entry->small_state = ICON_LOADED;
    return true;
}

static bool load_icon_variant(icon_id_t id, bool small) {
    if (id < 0 || id >= ICON_COUNT) return false;
    return load_icon_cache_entry(&g_icons[id], small);
}

static IconCacheEntry *folder_icon_cache_entry(const char *filename) {
    int slot = -1;
    if (!filename || !filename[0]) return NULL;
    for (int i = 0; i < FOLDER_ICON_CACHE_SIZE; i++) {
        if (g_folder_icon_cache[i].filename && str_eq(g_folder_icon_cache[i].filename, filename)) {
            return &g_folder_icon_cache[i];
        }
        if (slot < 0 && !g_folder_icon_cache[i].filename) slot = i;
    }
    if (slot < 0) {
        slot = g_folder_icon_cache_next++ % FOLDER_ICON_CACHE_SIZE;
    }
    g_folder_icon_cache[slot].filename = filename;
    g_folder_icon_cache[slot].large_state = ICON_UNTRIED;
    g_folder_icon_cache[slot].small_state = ICON_UNTRIED;
    return &g_folder_icon_cache[slot];
}

static void draw_file_glyph(int x, int y, item_type_t type, bool small) {
    int s = small ? SIDE_ICON_SIZE : ICON_SIZE;
    int r = small ? 5 : 10;
    if (type == ITEM_DIR) {
        draw_round_rect(x + s / 9, y + s / 7, s / 2, s / 4, 4, COLOR_ACCENT);
        draw_round_rect(x, y + s / 4, s, (s * 2) / 3, r, COLOR_FOLDER);
        draw_round_rect(x + 3, y + s / 3, s - 6, s / 2, max_i(3, r - 3), 0xFF74B8FF);
        return;
    }
    if (type == ITEM_ELF) {
        draw_round_rect(x, y, s, s, r, COLOR_ELF);
        ui_draw_rect(g.win, x + s / 4, y + s / 4, s / 2, 2, 0xFF78D878);
        ui_draw_rect(g.win, x + s / 4, y + s / 2, s / 3, 2, 0xFF78D878);
        ui_draw_rect(g.win, x + s / 4, y + (s * 3) / 4, s / 2, 2, 0xFF78D878);
        return;
    }
    if (type == ITEM_ARCHIVE || type == ITEM_UNSUPPORTED_ARCHIVE) {
        draw_round_rect(x, y, s, s, r, 0xFFB1793B);
        ui_draw_rect(g.win, x + s / 3, y + 7, s / 3, s - 14, 0xFFFFD08A);
        ui_draw_rect(g.win, x + s / 3, y + s / 2, s / 3, 2, 0xFF7A4A1A);
        return;
    }
    draw_round_rect(x + 2, y, s - 4, s, r, COLOR_DOC);
    if (type == ITEM_IMAGE) {
        draw_round_rect(x + 7, y + 10, s - 14, s - 18, 5, COLOR_IMAGE);
    } else if (type == ITEM_PDF) {
        draw_round_rect(x + 7, y + 10, s - 14, 15, 4, COLOR_PDF);
        ui_draw_string(g.win, x + 10, y + 14, "PDF", 0xFFFFFFFF);
    } else if (type == ITEM_MARKDOWN) {
        draw_round_rect(x + 7, y + 10, s - 14, 15, 4, COLOR_MD);
        ui_draw_string(g.win, x + 12, y + 14, "MD", 0xFFFFFFFF);
    } else {
        ui_draw_rect(g.win, x + 10, y + 15, s - 20, 2, 0xFFB0B6C0);
        ui_draw_rect(g.win, x + 10, y + 23, s - 20, 2, 0xFFB0B6C0);
        ui_draw_rect(g.win, x + 10, y + 31, s - 28, 2, 0xFFB0B6C0);
    }
}

static icon_id_t icon_for_item(const ExplorerItem *it) {
    if (!it) return ICON_FILE;
    if (it->is_dir) {
        if (str_eq_ci(it->name, "Documents")) return ICON_FOLDER_DOCUMENTS;
        if (str_eq_ci(it->name, "Downloads") || str_eq_ci(it->name, "Download")) return ICON_FOLDER_DOWNLOADS;
        if (str_eq_ci(it->name, "Pictures") || str_eq_ci(it->name, "Images") || str_eq_ci(it->name, "Photos")) return ICON_FOLDER_IMAGES;
        if (str_eq_ci(it->name, "Music")) return ICON_FOLDER_MUSIC;
        if (str_eq_ci(it->name, "Videos")) return ICON_FOLDER_VIDEOS;
        if (str_eq_ci(it->name, "Root")) return ICON_FOLDER_ROOT;
        if (str_eq_ci(it->name, "Recycle Bin")) return ICON_TRASH;
        return ICON_FOLDER;
    }
    if (it->type == ITEM_ARCHIVE || it->type == ITEM_UNSUPPORTED_ARCHIVE) return ICON_ARCHIVE;
    if (it->type == ITEM_IMAGE) return ICON_IMAGE;
    if (it->type == ITEM_MARKDOWN) return ICON_MARKDOWN;
    if (it->type == ITEM_TEXT) return ICON_TEXT;
    if (it->type == ITEM_PDF) return ICON_PDF;
    if (it->type == ITEM_ELF) return ICON_ELF;
    return ICON_FILE;
}

static icon_id_t icon_for_sidebar(sidebar_kind_t kind) {
    if (kind == SIDE_DOCUMENTS) return ICON_FOLDER_DOCUMENTS;
    if (kind == SIDE_DOWNLOADS) return ICON_FOLDER_DOWNLOADS;
    if (kind == SIDE_PICTURES) return ICON_FOLDER_IMAGES;
    if (kind == SIDE_MUSIC) return ICON_FOLDER_MUSIC;
    if (kind == SIDE_VIDEOS) return ICON_FOLDER_VIDEOS;
    if (kind == SIDE_TRASH) return ICON_TRASH;
    if (kind == SIDE_ROOT) return ICON_FOLDER_ROOT;
    if (kind == SIDE_DEVICE) return ICON_DEVICE;
    return ICON_FOLDER;
}

static void draw_icon_cached(icon_id_t id, item_type_t fallback, int x, int y, bool small) {
    if (load_icon_variant(id, small)) {
        uint32_t *pixels = small ? g_icons[id].small : g_icons[id].large;
        int size = small ? SIDE_ICON_SIZE : ICON_SIZE;
        ui_draw_image(g.win, x, y, size, size, pixels);
        return;
    }
    draw_file_glyph(x, y, fallback, small);
}

static void draw_folder_rule_icon_cached(int index, int x, int y, bool small) {
    if (index >= 0 && index < (int)(sizeof(g_folder_icon_rules) / sizeof(g_folder_icon_rules[0]))) {
        IconCacheEntry *entry = folder_icon_cache_entry(g_folder_icon_rules[index].filename);
        if (entry && load_icon_cache_entry(entry, small)) {
            uint32_t *pixels = small ? entry->small : entry->large;
            int size = small ? SIDE_ICON_SIZE : ICON_SIZE;
            ui_draw_image(g.win, x, y, size, size, pixels);
            return;
        }
    }
    draw_file_glyph(x, y, ITEM_DIR, small);
}

static void draw_item_icon(const ExplorerItem *it, int x, int y) {
    if (it && it->is_dir && it->folder_icon_index >= 0) {
        draw_folder_rule_icon_cached(it->folder_icon_index, x, y, false);
        return;
    }
    draw_icon_cached(icon_for_item(it), it ? it->type : ITEM_FILE, x, y, false);
}

static void build_sidebar(void) {
    static const struct {
        sidebar_kind_t kind;
        const char *label;
        const char *path;
    } base[] = {
        {SIDE_HOME, "Home", "/root"},
        {SIDE_DESKTOP, "Desktop", "/root/Desktop"},
        {SIDE_DOCUMENTS, "Documents", "/root/Documents"},
        {SIDE_DOWNLOADS, "Downloads", "/root/Downloads"},
        {SIDE_PICTURES, "Pictures", "/root/Pictures"},
        {SIDE_MUSIC, "Music", "/root/Music"},
        {SIDE_VIDEOS, "Videos", "/root/Videos"},
        {SIDE_TRASH, "Recycle Bin", "/RecycleBin"},
        {SIDE_ROOT, "Root", "/"},
    };
    int count = 0;
    for (int i = 0; i < (int)(sizeof(base) / sizeof(base[0])) && count < EXPLORER_MAX_SIDEBAR; i++) {
        g.sidebar[count].kind = base[i].kind;
        str_copy(g.sidebar[count].label, base[i].label, (int)sizeof(g.sidebar[count].label));
        str_copy(g.sidebar[count].path, base[i].path, (int)sizeof(g.sidebar[count].path));
        count++;
    }

    int mounts = sys_fs_mount_count();
    for (int i = 0; i < mounts && count < EXPLORER_MAX_SIDEBAR; i++) {
        mount_info_t info;
        if (sys_fs_mount_info(i, &info) != 0) continue;
        if (str_eq(info.path, "/") || str_eq(info.path, "/proc") ||
            str_eq(info.path, "/sys") || str_eq(info.path, "/boot")) {
            // already visible elsewhere, no need for dupes
            continue;
        }
        g.sidebar[count].kind = SIDE_DEVICE;
        if (info.device[0]) str_copy(g.sidebar[count].label, info.device, (int)sizeof(g.sidebar[count].label));
        else str_copy(g.sidebar[count].label, path_basename(info.path), (int)sizeof(g.sidebar[count].label));
        str_copy(g.sidebar[count].path, info.path, (int)sizeof(g.sidebar[count].path));
        count++;
    }
    g.sidebar_count = count;
    g.sidebar_content_h = SIDEBAR_PAD * 2 + count * SIDEBAR_ROW_H;
}

static void swap_items(int a, int b) {
    ExplorerItem tmp = g.items[a];
    g.items[a] = g.items[b];
    g.items[b] = tmp;
}

static void sort_items(void) {
    for (int i = 0; i < g.item_count - 1; i++) {
        for (int j = i + 1; j < g.item_count; j++) {
            if (g.items[i].is_dir != g.items[j].is_dir) {
                if (!g.items[i].is_dir && g.items[j].is_dir) swap_items(i, j);
            } else if (strcasecmp(g.items[i].name, g.items[j].name) > 0) {
                swap_items(i, j);
            }
        }
    }
}

static int visible_h(void) {
    return max_i(1, g.win_h - HEADER_H);
}

static int content_w(void) {
    return max_i(1, g.win_w - SIDEBAR_W);
}

static int grid_cols(void) {
    int usable = content_w() - GRID_PAD * 2;
    int cols = usable / CELL_W;
    return max_i(1, cols);
}

static void clamp_scroll(void) {
    int max_scroll = g.content_h - visible_h();
    if (max_scroll < 0) max_scroll = 0;
    if (g.scroll_y < 0) g.scroll_y = 0;
    if (g.scroll_y > max_scroll) g.scroll_y = max_scroll;

    int side_max = g.sidebar_content_h - visible_h();
    if (side_max < 0) side_max = 0;
    if (g.sidebar_scroll_y < 0) g.sidebar_scroll_y = 0;
    if (g.sidebar_scroll_y > side_max) g.sidebar_scroll_y = side_max;
}

static void layout_items(void) {
    int cols = grid_cols();
    int rows = (g.item_count + cols - 1) / cols;
    g.content_h = GRID_PAD * 2 + rows * CELL_H;
    clamp_scroll();
    for (int i = 0; i < g.item_count; i++) {
        int col = i % cols;
        int row = i / cols;
        int cell_x = SIDEBAR_W + GRID_PAD + col * CELL_W;
        int cell_y = HEADER_H + GRID_PAD + row * CELL_H - g.scroll_y;
        g.items[i].x = cell_x + (CELL_W - ITEM_W) / 2;
        g.items[i].y = cell_y;
        g.items[i].w = ITEM_W;
        g.items[i].h = ITEM_H;
    }
    g.layout_dirty = false;
}

static void update_title(void) {
    char title[160];
    str_copy(title, "Files - ", (int)sizeof(title));
    str_append(title, g.current_path, (int)sizeof(title));
    ui_window_set_title(g.win, title);
}

static void push_history_if_needed(const char *target, bool push_history) {
    if (!push_history || str_eq(g.current_path, target) || !g.current_path[0]) return;
    if (g.history_count == EXPLORER_MAX_HISTORY) {
        // fixed history, no heap here
        for (int i = 1; i < EXPLORER_MAX_HISTORY; i++) {
            str_copy(g.history[i - 1], g.history[i], EXPLORER_MAX_PATH);
        }
        g.history_count = EXPLORER_MAX_HISTORY - 1;
    }
    str_copy(g.history[g.history_count++], g.current_path, EXPLORER_MAX_PATH);
}

static bool read_exact(int fd, void *buf, int len) {
    uint8_t *dst = (uint8_t *)buf;
    int got_total = 0;

    // sys_read can come back short even with a good fd
    while (got_total < len) {
        int got = sys_read(fd, dst + got_total, (uint32_t)(len - got_total));
        if (got <= 0) return false;
        got_total += got;
    }
    return true;
}

static uint64_t tar_parse_octal(const char *str, int size) {
    uint64_t result = 0;

    // tar stores sizes as octal ascii
    while (size-- > 0) {
        if (*str >= '0' && *str <= '7') result = (result << 3) + (uint64_t)(*str - '0');
        str++;
    }
    return result;
}

static bool tar_header_empty(const struct tar_header *h) {
    const uint8_t *p = (const uint8_t *)h;
    for (int i = 0; i < 512; i++) {
        if (p[i] != 0) return false;
    }
    return true;
}

static void tar_full_name(const struct tar_header *h, char *out, int cap) {
    out[0] = 0;

    // ustar splits long names between prefix and name
    if (h->prefix[0]) {
        int i = 0;
        while (h->prefix[i] && i < 155 && (int)strlen(out) < cap - 1) {
            char tmp[2] = {h->prefix[i], 0};
            str_append(out, tmp, cap);
            i++;
        }
        str_append(out, "/", cap);
    }
    int n = 0;
    while (h->name[n] && n < 100 && (int)strlen(out) < cap - 1) {
        char tmp[2] = {h->name[n], 0};
        str_append(out, tmp, cap);
        n++;
    }
    while (out[0] == '/') {
        int i = 0;
        while (out[i]) {
            out[i] = out[i + 1];
            i++;
        }
    }
}

static void archive_display_path(const char *archive_path, const char *dir, char *out, int cap) {
    str_copy(out, archive_path, cap);
    str_append(out, ":/", cap);
    if (dir && dir[0]) {
        str_append(out, dir, cap);
        int len = (int)strlen(out);
        if (len > 3 && out[len - 1] == '/') out[len - 1] = 0;
    }
}

static bool parse_archive_display_path(const char *path, char *archive_path, int archive_cap,
                                       char *dir, int dir_cap) {
    if (!path) return false;

    // homemade path format for the tar address bar
    int len = (int)strlen(path);
    for (int i = 0; i < len - 1; i++) {
        if (path[i] == ':' && path[i + 1] == '/') {
            str_copy(archive_path, path, min_i(archive_cap, i + 1));
            const char *rest = path + i + 2;
            str_copy(dir, rest, dir_cap);
            if (dir[0]) {
                int dlen = (int)strlen(dir);
                if (dir[dlen - 1] != '/') str_append(dir, "/", dir_cap);
            }
            return is_supported_tar_name(archive_path);
        }
    }
    return false;
}

static bool archive_add_child(const char *name, bool is_dir, const char *entry_path,
                              uint32_t size, char typeflag) {
    if (!name || !name[0] || g.item_count >= EXPLORER_MAX_ITEMS) return false;
    for (int i = 0; i < g.item_count; i++) {
        if (str_eq(g.items[i].name, name)) {
            if (is_dir) {
                // a dir can show up after its children in the tar
                g.items[i].is_dir = true;
                g.items[i].archive_virtual_dir = true;
                g.items[i].type = ITEM_DIR;
                g.items[i].folder_icon_index = folder_icon_index_for_name(g.items[i].name);
            }
            return true;
        }
    }
    ExplorerItem *it = &g.items[g.item_count++];
    memset(it, 0, sizeof(*it));
    it->folder_icon_index = -1;
    str_copy(it->name, name, EXPLORER_MAX_NAME);
    archive_display_path(g.archive_path, entry_path, it->path, EXPLORER_MAX_PATH);
    str_copy(it->archive_entry, entry_path, EXPLORER_MAX_PATH);
    it->size = size;
    it->is_dir = is_dir;
    it->is_archive_item = true;
    it->archive_virtual_dir = is_dir || typeflag == '5';
    it->type = item_type_for(name, is_dir);
    if (it->is_dir) it->folder_icon_index = folder_icon_index_for_name(it->name);
    return true;
}

static bool archive_consider_entry(const char *entry_name, bool entry_is_dir, uint32_t size, char typeflag) {
    char name[EXPLORER_MAX_NAME];
    char entry_path[EXPLORER_MAX_PATH];
    const char *remaining = entry_name;
    int dir_len = (int)strlen(g.archive_dir);
    if (dir_len > 0) {
        if (!starts_with(entry_name, g.archive_dir)) return true;
        remaining = entry_name + dir_len;
    }
    if (!remaining[0]) return true;

    int slash = -1;

    // only show this level, the rest becomes virtual
    for (int i = 0; remaining[i]; i++) {
        if (remaining[i] == '/') {
            slash = i;
            break;
        }
    }

    if (slash >= 0) {
        if (slash == 0) return true;
        int n = min_i(slash, EXPLORER_MAX_NAME - 1);
        for (int i = 0; i < n; i++) name[i] = remaining[i];
        name[n] = 0;
        str_copy(entry_path, g.archive_dir, (int)sizeof(entry_path));
        str_append(entry_path, name, (int)sizeof(entry_path));
        str_append(entry_path, "/", (int)sizeof(entry_path));
        archive_add_child(name, true, entry_path, 0, '5');
    } else {
        str_copy(name, remaining, (int)sizeof(name));
        str_copy(entry_path, g.archive_dir, (int)sizeof(entry_path));
        str_append(entry_path, name, (int)sizeof(entry_path));
        if (entry_is_dir) str_append(entry_path, "/", (int)sizeof(entry_path));
        archive_add_child(name, entry_is_dir, entry_path, size, typeflag);
    }
    return true;
}

static bool load_archive_directory(const char *archive_path, const char *dir, bool push_history);
static bool load_location(const char *path, bool push_history);

static bool load_directory(const char *path, bool push_history) {
    int count;
    char target[EXPLORER_MAX_PATH];
    make_absolute(path, target, (int)sizeof(target));

    if (!path_is_dir(target)) {
        set_error("Folder not found.");
        return false;
    }

    count = sys_list(target, g_dir_entries, EXPLORER_MAX_ITEMS);
    if (count < 0) {
        set_error("Cannot read folder.");
        return false;
    }

    push_history_if_needed(target, push_history);
    g.archive_mode = false;
    g.archive_path[0] = 0;
    g.archive_dir[0] = 0;
    g.item_count = min_i(count, EXPLORER_MAX_ITEMS);
    for (int i = 0; i < g.item_count; i++) {
        memset(&g.items[i], 0, sizeof(g.items[i]));
        g.items[i].folder_icon_index = -1;
        str_copy(g.items[i].name, g_dir_entries[i].name, EXPLORER_MAX_NAME);
        path_join(g.items[i].path, target, g_dir_entries[i].name, EXPLORER_MAX_PATH);
        g.items[i].is_dir = g_dir_entries[i].is_directory != 0;
        g.items[i].size = g_dir_entries[i].size;
        g.items[i].type = item_type_for(g.items[i].name, g.items[i].is_dir);
        if (g.items[i].is_dir) g.items[i].folder_icon_index = folder_icon_index_for_name(g.items[i].name);
    }
    sort_items();

    str_copy(g.current_path, target, EXPLORER_MAX_PATH);
    str_copy(g.path_input, target, EXPLORER_MAX_PATH);
    g.path_cursor = (int)strlen(g.path_input);
    g.selected_item = -1;
    g.hovered_item = -1;
    g.scroll_y = 0;
    g.last_clicked_item = -1;
    build_sidebar();
    g.layout_dirty = true;
    layout_items();
    update_title();
    request_full_repaint();
    return true;
}

static bool load_archive_directory(const char *archive_path, const char *dir, bool push_history) {
    if (!archive_path || !is_supported_tar_name(archive_path)) {
        set_error("Only TAR archives are supported.");
        return false;
    }

    int fd = sys_open(archive_path, "r");
    if (fd < 0) {
        set_error("Cannot open archive.");
        return false;
    }

    char normalized_dir[EXPLORER_MAX_PATH];
    str_copy(normalized_dir, dir ? dir : "", (int)sizeof(normalized_dir));
    if (normalized_dir[0]) {
        int len = (int)strlen(normalized_dir);
        if (normalized_dir[len - 1] != '/') str_append(normalized_dir, "/", (int)sizeof(normalized_dir));
    }

    char display[EXPLORER_MAX_PATH];
    archive_display_path(archive_path, normalized_dir, display, (int)sizeof(display));
    push_history_if_needed(display, push_history);

    g.archive_mode = true;
    str_copy(g.archive_path, archive_path, EXPLORER_MAX_PATH);
    str_copy(g.archive_dir, normalized_dir, EXPLORER_MAX_PATH);
    g.item_count = 0;

    while (1) {
        struct tar_header h;
        if (!read_exact(fd, &h, 512)) break;
        if (tar_header_empty(&h)) break;
        uint64_t size = tar_parse_octal(h.size, 12);
        char entry_name[EXPLORER_MAX_PATH];
        tar_full_name(&h, entry_name, (int)sizeof(entry_name));
        bool is_dir = (h.typeflag == '5') || (entry_name[0] && entry_name[strlen(entry_name) - 1] == '/');
        if (entry_name[0]) archive_consider_entry(entry_name, is_dir, (uint32_t)size, h.typeflag);
        uint64_t padded = ((size + 511) / 512) * 512;
        // align to the next tar header
        if (padded > 0) sys_seek(fd, (int)padded, 1);
    }
    sys_close(fd);
    sort_items();

    str_copy(g.current_path, display, EXPLORER_MAX_PATH);
    str_copy(g.path_input, display, EXPLORER_MAX_PATH);
    g.path_cursor = (int)strlen(g.path_input);
    g.selected_item = -1;
    g.hovered_item = -1;
    g.scroll_y = 0;
    g.last_clicked_item = -1;
    build_sidebar();
    g.layout_dirty = true;
    layout_items();
    update_title();
    request_full_repaint();
    return true;
}

static bool load_location(const char *path, bool push_history) {
    char archive_path[EXPLORER_MAX_PATH];
    char archive_dir[EXPLORER_MAX_PATH];
    if (parse_archive_display_path(path, archive_path, (int)sizeof(archive_path), archive_dir, (int)sizeof(archive_dir))) {
        return load_archive_directory(archive_path, archive_dir, push_history);
    }
    return load_directory(path, push_history);
}

static void refresh_directory(void) {
    char path[EXPLORER_MAX_PATH];
    int old_scroll = g.scroll_y;
    str_copy(path, g.current_path, (int)sizeof(path));
    if (load_location(path, false)) {
        g.scroll_y = old_scroll;
        g.layout_dirty = true;
        layout_items();
    }
}

static void navigate_back(void) {
    char path[EXPLORER_MAX_PATH];
    if (g.history_count <= 0) return;
    str_copy(path, g.history[g.history_count - 1], (int)sizeof(path));
    g.history_count--;
    load_location(path, false);
}

static void navigate_up(void) {
    if (g.archive_mode) {
        if (!g.archive_dir[0]) {
            // leaving the tar goes back to its parent dir
            char parent[EXPLORER_MAX_PATH];
            path_parent(g.archive_path, parent, (int)sizeof(parent));
            load_directory(parent, true);
            return;
        }
        char parent_dir[EXPLORER_MAX_PATH];
        str_copy(parent_dir, g.archive_dir, (int)sizeof(parent_dir));
        int len = (int)strlen(parent_dir);
        while (len > 0 && parent_dir[len - 1] == '/') parent_dir[--len] = 0;
        while (len > 0 && parent_dir[len - 1] != '/') len--;
        parent_dir[len] = 0;
        load_archive_directory(g.archive_path, parent_dir, true);
        return;
    }
    char parent[EXPLORER_MAX_PATH];
    path_parent(g.current_path, parent, (int)sizeof(parent));
    if (!str_eq(parent, g.current_path)) load_directory(parent, true);
}

static int hit_item(int x, int y) {
    for (int i = 0; i < g.item_count; i++) {
        if (rect_contains(g.items[i].x, g.items[i].y, g.items[i].w, g.items[i].h, x, y)) return i;
    }
    return -1;
}

static int hit_sidebar(int x, int y) {
    if (x < 0 || x >= SIDEBAR_W || y < HEADER_H) return -1;
    for (int i = 0; i < g.sidebar_count; i++) {
        if (rect_contains(10, g.sidebar[i].y, SIDEBAR_W - 20, SIDEBAR_ROW_H - 4, x, y)) return i;
    }
    return -1;
}

static bool hit_header_back(int x, int y) { return rect_contains(14, 14, 34, 34, x, y); }
static bool hit_header_up(int x, int y) { return rect_contains(54, 14, 34, 34, x, y); }
static bool hit_header_menu(int x, int y) { return rect_contains(g.win_w - 48, 14, 34, 34, x, y); }
static int path_field_x(void) { return 102; }
static int path_field_w(void) { return max_i(80, g.win_w - path_field_x() - 62); }
static bool hit_path_field(int x, int y) { return rect_contains(path_field_x(), 14, path_field_w(), 34, x, y); }

static void open_file(const char *path) {
    const char *program = "/bin/txtedit.elf";
    if (is_supported_tar_name(path)) {
        load_archive_directory(path, "", true);
        return;
    }
    if (is_unsupported_archive_name(path)) {
        set_error("Only TAR archives are supported.");
        return;
    }
    if (ends_with_ci(path, ".elf")) {
        sys_spawn(path, NULL, SPAWN_FLAG_BACKGROUND, 0);
        return;
    }
    if (ends_with_ci(path, ".pdf")) program = "/bin/boredword.elf";
    else if (ends_with_ci(path, ".md")) program = "/bin/markdown.elf";
    else if (is_image_name(path)) program = "/bin/viewer.elf";
    else if (ends_with_ci(path, ".txt")) program = "/bin/txtedit.elf";
    sys_spawn(program, path, SPAWN_FLAG_BACKGROUND, 0);
}

static void open_item(int index) {
    if (index < 0 || index >= g.item_count) return;
    ExplorerItem *it = &g.items[index];
    if (it->is_archive_item) {
        if (it->is_dir) {
            load_archive_directory(g.archive_path, it->archive_entry, true);
        } else {
            // no writes inside tar yet
            set_error("Archive contents are read-only.");
        }
        return;
    }
    if (it->is_dir) load_directory(it->path, true);
    else open_file(it->path);
}

static FAT32_FileInfo *alloc_dir_entries(void) {
    return (FAT32_FileInfo *)malloc(sizeof(FAT32_FileInfo) * EXPLORER_MAX_ITEMS);
}

static bool delete_recursive(const char *path) {
    FAT32_FileInfo info;

    // dumb but useful guard rail
    if (!path || !path[0] || str_eq(path, "/")) return false;
    if (sys_get_file_info(path, &info) == 0 && info.is_directory) {
        FAT32_FileInfo *entries = alloc_dir_entries();
        if (!entries) return false;
        int count = sys_list(path, entries, EXPLORER_MAX_ITEMS);
        if (count < 0) {
            free(entries);
            return false;
        }
        for (int i = 0; i < count; i++) {
            char child[EXPLORER_MAX_PATH];
            if (str_eq(entries[i].name, ".") || str_eq(entries[i].name, "..")) continue;
            path_join(child, path, entries[i].name, (int)sizeof(child));
            if (!delete_recursive(child)) {
                free(entries);
                return false;
            }
        }
        free(entries);
    }
    return sys_delete(path) == 0;
}

static bool mkdir_parents_for_path(const char *path, bool path_is_dir_target) {
    char tmp[EXPLORER_MAX_PATH];
    str_copy(tmp, path, (int)sizeof(tmp));
    int len = (int)strlen(tmp);

    // tar extract creates parents on the way
    if (!path_is_dir_target) {
        while (len > 1 && tmp[len - 1] != '/') len--;
        if (len <= 1) return true;
        tmp[len - 1] = 0;
    }
    for (int i = 1; tmp[i]; i++) {
        if (tmp[i] == '/') {
            tmp[i] = 0;
            if (!sys_exists(tmp) && sys_mkdir(tmp) != 0) return false;
            tmp[i] = '/';
        }
    }
    if (tmp[0] && !sys_exists(tmp) && sys_mkdir(tmp) != 0) return false;
    return true;
}

static void tar_write_octal(char *field, int len, uint64_t value) {
    // ustar wants octal fields ending with NUL
    for (int i = 0; i < len; i++) field[i] = '0';
    field[len - 1] = 0;
    int pos = len - 2;
    while (value && pos >= 0) {
        field[pos--] = (char)('0' + (value & 7));
        value >>= 3;
    }
}

static bool tar_set_name(struct tar_header *h, const char *path) {
    int len = (int)strlen(path);
    if (len <= 99) {
        str_copy(h->name, path, 100);
        return true;
    }

    // old ustar split: prefix 155, name 100
    int split = -1;
    for (int i = len - 1; i > 0; i--) {
        if (path[i] == '/' && i <= 154 && len - i - 1 <= 99) {
            split = i;
            break;
        }
    }
    if (split < 0) return false;
    for (int i = 0; i < split && i < 155; i++) h->prefix[i] = path[i];
    h->prefix[split] = 0;
    str_copy(h->name, path + split + 1, 100);
    return true;
}

static bool tar_write_header(int fd, const char *tar_path, uint32_t size, char typeflag) {
    struct tar_header h;
    memset(&h, 0, sizeof(h));
    if (!tar_set_name(&h, tar_path)) {
        set_error("Archive path is too long.");
        return false;
    }
    tar_write_octal(h.mode, 8, typeflag == '5' ? 0755 : 0644);
    tar_write_octal(h.uid, 8, 0);
    tar_write_octal(h.gid, 8, 0);
    tar_write_octal(h.size, 12, typeflag == '5' ? 0 : size);
    tar_write_octal(h.mtime, 12, 0);

    // checksum with chksum filled with spaces
    for (int i = 0; i < 8; i++) h.chksum[i] = ' ';
    h.typeflag = typeflag;
    str_copy(h.magic, "ustar", 6);
    h.version[0] = '0';
    h.version[1] = '0';
    uint32_t sum = 0;
    uint8_t *raw = (uint8_t *)&h;
    for (int i = 0; i < 512; i++) sum += raw[i];
    tar_write_octal(h.chksum, 8, sum);
    h.chksum[6] = 0;
    h.chksum[7] = ' ';
    return sys_write_fs(fd, &h, 512) == 512;
}

static bool tar_write_padding(int fd, uint32_t size) {
    uint32_t pad = (512 - (size % 512)) % 512;
    if (pad == 0) return true;

    // every entry ends on a 512 block
    return sys_write_fs(fd, g_zero_block, pad) == (int)pad;
}

static bool add_path_to_tar(int out_fd, const char *fs_path, const char *tar_path) {
    FAT32_FileInfo info;
    if (sys_get_file_info(fs_path, &info) != 0) return false;
    if (info.is_directory) {
        char dir_name[EXPLORER_MAX_PATH];
        str_copy(dir_name, tar_path, (int)sizeof(dir_name));
        int len = (int)strlen(dir_name);
        if (len > 0 && dir_name[len - 1] != '/') str_append(dir_name, "/", (int)sizeof(dir_name));
        if (!tar_write_header(out_fd, dir_name, 0, '5')) return false;
        FAT32_FileInfo *entries = alloc_dir_entries();
        if (!entries) return false;
        int count = sys_list(fs_path, entries, EXPLORER_MAX_ITEMS);
        if (count < 0) {
            free(entries);
            return false;
        }
        for (int i = 0; i < count; i++) {
            char child_fs[EXPLORER_MAX_PATH];
            char child_tar[EXPLORER_MAX_PATH];
            if (str_eq(entries[i].name, ".") || str_eq(entries[i].name, "..")) continue;
            path_join(child_fs, fs_path, entries[i].name, (int)sizeof(child_fs));
            str_copy(child_tar, dir_name, (int)sizeof(child_tar));
            str_append(child_tar, entries[i].name, (int)sizeof(child_tar));
            if (!add_path_to_tar(out_fd, child_fs, child_tar)) {
                free(entries);
                return false;
            }
        }
        free(entries);
        return true;
    }

    if (!tar_write_header(out_fd, tar_path, info.size, '0')) return false;
    int in_fd = sys_open(fs_path, "r");
    if (in_fd < 0) return false;
    uint32_t remaining = info.size;
    while (remaining > 0) {
        uint32_t chunk = remaining > sizeof(g_io_buf) ? sizeof(g_io_buf) : remaining;
        int got = sys_read(in_fd, g_io_buf, chunk);
        if (got <= 0) {
            sys_close(in_fd);
            return false;
        }
        if (sys_write_fs(out_fd, g_io_buf, (uint32_t)got) != got) {
            sys_close(in_fd);
            return false;
        }
        remaining -= (uint32_t)got;
    }
    sys_close(in_fd);
    return tar_write_padding(out_fd, info.size);
}

static bool create_archive_from_folder(const char *folder_path) {
    char parent[EXPLORER_MAX_PATH];
    char archive_name[EXPLORER_MAX_NAME];
    char archive_path[EXPLORER_MAX_PATH];
    if (!folder_path || !folder_path[0] || str_eq(folder_path, "/")) {
        set_error("Cannot archive root folder.");
        return false;
    }
    if (!path_is_dir(folder_path)) {
        set_error("Select a folder first.");
        return false;
    }
    path_parent(folder_path, parent, (int)sizeof(parent));
    str_copy(archive_name, path_basename(folder_path), (int)sizeof(archive_name));
    str_append(archive_name, ".tar", (int)sizeof(archive_name));
    path_join(archive_path, parent, archive_name, (int)sizeof(archive_path));
    if (sys_exists(archive_path)) {
        set_error("Archive already exists.");
        return false;
    }
    int out_fd = sys_open(archive_path, "w");
    if (out_fd < 0) {
        set_error("Cannot create archive.");
        return false;
    }
    bool ok = add_path_to_tar(out_fd, folder_path, path_basename(folder_path));
    // two empty blocks mark tar end
    if (ok) ok = sys_write_fs(out_fd, g_zero_block, 512) == 512 &&
                 sys_write_fs(out_fd, g_zero_block, 512) == 512;
    sys_close(out_fd);
    if (!ok) {
        sys_delete(archive_path);
        set_error("Archive creation failed.");
        return false;
    }
    refresh_directory();
    return true;
}

static bool extract_tar_archive(const char *archive_path) {
    if (!is_supported_tar_name(archive_path)) {
        set_error("Only TAR archives are supported.");
        return false;
    }
    char parent[EXPLORER_MAX_PATH];
    char dest_name[EXPLORER_MAX_NAME];
    char dest_root[EXPLORER_MAX_PATH];
    path_parent(archive_path, parent, (int)sizeof(parent));
    basename_without_tar_ext(archive_path, dest_name, (int)sizeof(dest_name));
    path_join(dest_root, parent, dest_name, (int)sizeof(dest_root));
    if (sys_exists(dest_root)) {
        set_error("Extract destination already exists.");
        return false;
    }
    if (sys_mkdir(dest_root) != 0) {
        set_error("Cannot create extract folder.");
        return false;
    }

    int fd = sys_open(archive_path, "r");
    if (fd < 0) {
        set_error("Cannot open archive.");
        return false;
    }
    bool ok = true;
    while (ok) {
        struct tar_header h;
        if (!read_exact(fd, &h, 512)) break;
        if (tar_header_empty(&h)) break;
        uint64_t size = tar_parse_octal(h.size, 12);
        char entry_name[EXPLORER_MAX_PATH];
        tar_full_name(&h, entry_name, (int)sizeof(entry_name));
        char dest[EXPLORER_MAX_PATH];
        path_join(dest, dest_root, entry_name, (int)sizeof(dest));
        bool is_dir = (h.typeflag == '5') || (entry_name[0] && entry_name[strlen(entry_name) - 1] == '/');
        if (entry_name[0]) {
            if (is_dir) {
                ok = mkdir_parents_for_path(dest, true);
            } else if (h.typeflag == '0' || h.typeflag == 0) {
                ok = mkdir_parents_for_path(dest, false);
                // refuse overwrite during extract
                if (ok && sys_exists(dest)) ok = false;
                int out_fd = ok ? sys_open(dest, "w") : -1;
                if (out_fd < 0) ok = false;
                uint64_t remaining = size;
                while (ok && remaining > 0) {
                    uint32_t chunk = remaining > sizeof(g_io_buf) ? sizeof(g_io_buf) : (uint32_t)remaining;
                    int got = sys_read(fd, g_io_buf, chunk);
                    if (got <= 0) {
                        ok = false;
                        break;
                    }
                    if (sys_write_fs(out_fd, g_io_buf, (uint32_t)got) != got) {
                        ok = false;
                        break;
                    }
                    remaining -= (uint32_t)got;
                }
                if (out_fd >= 0) sys_close(out_fd);
                uint64_t pad = (512 - (size % 512)) % 512;
                // file is read, only padding left
                if (ok && pad > 0) sys_seek(fd, (int)pad, 1);
                continue;
            }
        }
        uint64_t padded = ((size + 511) / 512) * 512;
        if (padded > 0) sys_seek(fd, (int)padded, 1);
    }
    sys_close(fd);
    if (!ok) {
        set_error("Extract failed.");
        return false;
    }
    refresh_directory();
    return true;
}

static void open_new_folder_dialog(void) {
    g.dialog = DIALOG_NEW_FOLDER;
    str_copy(g.dialog_input, "New Folder", (int)sizeof(g.dialog_input));
    g.dialog_cursor = (int)strlen(g.dialog_input);
    g.menu_kind = MENU_NONE;
    request_full_repaint();
}

static void open_new_file_dialog(void) {
    g.dialog = DIALOG_NEW_FILE;
    str_copy(g.dialog_input, "New File.txt", (int)sizeof(g.dialog_input));
    g.dialog_cursor = (int)strlen(g.dialog_input);
    g.menu_kind = MENU_NONE;
    request_full_repaint();
}

static void open_rename_dialog(void) {
    if (g.archive_mode) {
        set_error("Archive contents are read-only.");
        return;
    }
    if (g.selected_item < 0 || g.selected_item >= g.item_count) {
        set_error("Select an item first.");
        return;
    }
    g.dialog = DIALOG_RENAME;
    str_copy(g.dialog_target, g.items[g.selected_item].path, (int)sizeof(g.dialog_target));
    str_copy(g.dialog_input, g.items[g.selected_item].name, (int)sizeof(g.dialog_input));
    g.dialog_cursor = (int)strlen(g.dialog_input);
    g.menu_kind = MENU_NONE;
    request_full_repaint();
}

static void open_delete_dialog(void) {
    if (g.archive_mode) {
        set_error("Archive contents are read-only.");
        return;
    }
    if (g.selected_item < 0 || g.selected_item >= g.item_count) {
        set_error("Select an item first.");
        return;
    }
    g.dialog = DIALOG_DELETE;
    str_copy(g.dialog_target, g.items[g.selected_item].path, (int)sizeof(g.dialog_target));
    g.menu_kind = MENU_NONE;
    request_full_repaint();
}

static const char *type_name_for_item(const ExplorerItem *it) {
    if (!it) return "File";
    if (it->is_dir) return "Folder";
    if (it->type == ITEM_ARCHIVE) return "TAR archive";
    if (it->type == ITEM_UNSUPPORTED_ARCHIVE) return "Archive";
    if (it->type == ITEM_IMAGE) return "Image";
    if (it->type == ITEM_MARKDOWN) return "Markdown";
    if (it->type == ITEM_TEXT) return "Text";
    if (it->type == ITEM_PDF) return "PDF";
    if (it->type == ITEM_ELF) return "Executable";
    return "File";
}

static void open_properties_dialog(void) {
    if (g.selected_item < 0 || g.selected_item >= g.item_count) {
        set_error("Select an item first.");
        return;
    }
    ExplorerItem *it = &g.items[g.selected_item];
    str_copy(g.props_name, it->name, (int)sizeof(g.props_name));
    str_copy(g.props_type, type_name_for_item(it), (int)sizeof(g.props_type));
    g.props_size = it->size;
    g.props_is_dir = it->is_dir;
    g.dialog = DIALOG_PROPERTIES;
    g.menu_kind = MENU_NONE;
    request_full_repaint();
}

static void confirm_new_folder(void) {
    char path[EXPLORER_MAX_PATH];
    if (g.archive_mode) {
        set_error("Archive contents are read-only.");
        return;
    }
    if (!g.dialog_input[0]) return;
    path_join(path, g.current_path, g.dialog_input, (int)sizeof(path));
    if (sys_exists(path)) {
        set_error("A file with this name already exists.");
        return;
    }
    if (sys_mkdir(path) != 0) {
        set_error("Could not create folder.");
        return;
    }
    g.dialog = DIALOG_NONE;
    refresh_directory();
}

static void confirm_new_file(void) {
    char path[EXPLORER_MAX_PATH];
    if (g.archive_mode) {
        set_error("Archive contents are read-only.");
        return;
    }
    if (!g.dialog_input[0]) return;
    path_join(path, g.current_path, g.dialog_input, (int)sizeof(path));
    if (sys_exists(path)) {
        set_error("A file with this name already exists.");
        return;
    }
    int fd = sys_open(path, "w");
    if (fd < 0) {
        set_error("Could not create file.");
        return;
    }
    sys_close(fd);
    g.dialog = DIALOG_NONE;
    refresh_directory();
}

static void confirm_rename(void) {
    char parent[EXPLORER_MAX_PATH];
    char dest[EXPLORER_MAX_PATH];
    if (!g.dialog_input[0]) return;
    path_parent(g.dialog_target, parent, (int)sizeof(parent));
    path_join(dest, parent, g.dialog_input, (int)sizeof(dest));
    if (str_eq(g.dialog_target, dest)) {
        g.dialog = DIALOG_NONE;
        request_full_repaint();
        return;
    }
    if (sys_exists(dest)) {
        set_error("A file with this name already exists.");
        return;
    }
    if (sys_rename(g.dialog_target, dest) != 0) {
        set_error("Rename failed.");
        return;
    }
    g.dialog = DIALOG_NONE;
    refresh_directory();
}

static void confirm_delete(void) {
    if (!delete_recursive(g.dialog_target)) {
        set_error("Delete failed.");
        return;
    }
    g.dialog = DIALOG_NONE;
    refresh_directory();
}

static const char **menu_labels(menu_kind_t kind, int *count) {
    static const char *file_items[] = {"Open", "Rename", "Delete", "Properties"};
    static const char *folder_items[] = {"Open", "Rename", "Add to archive", "Delete", "Properties"};
    static const char *archive_items[] = {"Open", "Extract", "Rename", "Delete", "Properties"};
    static const char *empty_items[] = {"New folder", "New file", "Refresh"};
    if (kind == MENU_FILE) {
        *count = 4;
        return file_items;
    }
    if (kind == MENU_FOLDER) {
        *count = 5;
        return folder_items;
    }
    if (kind == MENU_ARCHIVE) {
        *count = 5;
        return archive_items;
    }
    *count = 3;
    return empty_items;
}

static int menu_row_at(int x, int y) {
    int count = 0;
    if (g.menu_kind == MENU_NONE) return -1;
    menu_labels(g.menu_kind, &count);
    for (int i = 0; i < count; i++) {
        int row_y = g.menu_y + 8 + i * 31;
        if (rect_contains(g.menu_x + 8, row_y, g.menu_w - 16, 28, x, y)) return i;
    }
    return -1;
}

static void menu_action(int idx) {
    menu_kind_t kind = g.menu_kind;
    int target = g.menu_target;
    g.menu_kind = MENU_NONE;
    g.menu_hovered = -1;
    if (target >= 0 && target < g.item_count) g.selected_item = target;

    if (kind == MENU_EMPTY) {
        if (idx == 0) open_new_folder_dialog();
        else if (idx == 1) open_new_file_dialog();
        else if (idx == 2) refresh_directory();
    } else if (kind == MENU_FILE) {
        if (idx == 0) open_item(g.selected_item);
        else if (idx == 1) open_rename_dialog();
        else if (idx == 2) open_delete_dialog();
        else if (idx == 3) open_properties_dialog();
    } else if (kind == MENU_FOLDER) {
        if (idx == 0) open_item(g.selected_item);
        else if (idx == 1) open_rename_dialog();
        else if (idx == 2) create_archive_from_folder(g.items[g.selected_item].path);
        else if (idx == 3) open_delete_dialog();
        else if (idx == 4) open_properties_dialog();
    } else if (kind == MENU_ARCHIVE) {
        if (idx == 0) open_item(g.selected_item);
        else if (idx == 1) extract_tar_archive(g.items[g.selected_item].path);
        else if (idx == 2) open_rename_dialog();
        else if (idx == 3) open_delete_dialog();
        else if (idx == 4) open_properties_dialog();
    }
    request_full_repaint();
}

static void open_context_menu(menu_kind_t kind, int target, int x, int y) {
    g.menu_kind = kind;
    g.menu_target = target;
    g.menu_hovered = -1;
    g.menu_x = x;
    g.menu_y = y;
    if (g.menu_x + 168 > g.win_w) g.menu_x = g.win_w - 176;
    if (g.menu_x < 8) g.menu_x = 8;
    int count = 0;
    menu_labels(kind, &count);
    g.menu_w = 168;
    g.menu_h = 16 + count * 31;
    if (g.menu_y + g.menu_h > g.win_h) g.menu_y = g.win_h - g.menu_h - 8;
    if (g.menu_y < HEADER_H + 8) g.menu_y = HEADER_H + 8;
    g.menu_hovered = menu_row_at(x, y);
    request_full_repaint();
}

static void draw_header(void) {
    int px = path_field_x();
    int pw = path_field_w();
    char path_buf[EXPLORER_MAX_PATH];
    ui_draw_rect(g.win, 0, 0, g.win_w, HEADER_H, COLOR_HEADER);
    ui_draw_rect(g.win, 0, HEADER_H - 1, g.win_w, 1, COLOR_BORDER);

    draw_button_rect(14, 14, 34, 34, "<", rect_contains(14, 14, 34, 34, g.mouse_x, g.mouse_y), false);
    draw_button_rect(54, 14, 34, 34, "^", rect_contains(54, 14, 34, 34, g.mouse_x, g.mouse_y), false);

    draw_round_rect(px, 14, pw, 34, 10, g.path_focused ? COLOR_ACCENT_2 : COLOR_BORDER);
    draw_round_rect(px + 1, 15, pw - 2, 32, 9, COLOR_FIELD);
    fit_text_tail(g.path_input, path_buf, (int)sizeof(path_buf), pw - 24);
    ui_draw_string(g.win, px + 12, 25, path_buf, COLOR_TEXT);
    if (g.path_focused) {
        int cx = px + 12 + min_i((int)ui_get_string_width(path_buf), pw - 28);
        ui_draw_rect(g.win, cx, 23, 2, 17, COLOR_ACCENT);
    }

    draw_button_rect(g.win_w - 48, 14, 34, 34, "...",
                     rect_contains(g.win_w - 48, 14, 34, 34, g.mouse_x, g.mouse_y),
                     g.menu_kind != MENU_NONE);
}

static void draw_sidebar(void) {
    ui_draw_rect(g.win, 0, HEADER_H, SIDEBAR_W, g.win_h - HEADER_H, COLOR_SIDEBAR);
    ui_draw_rect(g.win, SIDEBAR_W - 1, HEADER_H, 1, g.win_h - HEADER_H, COLOR_BORDER);

    for (int i = 0; i < g.sidebar_count; i++) {
        int y = HEADER_H + SIDEBAR_PAD + i * SIDEBAR_ROW_H - g.sidebar_scroll_y;
        bool visible = y > HEADER_H - SIDEBAR_ROW_H && y < g.win_h;
        bool selected = !g.archive_mode && (str_eq(g.current_path, g.sidebar[i].path) ||
                        (!str_eq(g.sidebar[i].path, "/") && starts_with(g.current_path, g.sidebar[i].path) &&
                         (g.current_path[strlen(g.sidebar[i].path)] == '/' ||
                          g.current_path[strlen(g.sidebar[i].path)] == 0)));
        g.sidebar[i].y = y;
        if (!visible) continue;
        if (i == 9) ui_draw_rect(g.win, 18, y - 8, SIDEBAR_W - 36, 1, COLOR_BORDER);
        if (selected || i == g.sidebar_hovered) {
            draw_round_rect(10, y, SIDEBAR_W - 20, SIDEBAR_ROW_H - 4, 8, selected ? COLOR_SELECTED : COLOR_HOVER);
        }
        draw_icon_cached(icon_for_sidebar(g.sidebar[i].kind), ITEM_DIR, 20, y + 6, true);
        draw_text_fit(52, y + 10, g.sidebar[i].label, selected ? COLOR_TEXT : COLOR_MUTED, SIDEBAR_W - 68);
    }

    if (g.sidebar_content_h > visible_h()) {
        int track_h = visible_h() - 18;
        int thumb_h = max_i(24, (visible_h() * track_h) / g.sidebar_content_h);
        int max_scroll = g.sidebar_content_h - visible_h();
        int thumb_y = HEADER_H + 9 + (g.sidebar_scroll_y * (track_h - thumb_h)) / max_i(1, max_scroll);
        draw_round_rect(SIDEBAR_W - 8, thumb_y, 4, thumb_h, 2, COLOR_DIM);
    }
    g.sidebar_dirty = false;
}

static void draw_item_cell(int i) {
    if (i < 0 || i >= g.item_count) return;
    ExplorerItem *it = &g.items[i];
    if (it->y + it->h < HEADER_H || it->y > g.win_h) return;
    ui_draw_rect(g.win, it->x - 4, it->y - 4, it->w + 8, it->h + 8, COLOR_BG);
    if (i == g.selected_item || i == g.hovered_item) {
        draw_round_rect(it->x, it->y, it->w, it->h, 10, i == g.selected_item ? COLOR_SELECTED : COLOR_HOVER);
    }
    int icon_x = it->x + (it->w - ICON_SIZE) / 2;
    draw_item_icon(it, icon_x, it->y + 14);
    draw_text_fit(it->x + 8, it->y + 72, it->name, COLOR_TEXT, it->w - 16);
}

static void draw_grid(void) {
    ui_draw_rect(g.win, SIDEBAR_W, HEADER_H, g.win_w - SIDEBAR_W, g.win_h - HEADER_H, COLOR_BG);

    if (g.item_count == 0) {
        const char *empty = g.archive_mode ? "Empty archive folder" : "Empty folder";
        int tw = (int)ui_get_string_width(empty);
        ui_draw_string(g.win, SIDEBAR_W + (content_w() - tw) / 2, HEADER_H + visible_h() / 2, empty, COLOR_DIM);
        return;
    }

    for (int i = 0; i < g.item_count; i++) draw_item_cell(i);

    if (g.content_h > visible_h()) {
        int track_h = visible_h() - 18;
        int thumb_h = max_i(32, (visible_h() * track_h) / g.content_h);
        int max_scroll = g.content_h - visible_h();
        int thumb_y = HEADER_H + 9 + (g.scroll_y * (track_h - thumb_h)) / max_i(1, max_scroll);
        draw_round_rect(g.win_w - 8, thumb_y, 4, thumb_h, 2, COLOR_DIM);
    }
}

static void draw_menu(void) {
    int count = 0;
    const char **items = menu_labels(g.menu_kind, &count);
    if (g.menu_kind == MENU_NONE) return;
    draw_round_rect(g.menu_x, g.menu_y, g.menu_w, g.menu_h, 10, COLOR_BORDER);
    draw_round_rect(g.menu_x + 1, g.menu_y + 1, g.menu_w - 2, g.menu_h - 2, 9, COLOR_PANEL);
    for (int i = 0; i < count; i++) {
        int y = g.menu_y + 8 + i * 31;
        if (i == g.menu_hovered) draw_round_rect(g.menu_x + 8, y, g.menu_w - 16, 28, 7, COLOR_HOVER);
        uint32_t color = (str_eq(items[i], "Delete")) ? COLOR_DANGER : COLOR_TEXT;
        ui_draw_string(g.win, g.menu_x + 18, y + 8, items[i], color);
    }
}

static void draw_dialog_button(int x, int y, int w, const char *text, bool primary, bool danger) {
    uint32_t color = danger ? COLOR_DANGER : (primary ? COLOR_ACCENT_2 : COLOR_PANEL);
    draw_round_rect(x, y, w, 32, 8, primary || danger ? color : COLOR_BORDER);
    draw_round_rect(x + 1, y + 1, w - 2, 30, 7, color);
    int tw = (int)ui_get_string_width(text);
    ui_draw_string(g.win, x + (w - tw) / 2, y + 9, text, COLOR_TEXT);
}

static void draw_size_text(uint32_t size, char *out, int cap) {
    char digits[32];
    int len = 0;
    uint32_t val = size;
    if (val == 0) {
        str_copy(out, "0 B", cap);
        return;
    }
    while (val > 0 && len < (int)sizeof(digits) - 1) {
        digits[len++] = (char)('0' + (val % 10));
        val /= 10;
    }
    int p = 0;
    for (int i = len - 1; i >= 0 && p < cap - 1; i--) out[p++] = digits[i];
    out[p] = 0;
    str_append(out, " B", cap);
}

static void draw_dialog(void) {
    int w = 400;
    int h = 178;
    int x = (g.win_w - w) / 2;
    int y = (g.win_h - h) / 2;
    const char *title = "";
    const char *primary = "OK";
    bool input = false;
    bool danger = false;
    char input_fit[EXPLORER_DIALOG_INPUT_MAX];

    if (g.dialog == DIALOG_NONE) return;
    if (g.dialog == DIALOG_NEW_FOLDER) {
        title = "New Folder";
        primary = "Create";
        input = true;
    } else if (g.dialog == DIALOG_NEW_FILE) {
        title = "New File";
        primary = "Create";
        input = true;
    } else if (g.dialog == DIALOG_RENAME) {
        title = "Rename";
        primary = "Rename";
        input = true;
    } else if (g.dialog == DIALOG_DELETE) {
        title = "Delete item?";
        primary = "Delete";
        danger = true;
    } else if (g.dialog == DIALOG_PROPERTIES) {
        title = "Properties";
        h = 210;
        y = (g.win_h - h) / 2;
    } else if (g.dialog == DIALOG_ERROR) {
        title = "Files";
        h = 148;
        y = (g.win_h - h) / 2;
    }

    draw_round_rect(x, y, w, h, 14, COLOR_BORDER);
    draw_round_rect(x + 1, y + 1, w - 2, h - 2, 13, COLOR_PANEL);
    ui_draw_string(g.win, x + 20, y + 18, title, COLOR_TEXT);

    if (input) {
        draw_round_rect(x + 20, y + 58, w - 40, 34, 8, COLOR_BORDER);
        draw_round_rect(x + 21, y + 59, w - 42, 32, 7, COLOR_FIELD);
        fit_text_tail(g.dialog_input, input_fit, (int)sizeof(input_fit), w - 64);
        ui_draw_string(g.win, x + 32, y + 68, input_fit, COLOR_TEXT);
        ui_draw_rect(g.win, x + 32 + min_i((int)ui_get_string_width(input_fit), w - 68), y + 66, 2, 17, COLOR_ACCENT);
    } else if (g.dialog == DIALOG_DELETE) {
        char msg[EXPLORER_MAX_NAME + 32];
        str_copy(msg, "Delete ", (int)sizeof(msg));
        str_append(msg, path_basename(g.dialog_target), (int)sizeof(msg));
        str_append(msg, "?", (int)sizeof(msg));
        draw_text_fit(x + 20, y + 58, msg, COLOR_MUTED, w - 40);
    } else if (g.dialog == DIALOG_PROPERTIES) {
        char size_buf[40];
        char line[EXPLORER_MAX_NAME + 32];
        str_copy(line, "Name: ", (int)sizeof(line));
        str_append(line, g.props_name, (int)sizeof(line));
        draw_text_fit(x + 20, y + 58, line, COLOR_TEXT, w - 40);
        str_copy(line, "Type: ", (int)sizeof(line));
        str_append(line, g.props_type, (int)sizeof(line));
        draw_text_fit(x + 20, y + 84, line, COLOR_MUTED, w - 40);
        str_copy(line, "Size: ", (int)sizeof(line));
        draw_size_text(g.props_is_dir ? 0 : g.props_size, size_buf, (int)sizeof(size_buf));
        str_append(line, g.props_is_dir ? "-" : size_buf, (int)sizeof(line));
        draw_text_fit(x + 20, y + 110, line, COLOR_MUTED, w - 40);
    } else {
        draw_text_fit(x + 20, y + 58, g.error_text, COLOR_MUTED, w - 40);
    }

    if (g.dialog == DIALOG_ERROR || g.dialog == DIALOG_PROPERTIES) {
        draw_dialog_button(x + w - 104, y + h - 48, 84, primary, true, false);
    } else {
        draw_dialog_button(x + w - 202, y + h - 48, 84, "Cancel", false, false);
        draw_dialog_button(x + w - 108, y + h - 48, 88, primary, true, danger);
    }
}

static void paint(void) {
    if (g.layout_dirty || g.repaint_all) layout_items();
    if (g.repaint_all) {
        ui_draw_rect(g.win, 0, 0, g.win_w, g.win_h, COLOR_BG);
        draw_header();
        draw_sidebar();
        draw_grid();
        draw_menu();
        draw_dialog();
        ui_mark_dirty(g.win, 0, 0, g.win_w, g.win_h);
    } else {
        bool did_partial = false;
        if (g.sidebar_dirty) {
            draw_sidebar();
            ui_mark_dirty(g.win, 0, HEADER_H, SIDEBAR_W, g.win_h - HEADER_H);
            did_partial = true;
        }
        for (int i = 0; i < g.dirty_item_count; i++) {
            int idx = g.dirty_items[i];
            draw_item_cell(idx);
            ExplorerItem *it = &g.items[idx];
            ui_mark_dirty(g.win, it->x - 4, it->y - 4, it->w + 8, it->h + 8);
            did_partial = true;
        }
        if (!did_partial) {
            // weird state, repaint wide
            request_full_repaint();
            return;
        }
    }
    g.dirty_item_count = 0;
    g.sidebar_dirty = false;
    g.needs_paint = false;
    g.repaint_all = false;
}

static void dialog_key(int legacy, uint32_t cp) {
    char *buf = g.dialog_input;
    int *cursor = &g.dialog_cursor;
    int max_len = EXPLORER_DIALOG_INPUT_MAX;
    int len = (int)strlen(buf);
    if (g.dialog == DIALOG_ERROR || g.dialog == DIALOG_PROPERTIES) {
        if (legacy == KEY_ENTER || legacy == KEY_ESCAPE) g.dialog = DIALOG_NONE;
        request_full_repaint();
        return;
    }
    if (g.dialog == DIALOG_DELETE) {
        if (legacy == KEY_ENTER) confirm_delete();
        else if (legacy == KEY_ESCAPE) g.dialog = DIALOG_NONE;
        request_full_repaint();
        return;
    }
    if (legacy == KEY_ENTER) {
        if (g.dialog == DIALOG_NEW_FOLDER) confirm_new_folder();
        else if (g.dialog == DIALOG_NEW_FILE) confirm_new_file();
        else if (g.dialog == DIALOG_RENAME) confirm_rename();
        return;
    }
    if (legacy == KEY_ESCAPE) {
        g.dialog = DIALOG_NONE;
        request_full_repaint();
        return;
    }
    if (legacy == KEY_LEFT) {
        if (*cursor > 0) (*cursor)--;
    } else if (legacy == KEY_RIGHT) {
        if (*cursor < len) (*cursor)++;
    } else if (legacy == KEY_BACKSPACE) {
        if (*cursor > 0) {
            for (int i = *cursor; i <= len; i++) buf[i - 1] = buf[i];
            (*cursor)--;
        }
    } else if (cp >= 32 && cp < 127 && len < max_len - 1 && cp != '/') {
        // no slash in names made by dialogs
        for (int i = len; i >= *cursor; i--) buf[i + 1] = buf[i];
        buf[*cursor] = (char)cp;
        (*cursor)++;
    }
    request_full_repaint();
}

static void path_key(int legacy, uint32_t cp) {
    int len = (int)strlen(g.path_input);
    if (!g.path_focused) return;
    if (legacy == KEY_ENTER) {
        char target[EXPLORER_MAX_PATH];
        make_absolute(g.path_input, target, (int)sizeof(target));
        if (load_location(target, true)) g.path_focused = false;
        return;
    }
    if (legacy == KEY_ESCAPE) {
        str_copy(g.path_input, g.current_path, EXPLORER_MAX_PATH);
        g.path_cursor = (int)strlen(g.path_input);
        g.path_focused = false;
    } else if (legacy == KEY_LEFT) {
        if (g.path_cursor > 0) g.path_cursor--;
    } else if (legacy == KEY_RIGHT) {
        if (g.path_cursor < len) g.path_cursor++;
    } else if (legacy == KEY_BACKSPACE) {
        if (g.path_cursor > 0) {
            for (int i = g.path_cursor; i <= len; i++) g.path_input[i - 1] = g.path_input[i];
            g.path_cursor--;
        }
    } else if (cp >= 32 && cp < 127 && len < EXPLORER_MAX_PATH - 1) {
        for (int i = len; i >= g.path_cursor; i--) g.path_input[i + 1] = g.path_input[i];
        g.path_input[g.path_cursor] = (char)cp;
        g.path_cursor++;
    }
    request_full_repaint();
}

static void handle_key(gui_event_t *ev) {
    int legacy = ev->arg1;
    uint32_t cp = (uint32_t)ev->arg4;
    if (g.dialog != DIALOG_NONE) {
        dialog_key(legacy, cp);
        return;
    }
    if (g.path_focused) {
        path_key(legacy, cp);
        return;
    }
    if (legacy == KEY_BACKSPACE) navigate_up();
    else if (legacy == KEY_ENTER && g.selected_item >= 0) open_item(g.selected_item);
    else if (legacy == KEY_ESCAPE) {
        g.menu_kind = MENU_NONE;
        g.menu_hovered = -1;
        int old = g.selected_item;
        g.selected_item = -1;
        request_item_repaint(old);
        request_full_repaint();
    }
}

static bool handle_dialog_click(int x, int y) {
    int w = 400;
    int h = (g.dialog == DIALOG_ERROR) ? 148 : (g.dialog == DIALOG_PROPERTIES ? 210 : 178);
    int dx = (g.win_w - w) / 2;
    int dy = (g.win_h - h) / 2;
    if (g.dialog == DIALOG_NONE) return false;
    if (g.dialog == DIALOG_ERROR || g.dialog == DIALOG_PROPERTIES) {
        if (rect_contains(dx + w - 104, dy + h - 48, 84, 32, x, y)) {
            g.dialog = DIALOG_NONE;
            request_full_repaint();
        }
        return true;
    }
    if (rect_contains(dx + w - 202, dy + h - 48, 84, 32, x, y)) {
        g.dialog = DIALOG_NONE;
        request_full_repaint();
        return true;
    }
    if (rect_contains(dx + w - 108, dy + h - 48, 88, 32, x, y)) {
        if (g.dialog == DIALOG_NEW_FOLDER) confirm_new_folder();
        else if (g.dialog == DIALOG_NEW_FILE) confirm_new_file();
        else if (g.dialog == DIALOG_RENAME) confirm_rename();
        else if (g.dialog == DIALOG_DELETE) confirm_delete();
        return true;
    }
    return true;
}

static bool handle_menu_click(int x, int y) {
    if (g.menu_kind == MENU_NONE) return false;
    if (!rect_contains(g.menu_x, g.menu_y, g.menu_w, g.menu_h, x, y)) {
        g.menu_kind = MENU_NONE;
        g.menu_hovered = -1;
        request_full_repaint();
        return false;
    }
    int row = menu_row_at(x, y);
    if (row >= 0) {
        menu_action(row);
        return true;
    }
    return true;
}

static void click_item(int index) {
    uint64_t now = ticks_now();
    if (index < 0 || index >= g.item_count) return;

    // tick threshold, short enough to avoid random opens
    if (g.last_clicked_item == index && now - g.last_click_tick <= 30) {
        g.last_clicked_item = -1;
        open_item(index);
        return;
    }
    int old = g.selected_item;
    g.selected_item = index;
    g.last_clicked_item = index;
    g.last_click_tick = now;
    request_item_repaint(old);
    request_item_repaint(index);
}

static void mouse_down(int x, int y) {
    g.mouse_down = true;
    g.mouse_x = x;
    g.mouse_y = y;

    if (handle_dialog_click(x, y)) return;
    if (handle_menu_click(x, y)) return;

    if (hit_header_back(x, y)) {
        navigate_back();
        return;
    }
    if (hit_header_up(x, y)) {
        navigate_up();
        return;
    }
    if (hit_header_menu(x, y)) {
        open_context_menu(MENU_EMPTY, -1, g.win_w - 176, 54);
        g.path_focused = false;
        return;
    }
    if (hit_path_field(x, y)) {
        g.path_focused = true;
        g.menu_kind = MENU_NONE;
        g.path_cursor = (int)strlen(g.path_input);
        request_full_repaint();
        return;
    }
    g.path_focused = false;
    g.menu_kind = MENU_NONE;

    int side = hit_sidebar(x, y);
    if (side >= 0) {
        if (path_is_dir(g.sidebar[side].path)) load_directory(g.sidebar[side].path, true);
        else set_error("Folder not found.");
        return;
    }

    int item = hit_item(x, y);
    if (item >= 0) {
        click_item(item);
    } else if (x >= SIDEBAR_W && y >= HEADER_H) {
        int old = g.selected_item;
        g.selected_item = -1;
        request_item_repaint(old);
    }
}

static void mouse_up(int x, int y) {
    g.mouse_down = false;
    g.mouse_x = x;
    g.mouse_y = y;
}

static void mouse_move(int x, int y) {
    int old_hover = g.hovered_item;
    int old_side = g.sidebar_hovered;
    g.mouse_x = x;
    g.mouse_y = y;
    if (g.dialog != DIALOG_NONE || g.path_focused) {
        return;
    }
    if (g.menu_kind != MENU_NONE) {
        int old_menu_hover = g.menu_hovered;
        g.menu_hovered = menu_row_at(x, y);
        if (old_menu_hover != g.menu_hovered) request_full_repaint();
        return;
    }
    g.hovered_item = hit_item(x, y);
    g.sidebar_hovered = hit_sidebar(x, y);
    if (old_hover != g.hovered_item) {
        request_item_repaint(old_hover);
        request_item_repaint(g.hovered_item);
    }
    if (old_side != g.sidebar_hovered) request_sidebar_repaint();
}

static void mouse_wheel(int dz) {
    if (g.mouse_x < SIDEBAR_W) {
        g.sidebar_scroll_y -= dz * 28;
        clamp_scroll();
        request_sidebar_repaint();
    } else {
        g.scroll_y -= dz * 48;
        clamp_scroll();
        g.layout_dirty = true;
        request_full_repaint();
    }
}

static void handle_right_click(int x, int y) {
    if (g.dialog != DIALOG_NONE) return;
    int item = hit_item(x, y);
    if (item >= 0) {
        int old = g.selected_item;
        g.selected_item = item;
        request_item_repaint(old);
        request_item_repaint(item);
        if (g.items[item].type == ITEM_ARCHIVE || g.items[item].type == ITEM_UNSUPPORTED_ARCHIVE) {
            open_context_menu(MENU_ARCHIVE, item, x, y);
        } else if (g.items[item].is_dir) {
            open_context_menu(MENU_FOLDER, item, x, y);
        } else {
            open_context_menu(MENU_FILE, item, x, y);
        }
    } else if (x >= SIDEBAR_W && y >= HEADER_H) {
        int old = g.selected_item;
        g.selected_item = -1;
        request_item_repaint(old);
        open_context_menu(MENU_EMPTY, -1, x, y);
    }
}

static void init_state(void) {
    memset(&g, 0, sizeof(g));
    g.win_w = WIN_DEFAULT_W;
    g.win_h = WIN_DEFAULT_H;
    g.selected_item = -1;
    g.hovered_item = -1;
    g.sidebar_hovered = -1;
    g.menu_target = -1;
    g.menu_hovered = -1;
    g.last_clicked_item = -1;
    g.repaint_all = true;
    g.layout_dirty = true;
    str_copy(g.current_path, "/root", EXPLORER_MAX_PATH);
    str_copy(g.path_input, "/root", EXPLORER_MAX_PATH);
    g.path_cursor = (int)strlen(g.path_input);
}

int main(int argc, char **argv) {
    init_state();
    if (argc > 1 && argv[1] && argv[1][0]) {
        str_copy(g.current_path, argv[1], EXPLORER_MAX_PATH);
        str_copy(g.path_input, argv[1], EXPLORER_MAX_PATH);
    }

    g.win = ui_window_create("Files", 90, 70, g.win_w, g.win_h);
    if (!g.win) return 1;
    ui_window_set_resizable(g.win, true);

    if (!load_location(g.current_path, false)) {
        load_directory("/", false);
    }

    gui_event_t ev;
    while (1) {
        if (ui_get_event(g.win, &ev)) {
            if (ev.type == GUI_EVENT_PAINT) {
                request_full_repaint();
            } else if (ev.type == GUI_EVENT_RESIZE) {
                g.win_w = max_i(520, ev.arg1);
                g.win_h = max_i(360, ev.arg2);
                g.layout_dirty = true;
                request_full_repaint();
            } else if (ev.type == GUI_EVENT_MOUSE_DOWN) {
                mouse_down(ev.arg1, ev.arg2);
            } else if (ev.type == GUI_EVENT_MOUSE_UP) {
                mouse_up(ev.arg1, ev.arg2);
            } else if (ev.type == GUI_EVENT_MOUSE_MOVE) {
                mouse_move(ev.arg1, ev.arg2);
            } else if (ev.type == GUI_EVENT_MOUSE_WHEEL) {
                mouse_wheel(ev.arg1);
            } else if (ev.type == GUI_EVENT_RIGHT_CLICK) {
                handle_right_click(ev.arg1, ev.arg2);
            } else if (ev.type == GUI_EVENT_KEY) {
                handle_key(&ev);
            } else if (ev.type == GUI_EVENT_CLOSE) {
                sys_exit(0);
            }
        } else {
            if (g.needs_paint) paint();
            // otherwise this loop eats CPU
            sleep(8);
        }
        if (g.needs_paint) paint();
    }
    return 0;
}
