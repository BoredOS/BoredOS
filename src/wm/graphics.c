// Copyright (c) 2023-2026 Chris (boreddevnl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
#include <stddef.h>
#include "graphics.h"
#include "font.h"
#include "io.h"
#include "font_manager.h"
#include "../mem/memory_manager.h"
#include "sys/spinlock.h"

static struct limine_framebuffer *g_fb = NULL;
static uint32_t g_bg_color = 0xFF696969;

extern void serial_write(const char *str);
extern uint32_t blend_src_over_dst(uint32_t dst, uint32_t src);

static int g_color_mode = 0;

#define PATTERN_SIZE 128
static uint32_t g_bg_pattern[PATTERN_SIZE * PATTERN_SIZE];
static bool g_use_pattern = false;

static uint32_t *g_bg_image = NULL;
static int g_bg_image_w = 0;
static int g_bg_image_h = 0;
static bool g_use_image = false;
static uint32_t *g_logo_pixels = NULL;
static int g_logo_w = 0;
static int g_logo_h = 0;

static DirtyRect g_dirty = {0, 0, 0, 0, false};
static spinlock_t graphics_lock = SPINLOCK_INIT;


#define MAX_FB_WIDTH 2048
#define MAX_FB_HEIGHT 2048
static uint32_t g_back_buffer[MAX_FB_WIDTH * MAX_FB_HEIGHT] __attribute__((aligned(4096)));

#define MAX_RENDER_CPUS 32
#define CLIP_STACK_DEPTH 8
static int g_clip_stack_x[MAX_RENDER_CPUS][CLIP_STACK_DEPTH];
static int g_clip_stack_y[MAX_RENDER_CPUS][CLIP_STACK_DEPTH];
static int g_clip_stack_w[MAX_RENDER_CPUS][CLIP_STACK_DEPTH];
static int g_clip_stack_h[MAX_RENDER_CPUS][CLIP_STACK_DEPTH];
static int g_clip_stack_ptr[MAX_RENDER_CPUS] = {0};
static bool g_clip_enabled[MAX_RENDER_CPUS] = {false};

extern uint32_t smp_this_cpu_id(void);
static uint32_t *g_render_target[MAX_RENDER_CPUS] = {0};
static int g_rt_width[MAX_RENDER_CPUS] = {0};
static int g_rt_height[MAX_RENDER_CPUS] = {0};

static ttf_font_t *g_current_ttf = NULL;

void graphics_init(struct limine_framebuffer *fb) {
    g_fb = fb;
    g_dirty.active = false;
    // Initialize back buffer to black
    for (int i = 0; i < MAX_FB_WIDTH * MAX_FB_HEIGHT; i++) {
        g_back_buffer[i] = 0;
    }
}

void graphics_init_fonts(void) {
    font_manager_init();
    g_current_ttf = font_manager_load("/Library/Fonts/firamono.ttf", 15.0f);
    if (!g_current_ttf) {
        serial_write("[FONT] Falling back to bitmap font\n");
    }
    font_manager_set_fallback_font(font_manager_load("/Library/Fonts/Emoji/NotoEmoji-VariableFont_wght.ttf", 15.0f));
}

void graphics_update_resolution(int width, int height, int bpp, void* fb_addr, int color_mode) {
    if (!g_fb) return;
    
    uint64_t rflags;
    asm volatile("pushfq; pop %0; cli" : "=r"(rflags));
    
    g_fb->width = width;
    g_fb->height = height;
    g_fb->bpp = bpp;
    g_fb->pitch = width * (bpp / 8);
    g_fb->address = fb_addr;
    g_color_mode = color_mode;
    
    // Clear back buffer
    for (int i = 0; i < MAX_FB_WIDTH * MAX_FB_HEIGHT; i++) {
        g_back_buffer[i] = 0;
    }
    
    // Clear dirty rect
    g_dirty.active = false;
    
    asm volatile("push %0; popfq" : : "r"(rflags));
}

void graphics_set_font(const char *path) {
    ttf_font_t *new_font = font_manager_load(path, 15.0f);
    if (new_font) {
        // TODO: free old font data if needed
        g_current_ttf = new_font;
        serial_write("[FONT] Switched to: ");
        serial_write(path);
        serial_write("\n");
    }
}

int get_screen_width(void) {
    return g_fb ? g_fb->width : 0;
}

int get_screen_height(void) {
    return g_fb ? g_fb->height : 0;
}

uint64_t graphics_get_fb_addr(void) {
    return g_fb ? (uint64_t)g_fb->address : 0;
}

int graphics_get_fb_bpp(void) {
    return g_fb ? g_fb->bpp : 0;
}

// Merge new dirty rect with existing one
static void merge_dirty_rect(int x, int y, int w, int h) {
    if (!g_dirty.active) {
        g_dirty.x = x;
        g_dirty.y = y;
        g_dirty.w = w;
        g_dirty.h = h;
        g_dirty.active = true;
    } else {
        // Calculate union of two rectangles
        int x1 = g_dirty.x;
        int y1 = g_dirty.y;
        int x2 = g_dirty.x + g_dirty.w;
        int y2 = g_dirty.y + g_dirty.h;
        
        int new_x1 = x;
        int new_y1 = y;
        int new_x2 = x + w;
        int new_y2 = y + h;
        
        g_dirty.x = new_x1 < x1 ? new_x1 : x1;
        g_dirty.y = new_y1 < y1 ? new_y1 : y1;
        g_dirty.w = (new_x2 > x2 ? new_x2 : x2) - g_dirty.x;
        g_dirty.h = (new_y2 > y2 ? new_y2 : y2) - g_dirty.y;
    }
}

