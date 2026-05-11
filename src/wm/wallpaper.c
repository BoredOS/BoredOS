#include "wallpaper.h"
#define STBI_NO_STDIO
#include "userland/stb_image.h"
#include "graphics.h"
#include "fat32.h"
#include "memory_manager.h"
#include "wm.h"
#include "io.h"
#include "core/kutils.h"
#include <stddef.h>

// Static buffer for the current wallpaper (max 1920x1080)
#define MAX_WP_WIDTH 1920
#define MAX_WP_HEIGHT 1080
static uint32_t* wp_pixels = NULL;
static int wp_width = 0;
static int wp_height = 0;

static volatile const char *pending_wallpaper_path = NULL;
static char pending_path_buf[256];

#define WALLPAPER_CONF_PATH "/Library/BWM/Wallpaper/wallpaper"
#define WALLPAPER_CACHE_PATH "/Library/Caches/Wallpaper.bin"

static void wallpaper_save_cache(void) {
    if (!wp_pixels || wp_width <= 0 || wp_height <= 0) return;
    fat32_mkdir("/Library/Caches");
    FAT32_FileHandle *fh = fat32_open(WALLPAPER_CACHE_PATH, "w");
    if (fh && fh->valid) {
        uint32_t w = (uint32_t)wp_width;
        uint32_t h = (uint32_t)wp_height;
        fat32_write(fh, &w, 4);
        fat32_write(fh, &h, 4);
        fat32_write(fh, wp_pixels, w * h * 4);
        fat32_close(fh);
    }
}

static int wallpaper_load_cache(void) {
    if (!fat32_exists(WALLPAPER_CACHE_PATH)) return 0;
    FAT32_FileHandle *fh = fat32_open(WALLPAPER_CACHE_PATH, "r");
    if (!fh || !fh->valid) return 0;
    
    uint32_t w, h;
    fat32_read(fh, &w, 4);
    fat32_read(fh, &h, 4);
    
    if (w != (uint32_t)get_screen_width() || h != (uint32_t)get_screen_height()) {
        fat32_close(fh);
        return 0;
    }
    
    if (!wp_pixels) {
        wp_pixels = (uint32_t*)kmalloc(MAX_WP_WIDTH * MAX_WP_HEIGHT * sizeof(uint32_t));
        if (!wp_pixels) { fat32_close(fh); return 0; }
    }
    
    uint32_t total_size = w * h * 4;
    uint32_t bytes_read = fat32_read(fh, wp_pixels, total_size);
    fat32_close(fh);
    
    if (bytes_read == total_size) {
        wp_width = (int)w;
        wp_height = (int)h;
        graphics_set_bg_image(wp_pixels, wp_width, wp_height);
        return 1;
    }
    return 0;
}

// Simple nearest-neighbor scale from decoded RGBA to ARGB pixel buffer
static void scale_rgba_to_argb(const unsigned char *rgba, int src_w, int src_h,
                               uint32_t *dst, int dst_w, int dst_h) {
    for (int y = 0; y < dst_h; y++) {
        int src_y = y * src_h / dst_h;
        if (src_y >= src_h) src_y = src_h - 1;
        for (int x = 0; x < dst_w; x++) {
            int src_x = x * src_w / dst_w;
            if (src_x >= src_w) src_x = src_w - 1;
            
            size_t idx = ((size_t)src_y * (size_t)src_w + (size_t)src_x) * 4;
            unsigned char r = rgba[idx];
            unsigned char g = rgba[idx + 1];
            unsigned char b = rgba[idx + 2];
            unsigned char a = rgba[idx + 3];
            dst[y * dst_w + x] = ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
        }
    }
}

// Decode JPEG data from memory and set as wallpaper (MUST be called from non-interrupt context)
static int decode_and_set_wallpaper(const unsigned char *jpg_data, unsigned int jpg_size) {
    int img_w, img_h, channels;
    // We request 4 channels (RGBA) for better alignment and consistency with working userland code
    unsigned char *rgba = stbi_load_from_memory(jpg_data, (int)jpg_size, &img_w, &img_h, &channels, 4);
    
    if (!rgba || img_w <= 0 || img_h <= 0) {
        return 0;
    }

    // Scale to screen size
    int screen_w = get_screen_width();
    int screen_h = get_screen_height();
    if (screen_w > MAX_WP_WIDTH) screen_w = MAX_WP_WIDTH;
    if (screen_h > MAX_WP_HEIGHT) screen_h = MAX_WP_HEIGHT;

    if (!wp_pixels) {
        wp_pixels = (uint32_t*)kmalloc(MAX_WP_WIDTH * MAX_WP_HEIGHT * sizeof(uint32_t));
        if (!wp_pixels) {
            stbi_image_free(rgba);
            return 0;
        }
    }

    scale_rgba_to_argb(rgba, img_w, img_h, wp_pixels, screen_w, screen_h);
    wp_width = screen_w;
    wp_height = screen_h;

    stbi_image_free(rgba);

    graphics_set_bg_image(wp_pixels, wp_width, wp_height);
    wallpaper_save_cache();
    return 1;
}

