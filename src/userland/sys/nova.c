#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/kd.h>
#include <stdint.h>
#include <stdbool.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>
#include <sys/stat.h>
#include <syscall.h>
#include <signal.h>
#include <math.h>
#include "libtheme/theme.h"
#include "libui/ui.h"
#include "libnovaproto/novaproto.h"

#define MAX_CLIENTS 64
#define TITLEBAR_HEIGHT 26
#define BORDER_WIDTH 1
#define RESIZE_EDGE_MARGIN 8
#define RESIZE_MIN_CONTENT_W 64
#define RESIZE_MIN_CONTENT_H 48
#define RESIZE_REQUEST_INTERVAL_MS 120

#define RESIZE_EDGE_LEFT 16
#define RESIZE_EDGE_RIGHT 32
#define RESIZE_EDGE_TOP 64
#define RESIZE_EDGE_BOTTOM 128

#define SURFACE_FLAG_TRANSPARENT 0x1

// Autostart configuration items
typedef struct {
    char path[128];
    char name[32];
    int pid;
    bool respawn;
    int retry_count;
    uint32_t respawn_at_ms;
} autostart_t;

static autostart_t autostarts[8];
static int autostart_count = 0;

// Surface represention
typedef struct surface {
    uint32_t surface_id;
    int client_fd;
    int x, y; // Top-left of CLIENT CONTENT area (not decoration)
    uint32_t w, h;
    uint8_t layer;
    uint32_t flags;
    char title[128];
    char icon_path[256];
    uint32_t state_flags;
    char shm_path[128];
    uint32_t *pixels;
    uint32_t shm_size;
    bool mapped;
    bool focused;
    
    // Resizing transition segments (Wayland-style optimistic resizing)
    char pending_shm_path[128];
    uint32_t *pending_pixels;
    uint32_t pending_w, pending_h;

    // Drag states
    bool is_dragging;
    int drag_offset_x;
    int drag_offset_y;

    // Resize drag states
    bool is_resizing;
    int resize_edge;
    int resize_start_mouse_x;
    int resize_start_mouse_y;
    int resize_start_x;
    int resize_start_y;
    uint32_t resize_start_w;
    uint32_t resize_start_h;
    uint32_t resize_desired_w;
    uint32_t resize_desired_h;
    bool resize_request_queued;
    bool resize_force_next_request;
    uint32_t resize_last_request_ms;
    bool resize_preview_active;

    // Maximization states
    bool is_maximized;
    int normal_x, normal_y;
    uint32_t normal_w, normal_h;

    struct surface *next;
    struct surface *prev;
} surface_t;

static surface_t *surface_head = NULL;
static surface_t *surface_tail = NULL;
static uint32_t next_surface_id = 1;
static bool quit_loop = false;

// Global theme options
static uint32_t active_titlebar_top = 0xFF393939;
static uint32_t active_titlebar_bottom = 0xFF727272;
static uint32_t inactive_titlebar_top = 0xFF1F1E1E;
static uint32_t inactive_titlebar_bottom = 0xFF3C3C3C;
static uint32_t active_border = 0xFF393939;
static uint32_t inactive_border = 0xFF3C3C3C;
static uint32_t desktop_bg = 0xFF2D2D2D;

// Graphics structures
static int fb_fd = -1;
static uint32_t *fb_mem = NULL;
static uint32_t fb_size = 0;
static struct fb_fix_screeninfo finfo;
static struct fb_var_screeninfo vinfo;
static int screen_w = 0;
static int screen_h = 0;
static uint32_t *back_buffer = NULL;
static uint32_t *resize_preview_pixels = NULL;
static uint32_t resize_preview_capacity = 0;

void copy_box_to_fb(int bx, int by, int bw, int bh);

// Mouse coordinates
static int mx = 0;
static int my = 0;
static bool last_left_pressed = false;
static int last_cursor_x = -1;
static int last_cursor_y = -1;
static bool cursor_visible = false;

// Bounding box of the dirty region for partial compositing
static int dirty_x = 0;
static int dirty_y = 0;
static int dirty_w = 0;
static int dirty_h = 0;
static bool has_dirty_rect = false;

// Composition flags
static bool needs_composite = false;
static bool needs_cursor_only = false;

// Signal self-pipe descriptors
static int sig_pipe[2] = { -1, -1 };

// Keyboard Modifier states
static bool shift_pressed = false;
static bool ctrl_pressed = false;
static bool alt_pressed = false;

// List of all connected clients
typedef struct {
    int fd;
    bool active;
} client_t;

static client_t clients[MAX_CLIENTS];
static int client_count = 0;

// PS/2 Set 1 scancode to NovaKeycode conversion lookup table
static const NovaKeycode scancode_to_novakey[] = {
    [0x01] = KEY_ESCAPE,
    [0x02] = KEY_1, [0x03] = KEY_2, [0x04] = KEY_3, [0x05] = KEY_4,
    [0x06] = KEY_5, [0x07] = KEY_6, [0x08] = KEY_7, [0x09] = KEY_8,
    [0x0A] = KEY_9, [0x0B] = KEY_0,
    [0x0E] = KEY_BACKSPACE,
    [0x0F] = KEY_TAB,
    [0x10] = KEY_Q, [0x11] = KEY_W, [0x12] = KEY_E, [0x13] = KEY_R,
    [0x14] = KEY_T, [0x15] = KEY_Y, [0x16] = KEY_U, [0x17] = KEY_I,
    [0x18] = KEY_O, [0x19] = KEY_P,
    [0x1C] = KEY_ENTER,
    [0x1D] = KEY_LCTRL,
    [0x1E] = KEY_A, [0x1F] = KEY_S, [0x20] = KEY_D, [0x21] = KEY_F,
    [0x22] = KEY_G, [0x23] = KEY_H, [0x24] = KEY_J, [0x25] = KEY_K,
    [0x26] = KEY_L,
    [0x2A] = KEY_LSHIFT,
    [0x2C] = KEY_Z, [0x2D] = KEY_X, [0x2E] = KEY_C, [0x2F] = KEY_V,
    [0x30] = KEY_B, [0x31] = KEY_N, [0x32] = KEY_M,
    [0x36] = KEY_RSHIFT,
    [0x38] = KEY_LALT,
    [0x39] = KEY_SPACE
};

static const NovaKeycode scancode_to_novakey_ext[] = {
    [0x48] = KEY_UP,
    [0x50] = KEY_DOWN,
    [0x4B] = KEY_LEFT,
    [0x4D] = KEY_RIGHT,
    [0x1D] = KEY_RCTRL,
    [0x38] = KEY_RALT,
    [0x1C] = KEY_ENTER // Numpad Enter
};