void graphics_mark_dirty(int x, int y, int w, int h) {
    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    if (x + w > get_screen_width()) {
        w = get_screen_width() - x;
    }
    if (y + h > get_screen_height()) {
        h = get_screen_height() - y;
    }
    
    if (w <= 0 || h <= 0) {
        return;
    }
    
    uint64_t flags = spinlock_acquire_irqsave(&graphics_lock);
    merge_dirty_rect(x, y, w, h);
    spinlock_release_irqrestore(&graphics_lock, flags);
}

void graphics_mark_screen_dirty(void) {
    uint64_t flags = spinlock_acquire_irqsave(&graphics_lock);
    g_dirty.x = 0;
    g_dirty.y = 0;
    g_dirty.w = get_screen_width();
    g_dirty.h = get_screen_height();
    g_dirty.active = true;
    spinlock_release_irqrestore(&graphics_lock, flags);
}

DirtyRect graphics_get_dirty_rect(void) {
    return g_dirty;
}

void graphics_clear_dirty(void) {
    uint64_t flags = spinlock_acquire_irqsave(&graphics_lock);
    g_dirty.active = false;
    spinlock_release_irqrestore(&graphics_lock, flags);
}

void graphics_clear_dirty_no_lock(void) {
    g_dirty.active = false;
}

void graphics_set_render_target(uint32_t *buffer, int w, int h) {
    uint32_t cpu = smp_this_cpu_id();
    if (cpu < MAX_RENDER_CPUS) {
        g_render_target[cpu] = buffer;
        g_rt_width[cpu] = w;
        g_rt_height[cpu] = h;
    }
}

void put_pixel(int x, int y, uint32_t color) {
    uint32_t cpu = smp_this_cpu_id();
    if (cpu < MAX_RENDER_CPUS && g_render_target[cpu]) {
        if (x >= 0 && x < g_rt_width[cpu] && y >= 0 && y < g_rt_height[cpu]) {
            g_render_target[cpu][y * g_rt_width[cpu] + x] = color;
        }
        return;
    }

    if (!g_fb) return;
    if (x < 0 || x >= (int)g_fb->width || y < 0 || y >= (int)g_fb->height) return;
    
    if (g_clip_enabled[cpu]) {
        int ptr = g_clip_stack_ptr[cpu];
        if (x < g_clip_stack_x[cpu][ptr] || x >= g_clip_stack_x[cpu][ptr] + g_clip_stack_w[cpu][ptr] ||
            y < g_clip_stack_y[cpu][ptr] || y >= g_clip_stack_y[cpu][ptr] + g_clip_stack_h[cpu][ptr]) {
            return;
        }
    }
    
    uint32_t pixel_offset = y * g_fb->width + x;
    g_back_buffer[pixel_offset] = color;
}

uint32_t graphics_get_pixel(int x, int y) {
    uint32_t cpu = smp_this_cpu_id();
    if (cpu < MAX_RENDER_CPUS && g_render_target[cpu]) {
        if (x >= 0 && x < g_rt_width[cpu] && y >= 0 && y < g_rt_height[cpu]) {
            return g_render_target[cpu][y * g_rt_width[cpu] + x];
        }
        return 0;
    }

    if (!g_fb) return 0;
    if (x < 0 || x >= (int)g_fb->width || y < 0 || y >= (int)g_fb->height) return 0;
    
    return g_back_buffer[y * g_fb->width + x];
}

void draw_rect(int x, int y, int w, int h, uint32_t color) {
    int x1 = x, y1 = y, x2 = x + w, y2 = y + h;

    uint32_t cpu = smp_this_cpu_id();
    if (cpu < MAX_RENDER_CPUS && g_render_target[cpu]) {
        if (x1 < 0) x1 = 0;
        if (y1 < 0) y1 = 0;
        if (x2 > g_rt_width[cpu]) x2 = g_rt_width[cpu];
        if (y2 > g_rt_height[cpu]) y2 = g_rt_height[cpu];
        if (x1 >= x2 || y1 >= y2) return;
        
        for (int i = y1; i < y2; i++) {
            uint32_t *row = &g_render_target[cpu][i * g_rt_width[cpu] + x1];
            int len = x2 - x1;
            for (int j = 0; j < len; j++) {
                row[j] = color;
            }
        }
        return;
    }

    if (!g_fb) return;
    
    if (g_clip_enabled[cpu]) {
        int ptr = g_clip_stack_ptr[cpu];
        if (x1 < g_clip_stack_x[cpu][ptr]) x1 = g_clip_stack_x[cpu][ptr];
        if (y1 < g_clip_stack_y[cpu][ptr]) y1 = g_clip_stack_y[cpu][ptr];
        if (x2 > g_clip_stack_x[cpu][ptr] + g_clip_stack_w[cpu][ptr]) x2 = g_clip_stack_x[cpu][ptr] + g_clip_stack_w[cpu][ptr];
        if (y2 > g_clip_stack_y[cpu][ptr] + g_clip_stack_h[cpu][ptr]) y2 = g_clip_stack_y[cpu][ptr] + g_clip_stack_h[cpu][ptr];
    }

    if (x1 < 0) x1 = 0;
    if (y1 < 0) y1 = 0;
    if (x2 > (int)g_fb->width) x2 = g_fb->width;
    if (y2 > (int)g_fb->height) y2 = g_fb->height;

    if (x1 >= x2 || y1 >= y2) return;

    for (int i = y1; i < y2; i++) {
        uint32_t *row = &g_back_buffer[i * g_fb->width + x1];
        int len = x2 - x1;
        for (int j = 0; j < len; j++) {
            row[j] = color;
        }
    }
}

