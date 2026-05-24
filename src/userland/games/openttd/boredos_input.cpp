extern "C" {
#include "libc/libui.h"
}

#include "boredos_input.h"

bool openttd_boredos_poll_input(ui_window_t window, BoredOSInputEvent *out_event)
{
    gui_event_t ev;
    if (out_event == 0) return false;

    out_event->type = BOREDOS_INPUT_NONE;
    out_event->key = 0;
    out_event->x = 0;
    out_event->y = 0;
    out_event->button = 0;
    out_event->wheel_delta = 0;

    if (!ui_get_event(window, &ev)) return false;

    switch (ev.type) {
        case GUI_EVENT_CLOSE:
            out_event->type = BOREDOS_INPUT_QUIT;
            break;
        case GUI_EVENT_PAINT:
            out_event->type = BOREDOS_INPUT_PAINT;
            break;
        case GUI_EVENT_KEY:
            out_event->type = BOREDOS_INPUT_KEY_DOWN;
            out_event->key = ev.arg1;
            break;
        case GUI_EVENT_KEYUP:
            out_event->type = BOREDOS_INPUT_KEY_UP;
            out_event->key = ev.arg1;
            break;
        case GUI_EVENT_CLICK:
        case GUI_EVENT_MOUSE_DOWN:
        case GUI_EVENT_RIGHT_CLICK:
            out_event->type = BOREDOS_INPUT_MOUSE_DOWN;
            out_event->x = ev.arg1;
            out_event->y = ev.arg2;
            out_event->button = ev.arg3;
            if (ev.type == GUI_EVENT_RIGHT_CLICK) out_event->button = 2;
            break;
        case GUI_EVENT_MOUSE_UP:
            out_event->type = BOREDOS_INPUT_MOUSE_UP;
            out_event->x = ev.arg1;
            out_event->y = ev.arg2;
            out_event->button = ev.arg3;
            break;
        case GUI_EVENT_MOUSE_MOVE:
            out_event->type = BOREDOS_INPUT_MOUSE_MOVE;
            out_event->x = ev.arg1;
            out_event->y = ev.arg2;
            break;
        case GUI_EVENT_MOUSE_WHEEL:
            out_event->type = BOREDOS_INPUT_MOUSE_WHEEL;
            out_event->wheel_delta = ev.arg1;
            break;
        default:
            out_event->type = BOREDOS_INPUT_NONE;
            break;
    }

    return true;
}

bool openttd_boredos_event_requests_quit(const BoredOSInputEvent *event)
{
    if (event == 0) return false;
    return event->type == BOREDOS_INPUT_QUIT ||
        (event->type == BOREDOS_INPUT_KEY_DOWN && (event->key == 27 || event->key == 'q' || event->key == 'Q'));
}