// Autostart config INI parser
void load_nova_config(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return;

    bool has_desktop_bg = false;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char *start = line;
        while (*start && (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n')) start++;
        if (*start == '\0' || *start == ';' || *start == '#') continue;

        if (*start == '[') continue;

        char *eq = strchr(start, '=');
        if (!eq) continue;
        *eq = '\0';
        char *key = start;
        char *val = eq + 1;

        char *key_end = key + strlen(key) - 1;
        while (key_end >= key && (*key_end == ' ' || *key_end == '\t')) *key_end-- = '\0';

        while (*val && (*val == ' ' || *val == '\t')) val++;
        char *val_end = val + strlen(val) - 1;
        while (val_end >= val && (*val_end == ' ' || *val_end == '\t' || *val_end == '\r' || *val_end == '\n')) *val_end-- = '\0';

        if (strcmp(key, "active_titlebar_top") == 0) {
            active_titlebar_top = theme_resolve_color(val, active_titlebar_top);
        } else if (strcmp(key, "active_titlebar_bottom") == 0) {
            active_titlebar_bottom = theme_resolve_color(val, active_titlebar_bottom);
        } else if (strcmp(key, "inactive_titlebar_top") == 0) {
            inactive_titlebar_top = theme_resolve_color(val, inactive_titlebar_top);
        } else if (strcmp(key, "inactive_titlebar_bottom") == 0) {
            inactive_titlebar_bottom = theme_resolve_color(val, inactive_titlebar_bottom);
        } else if (strcmp(key, "active_border") == 0) {
            active_border = theme_resolve_color(val, active_border);
        } else if (strcmp(key, "inactive_border") == 0) {
            inactive_border = theme_resolve_color(val, inactive_border);
        } else if (strcmp(key, "desktop_bg") == 0) {
            desktop_bg = theme_resolve_color(val, desktop_bg);
            has_desktop_bg = true;
        } else if (strcmp(key, "panel_bg") == 0) {
            if (!has_desktop_bg) {
                desktop_bg = theme_resolve_color(val, desktop_bg);
            }
        } else if (strcmp(key, "shelf") == 0 || strcmp(key, "topbar") == 0 || strcmp(key, "taskbar") == 0 || strcmp(key, "wallpaperd") == 0) {
            if (autostart_count < 8) {
                strncpy(autostarts[autostart_count].path, val, sizeof(autostarts[autostart_count].path) - 1);
                strncpy(autostarts[autostart_count].name, key, sizeof(autostarts[autostart_count].name) - 1);
                autostarts[autostart_count].pid = 0;
                autostarts[autostart_count].respawn = true;
                autostarts[autostart_count].retry_count = 0;
                autostarts[autostart_count].respawn_at_ms = 0;
                autostart_count++;
            }
        }
    }
    fclose(f);
}

// Surface scene graph helpers
void surface_add(surface_t *surf) {
    if (!surface_head) {
        surface_head = surf;
        surface_tail = surf;
        surf->next = NULL;
        surf->prev = NULL;
    } else {
        // Append at tail (highest visual priority inside layer rendering order)
        surface_tail->next = surf;
        surf->prev = surface_tail;
        surf->next = NULL;
        surface_tail = surf;
    }
}

static void present_framebuffer(int x, int y, int w, int h) {
    if (fb_fd >= 0 && back_buffer && fb_mem) {
        if (w <= 0 || h <= 0) return;
        copy_box_to_fb(x, y, w, h);
    }
}

void surface_remove(surface_t *surf) {
    if (surf->prev) surf->prev->next = surf->next;
    else surface_head = surf->next;

    if (surf->next) surf->next->prev = surf->prev;
    else surface_tail = surf->prev;

    surf->next = NULL;
    surf->prev = NULL;
}

void surface_raise(surface_t *surf) {
    // Bring window to front of layer (move to tail)
    surface_remove(surf);
    surface_add(surf);
}

surface_t *surface_find(uint32_t surf_id) {
    surface_t *curr = surface_head;
    while (curr) {
        if (curr->surface_id == surf_id) return curr;
        curr = curr->next;
    }
    return NULL;
}

static int send_frame(int fd, uint32_t type, uint32_t surface_id, const void *payload, uint32_t size);

static uint32_t get_ticks_ms(void) {
    return (uint32_t)sys_system(16 /* SYSTEM_CMD_GET_TICKS */, 0, 0, 0, 0);
}

static void scale_nearest_rgba(uint32_t *dst, uint32_t dst_w, uint32_t dst_h, const uint32_t *src, uint32_t src_w, uint32_t src_h) {
    if (!dst || !src || dst_w == 0 || dst_h == 0 || src_w == 0 || src_h == 0) return;

    for (uint32_t y = 0; y < dst_h; y++) {
        uint32_t src_y = (uint64_t)y * src_h / dst_h;
        if (src_y >= src_h) src_y = src_h - 1;
        for (uint32_t x = 0; x < dst_w; x++) {
            uint32_t src_x = (uint64_t)x * src_w / dst_w;
            if (src_x >= src_w) src_x = src_w - 1;
            dst[y * dst_w + x] = src[src_y * src_w + src_x];
        }
    }
}

static void render_resize_preview(surface_t *surf, uint32_t src_w, uint32_t src_h, uint32_t new_w, uint32_t new_h) {
    if (!surf || !resize_preview_pixels) return;

    uint64_t needed = (uint64_t)new_w * (uint64_t)new_h;
    if (needed == 0 || needed > resize_preview_capacity) {
        surf->resize_preview_active = false;
        return;
    }

    if (surf->pixels) {
        scale_nearest_rgba(resize_preview_pixels, new_w, new_h, surf->pixels, src_w, src_h);
        surf->resize_preview_active = true;
    } else {
        surf->resize_preview_active = false;
    }
}

static void clear_resize_state(surface_t *surf) {
    if (!surf) return;

    surf->is_resizing = false;
    surf->resize_edge = 0;
    surf->resize_request_queued = false;
    surf->resize_force_next_request = false;
}

static void compute_resize_geometry(surface_t *surf, int edge, int mouse_x, int mouse_y, int *out_x, int *out_y, uint32_t *out_w, uint32_t *out_h) {
    if (!surf || !out_x || !out_y || !out_w || !out_h) return;

    int dx = mouse_x - surf->resize_start_mouse_x;
    int dy = mouse_y - surf->resize_start_mouse_y;

    int new_x = surf->resize_start_x;
    int new_y = surf->resize_start_y;
    uint32_t new_w = surf->resize_start_w;
    uint32_t new_h = surf->resize_start_h;

    uint32_t max_w = (screen_w > BORDER_WIDTH * 2) ? (uint32_t)(screen_w - BORDER_WIDTH * 2) : RESIZE_MIN_CONTENT_W;
    uint32_t max_h = (screen_h > TITLEBAR_HEIGHT + BORDER_WIDTH * 2) ? (uint32_t)(screen_h - TITLEBAR_HEIGHT - BORDER_WIDTH * 2) : RESIZE_MIN_CONTENT_H;

    if (edge & RESIZE_EDGE_LEFT) {
        int candidate_w = (int)surf->resize_start_w - dx;
        if (candidate_w < RESIZE_MIN_CONTENT_W) candidate_w = RESIZE_MIN_CONTENT_W;
        if ((uint32_t)candidate_w > max_w) candidate_w = (int)max_w;
        new_x = surf->resize_start_x + (int)surf->resize_start_w - candidate_w;
        new_w = (uint32_t)candidate_w;
    }

    if (edge & RESIZE_EDGE_RIGHT) {
        uint32_t candidate_w = surf->resize_start_w + dx;
        if (candidate_w < RESIZE_MIN_CONTENT_W) candidate_w = RESIZE_MIN_CONTENT_W;
        if (candidate_w > max_w) candidate_w = max_w;
        new_w = candidate_w;
    }

    if (edge & RESIZE_EDGE_TOP) {
        int candidate_h = (int)surf->resize_start_h - dy;
        if (candidate_h < RESIZE_MIN_CONTENT_H) candidate_h = RESIZE_MIN_CONTENT_H;
        if ((uint32_t)candidate_h > max_h) candidate_h = (int)max_h;
        new_y = surf->resize_start_y + (int)surf->resize_start_h - candidate_h;
        new_h = (uint32_t)candidate_h;
    }

    if (edge & RESIZE_EDGE_BOTTOM) {
        uint32_t candidate_h = surf->resize_start_h + dy;
        if (candidate_h < RESIZE_MIN_CONTENT_H) candidate_h = RESIZE_MIN_CONTENT_H;
        if (candidate_h > max_h) candidate_h = max_h;
        new_h = candidate_h;
    }

    *out_x = new_x;
    *out_y = new_y;
    *out_w = new_w;
    *out_h = new_h;
}

static void queue_resize_request(surface_t *surf, uint32_t new_w, uint32_t new_h, bool force) {
    if (!surf) return;

    surf->resize_desired_w = new_w;
    surf->resize_desired_h = new_h;
    surf->resize_request_queued = true;
    if (force) surf->resize_force_next_request = true;

    if (surf->pending_pixels) return;

    uint32_t now = get_ticks_ms();
    if (!force && surf->resize_last_request_ms != 0 && (uint32_t)(now - surf->resize_last_request_ms) < RESIZE_REQUEST_INTERVAL_MS) {
        return;
    }

    struct {
        uint32_t surface_id;
        uint32_t w, h;
    } __attribute__((packed)) payload = {surf->surface_id, new_w, new_h};

    if (send_frame(surf->client_fd, EVT_RESIZE_REQUEST, surf->surface_id, &payload, sizeof(payload)) == 0) {
        surf->resize_last_request_ms = now;
        surf->resize_request_queued = false;
        if (force) surf->resize_force_next_request = false;
    }
}

static void update_resize_drag(surface_t *surf, int mouse_x, int mouse_y) {
    if (!surf || !surf->is_resizing) return;

    uint32_t src_w = surf->resize_start_w;
    uint32_t src_h = surf->resize_start_h;
    int new_x = surf->resize_start_x;
    int new_y = surf->resize_start_y;
    uint32_t new_w = surf->resize_start_w;
    uint32_t new_h = surf->resize_start_h;

    compute_resize_geometry(surf, surf->resize_edge, mouse_x, mouse_y, &new_x, &new_y, &new_w, &new_h);

    surf->x = new_x;
    surf->y = new_y;
    surf->w = new_w;
    surf->h = new_h;

    render_resize_preview(surf, src_w, src_h, new_w, new_h);
    queue_resize_request(surf, new_w, new_h, false);
    needs_composite = true;
}

static void finish_resize_drag(surface_t *surf) {
    if (!surf || !surf->is_resizing) return;

    surf->is_resizing = false;
    surf->resize_force_next_request = true;
    queue_resize_request(surf, surf->w, surf->h, true);
}

// focused normal window
surface_t *surface_get_focused(void) {
    surface_t *curr = surface_tail;
    while (curr) {
        if (curr->focused && (curr->layer == 1 || curr->layer == 2 || curr->layer == 4 || curr->layer == 5)) return curr;
        curr = curr->prev;
    }
    return NULL;
}

static int send_all(int fd, const void *buf, size_t size) {
    size_t written = 0;
    while (written < size) {
        ssize_t rc = send(fd, (const char *)buf + written, size - written, 0);
        if (rc <= 0) return -1;
        written += rc;
    }
    return 0;
}

static int recv_all(int fd, void *buf, size_t size) {
    size_t read_bytes = 0;
    while (read_bytes < size) {
        ssize_t rc = recv(fd, (char *)buf + read_bytes, size - read_bytes, 0);
        if (rc <= 0) return -1;
        read_bytes += rc;
    }
    return 0;
}

// Send frame headers to socket clients
static int send_frame(int fd, uint32_t type, uint32_t surface_id, const void *payload, uint32_t size) {
    (void)surface_id;
    NovaFrameHeader header;
    header.magic = NOVA_MAGIC;
    header.version = NOVA_VERSION;
    header.flags = 0;
    header.msg_type = type;
    header.payload_size = size;

    // Send header
    if (send_all(fd, &header, sizeof(header)) < 0) {
        return -1;
    }

    // Send payload
    if (size > 0 && payload) {
        if (send_all(fd, payload, size) < 0) {
            return -1;
        }
    }
    return 0;
}

// Broadcast window created/destroyed events to OVERLAY docks
void broadcast_window_event(uint32_t msg_type, surface_t *surf) {
    struct {
        uint32_t surface_id;
        char title[128];
        uint32_t state_flags;
        char icon_path[256];
    } __attribute__((packed)) payload;

    payload.surface_id = surf->surface_id;
    strncpy(payload.title, surf->title, 127);
    payload.title[127] = '\0';
    payload.state_flags = surf->state_flags;
    strncpy(payload.icon_path, surf->icon_path, 255);
    payload.icon_path[255] = '\0';

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].active) {
            // Find overlay clients
            surface_t *c = surface_head;
            while (c) {
                if (c->client_fd == clients[i].fd && c->layer == 3 /* OVERLAY */) {
                    send_frame(clients[i].fd, msg_type, surf->surface_id, &payload, sizeof(payload));
                    break;
                }
                c = c->next;
            }
        }
    }
}

// Sets system focus
void set_focus(surface_t *surf) {
    surface_t *focused = surface_get_focused();
    if (focused == surf) return;

    if (focused) {
        focused->focused = false;
        focused->state_flags &= ~1; // remove active flag
        uint32_t pl = focused->surface_id;
        send_frame(focused->client_fd, EVT_FOCUS_OUT, focused->surface_id, &pl, 4);
        broadcast_window_event(EVT_STATE_CHANGED, focused);
    }

    if (surf) {
        // If focusing a minimized (unmapped) surface, map it again
        if (!surf->mapped) {
            surf->mapped = true;
            needs_composite = true;
        }
        surf->focused = true;
        surf->state_flags |= 1; // set active flag
        surface_raise(surf);
        uint32_t pl = surf->surface_id;
        send_frame(surf->client_fd, EVT_FOCUS_IN, surf->surface_id, &pl, 4);
        broadcast_window_event(EVT_STATE_CHANGED, surf);
    }
    needs_composite = true;
}