// Simple integer-based square root approximation
static int isqrt(int n) {
    if (n < 0) return 0;
    if (n == 0) return 0;
    int x = n;
    int y = (x + 1) / 2;
    while (y < x) {
        x = y;
        y = (x + n / x) / 2;
    }
    return x;
}

// Draw rounded rectangle outline
void draw_rounded_rect(int x, int y, int w, int h, int radius, uint32_t color) {
    if (radius > w / 2) radius = w / 2;
    if (radius > h / 2) radius = h / 2;
    if (radius < 1) {
        // Draw a simple rect outline if no radius
        draw_rect(x, y, w, 1, color);
        draw_rect(x, y + h - 1, w, 1, color);
        draw_rect(x, y + 1, 1, h - 2, color);
        draw_rect(x + w - 1, y + 1, 1, h - 2, color);
        return;
    }
    
    // Draw top and bottom straight edges
    draw_rect(x + radius, y, w - 2*radius, 1, color);
    draw_rect(x + radius, y + h - 1, w - 2*radius, 1, color);
    
    // Draw left and right straight edges
    draw_rect(x, y + radius, 1, h - 2*radius, color);
    draw_rect(x + w - 1, y + radius, 1, h - 2*radius, color);
    
    // Draw four corner arcs
    for (int dy = 0; dy < radius; dy++) {
        int y_dist = radius - 1 - dy;
        int dx = isqrt(radius*radius - y_dist*y_dist);
        int next_dx = (dy < radius - 1) ? isqrt(radius*radius - (y_dist - 1)*(y_dist - 1)) : radius;
        
        for (int i = dx; i < next_dx && i <= radius; i++) {
            // Top-left
            put_pixel(x + radius - 1 - i, y + dy, color);
            // Top-right
            put_pixel(x + w - radius + i, y + dy, color);
            // Bottom-left
            put_pixel(x + radius - 1 - i, y + h - 1 - dy, color);
            // Bottom-right
            put_pixel(x + w - radius + i, y + h - 1 - dy, color);
        }
    }
}

// Draw filled rounded rectangle
void draw_rounded_rect_filled(int x, int y, int w, int h, int radius, uint32_t color) {
    if (radius > w / 2) radius = w / 2;
    if (radius > h / 2) radius = h / 2;
    if (radius < 1) {
        draw_rect(x, y, w, h, color);
        return;
    }
    
    // Draw main rectangle body
    draw_rect(x, y + radius, w, h - 2*radius, color);
    
    // Draw rounded top and bottom caps
    for (int dy = 0; dy < radius; dy++) {
        int y_dist = radius - 1 - dy;
        int dx = isqrt(radius*radius - y_dist*y_dist);
        
        draw_rect(x + radius - dx, y + dy, w - 2*radius + 2*dx, 1, color);
        draw_rect(x + radius - dx, y + h - 1 - dy, w - 2*radius + 2*dx, 1, color);
    }
}

static uint32_t blend_color_alpha(uint32_t bottom, uint32_t top, int alpha) {
    if (alpha <= 0) return bottom;
    if (alpha >= 255) return top;
    
    int rb = (bottom >> 16) & 0xFF;
    int gb = (bottom >> 8)  & 0xFF;
    int bb = bottom         & 0xFF;
    
    int rt = (top >> 16)    & 0xFF;
    int gt = (top >> 8)     & 0xFF;
    int bt = top            & 0xFF;
    
    int rr = rb + (((rt - rb) * alpha) >> 8);
    int gg = gb + (((gt - gb) * alpha) >> 8);
    int bb_new = bb + (((bt - bb) * alpha) >> 8);
    
    return (rr << 16) | (gg << 8) | bb_new;
}

