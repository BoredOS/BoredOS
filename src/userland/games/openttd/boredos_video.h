#ifndef BOREDOS_OPENTTD_VIDEO_H
#define BOREDOS_OPENTTD_VIDEO_H

#include <stdbool.h>
#include <stdint.h>

typedef uint64_t ui_window_t;

typedef struct {
    int width;
    int height;
    ui_window_t window;
    uint32_t *pixels;
} BoredOSVideo;

bool openttd_boredos_video_init(BoredOSVideo *video, const char *title, int width, int height);
void openttd_boredos_video_shutdown(BoredOSVideo *video);
void openttd_boredos_video_clear(BoredOSVideo *video, uint32_t argb);
void openttd_boredos_video_set_pixel(BoredOSVideo *video, int x, int y, uint32_t argb);
void openttd_boredos_video_fill_rect(BoredOSVideo *video, int x, int y, int w, int h, uint32_t argb);
void openttd_boredos_video_present(BoredOSVideo *video);
void openttd_boredos_video_draw_text(BoredOSVideo *video, int x, int y, const char *text, uint32_t argb);
uint32_t openttd_boredos_palette_to_argb(uint8_t index);

#endif