void toggle_maximize(surface_t *surf) {
    if (!surf) return;

    if (surf->is_maximized) {
        surf->x = surf->normal_x;
        surf->y = surf->normal_y;
        surf->is_maximized = false;

        struct {
            uint32_t surface_id;
            uint32_t w, h;
        } __attribute__((packed)) resize_payload = {surf->surface_id, surf->normal_w, surf->normal_h};
        send_frame(surf->client_fd, EVT_RESIZE_REQUEST, surf->surface_id, &resize_payload, sizeof(resize_payload));
        needs_composite = true;
    } else {
        surf->normal_x = surf->x;
        surf->normal_y = surf->y;
        surf->normal_w = surf->w;
        surf->normal_h = surf->h;

        surf->x = BORDER_WIDTH;
        surf->y = TITLEBAR_HEIGHT + BORDER_WIDTH;

        uint32_t target_w = screen_w - BORDER_WIDTH * 2;
        uint32_t target_h = screen_h - TITLEBAR_HEIGHT - BORDER_WIDTH * 2;

        bool taskbar_present = false;
        surface_t *curr = surface_head;
        while (curr) {
            if (curr->layer == 3) {
                taskbar_present = true;
                break;
            }
            curr = curr->next;
        }

        if (taskbar_present) {
            target_h -= 26; // Subtract taskbar height
        }

        surf->is_maximized = true;

        struct {
            uint32_t surface_id;
            uint32_t w, h;
        } __attribute__((packed)) resize_payload = {surf->surface_id, target_w, target_h};
        send_frame(surf->client_fd, EVT_RESIZE_REQUEST, surf->surface_id, &resize_payload, sizeof(resize_payload));
        needs_composite = true;
    }
}

static uint32_t lerp_color(uint32_t top_color, uint32_t bottom_color, float t) {
    uint32_t top_a = (top_color >> 24) & 0xFF;
    uint32_t top_r = (top_color >> 16) & 0xFF;
    uint32_t top_g = (top_color >> 8) & 0xFF;
    uint32_t top_b = top_color & 0xFF;

    uint32_t bot_a = (bottom_color >> 24) & 0xFF;
    uint32_t bot_r = (bottom_color >> 16) & 0xFF;
    uint32_t bot_g = (bottom_color >> 8) & 0xFF;
    uint32_t bot_b = bottom_color & 0xFF;

    uint32_t a = (uint32_t)(top_a + (bot_a - top_a) * t);
    uint32_t r = (uint32_t)(top_r + (bot_r - top_r) * t);
    uint32_t g = (uint32_t)(top_g + (bot_g - top_g) * t);
    uint32_t b = (uint32_t)(top_b + (bot_b - top_b) * t);
    return (a << 24) | (r << 16) | (g << 8) | b;
}

void draw_gradient_titlebar(uint32_t *buffer, int w, int h, int tx, int ty, int tw, int th, uint32_t border_color, uint32_t top_color, uint32_t bottom_color, int radius) {
    int x1 = tx;
    int y1 = ty;
    int x2 = tx + tw;
    int y2 = ty + th;

    if (x1 < 0) x1 = 0;
    if (y1 < 0) y1 = 0;
    if (x2 > w) x2 = w;
    if (y2 > h) y2 = h;

    uint32_t bo_a = (border_color >> 24) & 0xFF;
    uint32_t bo_r = (border_color >> 16) & 0xFF;
    uint32_t bo_g = (border_color >> 8) & 0xFF;
    uint32_t bo_b = border_color & 0xFF;

    for (int py = y1; py < y2; py++) {
        uint32_t *row = &buffer[py * w];
        float t = (float)(py - ty) / th;
        uint32_t bg = lerp_color(top_color, bottom_color, t);
        uint32_t bg_a = (bg >> 24) & 0xFF;
        uint32_t bg_r = (bg >> 16) & 0xFF;
        uint32_t bg_g = (bg >> 8) & 0xFF;
        uint32_t bg_b = bg & 0xFF;

        for (int px = x1; px < x2; px++) {
            int dx = 0, dy = 0;
            if (px < tx + radius) dx = (tx + radius) - px;
            else if (px >= tx + tw - radius) dx = px - (tx + tw - radius - 1);
            
            if (py < ty + radius) dy = (ty + radius) - py;
            
            uint32_t draw_a = bg_a;
            uint32_t draw_r = bg_r;
            uint32_t draw_g = bg_g;
            uint32_t draw_b = bg_b;

            if (dx > 0 && dy > 0) {
                float dist = (float)sqrt((double)(dx * dx + dy * dy));
                if (dist > (float)radius) {
                    float diff = dist - (float)radius;
                    if (diff < 1.0f) {
                        draw_a = (uint32_t)(255.0f * (1.0f - diff));
                    } else {
                        continue;
                    }
                }
            }

            // Outline border (1px)
            bool is_border = false;
            if (dx > 0 && dy > 0) {
                float dist = (float)sqrt((double)(dx * dx + dy * dy));
                if (dist >= (float)(radius - 1.0f) && dist <= (float)radius) {
                    is_border = true;
                }
            } else {
                if (px == tx || px == tx + tw - 1 || py == ty) {
                    is_border = true;
                }
            }

            if (is_border) {
                draw_a = bo_a;
                draw_r = bo_r;
                draw_g = bo_g;
                draw_b = bo_b;
            }

            if (draw_a == 255) {
                row[px] = (draw_a << 24) | (draw_r << 16) | (draw_g << 8) | draw_b;
            } else if (draw_a > 0) {
                uint32_t bg_pixel = row[px];
                uint32_t dest_a = (bg_pixel >> 24) & 0xFF;
                uint32_t dest_r = (bg_pixel >> 16) & 0xFF;
                uint32_t dest_g = (bg_pixel >> 8) & 0xFF;
                uint32_t dest_b = bg_pixel & 0xFF;

                uint32_t out_a = draw_a + dest_a * (255 - draw_a) / 255;
                if (out_a == 0) continue;
                uint32_t out_r = (draw_r * draw_a + dest_r * dest_a * (255 - draw_a) / 255) / out_a;
                uint32_t out_g = (draw_g * draw_a + dest_g * dest_a * (255 - draw_a) / 255) / out_a;
                uint32_t out_b = (draw_b * draw_a + dest_b * dest_a * (255 - draw_a) / 255) / out_a;

                row[px] = (out_a << 24) | (out_r << 16) | (out_g << 8) | out_b;
            }
        }
    }
}

// Finds window under mouse cursor (checks backwards from front tail to head)
surface_t *surface_at(int px, int py, int *click_region_out) {
    // 0 = content area, 1 = titlebar, 2 = close, 4 = min, resize edges use bit flags
    surface_t *curr = surface_tail;
    surface_t *best = NULL;
    int best_layer = -1;
    int best_region = 0;

    while (curr) {
        if (!curr->mapped) {
            curr = curr->prev;
            continue;
        }

        // Overlay & Desktop layouts have no titlebars
        bool has_decorations = (curr->layer == 1 || curr->layer == 2);

        bool hit = false;
        int region = 0;

        if (has_decorations) {
            int outer_left = curr->x - BORDER_WIDTH;
            int outer_top = curr->y - TITLEBAR_HEIGHT - BORDER_WIDTH;
            int outer_right = curr->x + (int)curr->w + BORDER_WIDTH;
            int outer_bottom = curr->y + (int)curr->h + BORDER_WIDTH;

            bool in_outer = px >= outer_left && px < outer_right && py >= outer_top && py < outer_bottom;

            bool on_titlebar = (py >= curr->y - TITLEBAR_HEIGHT && py < curr->y);
            if (px >= curr->x && px < curr->x + (int)curr->w && on_titlebar) {
                if (px >= curr->x + (int)curr->w - 24 && px < curr->x + (int)curr->w) {
                    region = 2;
                }
                else if (px >= curr->x + (int)curr->w - 48 && px < curr->x + (int)curr->w - 24) {
                    region = 4;
                } else {
                    region = 1;
                }
                hit = true;
            }

            if (in_outer && region != 2 && region != 4 && !on_titlebar) {
                bool near_left = px < outer_left + RESIZE_EDGE_MARGIN;
                bool near_right = px >= outer_right - RESIZE_EDGE_MARGIN;
                bool near_top = py < outer_top + RESIZE_EDGE_MARGIN;
                bool near_bottom = py >= outer_bottom - RESIZE_EDGE_MARGIN;

                if (near_left || near_right || near_top || near_bottom) {
                    region = 0;
                    if (near_left) region |= RESIZE_EDGE_LEFT;
                    if (near_right) region |= RESIZE_EDGE_RIGHT;
                    if (near_top) region |= RESIZE_EDGE_TOP;
                    if (near_bottom) region |= RESIZE_EDGE_BOTTOM;
                    hit = true;
                }
            }

            // Check Content area
            if (!hit && px >= curr->x && px < curr->x + (int)curr->w &&
                py >= curr->y && py < curr->y + (int)curr->h) {
                region = 0;
                hit = true;
            }
        } else {
            // Decoration-less layers
            if (px >= curr->x && px < curr->x + (int)curr->w &&
                py >= curr->y && py < curr->y + (int)curr->h) {
                region = 0;
                hit = true;
            }
        }

        if (hit) {
            if (curr->layer > best_layer) {
                best = curr;
                best_layer = curr->layer;
                best_region = region;
                if (best_layer >= 5) break;
            }
        }
        curr = curr->prev;
    }

    if (best && click_region_out) *click_region_out = best_region;
    return best;
}

// Autostart Spawner Engine
void spawn_autostart(int idx) {
    // Spawn using sys_spawn with terminal and tty-inheritance flags
    int pid = sys_spawn(autostarts[idx].path, NULL, 0x2 /* SPAWN_FLAG_INHERIT_TTY */, 0);
    if (pid > 0) {
        autostarts[idx].pid = pid;
        autostarts[idx].respawn_at_ms = 0;
        printf("Compositor: Autostart %s spawned (PID %d)\n", autostarts[idx].name, pid);
    } else {
        printf("Compositor: Failed to spawn autostart %s\n", autostarts[idx].name);
        autostarts[idx].pid = 0;
        autostarts[idx].respawn_at_ms = 0;
    }
}

