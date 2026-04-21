// BOREDOS_APP_DESC: DOOM game runtime.
// BOREDOS_APP_ICONS: /Library/images/icons/colloid/doom-2016.png;/Library/images/icons/colloid/applications-games.png
#include "doomgeneric.h"
#include "doomkeys.h"
#include <libui.h>
#include <syscall.h>

static ui_window_t doom_win = 0;

void DG_Init(void) {
    doom_win = ui_window_create("DOOM", (1920 - DOOMGENERIC_RESX) / 2, (1080 - DOOMGENERIC_RESY) / 2, DOOMGENERIC_RESX, DOOMGENERIC_RESY);
}

static uint32_t scaled_buffer[600 * 900]; // DOOMGENERIC_RESX * DOOMGENERIC_RESY

void DG_DrawFrame(void) {
    if (doom_win) {
        int src_w = 640;
        int src_h = 400;
        int dst_w = DOOMGENERIC_RESX;
        int dst_h = DOOMGENERIC_RESY;
        
        for (int y = 0; y < dst_h; y++) {
            int sy = y * src_h / dst_h;
            for (int x = 0; x < dst_w; x++) {
                int sx = x * src_w / dst_w;
                scaled_buffer[y * dst_w + x] = 0xFF000000 | ((uint32_t*)DG_ScreenBuffer)[sy * src_w + sx];
            }
        }
        
        ui_draw_image(doom_win, 0, 0, dst_w, dst_h, scaled_buffer);
        ui_mark_dirty(doom_win, 0, 0, dst_w, dst_h);
    }
}

void DG_SleepMs(uint32_t ms) {
    uint32_t end_ticks = DG_GetTicksMs() + ms;
    while (DG_GetTicksMs() < end_ticks) {
        for(volatile int x=0; x<1000; x++);
    }
}

uint32_t DG_GetTicksMs(void) {
    int fd = sys_open("/proc/uptime", "r");
    if (fd < 0) return 0;
    char buf[128];
    int bytes = sys_read(fd, buf, 127);
    sys_close(fd);
    if (bytes <= 0) return 0;
    buf[bytes] = 0;

    char *p = strstr(buf, "Raw_Ticks:");
    if (!p) return 0;
    uint32_t ticks = atoi(p + 10);
    // 60Hz to ms: ticks * 1000 / 60 = ticks * 50 / 3
    return (ticks * 50) / 3;
}

void DG_SetWindowTitle(const char * title) {
}

#define KQ_SIZE 64
static struct { int pressed; unsigned char key; } key_queue[KQ_SIZE];
static int kq_head = 0;
static int kq_tail = 0;
static uint32_t key_held_until[256];

static void push_key(int pressed, unsigned char key) {
    int next = (kq_head + 1) % KQ_SIZE;
    if (next != kq_tail) {
        key_queue[kq_head].pressed = pressed;
        key_queue[kq_head].key = key;
        kq_head = next;
    }
}

int DG_GetKey(int* pressed, unsigned char* key) {
    if (kq_tail != kq_head) {
        *pressed = key_queue[kq_tail].pressed;
        *key = key_queue[kq_tail].key;
        kq_tail = (kq_tail + 1) % KQ_SIZE;
        return 1;
    }

    gui_event_t ev;
    while (ui_get_event(doom_win, &ev)) {
        if (ev.type == GUI_EVENT_CLOSE) {
            sys_exit(0);
        } else if (ev.type == GUI_EVENT_KEY || ev.type == GUI_EVENT_KEYUP) {
            unsigned char k = (unsigned char)ev.arg1;
            unsigned char dk = k;
            if (k == 17) dk = KEY_UPARROW;
            else if (k == 18) dk = KEY_DOWNARROW;
            else if (k == 19) dk = KEY_LEFTARROW;
            else if (k == 20) dk = KEY_RIGHTARROW;
            else if (k == 21) dk = KEY_FIRE;
            else if (k == 22) dk = KEY_RALT;
            else if (k == 23) dk = KEY_CAPSLOCK;
            else if (k == 27) dk = KEY_ESCAPE;
            else if (k == ' ') dk = KEY_USE;
            else if (k == '\n' || k == '\r') dk = KEY_ENTER;
            else if (k >= 'A' && k <= 'Z') dk = k + 32;

            if (ev.type == GUI_EVENT_KEY) {
                push_key(1, dk); 
            } else if (ev.type == GUI_EVENT_KEYUP) {
                push_key(0, dk);
            }
        }
    }

    if (kq_tail != kq_head) {
        *pressed = key_queue[kq_tail].pressed;
        *key = key_queue[kq_tail].key;
        kq_tail = (kq_tail + 1) % KQ_SIZE;
        return 1;
    }
    return 0;
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    char* fake_argv[] = {"doom", "-iwad", "/Library/DOOM/doom1.wad"};
    doomgeneric_Create(3, fake_argv);

    while (1) {
        doomgeneric_Tick();
        sleep(1);
    }

    return 0;
}