void draw_rounded_rect_blurred(int x, int y, int w, int h, int radius, uint32_t tint_color, int blur_radius, int alpha) {
    if (!g_fb) return;
    int sw = get_screen_width();
    int sh = get_screen_height();
    
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > sw) w = sw - x;
    if (y + h > sh) h = sh - y;
    if (w <= 0 || h <= 0) return;

    if (radius > w / 2) radius = w / 2;
    if (radius > h / 2) radius = h / 2;
    if (radius < 1) radius = 1;
    
    uint32_t *tmp_buf = (uint32_t *)kmalloc(w * h * sizeof(uint32_t));
    if (!tmp_buf) {
        draw_rounded_rect_filled(x, y, w, h, radius, tint_color);
        return;
    }
    
    for (int r = 0; r < h; r++) {
        int g_y = y + r;
        if (g_y < 0 || g_y >= sh) continue;
        
        for (int c = 0; c < w; c++) {
            int g_x = x + c;
            if (g_x < 0 || g_x >= sw) continue;
            
            int r_sum = 0, g_sum = 0, b_sum = 0, count = 0;
            int start_kx = g_x - blur_radius;
            int end_kx = g_x + blur_radius;
            if (start_kx < 0) start_kx = 0;
            if (end_kx >= sw) end_kx = sw - 1;
            
            for (int kx = start_kx; kx <= end_kx; kx++) {
                uint32_t pixel = g_back_buffer[g_y * sw + kx];
                r_sum += (pixel >> 16) & 0xFF;
                g_sum += (pixel >> 8) & 0xFF;
                b_sum += pixel & 0xFF;
                count++;
            }
            if(count == 0) count = 1;
            uint32_t out_pixel = ((r_sum / count) << 16) | ((g_sum / count) << 8) | (b_sum / count);
            tmp_buf[r * w + c] = out_pixel;
        }
    }
    
    for (int r = 0; r < h; r++) {
        int g_y = y + r;
        if (g_y < 0 || g_y >= sh) continue;
        
        uint32_t cpu = smp_this_cpu_id();
        if (g_clip_enabled[cpu]) {
            int ptr = g_clip_stack_ptr[cpu];
            if (g_y < g_clip_stack_y[cpu][ptr] || g_y >= g_clip_stack_y[cpu][ptr] + g_clip_stack_h[cpu][ptr]) continue;
        }

        for (int c = 0; c < w; c++) {
            int g_x = x + c;
            if (g_x < 0 || g_x >= sw) continue;

            if (g_clip_enabled[cpu]) {
                int ptr = g_clip_stack_ptr[cpu];
                if (g_x < g_clip_stack_x[cpu][ptr] || g_x >= g_clip_stack_x[cpu][ptr] + g_clip_stack_w[cpu][ptr]) continue;
            }
            
            bool in_corner = false;
            int dx = 0, dy = 0;
            if (c < radius && r < radius) {
                dx = radius - c - 1; dy = radius - r - 1;
                in_corner = true;
            } else if (c >= w - radius && r < radius) {
                dx = c - (w - radius); dy = radius - r - 1;
                in_corner = true;
            } else if (c < radius && r >= h - radius) {
                dx = radius - c - 1; dy = r - (h - radius);
                in_corner = true;
            } else if (c >= w - radius && r >= h - radius) {
                dx = c - (w - radius); dy = r - (h - radius);
                in_corner = true;
            }
            
            if (in_corner) {
                if (dx*dx + dy*dy >= radius*radius) {
                    continue;
                }
            }
            
            int r_sum = 0, g_sum = 0, b_sum = 0, count = 0;
            int start_kr = r - blur_radius;
            int end_kr = r + blur_radius;
            if (start_kr < 0) start_kr = 0;
            if (end_kr >= h) end_kr = h - 1;
            
            for (int kr = start_kr; kr <= end_kr; kr++) {
                uint32_t pixel = tmp_buf[kr * w + c];
                r_sum += (pixel >> 16) & 0xFF;
                g_sum += (pixel >> 8) & 0xFF;
                b_sum += pixel & 0xFF;
                count++;
            }
            if(count == 0) count = 1;
            uint32_t blurred_pixel = ((r_sum / count) << 16) | ((g_sum / count) << 8) | (b_sum / count);
            
            uint32_t final_pixel = blend_color_alpha(blurred_pixel, tint_color, alpha);
            g_back_buffer[g_y * sw + g_x] = final_pixel;
        }
    }
    
    kfree(tmp_buf);
}

void draw_char(int x, int y, char c, uint32_t color) {
    if (g_current_ttf) {
        font_manager_render_char(g_current_ttf, x, y, c, color, put_pixel);
        return;
    }

    unsigned char uc = (unsigned char)c;
    if (uc > 127) return;

    uint32_t cpu = smp_this_cpu_id();
    bool has_rt = (cpu < MAX_RENDER_CPUS && g_render_target[cpu]);
    if (g_clip_enabled[cpu] && !has_rt) {
        int ptr = g_clip_stack_ptr[cpu];
        if (x + 8 <= g_clip_stack_x[cpu][ptr] || x >= g_clip_stack_x[cpu][ptr] + g_clip_stack_w[cpu][ptr] ||
            y + 8 <= g_clip_stack_y[cpu][ptr] || y >= g_clip_stack_y[cpu][ptr] + g_clip_stack_h[cpu][ptr]) {
            return;
        }
    }

    const uint8_t *glyph = font8x8_basic[uc];

    for (int row = 0; row < 8; row++) {
        for (int col = 0; col < 8; col++) {
            if ((glyph[row] >> (7 - col)) & 1) {
                put_pixel(x + col, y + row, color);
            }
        }
    }
}

void draw_char_bitmap(int x, int y, char c, uint32_t color) {
    unsigned char uc = (unsigned char)c;
    if (uc > 127) return;

    uint32_t cpu = smp_this_cpu_id();
    bool has_rt = (cpu < MAX_RENDER_CPUS && g_render_target[cpu]);
    if (g_clip_enabled[cpu] && !has_rt) {
        int ptr = g_clip_stack_ptr[cpu];
        if (x + 8 <= g_clip_stack_x[cpu][ptr] || x >= g_clip_stack_x[cpu][ptr] + g_clip_stack_w[cpu][ptr] ||
            y + 8 <= g_clip_stack_y[cpu][ptr] || y >= g_clip_stack_y[cpu][ptr] + g_clip_stack_h[cpu][ptr]) {
            return;
        }
    }

    const uint8_t *glyph = font8x8_basic[uc];
    for (int row = 0; row < 8; row++) {
        for (int col = 0; col < 8; col++) {
            if ((glyph[row] >> (7 - col)) & 1) {
                put_pixel(x + col, y + row, color);
            }
        }
    }
}