void trigger_autostart_respawn(int pid) {
    uint32_t now = (uint32_t)sys_system(16 /* SYSTEM_CMD_GET_TICKS */, 0, 0, 0, 0);
    for (int i = 0; i < autostart_count; i++) {
        if (autostarts[i].pid == pid) {
            autostarts[i].pid = 0;
            if (autostarts[i].respawn) {
                autostarts[i].retry_count++;
                
                // Exponential back-off delay: 1s, 2s, 4s, limit at 8s
                uint32_t delay = 1000 << (autostarts[i].retry_count - 1);
                if (delay > 8000) delay = 8000;
                
                autostarts[i].respawn_at_ms = now + delay;
                printf("Compositor: Autostart child %s (PID %d) exited. Scheduling respawn in %d ms.\n",
                       autostarts[i].name, pid, delay);
            }
            break;
        }
    }
}

// Signal handler routing
void handle_signal(int sig) {
    uint8_t byte = (uint8_t)sig;
    write(sig_pipe[1], &byte, 1);
}

// Drawing mouse cursor
void draw_cursor(uint32_t *buffer, int w, int h, int mouse_x, int mouse_y) {
    int cursor_w = 12;
    int cursor_h = 19;
    const char *cursor_sprite[] = {
        "X                  ",
        "XX                 ",
        "X.X                ",
        "X..X               ",
        "X...X              ",
        "X....X             ",
        "X.....X            ",
        "X......X           ",
        "X.......X          ",
        "X........X         ",
        "X.........X        ",
        "X......XXXXX       ",
        "X...X..X           ",
        "X..X X..X          ",
        "X.X  X..X          ",
        "XX    X..X         ",
        "      X..X         ",
        "       XX          ",
        "                   "
    };
    for (int y = 0; y < cursor_h; y++) {
        for (int x = 0; x < cursor_w; x++) {
            int px = mouse_x + x;
            int py = mouse_y + y;
            if (px >= 0 && px < w && py >= 0 && py < h) {
                char c = cursor_sprite[y][x];
                if (c == 'X') {
                    buffer[py * w + px] = 0xFF000000; // Black outline
                } else if (c == '.') {
                    buffer[py * w + px] = 0xFFFFFFFF; // White body
                }
            }
        }
    }
}
// Having this as a font would be "better", but for now this is good enough and "just works".
void copy_box_to_fb(int bx, int by, int bw, int bh) {
    if (bw <= 0 || bh <= 0 || bx >= screen_w || by >= screen_h || bx + bw <= 0 || by + bh <= 0) {
        return;
    }

    int start_x = bx < 0 ? 0 : bx;
    int end_x = (bx + bw > screen_w) ? screen_w : bx + bw;
    int start_y = by < 0 ? 0 : by;
    int end_y = (by + bh > screen_h) ? screen_h : by + bh;
    int copy_w = end_x - start_x;
    if (copy_w <= 0) return;

    for (int y = start_y; y < end_y; y++) {
        uint8_t *fb_row_bytes = (uint8_t*)fb_mem + (uint64_t)y * finfo.line_length;
        uint32_t *fb_row = (uint32_t*)(fb_row_bytes + (uint64_t)start_x * sizeof(uint32_t));
        uint32_t *bb_row = &back_buffer[y * screen_w + start_x];
        memcpy(fb_row, bb_row, (size_t)copy_w * sizeof(uint32_t));
    }
}

static bool rects_intersect(int ax, int ay, int aw, int ah, int bx, int by, int bw, int bh) {
    return ax < bx + bw && ax + aw > bx && ay < by + bh && ay + ah > by;
}

static void surface_visual_bounds(const surface_t *surf, int *out_x, int *out_y, int *out_w, int *out_h) {
    if (!surf || !out_x || !out_y || !out_w || !out_h) return;

    bool has_decorations = (surf->layer == 1 || surf->layer == 2);
    int border = has_decorations ? BORDER_WIDTH : 0;
    int titlebar = has_decorations ? (TITLEBAR_HEIGHT + BORDER_WIDTH) : 0;

    *out_x = surf->x - border;
    *out_y = surf->y - titlebar;
    *out_w = (int)surf->w + border * 2;
    *out_h = (int)surf->h + border + titlebar;
}

void compositor_composite(void);
void update_cursor_atomic_combined(int new_x, int new_y);
void copy_box_to_fb(int bx, int by, int bw, int bh);

static bool rect_intersect(int ax, int ay, int aw, int ah, int bx, int by, int bw, int bh, int *out_x, int *out_y, int *out_w, int *out_h) {
    int x1 = ax > bx ? ax : bx;
    int y1 = ay > by ? ay : by;
    int x2 = (ax + aw) < (bx + bw) ? (ax + aw) : (bx + bw);
    int y2 = (ay + ah) < (by + bh) ? (ay + ah) : (by + bh);
    if (x2 <= x1 || y2 <= y1) return false;
    *out_x = x1;
    *out_y = y1;
    *out_w = x2 - x1;
    *out_h = y2 - y1;
    return true;
}

static void compositor_composite_region(int x, int y, int w, int h) {
    bool prev_has_dirty = has_dirty_rect;
    int prev_x = dirty_x;
    int prev_y = dirty_y;
    int prev_w = dirty_w;
    int prev_h = dirty_h;

    has_dirty_rect = true;
    dirty_x = x;
    dirty_y = y;
    dirty_w = w;
    dirty_h = h;

    compositor_composite();

    has_dirty_rect = prev_has_dirty;
    dirty_x = prev_x;
    dirty_y = prev_y;
    dirty_w = prev_w;
    dirty_h = prev_h;
}

static void translate_back_buffer_region(int src_x, int src_y, int dst_x, int dst_y, int w, int h) {
    if (w <= 0 || h <= 0) return;

    if (src_x < 0) {
        int delta = -src_x;
        src_x += delta;
        dst_x += delta;
        w -= delta;
    }
    if (src_y < 0) {
        int delta = -src_y;
        src_y += delta;
        dst_y += delta;
        h -= delta;
    }
    if (dst_x < 0) {
        int delta = -dst_x;
        src_x += delta;
        dst_x += delta;
        w -= delta;
    }
    if (dst_y < 0) {
        int delta = -dst_y;
        src_y += delta;
        dst_y += delta;
        h -= delta;
    }

    if (src_x + w > screen_w) w = screen_w - src_x;
    if (dst_x + w > screen_w) w = screen_w - dst_x;
    if (src_y + h > screen_h) h = screen_h - src_y;
    if (dst_y + h > screen_h) h = screen_h - dst_y;
    if (w <= 0 || h <= 0) return;

    int row_start = 0;
    int row_end = h;
    int row_step = 1;
    if (dst_y > src_y) {
        row_start = h - 1;
        row_end = -1;
        row_step = -1;
    }

    for (int row = row_start; row != row_end; row += row_step) {
        uint32_t *src_row = &back_buffer[(src_y + row) * screen_w + src_x];
        uint32_t *dst_row = &back_buffer[(dst_y + row) * screen_w + dst_x];
        memmove(dst_row, src_row, (size_t)w * sizeof(uint32_t));
    }
}

static bool try_fast_translate_drag(surface_t *surf, int old_vis_x, int old_vis_y, int old_vis_w, int old_vis_h, int new_vis_x, int new_vis_y, int new_vis_w, int new_vis_h) {
    (void)surf; (void)old_vis_x; (void)old_vis_y; (void)old_vis_w; (void)old_vis_h;
    (void)new_vis_x; (void)new_vis_y; (void)new_vis_w; (void)new_vis_h;
    return false;
}

static void blit_surface_pixels(surface_t *surf, uint32_t dst_x, uint32_t dst_y, uint32_t copy_w, uint32_t copy_h) {
    if (!surf || !surf->pixels || copy_w == 0 || copy_h == 0) return;

    bool opaque = (surf->flags & SURFACE_FLAG_TRANSPARENT) == 0;
    if (!opaque) {
        ui_blend_pixels(back_buffer, screen_w, screen_h, surf->x, surf->y, surf->pixels, surf->w, surf->h, 1.0f);
        return;
    }

    uint32_t src_x = 0;
    uint32_t src_y = 0;
    uint32_t draw_x = dst_x;
    uint32_t draw_y = dst_y;
    uint32_t draw_w = copy_w;
    uint32_t draw_h = copy_h;

    if ((int)draw_x < surf->x) {
        uint32_t delta = (uint32_t)(surf->x - (int)draw_x);
        src_x += delta;
        draw_x += delta;
        draw_w -= delta;
    }
    if ((int)draw_y < surf->y) {
        uint32_t delta = (uint32_t)(surf->y - (int)draw_y);
        src_y += delta;
        draw_y += delta;
        draw_h -= delta;
    }
    if (draw_x + draw_w > (uint32_t)screen_w) draw_w = (uint32_t)screen_w - draw_x;
    if (draw_y + draw_h > (uint32_t)screen_h) draw_h = (uint32_t)screen_h - draw_y;
    if (draw_w == 0 || draw_h == 0) return;

    for (uint32_t y = 0; y < draw_h; y++) {
        uint32_t *dst_row = &back_buffer[(draw_y + y) * screen_w + draw_x];
        uint32_t *src_row = &surf->pixels[(src_y + y) * surf->w + src_x];
        memcpy(dst_row, src_row, (size_t)draw_w * sizeof(uint32_t));
    }
}

void update_cursor_atomic_combined(int new_x, int new_y) {
    int cursor_w = 12;
    int cursor_h = 19;
    
    // Calculate combined bounding box of old and new cursor
    int min_x = new_x;
    int min_y = new_y;
    int max_x = new_x + cursor_w;
    int max_y = new_y + cursor_h;
    
    if (cursor_visible) {
        if (last_cursor_x < min_x) min_x = last_cursor_x;
        if (last_cursor_y < min_y) min_y = last_cursor_y;
        if (last_cursor_x + cursor_w > max_x) max_x = last_cursor_x + cursor_w;
        if (last_cursor_y + cursor_h > max_y) max_y = last_cursor_y + cursor_h;
    }
    
    // Clamp to screen boundaries
    if (min_x < 0) min_x = 0;
    if (min_y < 0) min_y = 0;
    if (max_x > screen_w) max_x = screen_w;
    if (max_y > screen_h) max_y = screen_h;
    
    int bw = max_x - min_x;
    int bh = max_y - min_y;
    
    if (bw <= 0 || bh <= 0) return;
    
    uint32_t saved_pixels[12 * 19];
    for (int y = 0; y < cursor_h; y++) {
        int py = new_y + y;
        uint32_t *bb_row = (py >= 0 && py < screen_h) ? &back_buffer[py * screen_w] : NULL;
        for (int x = 0; x < cursor_w; x++) {
            int px = new_x + x;
            if (bb_row && px >= 0 && px < screen_w) {
                saved_pixels[y * cursor_w + x] = bb_row[px];
            } else {
                saved_pixels[y * cursor_w + x] = desktop_bg; // Fallback
            }
        }
    }
    draw_cursor(back_buffer, screen_w, screen_h, new_x, new_y);
    copy_box_to_fb(min_x, min_y, bw, bh);
    
    for (int y = 0; y < cursor_h; y++) {
        int py = new_y + y;
        if (py >= 0 && py < screen_h) {
            uint32_t *bb_row = &back_buffer[py * screen_w];
            for (int x = 0; x < cursor_w; x++) {
                int px = new_x + x;
                if (px >= 0 && px < screen_w) {
                    bb_row[px] = saved_pixels[y * cursor_w + x];
                }
            }
        }
    }
    last_cursor_x = new_x;
    last_cursor_y = new_y;
    cursor_visible = true;
}

