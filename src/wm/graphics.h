// Copyright (c) 2023-2026 Christiaan (chris@boreddev.nl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
#ifndef GRAPHICS_H
#define GRAPHICS_H

#include <stdint.h>
#include <stdbool.h>
#include "limine.h"

// Dirty rectangle structure
typedef struct {
    int x, y, w, h;
    bool active;
} DirtyRect;

void graphics_init(struct limine_framebuffer *fb);
void graphics_init_fonts(void);
void put_pixel(int x, int y, uint32_t color);
uint32_t graphics_get_pixel(int x, int y);
void draw_rect(int x, int y, int w, int h, uint32_t color);
void draw_rounded_rect(int x, int y, int w, int h, int radius, uint32_t color);
void draw_rounded_rect_filled(int x, int y, int w, int h, int radius, uint32_t color);
void draw_rounded_rect_blurred(int x, int y, int w, int h, int radius, uint32_t tint_color, int blur_radius, int alpha);
void draw_char(int x, int y, char c, uint32_t color);
void draw_char_bitmap(int x, int y, char c, uint32_t color);
void draw_string(int x, int y, const char *s, uint32_t color);
void draw_string_scaled(int x, int y, const char *s, uint32_t color, float scale);
void draw_string_sloped(int x, int y, const char *s, uint32_t color, float slope);
void draw_string_scaled_sloped(int x, int y, const char *s, uint32_t color, float scale, float slope);
void draw_desktop_background(void);
void graphics_set_bg_color(uint32_t color);
void graphics_set_bg_pattern(const uint32_t *pattern);  // 128x128 pattern
void graphics_set_bg_image(uint32_t *pixels, int w, int h);  // Full-screen wallpaper image
void graphics_set_render_target(uint32_t *buffer, int w, int h);
void graphics_blit_buffer(uint32_t *src, int dst_x, int dst_y, int w, int h);
void graphics_blit_buffer_opaque(uint32_t *src, int dst_x, int dst_y, int w, int h);
void graphics_copy_screenbuffer(uint32_t *dest);


void draw_boredos_logo(int x, int y, int scale);
void graphics_set_logo_pixels(uint32_t *pixels, int w, int h);

// Get screen dimensions
int get_screen_width(void);
int get_screen_height(void);
uint64_t graphics_get_fb_addr(void);
int graphics_get_fb_bpp(void);
void graphics_update_resolution(int width, int height, int bpp, void* fb_addr, int color_mode);

// Dirty rectangle management
void graphics_mark_dirty(int x, int y, int w, int h);
void graphics_mark_screen_dirty(void);
DirtyRect graphics_get_dirty_rect(void);
void graphics_clear_dirty(void);
void graphics_clear_dirty_no_lock(void);

// Double buffering
void graphics_flip_buffer(void);
void graphics_clear_back_buffer(uint32_t color);
void graphics_scroll_back_buffer(int lines);

// Clipping
void graphics_set_clipping(int x, int y, int w, int h);
void graphics_push_clipping(int x, int y, int w, int h);
void graphics_pop_clipping(void);
void graphics_clear_clipping(void);

// Font access (requires font_manager.h for ttf_font_t)
#include "font_manager.h"
ttf_font_t *graphics_get_current_ttf(void);
int graphics_get_font_height(void);
int graphics_get_font_height_scaled(float scale);
int graphics_get_string_width_scaled(const char *s, float scale);
void graphics_set_font(const char *path);

#endif
