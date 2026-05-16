#define STB_TRUETYPE_IMPLEMENTATION
#include "memory_manager.h"
#include "font_manager.h"
#include "stb_truetype.h"
#include "fat32.h"
#include "spinlock.h"
#include <stddef.h>

// Simple math implementations for stb_truetype
float ksqrtf(float x) {
    float res;
    asm volatile("sqrtss %1, %0" : "=x"(res) : "x"(x));
    return res;
}

float kfabsf(float x) {
    return (x < 0) ? -x : x;
}

float kpowf(float b, float e) {
    if (e == 0) return 1.0f;
    if (e == 1) return b;
    if (e == 0.5f) return ksqrtf(b);
    
    float res = 1.0f;
    for (int i = 0; i < (int)e; i++) res *= b;
    return res;
}

float kfmodf(float x, float y) {
    return x - (int)(x / y) * y;
}

float kcosf(float x) {
    float x2 = x * x;
    return 1.0f - (x2 / 2.0f) + (x2 * x2 / 24.0f) - (x2 * x2 * x2 / 720.0f);
}

float kacosf(float x) {
    if (x >= 1.0f) return 0;
    if (x <= -1.0f) return 3.14159f;
    return 1.57079f - x - (x*x*x)/6.0f;
}

extern void serial_write(const char *s);
extern uint32_t graphics_get_pixel(int x, int y);

static inline uint32_t alpha_blend(uint32_t bg, uint32_t fg, uint8_t alpha) {
    if (alpha == 0) return bg;
    if (alpha == 255) return fg;
    
    uint32_t rb = (((fg & 0xFF00FF) * alpha) + ((bg & 0xFF00FF) * (255 - alpha))) >> 8;
    uint32_t g = (((fg & 0x00FF00) * alpha) + ((bg & 0x00FF00) * (255 - alpha))) >> 8;
    return (rb & 0xFF00FF) | (g & 0x00FF00);
}

static ttf_font_t *default_font = NULL;
static ttf_font_t *fallback_font = NULL;

void font_manager_set_fallback_font(ttf_font_t *font) {
    fallback_font = font;
}

#define MAX_LOADED_FONTS 8
typedef struct {
    char path[128];
    ttf_font_t *font;
} loaded_font_t;

static loaded_font_t loaded_fonts[MAX_LOADED_FONTS];
static int loaded_font_count = 0;

#define FONT_CACHE_SIZE 2048
typedef struct {
    uint32_t codepoint;
    float scale;
    void *font;
    int w, h, xoff, yoff;
    unsigned char *bitmap;
} font_cache_entry_t;
static font_cache_entry_t font_cache[FONT_CACHE_SIZE] = {0};
// Global lock for the glyph cache. Prevents multi-core race conditions where 
// bitmaps were being freed while in use by other cores.
static spinlock_t font_cache_lock = SPINLOCK_INIT;

bool font_manager_init(void) {
    return true;
}

ttf_font_t* font_manager_load(const char *path, float size) {
    (void)size; 
    
    for(int i=0; i<loaded_font_count; i++) {
        int match = 1;
        for(int j=0; path[j] || loaded_fonts[i].path[j]; j++) {
            if (path[j] != loaded_fonts[i].path[j]) { match = 0; break; }
        }
        if (match) return loaded_fonts[i].font;
    }
    
    FAT32_FileHandle *fh = fat32_open(path, "r");
    if (!fh || !fh->valid) {
        serial_write("[FONT] Failed to open font file: ");
        serial_write(path);
        serial_write("\n");
        return NULL;
    }

    uint32_t fsize = fh->size;
    unsigned char *buffer = kmalloc(fsize);
    if (!buffer) {
        fat32_close(fh);
        return NULL;
    }

    int read = fat32_read(fh, buffer, fsize);
    fat32_close(fh);

    ttf_font_t *font = kmalloc(sizeof(ttf_font_t));
    if (!font) {
        kfree(buffer);
        return NULL;
    }

    stbtt_fontinfo *info = kmalloc(sizeof(stbtt_fontinfo));
    if (!info) {
        kfree(buffer);
        kfree(font);
        return NULL;
    }

    if (!stbtt_InitFont(info, buffer, 0)) {
        serial_write("[FONT] Failed to init font: ");
        serial_write(path);
        serial_write("\n");
        kfree(info);
        kfree(buffer);
        kfree(font);
        return NULL;
    }

    font->data = buffer;
    font->size = fsize;
    font->info = info;
    font->pixel_height = size;
    font->scale = stbtt_ScaleForPixelHeight(info, size);
    
    stbtt_GetFontVMetrics(info, &font->ascent, &font->descent, &font->line_gap);

    if (!default_font) default_font = font;
    
    if (loaded_font_count < MAX_LOADED_FONTS) {
        int i=0; while(path[i] && i<127) { loaded_fonts[loaded_font_count].path[i] = path[i]; i++; }
        loaded_fonts[loaded_font_count].path[i] = 0;
        loaded_fonts[loaded_font_count].font = font;
        loaded_font_count++;
    }

    return font;
}