ttf_font_t *graphics_get_current_ttf(void) {
    return g_current_ttf;
}

void draw_string_bitmap(int x, int y, const char *str, uint32_t color) {
    const char *s = str;
    int cur_x = x;
    int cur_y = y;
    while (*s) {
        if (*s == '\n') {
            cur_x = x;
            cur_y += 10;
        } else if (*s == '\t') {
            cur_x += 8 * 4;
        } else {
            draw_char_bitmap(cur_x, cur_y, *s, color);
            cur_x += 8;
        }
        s++;
    }
}

int graphics_get_font_height(void) {
    if (g_current_ttf) {
        return (int)((g_current_ttf->ascent - g_current_ttf->descent) * g_current_ttf->scale);
    }
    return 10; // Fallback bitmap height
}

int graphics_get_font_height_scaled(float scale) {
    if (g_current_ttf) {
        return font_manager_get_font_height_scaled(g_current_ttf, scale);
    }
    return 10; // Fallback bitmap height
}

int graphics_get_string_width_scaled(const char *s, float scale) {
    if (g_current_ttf) {
        return font_manager_get_string_width_scaled(g_current_ttf, s, scale);
    }
    int len = 0;
    while (s && *s) {
        uint32_t codepoint = utf8_decode(&s);
        if (codepoint == '\t') len += 4;
        else len++;
    }
    return len * 8; // Fallback bitmap width
}

void draw_string(int x, int y, const char *s, uint32_t color) {
    if (g_current_ttf) draw_string_scaled(x, y, s, color, g_current_ttf->pixel_height);
    else draw_string_scaled(x, y, s, color, 15.0f);
}

void draw_string_scaled(int x, int y, const char *s, uint32_t color, float scale) {
    if (!s) return;
    int cur_x = x;
    
    if (g_current_ttf) {
        int baseline = y + font_manager_get_font_ascent_scaled(g_current_ttf, scale) - 2;
        int line_height = font_manager_get_font_line_height_scaled(g_current_ttf, scale);
        
        while (*s) {
            uint32_t codepoint = utf8_decode(&s);
            if (codepoint == '\n') {
                cur_x = x;
                baseline += line_height;
            } else if (codepoint == '\t') {
                cur_x += font_manager_get_codepoint_width_scaled(g_current_ttf, ' ', scale) * 4;
            } else {
                font_manager_render_char_scaled(g_current_ttf, cur_x, baseline, codepoint, color, scale, put_pixel);
                cur_x += font_manager_get_codepoint_width_scaled(g_current_ttf, codepoint, scale);
            }
        }
        return;
    }

    int cur_y = y;
    while (*s) {
        uint32_t codepoint = utf8_decode(&s);
        if (codepoint == '\n') {
            cur_x = x;
            cur_y += 10;
        } else if (codepoint == '\t') {
            cur_x += 8 * 4;
        } else {
            draw_char(cur_x, cur_y, (codepoint < 128) ? (char)codepoint : '?', color);
            cur_x += 8;
        }
    }
}

void draw_string_sloped(int x, int y, const char *s, uint32_t color, float slope) {
    if (g_current_ttf) draw_string_scaled_sloped(x, y, s, color, g_current_ttf->pixel_height, slope);
    else draw_string_scaled(x, y, s, color, 15.0f); // Fast fallback if no ttf
}

void draw_string_scaled_sloped(int x, int y, const char *s, uint32_t color, float scale, float slope) {
    if (!s) return;
    int cur_x = x;
    
    if (g_current_ttf) {
        int baseline = y + font_manager_get_font_ascent_scaled(g_current_ttf, scale) - 2;
        int line_height = font_manager_get_font_line_height_scaled(g_current_ttf, scale);
        
        while (*s) {
            uint32_t codepoint = utf8_decode(&s);
            if (codepoint == '\n') {
                cur_x = x;
                baseline += line_height;
            } else if (codepoint == '\t') {
                cur_x += font_manager_get_codepoint_width_scaled(g_current_ttf, ' ', scale) * 4;
            } else {
                font_manager_render_char_sloped(g_current_ttf, cur_x, baseline, codepoint, color, scale, slope, put_pixel);
                cur_x += font_manager_get_codepoint_width_scaled(g_current_ttf, codepoint, scale);
            }
        }
        return;
    }

    // Fallback to normal draw_string_scaled if no TTF
    draw_string_scaled(x, y, s, color, scale);
}