// Compositor Render Loop
void compositor_composite(void) {
    int render_x = 0;
    int render_y = 0;
    int render_w = screen_w;
    int render_h = screen_h;
    bool partial_render = has_dirty_rect && dirty_w > 0 && dirty_h > 0;

    if (partial_render) {
        render_x = dirty_x;
        render_y = dirty_y;
        render_w = dirty_w;
        render_h = dirty_h;
    }

    // Fill background solid desktop background color
    if (partial_render) {
        for (int y = render_y; y < render_y + render_h; y++) {
            uint32_t *row = &back_buffer[y * screen_w];
            for (int x = render_x; x < render_x + render_w; x++) {
                row[x] = desktop_bg;
            }
        }
    } else {
        for (int i = 0; i < screen_w * screen_h; i++) {
            back_buffer[i] = desktop_bg;
        }
    }

    // Render surfaces in z-order (pass 0 up to 5)
    for (int layer = 0; layer <= 5; layer++) {
        surface_t *curr = surface_head;
        while (curr) {
            if (curr->mapped && curr->layer == layer) {
                if (partial_render) {
                    int surf_x = 0;
                    int surf_y = 0;
                    int surf_w = 0;
                    int surf_h = 0;
                    surface_visual_bounds(curr, &surf_x, &surf_y, &surf_w, &surf_h);
                    if (!rects_intersect(surf_x, surf_y, surf_w, surf_h, render_x, render_y, render_w, render_h)) {
                        curr = curr->next;
                        continue;
                    }
                }

                bool has_decorations = (layer == 1 || layer == 2);
                uint32_t border_color = curr->focused ? active_border : inactive_border;

                // Render titlebar if NORMAL or FLOATING window
                if (has_decorations) {
                    ui_draw_panel(back_buffer, screen_w, screen_h, 
                                  curr->x - BORDER_WIDTH, curr->y,
                                  curr->w + BORDER_WIDTH * 2, curr->h + BORDER_WIDTH,
                                  desktop_bg, border_color, 0);

                    ui_draw_panel(back_buffer, screen_w, screen_h,
                                  curr->x, curr->y, curr->w, curr->h,
                                  desktop_bg, 0, 0);

                    draw_gradient_titlebar(back_buffer, screen_w, screen_h,
                                           curr->x - BORDER_WIDTH, curr->y - TITLEBAR_HEIGHT - BORDER_WIDTH,
                                           curr->w + BORDER_WIDTH * 2, TITLEBAR_HEIGHT + BORDER_WIDTH,
                                           border_color,
                                           curr->focused ? active_titlebar_top : inactive_titlebar_top,
                                           curr->focused ? active_titlebar_bottom : inactive_titlebar_bottom,
                                           8);

                    // Draw titlebar header string text
                    ui_draw_string(back_buffer, screen_w, screen_h, curr->x + 10, curr->y - 20, curr->title, 0xFFFFFFFF);

                    uint32_t btn_color = curr->focused ? 0xFFFFFFFF : 0xFF7F849C;

                    int min_bx = curr->x + curr->w - 38;
                    int min_by = curr->y - 20;
                    for (int iy = 8; iy <= 9; iy++) {
                        for (int ix = 2; ix <= 9; ix++) {
                            int px = min_bx + ix;
                            int py = min_by + iy;
                            if (px >= 0 && px < screen_w && py >= 0 && py < screen_h) {
                                back_buffer[py * screen_w + px] = btn_color;
                            }
                        }
                    }

                    int cls_bx = curr->x + curr->w - 20;
                    int cls_by = curr->y - 20;
                    for (int i = 0; i <= 7; i++) {
                        int px1 = cls_bx + 2 + i;
                        int py1 = cls_by + 2 + i;
                        int px2 = cls_bx + 9 - i;
                        int py2 = cls_by + 2 + i;
                        if (px1 >= 0 && px1 < screen_w && py1 >= 0 && py1 < screen_h) {
                            back_buffer[py1 * screen_w + px1] = btn_color;
                        }
                        if (px2 >= 0 && px2 < screen_w && py2 >= 0 && py2 < screen_h) {
                            back_buffer[py2 * screen_w + px2] = btn_color;
                        }
                    }
                }

                // Blend client's mapped shm pixels onto backbuffer
                uint32_t *blend_pixels = (curr->resize_preview_active && resize_preview_pixels) ? resize_preview_pixels : curr->pixels;
                if (blend_pixels) {
                    surface_t temp = *curr;
                    temp.pixels = blend_pixels;
                    blit_surface_pixels(&temp, curr->x, curr->y, curr->w, curr->h);
                }
            }
            curr = curr->next;
        }
    }

    int cursor_w = 12;
    int cursor_h = 19;
    uint32_t saved_pixels[12 * 19];
    for (int y = 0; y < cursor_h; y++) {
        int py = my + y;
        uint32_t *bb_row = (py >= 0 && py < screen_h) ? &back_buffer[py * screen_w] : NULL;
        for (int x = 0; x < cursor_w; x++) {
            int px = mx + x;
            if (bb_row && px >= 0 && px < screen_w) {
                saved_pixels[y * cursor_w + x] = bb_row[px];
            } else {
                saved_pixels[y * cursor_w + x] = desktop_bg;
            }
        }
    }

    draw_cursor(back_buffer, screen_w, screen_h, mx, my);

    if (has_dirty_rect) {
        int min_x = dirty_x;
        int min_y = dirty_y;
        int max_x = dirty_x + dirty_w;
        int max_y = dirty_y + dirty_h;

        if (mx < min_x) min_x = mx;
        if (my < min_y) min_y = my;
        if (mx + cursor_w > max_x) max_x = mx + cursor_w;
        if (my + cursor_h > max_y) max_y = my + cursor_h;

        if (cursor_visible) {
            if (last_cursor_x < min_x) min_x = last_cursor_x;
            if (last_cursor_y < min_y) min_y = last_cursor_y;
            if (last_cursor_x + cursor_w > max_x) max_x = last_cursor_x + cursor_w;
            if (last_cursor_y + cursor_h > max_y) max_y = last_cursor_y + cursor_h;
        }

        if (min_x < 0) min_x = 0;
        if (min_y < 0) min_y = 0;
        if (max_x > screen_w) max_x = screen_w;
        if (max_y > screen_h) max_y = screen_h;

        dirty_x = min_x;
        dirty_y = min_y;
        dirty_w = max_x - min_x;
        dirty_h = max_y - min_y;
    }

    // Blit backbuffer directly to hardware framebuffer mapped address space
    if (has_dirty_rect) {
        copy_box_to_fb(dirty_x, dirty_y, dirty_w, dirty_h);
        has_dirty_rect = false; // Reset
    } else {
        for (int y = 0; y < screen_h; y++) {
            memcpy((uint8_t*)fb_mem + (uint64_t)y * finfo.line_length, &back_buffer[y * screen_w], screen_w * 4);
        }
    }

    // Restore the clean pixels back to back_buffer
    for (int y = 0; y < cursor_h; y++) {
        int py = my + y;
        if (py >= 0 && py < screen_h) {
            uint32_t *bb_row = &back_buffer[py * screen_w];
            for (int x = 0; x < cursor_w; x++) {
                int px = mx + x;
                if (px >= 0 && px < screen_w) {
                    bb_row[px] = saved_pixels[y * cursor_w + x];
                }
            }
        }
    }

    // Update state
    last_cursor_x = mx;
    last_cursor_y = my;
    cursor_visible = true;
}

