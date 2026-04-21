// BOREDOS_APP_DESC: Screen capture utility.
// BOREDOS_APP_ICONS: /Library/images/icons/colloid/accessories-screenshot.png
#include <stdint.h>
#include <stdbool.h>
#include "stdlib.h"
#include "libui.h"
#include "syscall_user.h"

#define STBI_WRITE_NO_STDIO
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#include <string.h>

#define GUI_CMD_GET_SCREEN_SIZE   50
#define GUI_CMD_GET_SCREENBUFFER  51
#define GUI_CMD_SHOW_NOTIFICATION 52
#define GUI_CMD_GET_DATETIME      53

#define PNG_WRITE_BUF_SIZE (8 * 1024 * 1024) // 8MB buffer to hold entire PNG
static uint8_t *png_output_buf = NULL;
static int png_output_idx = 0;

void png_write_func(void *context, void *data, int size) {
    (void)context;
    if (!png_output_buf) return;
    
    if (png_output_idx + size < PNG_WRITE_BUF_SIZE) {
        memcpy(png_output_buf + png_output_idx, data, size);
        png_output_idx += size;
    }
}

// Global filename for helper functions
static char g_filename[128];

void append_num(int num, int digits) {
    int len = 0; while (g_filename[len]) len++;
    if (digits == 4) {
        g_filename[len++] = '0' + (num / 1000) % 10;
        g_filename[len++] = '0' + (num / 100) % 10;
    }
    g_filename[len++] = '0' + (num / 10) % 10;
    g_filename[len++] = '0' + (num % 10);
    g_filename[len] = '\0';
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    
    // 1. Get screen size
    uint64_t w = 0, h = 0;
    syscall3(SYS_GUI, GUI_CMD_GET_SCREEN_SIZE, (uint64_t)&w, (uint64_t)&h);
    
    if (w == 0 || h == 0 || w > 4096 || h > 4096) {
        return 1;
    }
    
    // 2. Allocate buffers
    uint32_t *pixels = (uint32_t *)malloc(w * h * sizeof(uint32_t));
    uint8_t *rgb_pixels = (uint8_t *)malloc(w * h * 3);
    png_output_buf = (uint8_t *)malloc(PNG_WRITE_BUF_SIZE);
    
    if (!pixels || !rgb_pixels || !png_output_buf) {
        if (pixels) free(pixels);
        if (rgb_pixels) free(rgb_pixels);
        if (png_output_buf) free(png_output_buf);
        return 1;
    }
    
    // 3. Request screenbuffer
    syscall2(SYS_GUI, GUI_CMD_GET_SCREENBUFFER, (uint64_t)pixels);
    
    // 4. Convert 0xAARRGGBB to RGB
    for (int y = 0; y < (int)h; y++) {
        for (int x = 0; x < (int)w; x++) {
            uint32_t px = pixels[y * w + x];
            int idx = (y * w + x) * 3;
            rgb_pixels[idx + 0] = (px >> 16) & 0xFF; // R
            rgb_pixels[idx + 1] = (px >> 8) & 0xFF;  // G
            rgb_pixels[idx + 2] = (px) & 0xFF;       // B
        }
    }
    
    // 5. Get Datetime and construct filename
    uint64_t dt[6] = {0};
    syscall2(SYS_GUI, GUI_CMD_GET_DATETIME, (uint64_t)dt);
    
    strcpy(g_filename, "/root/Desktop/screenshot-");
    append_num((int)dt[0], 4); // Year
    append_num((int)dt[1], 2); // Month
    append_num((int)dt[2], 2); // Day
    
    int len = strlen(g_filename);
    g_filename[len++] = '-'; g_filename[len] = '\0';
    
    append_num((int)dt[3], 2); // Hour
    append_num((int)dt[4], 2); // Min
    append_num((int)dt[5], 2); // Sec
    strcat(g_filename, ".png");
    
    // 6. Generate PNG in memory
    png_output_idx = 0;
    int res = stbi_write_png_to_func(png_write_func, NULL, (int)w, (int)h, 3, rgb_pixels, (int)w * 3);
    
    // 7. Perform a SINGLE write to the filesystem
    if (res && png_output_idx > 0) {
        int fd = sys_open(g_filename, "w");
        if (fd >= 0) {
            sys_write_fs(fd, png_output_buf, png_output_idx);
            sys_close(fd);
            
            // Show notification
            char notif[256] = "Saved ";
            strcat(notif, g_filename + 14); // Skip "/root/Desktop/"
            syscall2(SYS_GUI, GUI_CMD_SHOW_NOTIFICATION, (uint64_t)notif);
        } else {
            res = 0;
        }
    }
    
    if (!res) {
        syscall2(SYS_GUI, GUI_CMD_SHOW_NOTIFICATION, (uint64_t)"Failed to save screenshot");
    }
    
    free(png_output_buf);
    free(rgb_pixels);
    free(pixels);
    
    return res ? 0 : 1;
}