// Simple serial output for debugging (COM1 = 0x3F8)
static void serial_char(char c) {
    while (!(inb(0x3F8 + 5) & 0x20));  // Wait for transmit ready
    outb(0x3F8, c);
}

static void serial_str(const char *s) {
    while (*s) serial_char(*s++);
}

static void serial_num(int n) {
    if (n < 0) { serial_char('-'); n = -n; }
    if (n >= 10) serial_num(n / 10);
    serial_char('0' + (n % 10));
}

// Request wallpaper change by file path (safe to call from interrupt context)
void wallpaper_request_set_from_file(const char *path) {
    // Copy path to buffer
    int i = 0;
    while (path[i] && i < 255) {
        pending_path_buf[i] = path[i];
        i++;
    }
    pending_path_buf[i] = 0;
    pending_wallpaper_path = pending_path_buf;
}

void wallpaper_save_setting(const char *path) {
    if (!path) return;
    fat32_mkdir("/Library/BWM");
    fat32_mkdir("/Library/BWM/Wallpaper");
    FAT32_FileHandle *fh = fat32_open(WALLPAPER_CONF_PATH, "w");
    if (!fh || !fh->valid) {
        serial_str("[WALLPAPER] Failed to save setting\n");
        return;
    }
    fat32_write(fh, path, (uint32_t)strlen(path));
    fat32_close(fh);
    serial_str("[WALLPAPER] Setting saved: ");
    serial_str(path);
    serial_str("\n");
}

void wallpaper_load_setting(void) {
    if (!fat32_exists(WALLPAPER_CONF_PATH)) {
        serial_str("[WALLPAPER] No saved setting found\n");
        return;
    }
    FAT32_FileHandle *fh = fat32_open(WALLPAPER_CONF_PATH, "r");
    if (!fh || !fh->valid) return;
    
    char path_buf[256];
    int bytes_read = fat32_read(fh, path_buf, 255);
    fat32_close(fh);
    
    if (bytes_read > 0) {
        path_buf[bytes_read] = '\0';
        serial_str("[WALLPAPER] Loaded setting: ");
        serial_str(path_buf);
        serial_str("\n");
        if (fat32_exists(path_buf)) {
            wallpaper_request_set_from_file(path_buf);
        }
    }
}

void wallpaper_process_pending(void) {
    if (pending_wallpaper_path) {
        const char *path = (const char *)pending_wallpaper_path;
        pending_wallpaper_path = NULL;

        serial_str("[WM] Processing wallpaper: ");
        serial_str(path);
        serial_str("\n");

        // Read file from filesystem
        FAT32_FileHandle *fh = fat32_open(path, "r");
        if (fh) {
            uint32_t file_size = fh->size;
            if (file_size > 0 && file_size <= 4 * 1024 * 1024) {
                // Add padding to avoid stb_image reading past the end and potential corruption
                size_t padded_size = file_size + 128;
                unsigned char *buf = (unsigned char*)kmalloc(padded_size);
                if (buf) {
                    mem_memset(buf, 0, padded_size);
                    int total_read = 0;
                    while (total_read < (int)file_size) {
                        int chunk = fat32_read(fh, buf + total_read, (int)file_size - total_read);
                        if (chunk <= 0) break;
                        total_read += chunk;
                    }
                    fat32_close(fh);

                    if (total_read > 0) {
                        decode_and_set_wallpaper(buf, (unsigned int)total_read);
                        wallpaper_save_setting(path);
                        wm_refresh();
                    }
                    kfree(buf);
                } else {
                    fat32_close(fh);
                }
            } else {
                fat32_close(fh);
            }
        }
    }
}

uint32_t* wallpaper_get_pixels(void) { return wp_pixels; }
int wallpaper_get_width(void) { return wp_width; }
int wallpaper_get_height(void) { return wp_height; }

void wallpaper_init(void) {
    if (wallpaper_load_cache()) {
        serial_str("[WALLPAPER] Loaded from cache\n");
    } else {
        wallpaper_load_setting();
        
        if (pending_wallpaper_path == NULL) {
            if (fat32_exists("/Library/images/Wallpapers/boredos.png")) {
                wallpaper_request_set_from_file("/Library/images/Wallpapers/boredos.png");
            } else if (fat32_exists("/Library/images/Wallpapers/mountain.jpg")) {
                wallpaper_request_set_from_file("/Library/images/Wallpapers/mountain.jpg");
            }
        }
    }
}