void draw_desktop_background(void) {
    if (!g_fb) return;
    
    if (g_use_image && g_bg_image) {
        // Draw wallpaper image (stretch/scale to screen)
        int x1 = 0, y1 = 0, x2 = g_fb->width, y2 = g_fb->height;
        uint32_t cpu = smp_this_cpu_id();
        if (g_clip_enabled[cpu]) {
            int ptr = g_clip_stack_ptr[cpu];
            x1 = g_clip_stack_x[cpu][ptr]; y1 = g_clip_stack_y[cpu][ptr];
            x2 = g_clip_stack_x[cpu][ptr] + g_clip_stack_w[cpu][ptr]; y2 = g_clip_stack_y[cpu][ptr] + g_clip_stack_h[cpu][ptr];
        }
        for (int y = y1; y < y2; y++) {
            int src_y = y * g_bg_image_h / (int)g_fb->height;
            if (src_y >= g_bg_image_h) src_y = g_bg_image_h - 1;
            uint32_t *row = &g_back_buffer[y * g_fb->width + x1];
            for (int x = x1; x < x2; x++) {
                int src_x = x * g_bg_image_w / (int)g_fb->width;
                if (src_x >= g_bg_image_w) src_x = g_bg_image_w - 1;
                *row++ = g_bg_image[src_y * g_bg_image_w + src_x];
            }
        }
    } else if (g_use_pattern) {
        // Optimized tiled pattern: only draw within the clipping/dirty rect
        int x1 = 0, y1 = 0, x2 = g_fb->width, y2 = g_fb->height;
        uint32_t cpu = smp_this_cpu_id();
        if (g_clip_enabled[cpu]) {
            int ptr = g_clip_stack_ptr[cpu];
            x1 = g_clip_stack_x[cpu][ptr]; y1 = g_clip_stack_y[cpu][ptr];
            x2 = g_clip_stack_x[cpu][ptr] + g_clip_stack_w[cpu][ptr]; y2 = g_clip_stack_y[cpu][ptr] + g_clip_stack_h[cpu][ptr];
        }

        for (int y = y1; y < y2; y++) {
            uint32_t *row = &g_back_buffer[y * g_fb->width + x1];
            int py = y % PATTERN_SIZE;
            for (int x = x1; x < x2; x++) {
                *row++ = g_bg_pattern[py * PATTERN_SIZE + (x % PATTERN_SIZE)];
            }
        }
    } else {
        // Draw solid color
        draw_rect(0, 0, g_fb->width, g_fb->height, g_bg_color);
    }
}

void graphics_set_bg_color(uint32_t color) {
    g_bg_color = color;
    g_use_pattern = false;
    g_use_image = false;
}

void graphics_set_bg_pattern(const uint32_t *pattern) {
    if (!pattern) return;
    
    // Copy pattern to internal buffer
    for (int i = 0; i < PATTERN_SIZE * PATTERN_SIZE; i++) {
        g_bg_pattern[i] = pattern[i];
    }
    g_use_pattern = true;
    g_use_image = false;
}

void graphics_set_bg_image(uint32_t *pixels, int w, int h) {
    g_bg_image = pixels;
    g_bg_image_w = w;
    g_bg_image_h = h;
    g_use_image = true;
    g_use_pattern = false;
}

void graphics_set_logo_pixels(uint32_t *pixels, int w, int h) {
    g_logo_pixels = pixels;
    g_logo_w = w;
    g_logo_h = h;
}

void draw_boredos_logo(int x, int y, int scale) {
    if (g_logo_pixels) {
        for (int r = 0; r < g_logo_h; r++) {
            for (int c = 0; c < g_logo_w; c++) {
                uint32_t pixel = g_logo_pixels[r * g_logo_w + c];
                uint8_t a = (pixel >> 24) & 0xFF;
                if (a == 0) continue;
                
                if (scale == 1) {
                    if (a == 255) {
                        put_pixel(x + c, y + r, pixel);
                    } else {
                        uint32_t dst = graphics_get_pixel(x + c, y + r);
                        put_pixel(x + c, y + r, blend_src_over_dst(dst, pixel));
                    }
                } else {
                    for (int sy = 0; sy < scale; sy++) {
                        for (int sx = 0; sx < scale; sx++) {
                            if (a == 255) {
                                put_pixel(x + (c * scale) + sx, y + (r * scale) + sy, pixel);
                            } else {
                                uint32_t dst = graphics_get_pixel(x + (c * scale) + sx, y + (r * scale) + sy);
                                put_pixel(x + (c * scale) + sx, y + (r * scale) + sy, blend_src_over_dst(dst, pixel));
                            }
                        }
                    }
                }
            }
        }
        return;
    }

    // Width: 60, Height: 16
    // 1: Magenta, 2: Blue, 3: Cyan, 4: White, 0: Deadspace
    static const uint8_t boredos_bmp[] = {
        1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 

        2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 

        3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        3,3,3,3,3,3,3,3,3,3,3,3,3,3,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 

        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
    };

    const int bmp_w = 60;
    const int bmp_h = 16;

    for (int r = 0; r < bmp_h; r++) {
        for (int c = 0; c < bmp_w; c++) {
            uint8_t p = boredos_bmp[r * bmp_w + c];
            if (p == 0) continue;

            uint32_t color = 0;
            switch(p) {
                case 1: color = 0xFFB589D6; break; // Magenta
                case 2: color = 0xFF569CD6; break; // Blue
                case 3: color = 0xFF4EC9B0; break; // Cyan
                case 4: color = 0xFFFFFFFF; break; // White
            }
            
            draw_rect(x + (c * scale), y + (r * scale), scale, scale, color);
        }
    }
}

// Double buffering functions
void graphics_clear_back_buffer(uint32_t color) {
    if (!g_fb) return;
    uint32_t *buf = g_back_buffer;
    for (int i = 0; i < (int)g_fb->width * (int)g_fb->height; i++) {
        *buf++ = color;
    }
}