uint32_t utf8_decode(const char **s) {
    const unsigned char *u = (const unsigned char *)*s;
    if (!*u) return 0;
    
    if (u[0] < 0x80) {
        *s = (const char *)(u + 1);
        return u[0];
    }
    
    if ((u[0] & 0xE0) == 0xC0 && (u[1] & 0xC0) == 0x80) {
        *s = (const char *)(u + 2);
        return ((u[0] & 0x1F) << 6) | (u[1] & 0x3F);
    }
    
    if ((u[0] & 0xF0) == 0xE0 && (u[1] & 0xC0) == 0x80 && (u[2] & 0xC0) == 0x80) {
        *s = (const char *)(u + 3);
        return ((u[0] & 0x0F) << 12) | ((u[1] & 0x3F) << 6) | (u[2] & 0x3F);
    }
    
    if ((u[0] & 0xF8) == 0xF0 && (u[1] & 0xC0) == 0x80 && (u[2] & 0xC0) == 0x80 && (u[3] & 0xC0) == 0x80) {
        *s = (const char *)(u + 4);
        return ((u[0] & 0x07) << 18) | ((u[1] & 0x3F) << 12) | ((u[2] & 0x3F) << 6) | (u[3] & 0x3F);
    }
    
    uint32_t codepoint = u[0];
    *s = (const char *)(u + 1);
    if (codepoint == 128) return 0x2014;
    if (codepoint == 129) return 0x2013;
    if (codepoint == 130) return 0x2022;
    if (codepoint == 131) return 0x2026;
    if (codepoint == 132) return 0x2122;
    if (codepoint == 133) return 0x20AC;
    if (codepoint == 134) return 0x00B7;
    return codepoint;
}

static unsigned char *get_padded_glyph_bitmap(stbtt_fontinfo *info, int glyph, float scale, int pad, int *w, int *h, int *xoff, int *yoff) {
    int x0, y0, x1, y1;
    stbtt_GetGlyphBitmapBoxSubpixel(info, glyph, scale, scale, 0.0f, 0.0f, &x0, &y0, &x1, &y1);

    *w = (x1 - x0) + pad * 2;
    *h = (y1 - y0) + pad * 2;
    *xoff = x0 - pad;
    *yoff = y0 - pad;

    if (*w <= 0 || *h <= 0) return NULL;

    stbtt_vertex *vertices;
    int num_verts = stbtt_GetGlyphShape(info, glyph, &vertices);
    if (!vertices) return NULL;

    unsigned char *bitmap = (unsigned char *)kmalloc(*w * *h);
    if (!bitmap) {
        stbtt_FreeShape(info, vertices);
        return NULL;
    }
    
    for (int i = 0; i < *w * *h; i++) bitmap[i] = 0;

    stbtt__bitmap gbm;
    gbm.pixels = bitmap;
    gbm.w = *w;
    gbm.h = *h;
    gbm.stride = *w;

    // shift the glyph into center
    stbtt_Rasterize(&gbm, 0.35f, vertices, num_verts, scale, scale, 0.0f, 0.0f, *xoff, *yoff, 1, info->userdata);
    stbtt_FreeShape(info, vertices);

    return bitmap;
}

void font_manager_render_char(ttf_font_t *font, int x, int y, uint32_t codepoint, uint32_t color, void (*put_pixel_fn)(int, int, uint32_t)) {
    if (!font) font = default_font;
    if (!font) return;
    font_manager_render_char_scaled(font, x, y, codepoint, color, font->pixel_height, put_pixel_fn);
}

