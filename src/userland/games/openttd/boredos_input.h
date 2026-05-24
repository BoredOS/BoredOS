#ifndef BOREDOS_OPENTTD_INPUT_H
#define BOREDOS_OPENTTD_INPUT_H

#include <stdbool.h>
#include <stdint.h>

typedef uint64_t ui_window_t;

typedef enum {
    BOREDOS_INPUT_NONE = 0,
    BOREDOS_INPUT_QUIT,
    BOREDOS_INPUT_KEY_DOWN,
    BOREDOS_INPUT_KEY_UP,
    BOREDOS_INPUT_MOUSE_DOWN,
    BOREDOS_INPUT_MOUSE_UP,
    BOREDOS_INPUT_MOUSE_MOVE,
    BOREDOS_INPUT_MOUSE_WHEEL,
    BOREDOS_INPUT_PAINT
} BoredOSInputType;

typedef struct {
    BoredOSInputType type;
    int key;
    int x;
    int y;
    int button;
    int wheel_delta;
} BoredOSInputEvent;

bool openttd_boredos_poll_input(ui_window_t window, BoredOSInputEvent *out_event);
bool openttd_boredos_event_requests_quit(const BoredOSInputEvent *event);

#endif