void graphics_flip_buffer(void) {
    if (!g_fb) return;

    uint64_t flags = spinlock_acquire_irqsave(&graphics_lock);
    if (!g_dirty.active) {
        spinlock_release_irqrestore(&graphics_lock, flags);
        return;
    }

    int x = g_dirty.x;
    int y = g_dirty.y;
    int w = g_dirty.w;
    int h = g_dirty.h;
    
    // Clear dirty state
    g_dirty.active = false;
    spinlock_release_irqrestore(&graphics_lock, flags);

    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > (int)g_fb->width) w = g_fb->width - x;
    if (y + h > (int)g_fb->height) h = g_fb->height - y;

    if (w <= 0 || h <= 0) return;

    for (int i = 0; i < h; i++) {
        int curr_y = y + i;
        uint32_t *src_row = &g_back_buffer[curr_y * g_fb->width + x];
        
        if (g_fb->bpp == 32) {
            extern void mem_memcpy(void *dest, const void *src, size_t len);
            uint32_t *dst_row = (uint32_t *)((uint8_t *)g_fb->address + curr_y * g_fb->pitch) + x;
            mem_memcpy(dst_row, src_row, w * 4);
        } else if (g_fb->bpp == 16) {
            uint16_t *dst_row = (uint16_t *)((uint8_t *)g_fb->address + curr_y * g_fb->pitch) + x;
            for (int j = 0; j < w; j++) {
                uint32_t c = src_row[j];
                uint16_t r = ((c >> 16) & 0xFF) >> 3;
                uint16_t g = ((c >> 8)  & 0xFF) >> 2;
                uint16_t b = (c         & 0xFF) >> 3;
                dst_row[j] = (r << 11) | (g << 5) | b;
            }
        } else if (g_fb->bpp == 8) {
            uint8_t *dst_row = (uint8_t *)((uint8_t *)g_fb->address + curr_y * g_fb->pitch) + x;
            if (g_color_mode == 1) { // Grayscale
                for (int j = 0; j < w; j++) {
                    uint32_t c = src_row[j];
                    uint8_t r = (c >> 16) & 0xFF;
                    uint8_t g = (c >> 8) & 0xFF;
                    uint8_t b = c & 0xFF;
                    dst_row[j] = (uint8_t)((r * 77 + g * 150 + b * 29) >> 8);
                }
            } else if (g_color_mode == 2) { // Monochrome
                static const uint8_t bayer2[2][2] = {
                    {  0, 128 },
                    {192,  64 }
                };
                for (int j = 0; j < w; j++) {
                    uint32_t c = src_row[j];
                    uint8_t r = (c >> 16) & 0xFF;
                    uint8_t g = (c >> 8) & 0xFF;
                    uint8_t b = c & 0xFF;
                    
                    int gray = (r * 77 + g * 150 + b * 29) >> 8;
                    
                    gray = gray * 2;
                    if (gray > 255) gray = 255;
                    
                    int sx = x + j;
                    uint8_t threshold = bayer2[curr_y & 1][sx & 1];
                    
                    dst_row[j] = (gray > threshold) ? 255 : 0;
                }
            } else { // 256 Colors (Standard)
                for (int j = 0; j < w; j++) {
                    uint32_t c = src_row[j];
                    uint8_t r = ((c >> 16) & 0xFF) >> 5;
                    uint8_t g = ((c >> 8) & 0xFF) >> 5;
                    uint8_t b = (c & 0xFF) >> 6;
                    dst_row[j] = (r << 5) | (g << 2) | b;
                }
            }
        }
    }
}

void graphics_copy_screenbuffer(uint32_t *dest) {
    if (!g_fb || !dest) return;
    
    extern uint64_t wm_lock_acquire(void);
    extern void wm_lock_release(uint64_t);
    uint64_t rflags = wm_lock_acquire();

    int sw = (int)g_fb->width;
    int sh = (int)g_fb->height;
    
    // Copy from the composition back buffer, applying color mode transformations if necessary
    for (int y = 0; y < sh; y++) {
        uint32_t *src_row = &g_back_buffer[y * sw];
        for (int x = 0; x < sw; x++) {
            uint32_t px = src_row[x];
            
            if (g_color_mode == 1) { // 8-bit Grayscale
                uint8_t r = (px >> 16) & 0xFF;
                uint8_t g = (px >> 8) & 0xFF;
                uint8_t b = px & 0xFF;
                uint8_t gray = (uint8_t)((r * 77 + g * 150 + b * 29) >> 8);
                dest[y * sw + x] = 0xFF000000 | (gray << 16) | (gray << 8) | gray;
            } else if (g_color_mode == 2) { // 1-bit Monochrome (Dithered)
                static const uint8_t bayer2[2][2] = {
                    {  0, 128 },
                    {192,  64 }
                };
                uint8_t r = (px >> 16) & 0xFF;
                uint8_t g = (px >> 8) & 0xFF;
                uint8_t b = px & 0xFF;
                int gray = (r * 77 + g * 150 + b * 29) >> 8;
                
                // Boost contrast (matches graphics_flip_buffer logic)
                gray = gray * 2;
                if (gray > 255) gray = 255;
                
                uint8_t threshold = bayer2[y & 1][x & 1];
                dest[y * sw + x] = (gray > threshold) ? 0xFFFFFFFF : 0xFF000000;
            } else {
                // 32-bit (Standard)
                dest[y * sw + x] = px;
            }
        }
    }
    
    wm_lock_release(rflags);
}

