// BOREDOS_APP_NAME: OpenTTD Test
// BOREDOS_APP_DESC: Experimental OpenTTD port launcher/stub.

#include "libc/libui.h"
#include "libc/stdlib.h"
#include <stdio.h>

int main(void) {
    printf("OpenTTD Test launched from BoredOS.\n");
    printf("This is a native launcher stub only; upstream OpenTTD is not built yet.\n");

    ui_window_t win = ui_window_create("OpenTTD Test", 180, 120, 440, 180);
    if (win < 0) {
        sleep(5000);
        return 0;
    }

    ui_draw_rect(win, 0, 0, 440, 180, 0xFF18222A);
    ui_draw_string(win, 22, 24, "OpenTTD Test", 0xFFFFFFFF);
    ui_draw_string(win, 22, 58, "BoredOS-native launcher stub is running.", 0xFFE6F2FF);
    ui_draw_string(win, 22, 86, "No SDL, OpenGL, audio, networking, or threads yet.", 0xFFC8D6E0);
    ui_draw_string(win, 22, 124, "Close this window to exit.", 0xFF9FB4C4);
    ui_mark_dirty(win, 0, 0, 440, 180);

    gui_event_t ev;
    while (1) {
        if (ui_get_event(win, &ev) && ev.type == GUI_EVENT_CLOSE) {
            break;
        }
        sleep(50);
    }

    return 0;
}
