extern "C" {
#include "libc/libui.h"
#include "libc/stdlib.h"
#include "libc/string.h"
}

#include "boredos_video.h"

bool openttd_boredos_video_init(BoredOSVideo *video, const char *title, int width, int height)
{
    if (video == 0 || width <= 0 || height <= 0) return false;

    video->width = width;
    video->height = height;
    video->pixels = (uint32_t *)malloc((size_t)width * (size_t)height * sizeof(uint32_t));
    if (video->pixels == 0) return false;

    video->window = ui_window_create(title, 160, 90, width, height);
    if (video->window == 0) {
        free(video->pixels);
        video->pixels = 0;
        return false;
    }

    ui_window_set_resizable(video->window, false);
    openttd_boredos_video_clear(video, 0xFF0B1415u);
    openttd_boredos_video_present(video);
    ui_mark_dirty(video->window, 0, 0, video->width, video->height);
    return true;
}

void openttd_boredos_video_shutdown(BoredOSVideo *video)
{
    if (video == 0) return;
    if (video->pixels != 0) {
        free(video->pixels);
        video->pixels = 0;
    }
}

void openttd_boredos_video_clear(BoredOSVideo *video, uint32_t argb)
{
    if (video == 0 || video->pixels == 0) return;
    for (int y = 0; y < video->height; y++) {
        for (int x = 0; x < video->width; x++) {
            video->pixels[y * video->width + x] = argb;
        }
    }
}

void openttd_boredos_video_set_pixel(BoredOSVideo *video, int x, int y, uint32_t argb)
{
    if (video == 0 || video->pixels == 0) return;
    if (x < 0 || y < 0 || x >= video->width || y >= video->height) return;
    video->pixels[y * video->width + x] = argb;
}

void openttd_boredos_video_fill_rect(BoredOSVideo *video, int x, int y, int w, int h, uint32_t argb)
{
    if (video == 0 || video->pixels == 0 || w <= 0 || h <= 0) return;

    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + w;
    int y1 = y + h;
    if (x1 > video->width) x1 = video->width;
    if (y1 > video->height) y1 = video->height;

    for (int py = y0; py < y1; py++) {
        for (int px = x0; px < x1; px++) {
            video->pixels[py * video->width + px] = argb;
        }
    }
}

void openttd_boredos_video_present(BoredOSVideo *video)
{
    if (video == 0 || video->window == 0 || video->pixels == 0) return;
    ui_draw_image(video->window, 0, 0, video->width, video->height, video->pixels);
}

void openttd_boredos_video_draw_text(BoredOSVideo *video, int x, int y, const char *text, uint32_t argb)
{
    if (video == 0 || video->window == 0 || text == 0) return;
    ui_draw_string(video->window, x, y, text, argb);
}

uint32_t openttd_boredos_palette_to_argb(uint8_t index)
{
    uint8_t shade = (uint8_t)(32u + (index / 2u));
    uint8_t green = (uint8_t)(48u + (index / 3u));
    uint8_t blue = (uint8_t)(24u + (index / 5u));
    return 0xFF000000u | ((uint32_t)shade << 16) | ((uint32_t)green << 8) | blue;
}