void font_manager_render_char_scaled(ttf_font_t *font, int x, int y, uint32_t codepoint, uint32_t color, float scale, void (*put_pixel_fn)(int, int, uint32_t)) {
    if (!font) font = default_font;
    if (!font) return;

    stbtt_fontinfo *info = (stbtt_fontinfo *)font->info;
    float real_scale = stbtt_ScaleForPixelHeight(info, scale);

    int glyph = stbtt_FindGlyphIndex(info, codepoint);
    if (glyph == 0 && fallback_font) {
        info = (stbtt_fontinfo *)fallback_font->info;
        real_scale = stbtt_ScaleForPixelHeight(info, scale);
        glyph = stbtt_FindGlyphIndex(info, codepoint);
    }

    int out_w, out_h, xoff, yoff;
    // pad it by like 2 pixels, which appears to be fine
    unsigned char *bitmap = get_padded_glyph_bitmap(info, glyph, real_scale, 2, &out_w, &out_h, &xoff, &yoff);
    if (!bitmap) return;

    uint8_t r_src = (color >> 16) & 0xFF;
    uint8_t g_src = (color >> 8) & 0xFF;
    uint8_t b_src = color & 0xFF;

    for (int byte_y = 0; byte_y < out_h; byte_y++) {
        int screen_y = y + yoff + byte_y; 
        
        for (int byte_x = 0; byte_x < out_w; byte_x++) {
            uint8_t alpha = bitmap[byte_y * out_w + byte_x];
            if (alpha == 0) continue;

            int screen_x = x + xoff + byte_x;

            uint32_t bg_col = graphics_get_pixel(screen_x, screen_y); 
            uint8_t r_bg = (bg_col >> 16) & 0xFF;
            uint8_t g_bg = (bg_col >> 8) & 0xFF;
            uint8_t b_bg = bg_col & 0xFF;

            uint8_t r_out = ((r_src * alpha) + (r_bg * (255 - alpha))) / 255;
            uint8_t g_out = ((g_src * alpha) + (g_bg * (255 - alpha))) / 255;
            uint8_t b_out = ((b_src * alpha) + (b_bg * (255 - alpha))) / 255;

            uint32_t final_color = (0xFF000000) | (r_out << 16) | (g_out << 8) | b_out;

            put_pixel_fn(screen_x, screen_y, final_color);
        }
    }

    kfree(bitmap);
}

void font_manager_render_char_sloped(ttf_font_t *font, int x, int y, uint32_t codepoint, uint32_t color, float scale, float slope, void (*put_pixel_fn)(int, int, uint32_t)) {
    if (!font) font = default_font;
    if (!font) return;

    uint32_t hash = (codepoint * 31 + (uint32_t)scale * 73) % FONT_CACHE_SIZE;
    uint64_t fflags = spinlock_acquire_irqsave(&font_cache_lock);
    font_cache_entry_t *entry = &font_cache[hash];
    
    unsigned char *bitmap = NULL;
    int w, h, xoff, yoff;
    
    if (entry->bitmap && entry->codepoint == codepoint && entry->scale == scale && entry->font == font) {
        bitmap = entry->bitmap;
        w = entry->w; h = entry->h; xoff = entry->xoff; yoff = entry->yoff;
    } else {
        stbtt_fontinfo *info = (stbtt_fontinfo *)font->info;
        float real_scale = stbtt_ScaleForPixelHeight(info, scale);
        
        int glyph = stbtt_FindGlyphIndex(info, codepoint);
        if (glyph == 0 && fallback_font) {
            info = (stbtt_fontinfo *)fallback_font->info;
            real_scale = stbtt_ScaleForPixelHeight(info, scale);
            glyph = stbtt_FindGlyphIndex(info, codepoint);
        }

        spinlock_release_irqrestore(&font_cache_lock, fflags);
        bitmap = get_padded_glyph_bitmap(info, glyph, real_scale, 2, &w, &h, &xoff, &yoff);
        fflags = spinlock_acquire_irqsave(&font_cache_lock);

        if (entry->bitmap && entry->codepoint == codepoint && entry->scale == scale && entry->font == font) {
             if (bitmap) kfree(bitmap); 
             bitmap = entry->bitmap;
             w = entry->w; h = entry->h; xoff = entry->xoff; yoff = entry->yoff;
        } else {
            if (entry->bitmap) kfree(entry->bitmap);
            entry->codepoint = codepoint;
            entry->scale = scale;
            entry->font = font;
            entry->w = w; entry->h = h; entry->xoff = xoff; entry->yoff = yoff;
            entry->bitmap = bitmap;
        }
    }

    if (bitmap) {
        for (int row = 0; row < h; row++) {
            int slant_offset = (int)((h - row) * slope);

            for (int col = 0; col < w; col++) {
                unsigned char alpha = bitmap[row * w + col];
                if (alpha > 0) {
                    int px = x + col + xoff + slant_offset;
                    int py = y + row + yoff;
                    uint32_t bg = graphics_get_pixel(px, py);
                    put_pixel_fn(px, py, alpha_blend(bg, color, alpha));
                }
            }
        }
    }
    spinlock_release_irqrestore(&font_cache_lock, fflags);
}

