// Copyright (c) 2026 BoredOS Contributors
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
// BOREDOS_APP_DESC: Clipboard integration test.
// BOREDOS_APP_ICONS: /Library/images/icons/colloid/copyq.png
#include "libc/syscall.h"
#include "libc/libui.h"
#include "libc/string.h"
#include <stdbool.h>
#include <stdint.h>

#define WIN_W 400
#define WIN_H 200
#define BG    0xFF1E1E2E
#define WHITE 0xFFCDD6F4
#define GREEN 0xFFA6E3A1
#define RED   0xFFF38BA8
#define GREY  0xFF45475A

static void draw(ui_window_t win, const char *written, const char *read_back, bool match) {
    ui_draw_rect(win, 0, 0, WIN_W, WIN_H, BG);

    ui_draw_string(win, 10, 20, "Clipboard Test", WHITE);

    ui_draw_string(win, 10, 60, "Written:", GREY);
    ui_draw_string(win, 90, 60, written, WHITE);

    ui_draw_string(win, 10, 90, "Read back:", GREY);
    ui_draw_string(win, 90, 90, read_back, WHITE);

    ui_draw_string(win, 10, 130, "Result:", GREY);
    if (match)
        ui_draw_string(win, 90, 130, "PASS - write/read match", GREEN);
    else
        ui_draw_string(win, 90, 130, "FAIL - mismatch!", RED);

    ui_mark_dirty(win, 0, 0, WIN_W, WIN_H);
}

int main(void) {
    const char *test_str = "Hello, Clipboard!";
    char read_buf[64] = {0};

    ui_clipboard_set(test_str);
    int n = ui_clipboard_get(read_buf, sizeof(read_buf));

    bool match = (n > 0) && (strcmp(test_str, read_buf) == 0);

    ui_window_t win = ui_window_create("Clipboard Test", 100, 100, WIN_W, WIN_H);

    draw(win, test_str, read_buf, match);

    gui_event_t ev;
    while (1) {
        if (!ui_get_event(win, &ev)) { sys_yield(); continue; }
        if (ev.type == GUI_EVENT_CLOSE) break;
        if (ev.type == GUI_EVENT_PAINT) draw(win, test_str, read_buf, match);
    }

    return 0;
}
