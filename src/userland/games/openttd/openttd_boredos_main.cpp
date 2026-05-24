// BOREDOS_APP_NAME: OpenTTD Port
// BOREDOS_APP_DESC: Native BoredOS OpenTTD port launcher and driver bring-up.
// BOREDOS_APP_ICONS: /Library/images/icons/colloid/applications-games.png

extern "C" {
#include "libc/libui.h"
#include "libc/stdio.h"
#include "libc/syscall.h"
}

#include "boredos_files.h"
#include "boredos_input.h"
#include "boredos_timer.h"
#include "boredos_video.h"
#include "port_config.h"

void openttd_boredos_sound_null_init(void);
void openttd_boredos_sound_null_shutdown(void);

static void draw_isometric_tile(BoredOSVideo *video, int cx, int cy, int w, int h, uint32_t top, uint32_t left, uint32_t right)
{
    for (int y = 0; y < h; y++) {
        int half_width = (y < h / 2) ? (y * w / h) : ((h - y) * w / h);
        for (int x = -half_width; x <= half_width; x++) {
            openttd_boredos_video_set_pixel(video, cx + x, cy + y, top);
        }
    }

    for (int y = 0; y < h / 2; y++) {
        for (int x = 0; x < w / 2 - y; x++) {
            openttd_boredos_video_set_pixel(video, cx - x, cy + h / 2 + y, left);
            openttd_boredos_video_set_pixel(video, cx + x, cy + h / 2 + y, right);
        }
    }
}

static bool openttd_boredos_basesets_present(void)
{
    return openttd_boredos_file_exists(OPENTTD_BOREDOS_REQUIRED_GFX) &&
        openttd_boredos_file_exists(OPENTTD_BOREDOS_REQUIRED_SFX) &&
        openttd_boredos_file_exists(OPENTTD_BOREDOS_REQUIRED_MSX);
}

static void draw_port_checkpoint(BoredOSVideo *video, int mouse_x, int mouse_y, bool assets_present)
{
    openttd_boredos_video_clear(video, 0xFF102024u);

    for (int y = 0; y < video->height; y++) {
        uint32_t band = (uint32_t)(y / 8);
        uint32_t color = 0xFF14323Au + ((band & 7u) << 8);
        openttd_boredos_video_fill_rect(video, 0, y, video->width, 1, color);
    }

    for (int row = 0; row < 7; row++) {
        for (int col = 0; col < 9; col++) {
            int cx = 92 + col * 54 - row * 18;
            int cy = 176 + row * 24 + col * 4;
            uint32_t top = ((row + col) & 1) ? 0xFF3E8F4Eu : 0xFF4AA75Au;
            draw_isometric_tile(video, cx, cy, 46, 26, top, 0xFF2E6E42u, 0xFF245536u);
        }
    }

    openttd_boredos_video_fill_rect(video, 305, 226, 180, 10, 0xFF7A4A24u);
    openttd_boredos_video_fill_rect(video, 330, 206, 120, 20, 0xFFB24732u);
    openttd_boredos_video_fill_rect(video, 352, 186, 56, 24, 0xFF243A5Eu);
    openttd_boredos_video_fill_rect(video, 360, 192, 14, 8, 0xFFCAE6FFu);
    openttd_boredos_video_fill_rect(video, 386, 192, 14, 8, 0xFFCAE6FFu);
    openttd_boredos_video_fill_rect(video, 326, 236, 14, 14, 0xFF202020u);
    openttd_boredos_video_fill_rect(video, 430, 236, 14, 14, 0xFF202020u);

    openttd_boredos_video_fill_rect(video, 0, 0, video->width, 62, 0xEE0A1418u);
    openttd_boredos_video_fill_rect(video, 0, 418, video->width, 62, 0xEE0A1418u);
    openttd_boredos_video_present(video);

    openttd_boredos_video_draw_text(video, 20, 18, "OpenTTD Port for BoredOS", 0xFFFFFFFFu);
    openttd_boredos_video_draw_text(video, 20, 42, "Bring-up launcher: upstream source is copied, full game core is not linked yet.", 0xFFD7E9EFu);

    if (assets_present) {
        openttd_boredos_video_draw_text(video, 20, 426, "OpenGFX/OpenSFX/OpenMSX base-set archives are packaged in /Library/OpenTTD/baseset.", 0xFF9BE889u);
    } else {
        openttd_boredos_video_draw_text(video, 20, 426, "Missing base-set archive: expected OpenGFX, OpenSFX, and OpenMSX under /Library/OpenTTD/baseset.", 0xFFFFD36Eu);
    }
    openttd_boredos_video_draw_text(video, 20, 448, "Esc/Q/window close exits. Mouse and keyboard events are being consumed natively.", 0xFFC8D6E0u);

    char buf[96];
    snprintf(buf, sizeof(buf), "mouse=%d,%d tick_ms=%u upstream=%s", mouse_x, mouse_y, openttd_boredos_ticks_ms(), OPENTTD_BOREDOS_UPSTREAM_COMMIT);
    openttd_boredos_video_draw_text(video, 20, 464, buf, 0xFF9FB4C4u);
    ui_mark_dirty(video->window, 0, 0, video->width, video->height);
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    printf("OpenTTD/BoredOS port launcher starting\n");
    printf("video=boredos sound=null music=null network=disabled threads=disabled\n");

    BoredOSVideo video;
    video.window = 0;
    video.pixels = 0;
    video.width = 0;
    video.height = 0;

    if (!openttd_boredos_video_init(&video, "OpenTTD", OPENTTD_BOREDOS_WINDOW_WIDTH, OPENTTD_BOREDOS_WINDOW_HEIGHT)) {
        printf("OpenTTD/BoredOS: failed to create native BoredOS window\n");
        return 1;
    }

    openttd_boredos_sound_null_init();

    int mouse_x = 0;
    int mouse_y = 0;
    bool assets_present = openttd_boredos_basesets_present();
    bool running = true;
    draw_port_checkpoint(&video, mouse_x, mouse_y, assets_present);

    while (running) {
        BoredOSInputEvent event;
        bool needs_redraw = false;

        while (openttd_boredos_poll_input(video.window, &event)) {
            if (openttd_boredos_event_requests_quit(&event)) {
                running = false;
                break;
            }
            if (event.type == BOREDOS_INPUT_MOUSE_MOVE || event.type == BOREDOS_INPUT_MOUSE_DOWN || event.type == BOREDOS_INPUT_MOUSE_UP) {
                mouse_x = event.x;
                mouse_y = event.y;
                needs_redraw = true;
            } else if (event.type == BOREDOS_INPUT_PAINT || event.type == BOREDOS_INPUT_KEY_DOWN) {
                needs_redraw = true;
            }
        }

        if (needs_redraw) {
            assets_present = openttd_boredos_basesets_present();
            draw_port_checkpoint(&video, mouse_x, mouse_y, assets_present);
        }

        openttd_boredos_sleep_ms(16);
    }

    openttd_boredos_sound_null_shutdown();
    openttd_boredos_video_shutdown(&video);
    return 0;
}