int font_manager_get_string_width(ttf_font_t *font, const char *s) {
    if (!font) font = default_font;
    if (!font) return 0;
    return font_manager_get_string_width_scaled(font, s, font->pixel_height);
}

int font_manager_get_font_height_scaled(ttf_font_t *font, float scale) {
    if (!font) font = default_font;
    if (!font) return 0;
    float real_scale = stbtt_ScaleForPixelHeight((stbtt_fontinfo *)font->info, scale);
    return (int)((font->ascent - font->descent) * real_scale + 0.5f);
}

int font_manager_get_font_ascent_scaled(ttf_font_t *font, float scale) {
    if (!font) font = default_font;
    if (!font) return 0;
    float real_scale = stbtt_ScaleForPixelHeight((stbtt_fontinfo *)font->info, scale);
    return (int)(font->ascent * real_scale + 0.5f);
}

int font_manager_get_font_line_height_scaled(ttf_font_t *font, float scale) {
    if (!font) font = default_font;
    if (!font) return 0;
    float real_scale = stbtt_ScaleForPixelHeight((stbtt_fontinfo *)font->info, scale);
    return (int)((font->ascent - font->descent + font->line_gap) * real_scale + 0.5f);
}

int font_manager_get_string_width_scaled(ttf_font_t *font, const char *s, float scale) {
    if (!font) font = default_font;
    if (!font || !s) return 0;

    stbtt_fontinfo *info = (stbtt_fontinfo *)font->info;
    float real_scale = stbtt_ScaleForPixelHeight(info, scale);
    int width = 0;
    while (*s) {
        int advance, lsb;
        uint32_t codepoint = utf8_decode(&s);
        
        if (codepoint == '\t') {
            stbtt_GetCodepointHMetrics(info, ' ', &advance, &lsb);
            width += (int)(advance * real_scale + 0.5f) * 4;
            continue;
        }
        
        stbtt_fontinfo *current_info = info;
        float current_scale = real_scale;
        
        if (stbtt_FindGlyphIndex(current_info, codepoint) == 0 && fallback_font) {
            current_info = (stbtt_fontinfo *)fallback_font->info;
            current_scale = stbtt_ScaleForPixelHeight(current_info, scale);
        }
        
        stbtt_GetCodepointHMetrics(current_info, codepoint, &advance, &lsb);
        width += (int)(advance * current_scale + 0.5f);
    }
    return width;
}

int font_manager_get_codepoint_width_scaled(ttf_font_t *font, uint32_t codepoint, float scale) {
    if (!font) font = default_font;
    if (!font) return 0;
    stbtt_fontinfo *info = (stbtt_fontinfo *)font->info;
    float real_scale = stbtt_ScaleForPixelHeight(info, scale);
    
    if (codepoint == '\t') {
        int advance, lsb;
        stbtt_GetCodepointHMetrics(info, ' ', &advance, &lsb);
        return (int)(advance * real_scale + 0.5f) * 4;
    }
    
    if (stbtt_FindGlyphIndex(info, codepoint) == 0 && fallback_font) {
        info = (stbtt_fontinfo *)fallback_font->info;
        real_scale = stbtt_ScaleForPixelHeight(info, scale);
    }
    
    int advance, lsb;
    stbtt_GetCodepointHMetrics(info, codepoint, &advance, &lsb);
    return (int)(advance * real_scale + 0.5f);
}
