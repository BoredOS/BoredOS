#ifndef LIBUI_H
#define LIBUI_H

#include <stdint.h>
#include <stdbool.h>

// GUI Command IDs
#define GUI_CMD_WINDOW_CREATE 1
#define GUI_CMD_DRAW_RECT     2
#define GUI_CMD_DRAW_STRING   3
#define GUI_CMD_MARK_DIRTY    4
#define GUI_CMD_GET_EVENT     5
#define GUI_CMD_DRAW_ROUNDED_RECT_FILLED 6
#define GUI_CMD_DRAW_IMAGE    7
#define GUI_CMD_GET_STRING_WIDTH 8
#define GUI_CMD_GET_FONT_HEIGHT  9
#define GUI_CMD_WINDOW_SET_RESIZABLE 14
#define GUI_CMD_DRAW_STRING_BITMAP 10
#define GUI_CMD_DRAW_STRING_SCALED 11
#define GUI_CMD_GET_STRING_WIDTH_SCALED 12
#define GUI_CMD_GET_FONT_HEIGHT_SCALED 13
#define GUI_CMD_WINDOW_SET_TITLE 15
#define GUI_CMD_SET_FONT         16
#define GUI_CMD_DRAW_STRING_SCALED_SLOPED 18
#define GUI_CMD_GET_SCREEN_SIZE  50
#define GUI_CMD_GET_SCREENBUFFER 51
#define GUI_CMD_SHOW_NOTIFICATION 52
#define GUI_CMD_GET_DATETIME     53

// Event Types
#define GUI_EVENT_NONE        0
#define GUI_EVENT_PAINT       1
#define GUI_EVENT_CLICK       2
#define GUI_EVENT_RIGHT_CLICK 3
#define GUI_EVENT_CLOSE       4
#define GUI_EVENT_KEY         5
#define GUI_EVENT_KEYUP       10

#define GUI_EVENT_MOUSE_DOWN  6
#define GUI_EVENT_MOUSE_UP    7
#define GUI_EVENT_MOUSE_MOVE  8
#define GUI_EVENT_MOUSE_WHEEL 9
#define GUI_EVENT_RESIZE      11

typedef struct {
    int type;
    int arg1; // CLICK: x / KEY: legacy char-or-compat key
    int arg2; // CLICK: y / KEY: keycode
    int arg3; // CLICK: button state / KEY: modifier mask
    int arg4; // KEY: Unicode codepoint (0 if none)
} gui_event_t;

// Window Handle
typedef uint64_t ui_window_t;

// libui API
ui_window_t ui_window_create(const char *title, int x, int y, int w, int h);
bool ui_get_event(ui_window_t win, gui_event_t *ev);

void ui_draw_rect(ui_window_t win, int x, int y, int w, int h, uint32_t color);
void ui_draw_rounded_rect_filled(ui_window_t win, int x, int y, int w, int h, int radius, uint32_t color);
void ui_draw_string(ui_window_t win, int x, int y, const char *str, uint32_t color);
void ui_mark_dirty(ui_window_t win, int x, int y, int w, int h);
void ui_draw_image(ui_window_t win, int x, int y, int w, int h, uint32_t *image_data);
uint32_t ui_get_string_width(const char *str);
uint32_t ui_get_font_height(void);
void ui_get_screen_size(uint64_t *out_w, uint64_t *out_h);
void ui_draw_string_bitmap(ui_window_t win, int x, int y, const char *str, uint32_t color);

void ui_draw_string_scaled(ui_window_t win, int x, int y, const char *str, uint32_t color, float scale);
void ui_draw_string_scaled_sloped(ui_window_t win, int x, int y, const char *str, uint32_t color, float scale, float slope);
uint32_t ui_get_string_width_scaled(const char *str, float scale);
uint32_t ui_get_font_height_scaled(float scale);
void ui_window_set_title(ui_window_t win, const char *title);
void ui_window_set_resizable(ui_window_t win, bool resizable);
void ui_set_font(ui_window_t win, const char *path);

#endif