// Client IPC message handlers
void handle_client_message(int fd, surface_t **surf_ptr) {
    NovaFrameHeader header;
    uint8_t buffer[1024];

    if (recv_all(fd, &header, sizeof(header)) < 0) {
        return;
    }

    if (header.magic != NOVA_MAGIC) {
        return;
    }

    if (header.payload_size > 0) {
        uint32_t to_read = header.payload_size;
        if (to_read > sizeof(buffer)) to_read = sizeof(buffer);
        
        if (recv_all(fd, buffer, to_read) < 0) {
            return;
        }
    }

    switch (header.msg_type) {
        case MSG_CREATE_SURFACE: {
            struct {
                uint32_t w, h;
                uint8_t layer;
                uint32_t flags;
            } __attribute__((packed)) *p = (void *)buffer;

            surface_t *surf = malloc(sizeof(surface_t));
            memset(surf, 0, sizeof(surface_t));
            surf->surface_id = next_surface_id++;
            surf->client_fd = fd;
            surf->w = p->w;
            surf->h = p->h;
            surf->layer = p->layer;
            surf->flags = p->flags;
            surf->state_flags = 0;
            surf->mapped = true;
            surf->resize_last_request_ms = 0;

            // Generate unique shm name containing Client fd and surface ID
            snprintf(surf->shm_path, sizeof(surf->shm_path), "/dev/shm/nova_surf_fd%d_%u", fd, surf->surface_id);

            // Pre-allocate and map segment inside Nova
            int shm_fd = open(surf->shm_path, O_RDWR | O_CREAT, 0777);
            if (shm_fd >= 0) {
                uint32_t sz = p->w * p->h * 4;
                surf->shm_size = sz;
                
                // Write zeros to pre-allocate size on shmfs VFS
                uint8_t *zeros = malloc(4096);
                memset(zeros, 0, 4096);
                uint32_t written = 0;
                while (written < sz) {
                    uint32_t chunk = (sz - written > 4096) ? 4096 : (sz - written);
                    write(shm_fd, zeros, chunk);
                    written += chunk;
                }
                free(zeros);

                surf->pixels = mmap(NULL, sz, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
                close(shm_fd);
            }

            if (surf->layer == 1 || surf->layer == 2) {
                surf->x = (screen_w - (int)surf->w) / 2;
                surf->y = (screen_h - (int)surf->h) / 2;
                if (surf->y < TITLEBAR_HEIGHT) surf->y = TITLEBAR_HEIGHT;
                strcpy(surf->title, "Application Window");
            } else {
                surf->x = 0;
                surf->y = 0;
            }

            surface_add(surf);
            *surf_ptr = surf;

            // Focus normal windows on creation
            if (surf->layer == 1 || surf->layer == 2) {
                set_focus(surf);
            }

            // Reply to client
            struct {
                uint32_t surface_id;
                char shm_path[108];
            } __attribute__((packed)) reply;
            reply.surface_id = surf->surface_id;
            strcpy(reply.shm_path, surf->shm_path);

            send_frame(fd, MSG_CREATE_SURFACE, surf->surface_id, &reply, sizeof(reply));
            broadcast_window_event(EVT_WINDOW_CREATED, surf);
            break;
        }

        case MSG_RESIZE_SURFACE: {
            struct {
                uint32_t surface_id;
                uint32_t w, h;
            } __attribute__((packed)) *p = (void *)buffer;

            surface_t *surf = surface_find(p->surface_id);
            if (surf) {
                surf->pending_w = p->w;
                surf->pending_h = p->h;
                snprintf(surf->pending_shm_path, sizeof(surf->pending_shm_path), "/dev/shm/nova_surf_fd%d_%u_v2", fd, surf->surface_id);

                int shm_fd = open(surf->pending_shm_path, O_RDWR | O_CREAT, 0777);
                if (shm_fd >= 0) {
                    uint32_t sz = p->w * p->h * 4;
                    uint8_t *zeros = malloc(4096);
                    memset(zeros, 0, 4096);
                    uint32_t written = 0;
                    while (written < sz) {
                        uint32_t chunk = (sz - written > 4096) ? 4096 : (sz - written);
                        write(shm_fd, zeros, chunk);
                        written += chunk;
                    }
                    free(zeros);

                    surf->pending_pixels = mmap(NULL, sz, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
                    close(shm_fd);
                }

                struct {
                    char shm_path[108];
                } __attribute__((packed)) reply;
                strcpy(reply.shm_path, surf->pending_shm_path);

                send_frame(fd, MSG_RESIZE_SURFACE, surf->surface_id, &reply, sizeof(reply));
            }
            break;
        }

        case MSG_DAMAGE: {
            uint32_t surf_id = *(uint32_t *)buffer;
            surface_t *surf = surface_find(surf_id);
            if (surf) {
                // If there's a pending resized canvas committed, finalize swap
                if (surf->pending_pixels) {
                    // Unmap old shm surface
                    if (surf->pixels) {
                        munmap(surf->pixels, surf->shm_size);
                    }
                    // Unlink old path
                    unlink(surf->shm_path);

                    surf->pixels = surf->pending_pixels;
                    surf->w = surf->pending_w;
                    surf->h = surf->pending_h;
                    surf->shm_size = surf->w * surf->h * 4;
                    strcpy(surf->shm_path, surf->pending_shm_path);

                    surf->pending_pixels = NULL;
                    surf->pending_shm_path[0] = '\0';
                    surf->resize_preview_active = false;

                    if (surf->resize_request_queued) {
                        queue_resize_request(surf, surf->resize_desired_w, surf->resize_desired_h, surf->resize_force_next_request);
                    }
                }
            }
            break;
        }

        case MSG_MOVE_SURFACE: {
            struct {
                uint32_t surface_id;
                int x, y;
            } __attribute__((packed)) *p = (void *)buffer;

            surface_t *surf = surface_find(p->surface_id);
            if (surf) {
                surf->x = p->x;
                surf->y = p->y;
            }
            break;
        }

        case MSG_SET_STATE: {
            struct {
                uint32_t surface_id;
                uint32_t state_flags;
            } __attribute__((packed)) *p = (void *)buffer;

            surface_t *surf = surface_find(p->surface_id);
            if (surf) {
                surf->state_flags = p->state_flags;
                if (p->state_flags & 1) {
                    set_focus(surf);
                } else {
                    if (surf->focused) {
                        surf->focused = false;
                    }
                    broadcast_window_event(EVT_STATE_CHANGED, surf);
                }
            }
            break;
        }

        case MSG_SET_TITLE: {
            struct {
                uint32_t surface_id;
                char title[128];
            } __attribute__((packed)) *p = (void *)buffer;

            surface_t *surf = surface_find(p->surface_id);
            if (surf) {
                strncpy(surf->title, p->title, 127);
                surf->title[127] = '\0';
                broadcast_window_event(EVT_WINDOW_TITLE_CHANGED, surf);
            }
            break;
        }

        case MSG_SET_ICON: {
            struct {
                uint32_t surface_id;
                char icon_path[256];
            } __attribute__((packed)) *p = (void *)buffer;

            surface_t *surf = surface_find(p->surface_id);
            if (surf) {
                strncpy(surf->icon_path, p->icon_path, 255);
                surf->icon_path[255] = '\0';
                broadcast_window_event(EVT_WINDOW_TITLE_CHANGED, surf);
            }
            break;
        }

        case MSG_DESTROY_SURFACE: {
            uint32_t surf_id = *(uint32_t *)buffer;
            surface_t *surf = surface_find(surf_id);
            if (surf) {
                broadcast_window_event(EVT_WINDOW_DESTROYED, surf);
                if (surf->pixels) {
                    munmap(surf->pixels, surf->shm_size);
                }
                unlink(surf->shm_path);
                
                if (surf->pending_pixels) {
                    munmap(surf->pending_pixels, surf->pending_w * surf->pending_h * 4);
                    unlink(surf->pending_shm_path);
                }
                    surf->resize_preview_active = false;
                    clear_resize_state(surf);

                if (surf->focused) {
                    surf->focused = false;
                    surface_t *fallback = surface_get_focused();
                    if (fallback && fallback != surf) {
                        set_focus(fallback);
                    }
                }

                surface_remove(surf);
                free(surf);
                *surf_ptr = NULL;
            }
            break;
        }

        case MSG_QUIT: {
            quit_loop = true;
            break;
        }

        case MSG_QUERY_WINDOWS: {
            surface_t *c = surface_head;
            while (c) {
                if (c->layer == 1 || c->layer == 2) {
                    // Send details to query requester
                    struct {
                        uint32_t surface_id;
                        char title[128];
                        uint32_t state_flags;
                        char icon_path[256];
                    } __attribute__((packed)) pl;

                    pl.surface_id = c->surface_id;
                    strncpy(pl.title, c->title, 127);
                    pl.title[127] = '\0';
                    pl.state_flags = c->state_flags;
                    strncpy(pl.icon_path, c->icon_path, 255);
                    pl.icon_path[255] = '\0';

                    send_frame(fd, EVT_WINDOW_CREATED, c->surface_id, &pl, sizeof(pl));
                }
                c = c->next;
            }
            uint32_t sentinel = 0;
            send_frame(fd, EVT_WINDOW_LIST_END, 0, &sentinel, 4);
            break;
        }

        default:
            break;
    }
}

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    // Load compositor settings
    load_nova_config("/etc/nova/nova.conf");

    // Open Linear Framebuffer device
    fb_fd = open("/dev/fb0", O_RDWR);
    if (fb_fd < 0) {
        fprintf(stderr, "Nova Compositor Error: Cannot open /dev/fb0\n");
        return 1;
    }

    if (ioctl(fb_fd, FBIOGET_VSCREENINFO, &vinfo) < 0 || ioctl(fb_fd, FBIOGET_FSCREENINFO, &finfo) < 0) {
        fprintf(stderr, "Nova Compositor Error: FBIOGET screen details failed\n");
        close(fb_fd);
        return 1;
    }

    screen_w = vinfo.xres;
    screen_h = vinfo.yres;

    // Allocate back buffer canvas
    fb_size = finfo.line_length * screen_h;
    back_buffer = malloc(screen_w * screen_h * 4);
    if (!back_buffer) {
        fprintf(stderr, "Nova Compositor Error: Out of memory allocating double buffer\n");
        close(fb_fd);
        return 1;
    }

    resize_preview_capacity = (uint32_t)((uint64_t)screen_w * (uint64_t)screen_h);
    resize_preview_pixels = malloc((size_t)resize_preview_capacity * sizeof(uint32_t));
    if (!resize_preview_pixels) {
        fprintf(stderr, "Nova Compositor Warning: Resize preview buffer allocation failed\n");
    }

    // Set graphics mode on active console
    if (ioctl(0, KDSETMODE, (void*)KD_GRAPHICS) < 0) {
        fprintf(stderr, "Nova Compositor Warning: Cannot set KD_GRAPHICS console\n");
    }

    // Map screen memory
    fb_mem = mmap(NULL, fb_size, PROT_READ | PROT_WRITE, MAP_SHARED, fb_fd, 0);
    if (fb_mem == MAP_FAILED) {
        fprintf(stderr, "Nova Compositor Error: mmap framebuffer failed\n");
        ioctl(0, KDSETMODE, (void*)KD_TEXT);
        close(fb_fd);
        return 1;
    }

    // Initialize UI fonts
    ui_font_init("/Library/Fonts/inter.ttf", 13);

    // Set up signals via self-pipe
    if (sys_pipe(sig_pipe) < 0) {
        fprintf(stderr, "Nova Compositor Error: Cannot initialize self-pipe\n");
    }

    signal(SIGCHLD, handle_signal);
    signal(SIGUSR1, handle_signal);

    // Remove old UNIX socket if exists
    unlink("/tmp/nova.sock");

    // Initialize Socket Server binding
    int server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd < 0) {
        fprintf(stderr, "Nova Compositor Error: Cannot create socket\n");
        return 1;
    }

    struct sockaddr_un server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sun_family = AF_UNIX;
    strcpy(server_addr.sun_path, "/tmp/nova.sock");

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        fprintf(stderr, "Nova Compositor Error: Bind socket failed\n");
        close(server_fd);
        return 1;
    }

    if (listen(server_fd, 8) < 0) {
        fprintf(stderr, "Nova Compositor Error: Listen socket failed\n");
        close(server_fd);
        return 1;
    }

    // Open Exclusive Inputs
    int kbd_fd = open("/dev/keyboard", O_RDONLY | O_NONBLOCK);
    int mouse_fd = open("/dev/mouse", O_RDONLY | O_NONBLOCK);
    if (mouse_fd >= 0) {
        uint8_t dummy;
        while (read(mouse_fd, &dummy, 1) == 1);
    }

    // Initial mouse center coordinates
    mx = screen_w / 2;
    my = screen_h / 2;

    for (int i = 0; i < autostart_count; i++) {
        spawn_autostart(i);
    }

    // Event Polling initialization
    struct pollfd poll_fds[128];
    surface_t *client_surfaces[128]; // Map client fd index to surface

    bool e0_prefix = false;
    uint8_t mouse_buf[4];
    int mouse_idx = 0;

    // Initial composition rendering
    compositor_composite();
    present_framebuffer(0, 0, screen_w, screen_h);

    while (!quit_loop) {
        // Record coordinate and mapping state of all active surfaces before handling any inputs/events
        struct {
            uint32_t surface_id;
            int x, y;
            uint32_t w, h;
            bool mapped;
            uint8_t layer;
        } old_surfs[64];
        int old_surf_count = 0;
        surface_t *curr = surface_head;
        while (curr && old_surf_count < 64) {
            old_surfs[old_surf_count].surface_id = curr->surface_id;
            old_surfs[old_surf_count].x = curr->x;
            old_surfs[old_surf_count].y = curr->y;
            old_surfs[old_surf_count].w = curr->w;
            old_surfs[old_surf_count].h = curr->h;
            old_surfs[old_surf_count].mapped = curr->mapped;
            old_surfs[old_surf_count].layer = curr->layer;
            old_surf_count++;
            curr = curr->next;
        }

        // Collect fds for sys_poll
        int fd_idx = 0;
        
        // 0. Keyboard device
        if (kbd_fd >= 0) {
            poll_fds[fd_idx].fd = kbd_fd;
            poll_fds[fd_idx].events = POLLIN;
            poll_fds[fd_idx].revents = 0;
            client_surfaces[fd_idx] = NULL;
            fd_idx++;
        }

        // 1. Mouse device
        if (mouse_fd >= 0) {
            poll_fds[fd_idx].fd = mouse_fd;
            poll_fds[fd_idx].events = POLLIN;
            poll_fds[fd_idx].revents = 0;
            client_surfaces[fd_idx] = NULL;
            fd_idx++;
        }

        // 2. Self-pipe signal reader
        if (sig_pipe[0] >= 0) {
            poll_fds[fd_idx].fd = sig_pipe[0];
            poll_fds[fd_idx].events = POLLIN;
            poll_fds[fd_idx].revents = 0;
            client_surfaces[fd_idx] = NULL;
            fd_idx++;
        }

        // 3. Socket listener connection accepter
        poll_fds[fd_idx].fd = server_fd;
        poll_fds[fd_idx].events = POLLIN;
        poll_fds[fd_idx].revents = 0;
        client_surfaces[fd_idx] = NULL;
        fd_idx++;

        // 4. Connected active sockets
        int client_poll_start = fd_idx;
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i].active && fd_idx < 120) {
                poll_fds[fd_idx].fd = clients[i].fd;
                poll_fds[fd_idx].events = POLLIN;
                poll_fds[fd_idx].revents = 0;

                // Find surface belonging to this client fd
                surface_t *c = surface_head;
                client_surfaces[fd_idx] = NULL;
                while (c) {
                    if (c->client_fd == clients[i].fd) {
                        client_surfaces[fd_idx] = c;
                        break;
                    }
                    c = c->next;
                }
                fd_idx++;
            }
        }

        // Execute blocking poll with 16ms refresh frequency
        int pr = poll(poll_fds, fd_idx, 16);

        // Process Autostart respawn timers
        uint32_t now = (uint32_t)sys_system(16 /* SYSTEM_CMD_GET_TICKS */, 0, 0, 0, 0);
        for (int i = 0; i < autostart_count; i++) {
            if (autostarts[i].respawn_at_ms != 0 && now >= autostarts[i].respawn_at_ms) {
                spawn_autostart(i);
            }
        }

        if (pr < 0) continue;

        // Process polled triggers
        needs_composite = false;
        needs_cursor_only = false;

        for (int i = 0; i < fd_idx; i++) {
            if (poll_fds[i].revents & POLLIN) {
                // 0. Keyboard event
                if (poll_fds[i].fd == kbd_fd) {
                    uint8_t scancode;
                    while (read(kbd_fd, &scancode, 1) == 1) {
                        if (scancode == 0xE0) {
                            e0_prefix = true;
                            continue;
                        }

                        bool pressed = !(scancode & 0x80);
                        uint8_t base_sc = scancode & 0x7F;

                        // Track modifiers
                        if (base_sc == 0x2A || base_sc == 0x36) shift_pressed = pressed;
                        else if (base_sc == 0x1D) ctrl_pressed = pressed;
                        else if (base_sc == 0x38) alt_pressed = pressed;

                        NovaKeycode key = KEY_UNKNOWN;
                        if (e0_prefix) {
                            if (base_sc < sizeof(scancode_to_novakey_ext)/sizeof(scancode_to_novakey_ext[0])) {
                                key = scancode_to_novakey_ext[base_sc];
                            }
                            e0_prefix = false;
                        } else {
                            if (base_sc < sizeof(scancode_to_novakey)/sizeof(scancode_to_novakey[0])) {
                                key = scancode_to_novakey[base_sc];
                            }
                        }

                        // Global hotkeys
                        if (pressed && key == KEY_Q && shift_pressed && alt_pressed) {
                            quit_loop = true;
                        }

                        // Dispatch to focused window
                        surface_t *focused = surface_get_focused();
                        if (!quit_loop && focused && key != KEY_UNKNOWN) {
                            uint32_t modifiers = 0;
                            if (shift_pressed) modifiers |= 0x1;
                            if (ctrl_pressed) modifiers |= 0x2;
                            if (alt_pressed) modifiers |= 0x4;

                            struct {
                                uint32_t surface_id;
                                uint32_t keycode;
                                uint32_t modifiers;
                                uint8_t pressed;
                            } __attribute__((packed)) pl = {focused->surface_id, key, modifiers, pressed ? 1 : 0};

                            send_frame(focused->client_fd, EVT_KEY, focused->surface_id, &pl, sizeof(pl));
                        }
                    }
                }

                // Mouse event
                else if (poll_fds[i].fd == mouse_fd) {
                    uint8_t b;
                    while (read(mouse_fd, &b, 1) == 1) {
                        if (mouse_idx == 0 && (b & 0xC8) != 0x08) continue; // sync packet
                        mouse_buf[mouse_idx++] = b;

                        if (mouse_idx == 4) {
                            uint8_t flags = mouse_buf[0];
                            int dx = (int8_t)mouse_buf[1];
                            int dy = (int8_t)mouse_buf[2];

                            mx += dx;
                            my -= dy; // Invert PS/2 Y coords

                            if (mx < 0) mx = 0;
                            if (mx >= screen_w) mx = screen_w - 1;
                            if (my < 0) my = 0;
                            if (my >= screen_h) my = screen_h - 1;

                            bool fast_drag_handled = false;
                            bool left_pressed = (flags & 0x01) != 0;
                            int click_region = 0;
                            surface_t *hovered = NULL;

                            surface_t *focused = surface_get_focused();
                            bool active_drag = left_pressed && last_left_pressed && focused && (focused->is_dragging || focused->is_resizing);
                            if (!active_drag) {
                                hovered = surface_at(mx, my, &click_region);
                            }

                            if (left_pressed) {
                                if (!last_left_pressed) {
                                    // Pointer Press: Focus Shift
                                    if (hovered) {
                                        if (hovered->layer == 1 || hovered->layer == 2) {
                                            set_focus(hovered);
                                        }

                                        if (click_region == 1) {
                                            // Start titlebar drag
                                            hovered->is_dragging = true;
                                            hovered->drag_offset_x = mx - hovered->x;
                                            hovered->drag_offset_y = my - hovered->y;
                                        } else if (click_region & (RESIZE_EDGE_LEFT | RESIZE_EDGE_RIGHT | RESIZE_EDGE_TOP | RESIZE_EDGE_BOTTOM)) {
                                            // Start border resize drag
                                            hovered->is_resizing = true;
                                            hovered->resize_edge = click_region;
                                            hovered->resize_start_mouse_x = mx;
                                            hovered->resize_start_mouse_y = my;
                                            hovered->resize_start_x = hovered->x;
                                            hovered->resize_start_y = hovered->y;
                                            hovered->resize_start_w = hovered->w;
                                            hovered->resize_start_h = hovered->h;
                                            hovered->resize_desired_w = hovered->w;
                                            hovered->resize_desired_h = hovered->h;
                                            hovered->resize_request_queued = false;
                                            hovered->resize_force_next_request = false;
                                            hovered->resize_preview_active = true;
                                            update_resize_drag(hovered, mx, my);
                                        } else if (click_region == 2) {
                                            // Close button clicked: EVT_CLOSE_REQUEST
                                            uint32_t pl = hovered->surface_id;
                                            send_frame(hovered->client_fd, EVT_CLOSE_REQUEST, hovered->surface_id, &pl, 4);
                                        } else if (click_region == 4) {
                                            // Minimize button clicked: unmap surface
                                            hovered->mapped = false;
                                            hovered->focused = false;
                                            hovered->state_flags &= ~1;

                                            // Focus next window
                                            surface_t *fallback = surface_get_focused();
                                            if (fallback && fallback != hovered) {
                                                set_focus(fallback);
                                            } else {
                                                set_focus(NULL);
                                            }
                                            broadcast_window_event(EVT_STATE_CHANGED, hovered);
                                            needs_composite = true;
                                        } else if (click_region == 0) {
                                            // Content click: route pointer event
                                            struct {
                                                uint32_t surface_id;
                                                int x, y;
                                                uint32_t buttons;
                                            } __attribute__((packed)) pl = {hovered->surface_id, mx - hovered->x, my - hovered->y, flags & 0x7};
                                            send_frame(hovered->client_fd, EVT_POINTER, hovered->surface_id, &pl, sizeof(pl));
                                        }
                                    } else {
                                        // Clicked background: unfocus active window
                                        set_focus(NULL);
                                    }
                                } else {
                                    // Pointer Dragging
                                    if (focused && focused->is_resizing) {
                                        update_resize_drag(focused, mx, my);
                                    } else if (focused && focused->is_dragging) {
                                        int old_vis_x, old_vis_y, old_vis_w, old_vis_h;
                                        surface_visual_bounds(focused, &old_vis_x, &old_vis_y, &old_vis_w, &old_vis_h);

                                        focused->x = mx - focused->drag_offset_x;
                                        focused->y = my - focused->drag_offset_y;

                                        // Constrain titlebar inside screen boundaries
                                        if (focused->y < TITLEBAR_HEIGHT) focused->y = TITLEBAR_HEIGHT;
                                        if (focused->x < -((int)focused->w) + 40) focused->x = -((int)focused->w) + 40;
                                        if (focused->x > screen_w - 40) focused->x = screen_w - 40;

                                        int new_vis_x, new_vis_y, new_vis_w, new_vis_h;
                                        surface_visual_bounds(focused, &new_vis_x, &new_vis_y, &new_vis_w, &new_vis_h);

                                        if (try_fast_translate_drag(focused, old_vis_x, old_vis_y, old_vis_w, old_vis_h,
                                                                    new_vis_x, new_vis_y, new_vis_w, new_vis_h)) {
                                            fast_drag_handled = true;
                                        } else {
                                            needs_composite = true;
                                        }
                                    } else if (hovered && click_region == 0) {
                                        // Continuous content pointer moves
                                        struct {
                                            uint32_t surface_id;
                                            int x, y;
                                            uint32_t buttons;
                                        } __attribute__((packed)) pl = {hovered->surface_id, mx - hovered->x, my - hovered->y, flags & 0x7};
                                        send_frame(hovered->client_fd, EVT_POINTER, hovered->surface_id, &pl, sizeof(pl));
                                    }
                                }
                            } else {
                                // Left button released
                                if (last_left_pressed) {
                                    // End drag states
                                    surface_t *c = surface_head;
                                    while (c) {
                                        if (c->is_resizing) {
                                            finish_resize_drag(c);
                                        }
                                        c->is_dragging = false;
                                        c->is_resizing = false;
                                        c = c->next;
                                    }
                                }

                                // Normal pointer movements (hover routing)
                                if (hovered && click_region == 0) {
                                    struct {
                                        uint32_t surface_id;
                                        int x, y;
                                        uint32_t buttons;
                                    } __attribute__((packed)) pl = {hovered->surface_id, mx - hovered->x, my - hovered->y, 0};
                                    send_frame(hovered->client_fd, EVT_POINTER, hovered->surface_id, &pl, sizeof(pl));
                                }
                            }

                            if ((dx != 0 || dy != 0) && !fast_drag_handled) {
                                if (!needs_composite) {
                                    needs_cursor_only = true;
                                }
                            }
                            if (left_pressed != last_left_pressed) {
                                needs_composite = true;
                            }
                            last_left_pressed = left_pressed;
                            mouse_idx = 0;
                        }
                    }
                }

                // Self-pipe signal event
                else if (poll_fds[i].fd == sig_pipe[0]) {
                    uint8_t sig;
                    if (read(sig_pipe[0], &sig, 1) == 1) {
                        if (sig == SIGCHLD) {
                            // Reap exited autostart items
                            int status;
                            int reaped;
                            while ((reaped = sys_waitpid(-1, &status, 1 /* WNOHANG */)) > 0) {
                                trigger_autostart_respawn(reaped);
                            }
                        } else if (sig == SIGUSR1) {
                            // Reload settings
                            load_nova_config("/etc/nova/nova.conf");
                            needs_composite = true;
                        }
                    }
                }

                // New socket client connecting
                else if (poll_fds[i].fd == server_fd) {
                    int client_fd = accept(server_fd, NULL, NULL);
                    if (client_fd >= 0) {
                        // Store descriptor inside list
                        for (int k = 0; k < MAX_CLIENTS; k++) {
                            if (!clients[k].active) {
                                clients[k].fd = client_fd;
                                clients[k].active = true;
                                client_count++;
                                break;
                            }
                        }
                    }
                }

                // 4. Existing client communication frame
                else if (i >= client_poll_start) {
                    int client_fd = poll_fds[i].fd;
                    surface_t *surf = client_surfaces[i];
                    handle_client_message(client_fd, &surf);
                    needs_composite = true;
                }
            } else if (poll_fds[i].revents & (POLLHUP | POLLERR)) {
                // Client closed socket connection
                int client_fd = poll_fds[i].fd;
                
                // Release active client list slot
                for (int k = 0; k < MAX_CLIENTS; k++) {
                    if (clients[k].active && clients[k].fd == client_fd) {
                        clients[k].active = false;
                        close(client_fd);
                        client_count--;
                        break;
                    }
                }

                // Delete surface belonging to closed client descriptor
                surface_t *curr = surface_head;
                while (curr) {
                    surface_t *next = curr->next;
                    if (curr->client_fd == client_fd) {
                        broadcast_window_event(EVT_WINDOW_DESTROYED, curr);
                        if (curr->pixels) {
                            munmap(curr->pixels, curr->shm_size);
                        }
                        unlink(curr->shm_path);
                        
                        if (curr->pending_pixels) {
                            munmap(curr->pending_pixels, curr->pending_w * curr->pending_h * 4);
                            unlink(curr->pending_shm_path);
                        }
                        curr->resize_preview_active = false;
                        clear_resize_state(curr);

                        if (curr->focused) {
                            curr->focused = false;
                            surface_t *fallback = surface_get_focused();
                            if (fallback && fallback != curr) {
                                set_focus(fallback);
                            }
                        }

                        surface_remove(curr);
                        free(curr);
                        needs_composite = true;
                    }
                    curr = next;
                }
            }
        }

        // Detect any changes to surface properties or layout
        int min_x = screen_w, min_y = screen_h, max_x = 0, max_y = 0;
        bool any_change = false;

        for (int i = 0; i < old_surf_count; i++) {
            surface_t *s = surface_find(old_surfs[i].surface_id);
            bool has_decorations = (old_surfs[i].layer == 1 || old_surfs[i].layer == 2);
            int old_border = has_decorations ? BORDER_WIDTH : 0;
            int old_titlebar = has_decorations ? (TITLEBAR_HEIGHT + BORDER_WIDTH) : 0;

            int old_l = old_surfs[i].x - old_border;
            int old_t = old_surfs[i].y - old_titlebar;
            int old_r = old_surfs[i].x + old_surfs[i].w + old_border;
            int old_b = old_surfs[i].y + old_surfs[i].h + old_border;

            if (!s) {
                // Surface was destroyed/removed. Redraw its old area!
                if (old_surfs[i].mapped) {
                    if (old_l < min_x) min_x = old_l;
                    if (old_t < min_y) min_y = old_t;
                    if (old_r > max_x) max_x = old_r;
                    if (old_b > max_y) max_y = old_b;
                    any_change = true;
                }
            } else {
                bool new_has_decorations = (s->layer == 1 || s->layer == 2);
                int new_border = new_has_decorations ? BORDER_WIDTH : 0;
                int new_titlebar = new_has_decorations ? (TITLEBAR_HEIGHT + BORDER_WIDTH) : 0;

                int new_l = s->x - new_border;
                int new_t = s->y - new_titlebar;
                int new_r = s->x + s->w + new_border;
                int new_b = s->y + s->h + new_border;

                if (s->x != old_surfs[i].x || s->y != old_surfs[i].y ||
                    s->w != old_surfs[i].w || s->h != old_surfs[i].h ||
                    s->mapped != old_surfs[i].mapped || s->layer != old_surfs[i].layer) {
                    
                    // Old position (if it was mapped)
                    if (old_surfs[i].mapped) {
                        if (old_l < min_x) min_x = old_l;
                        if (old_t < min_y) min_y = old_t;
                        if (old_r > max_x) max_x = old_r;
                        if (old_b > max_y) max_y = old_b;
                    }
                    // New position (if it is mapped)
                    if (s->mapped) {
                        if (new_l < min_x) min_x = new_l;
                        if (new_t < min_y) min_y = new_t;
                        if (new_r > max_x) max_x = new_r;
                        if (new_b > max_y) max_y = new_b;
                    }
                    any_change = true;
                }
            }
        }

        // Also check if any NEW surface was created that wasn't in old_surfs
        curr = surface_head;
        while (curr) {
            bool found = false;
            for (int i = 0; i < old_surf_count; i++) {
                if (old_surfs[i].surface_id == curr->surface_id) {
                    found = true;
                    break;
                }
            }
            if (!found && curr->mapped) {
                bool has_decorations = (curr->layer == 1 || curr->layer == 2);
                int border = has_decorations ? BORDER_WIDTH : 0;
                int titlebar = has_decorations ? (TITLEBAR_HEIGHT + BORDER_WIDTH) : 0;

                int l = curr->x - border;
                int t = curr->y - titlebar;
                int r = curr->x + curr->w + border;
                int b = curr->y + curr->h + border;

                if (l < min_x) min_x = l;
                if (t < min_y) min_y = t;
                if (r > max_x) max_x = r;
                if (b > max_y) max_y = b;
                any_change = true;
            }
            curr = curr->next;
        }

        if (any_change) {
            if (min_x < 0) min_x = 0;
            if (min_y < 0) min_y = 0;
            if (max_x > screen_w) max_x = screen_w;
            if (max_y > screen_h) max_y = screen_h;

            dirty_x = min_x;
            dirty_y = min_y;
            dirty_w = max_x - min_x;
            dirty_h = max_y - min_y;
            has_dirty_rect = true;
            needs_composite = true;
        }

        // Periodic rendering if cursor moved or window states changed
        if (needs_composite) {
            compositor_composite();
        } else if (needs_cursor_only) {
            update_cursor_atomic_combined(mx, my);
        }
    }

    // Clean up
    close(server_fd);
    unlink("/tmp/nova.sock");
    munmap(fb_mem, fb_size);
    close(fb_fd);

    if (ioctl(0, KDSETMODE, (void*)KD_TEXT) < 0) {
        fprintf(stderr, "Nova Compositor Warning: Cannot restore TTY mode\n");
    }

    return 0;
}
