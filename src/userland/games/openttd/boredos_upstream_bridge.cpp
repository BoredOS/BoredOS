extern "C" {
#include "libc/libui.h"
#include "libc/stdlib.h"
#include "libc/string.h"
#include "libc/syscall.h"
#include "libc/syscall_user.h"
}

#include <stdint.h>

typedef struct {
    ui_window_t window;
    int width;
    int height;
} OpenTTDBoredOSWindow;

typedef struct {
    int type;
    int legacy;
    int keycode;
    int mods;
    uint32_t codepoint;
    int x;
    int y;
    int buttons;
    int wheel;
} OpenTTDBoredOSEvent;

extern "C" void *ottd_boredos_window_create(const char *title, int width, int height)
{
    if (width <= 0 || height <= 0) return 0;
    sys_serial_write("[OpenTTD] creating BoredOS window\n");

    OpenTTDBoredOSWindow *window = (OpenTTDBoredOSWindow *)malloc(sizeof(OpenTTDBoredOSWindow));
    if (window == 0) {
        sys_serial_write("[OpenTTD] window wrapper allocation failed\n");
        return 0;
    }

    window->window = ui_window_create(title ? title : "OpenTTD", 160, 90, width, height + 20);
    if (window->window == 0) {
        sys_serial_write("[OpenTTD] ui_window_create failed\n");
        free(window);
        return 0;
    }

    window->width = width;
    window->height = height;
    ui_window_set_resizable(window->window, true);
    sys_serial_write("[OpenTTD] BoredOS window created\n");
    return window;
}

extern "C" void ottd_boredos_window_destroy(void *opaque)
{
    OpenTTDBoredOSWindow *window = (OpenTTDBoredOSWindow *)opaque;
    if (window == 0) return;
    free(window);
}

extern "C" void ottd_boredos_window_present(void *opaque, const uint32_t *pixels, int width, int height)
{
    OpenTTDBoredOSWindow *window = (OpenTTDBoredOSWindow *)opaque;
    if (window == 0 || pixels == 0 || width <= 0 || height <= 0) return;

    ui_present_image_opaque_strided(window->window, 0, 0, width, height, width, pixels);
}

extern "C" void ottd_boredos_window_present_rect(void *opaque, const uint32_t *pixels, int stride, int x, int y, int width, int height)
{
    OpenTTDBoredOSWindow *window = (OpenTTDBoredOSWindow *)opaque;
    if (window == 0 || pixels == 0 || stride <= 0 || width <= 0 || height <= 0) return;
    if (x < 0 || y < 0 || x + width > stride) return;

    ui_present_image_opaque_strided(window->window, x, y, width, height, stride, pixels + ((size_t)y * (size_t)stride) + x);
}

extern "C" int ottd_boredos_window_poll(void *opaque, OpenTTDBoredOSEvent *out)
{
    OpenTTDBoredOSWindow *window = (OpenTTDBoredOSWindow *)opaque;
    gui_event_t ev;
    if (window == 0 || out == 0) return 0;
    if (!ui_get_event(window->window, &ev)) return 0;

    memset(out, 0, sizeof(*out));
    out->type = ev.type;

    switch (ev.type) {
        case GUI_EVENT_CLOSE:
            out->legacy = 27;
            out->keycode = 1;
            break;
        case GUI_EVENT_KEY:
        case GUI_EVENT_KEYUP:
            out->legacy = ev.arg1;
            out->keycode = ev.arg2;
            out->mods = ev.arg3;
            out->codepoint = (uint32_t)ev.arg4;
            break;
        case GUI_EVENT_CLICK:
            out->x = ev.arg1;
            out->y = ev.arg2;
            out->buttons = 1;
            break;
        case GUI_EVENT_MOUSE_DOWN:
        case GUI_EVENT_MOUSE_UP:
        case GUI_EVENT_MOUSE_MOVE:
        case GUI_EVENT_RIGHT_CLICK:
            out->x = ev.arg1;
            out->y = ev.arg2;
            out->buttons = ev.arg3;
            if (ev.type == GUI_EVENT_MOUSE_DOWN || ev.type == GUI_EVENT_CLICK) out->buttons |= 1;
            if (ev.type == GUI_EVENT_RIGHT_CLICK) out->buttons |= 2;
            break;
        case GUI_EVENT_MOUSE_WHEEL:
            out->wheel = ev.arg1;
            break;
        case GUI_EVENT_RESIZE:
            window->width = ev.arg1;
            window->height = ev.arg2 > 20 ? ev.arg2 - 20 : ev.arg2;
            out->x = window->width;
            out->y = window->height;
            break;
        default:
            break;
    }

    return 1;
}

extern "C" void ottd_boredos_sleep_ms(uint32_t ms)
{
    sys_system(SYSTEM_CMD_SLEEP, ms, 0, 0, 0);
}
