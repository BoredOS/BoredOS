// Copyright (c) 2026 Lluciocc (https://github.com/lluciocc)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
#include "libc/syscall.h"
#include "libc/libui.h"
#include "libc/stdlib.h"
#include <stdint.h>
#include <stdbool.h>

#define WINDOW_W 400
#define WINDOW_H 200

static int last_key = -1;

static void draw(ui_window_t win) {
    ui_draw_rect(win, 0, 0, WINDOW_W, WINDOW_H, 0xFF121212);

    ui_draw_string(win, 20, 20, "Key Logger", 0xFFFFFFFF);

    if (last_key >= 0) {
        char buf[32];
        itoa(last_key, buf);

        ui_draw_string(win, 20, 60, "Last key:", 0xFFAAAAAA);
        ui_draw_string(win, 20, 90, buf, 0xFF6EA8FE);
    } else {
        ui_draw_string(win, 20, 60, "Press any key...", 0xFFAAAAAA);
    }
}

int main(void) {
    ui_window_t win = ui_window_create("keylog", 100, 100, WINDOW_W, WINDOW_H);
    if (!win) return 1;

    gui_event_t ev;

    while (1) {
        if (ui_get_event(win, &ev)) {

            if (ev.type == GUI_EVENT_KEY) {
                last_key = ev.arg1;

                printf("Key pressed: %d\n", ev.arg1);
            }

            if (ev.type == GUI_EVENT_CLOSE) {
                sys_exit(0);
            }

            draw(win);
            ui_mark_dirty(win, 0, 0, WINDOW_W, WINDOW_H);
        } else {
            sys_system(SYSTEM_CMD_SLEEP, 10, 0, 0, 0);
        }
    }

    return 0;
}