void graphics_set_clipping(int x, int y, int w, int h) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    int sw = get_screen_width();
    int sh = get_screen_height();
    if (x + w > sw) w = sw - x;
    if (y + h > sh) h = sh - y;
    if (w < 0) w = 0;
    if (h < 0) h = 0;

    uint32_t cpu = smp_this_cpu_id();
    g_clip_stack_x[cpu][0] = x;
    g_clip_stack_y[cpu][0] = y;
    g_clip_stack_w[cpu][0] = w;
    g_clip_stack_h[cpu][0] = h;
    g_clip_stack_ptr[cpu] = 0; // Reset to base
    g_clip_enabled[cpu] = true;
}

void graphics_push_clipping(int x, int y, int w, int h) {
    uint32_t cpu = smp_this_cpu_id();
    int cur_ptr = g_clip_stack_ptr[cpu];
    if (cur_ptr + 1 >= CLIP_STACK_DEPTH) return; // Stack overflow

    // Intersect with current top
    int cx1 = g_clip_stack_x[cpu][cur_ptr];
    int cy1 = g_clip_stack_y[cpu][cur_ptr];
    int cx2 = cx1 + g_clip_stack_w[cpu][cur_ptr];
    int cy2 = cy1 + g_clip_stack_h[cpu][cur_ptr];

    int nx1 = x;
    int ny1 = y;
    int nx2 = x + w;
    int ny2 = y + h;

    if (nx1 < cx1) nx1 = cx1;
    if (ny1 < cy1) ny1 = cy1;
    if (nx2 > cx2) nx2 = cx2;
    if (ny2 > cy2) ny2 = cy2;

    int nw = nx2 - nx1;
    int nh = ny2 - ny1;
    if (nw < 0) nw = 0;
    if (nh < 0) nh = 0;

    g_clip_stack_ptr[cpu]++;
    g_clip_stack_x[cpu][cur_ptr + 1] = nx1;
    g_clip_stack_y[cpu][cur_ptr + 1] = ny1;
    g_clip_stack_w[cpu][cur_ptr + 1] = nw;
    g_clip_stack_h[cpu][cur_ptr + 1] = nh;
    g_clip_enabled[cpu] = true;
}

void graphics_pop_clipping(void) {
    uint32_t cpu = smp_this_cpu_id();
    if (g_clip_stack_ptr[cpu] > 0) {
        g_clip_stack_ptr[cpu]--;
    } else {
        g_clip_enabled[cpu] = false;
    }
}

void graphics_clear_clipping(void) {
    uint32_t cpu = smp_this_cpu_id();
    g_clip_stack_ptr[cpu] = 0;
    g_clip_enabled[cpu] = false;
}
void graphics_blit_buffer(uint32_t *src, int dst_x, int dst_y, int w, int h) {
    if (!g_fb || !src) return;
    int sw = get_screen_width();
    int sh = get_screen_height();
    
    uint32_t cpu = smp_this_cpu_id();
    int cx1 = 0, cy1 = 0, cx2 = sw, cy2 = sh;
    if (g_clip_enabled[cpu]) {
        int ptr = g_clip_stack_ptr[cpu];
        cx1 = g_clip_stack_x[cpu][ptr];
        cy1 = g_clip_stack_y[cpu][ptr];
        cx2 = cx1 + g_clip_stack_w[cpu][ptr];
        cy2 = cy1 + g_clip_stack_h[cpu][ptr];
    }

    int x1 = dst_x, y1 = dst_y, x2 = dst_x + w, y2 = dst_y + h;
    if (x1 < cx1) x1 = cx1;
    if (y1 < cy1) y1 = cy1;
    if (x2 > cx2) x2 = cx2;
    if (y2 > cy2) y2 = cy2;

    if (x1 >= x2 || y1 >= y2) return;

    for (int y = y1; y < y2; y++) {
        uint32_t *dst_row = &g_back_buffer[y * sw + x1];
        uint32_t *src_row = &src[(y - dst_y) * w + (x1 - dst_x)];
        int len = x2 - x1;
        for (int x = 0; x < len; x++) {
            uint32_t pcol = src_row[x];
            if ((pcol & 0xFF000000) != 0 || (pcol & 0xFFFFFF) != 0) {
                dst_row[x] = pcol;
            }
        }
    }
}
void graphics_scroll_back_buffer(int lines) {
    if (!g_fb || lines <= 0 || lines >= (int)g_fb->height) return;
    
    extern uint64_t wm_lock_acquire(void);
    extern void wm_lock_release(uint64_t);
    uint64_t rflags = wm_lock_acquire();

    int sw = (int)g_fb->width;
    int sh = (int)g_fb->height;
    
    for (int y = 0; y < sh - lines; y++) {
        uint32_t *dst = &g_back_buffer[y * sw];
        uint32_t *src = &g_back_buffer[(y + lines) * sw];
        for (int x = 0; x < sw; x++) dst[x] = src[x];
    }
    
    for (int y = sh - lines; y < sh; y++) {
        uint32_t *dst = &g_back_buffer[y * sw];
        for (int x = 0; x < sw; x++) dst[x] = 0;
    }

    wm_lock_release(rflags);
}
