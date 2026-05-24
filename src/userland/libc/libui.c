#include "libui.h"
#include "syscall.h"
#include "syscall_user.h"
#include <stddef.h>

extern uint64_t syscall3(uint64_t sys_num, uint64_t arg1, uint64_t arg2, uint64_t arg3);
extern uint64_t syscall4(uint64_t sys_num, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4);
extern uint64_t syscall5(uint64_t sys_num, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5);
extern uint64_t syscall6(uint64_t sys_num, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5, uint64_t arg6);

// sys_gui uses syscall #3
#define SYS_GUI 3

static uint32_t ui_float_bits(float value) {
    union {
        float f;
        uint32_t u;
    } bits;
    bits.f = value;
    return bits.u;
}

ui_window_t ui_window_create(const char *title, int x, int y, int w, int h) {
    uint64_t params[4] = { (uint64_t)x, (uint64_t)y, (uint64_t)w, (uint64_t)h };
    return (ui_window_t)syscall3(SYS_GUI, GUI_CMD_WINDOW_CREATE, (uint64_t)title, (uint64_t)params);
}

bool ui_get_event(ui_window_t win, gui_event_t *ev) {
    int res = (int)syscall3(SYS_GUI, GUI_CMD_GET_EVENT, (uint64_t)win, (uint64_t)ev);
    return res != 0;
}

void ui_draw_rect(ui_window_t win, int x, int y, int w, int h, uint32_t color) {
    uint64_t params[4] = { (uint64_t)x, (uint64_t)y, (uint64_t)w, (uint64_t)h };
    syscall4(SYS_GUI, GUI_CMD_DRAW_RECT, (uint64_t)win, (uint64_t)params, (uint64_t)color);
}

void ui_draw_rounded_rect_filled(ui_window_t win, int x, int y, int w, int h, int radius, uint32_t color) {
    uint64_t params[5] = { (uint64_t)x, (uint64_t)y, (uint64_t)w, (uint64_t)h, (uint64_t)radius };
    syscall4(SYS_GUI, GUI_CMD_DRAW_ROUNDED_RECT_FILLED, (uint64_t)win, (uint64_t)params, (uint64_t)color);
}

void ui_draw_string(ui_window_t win, int x, int y, const char *str, uint32_t color) {
    uint64_t coords = ((uint64_t)x & 0xFFFFFFFF) | ((uint64_t)y << 32);
    syscall5(SYS_GUI, GUI_CMD_DRAW_STRING, (uint64_t)win, coords, (uint64_t)str, (uint64_t)color);
}

void ui_draw_string_bitmap(ui_window_t win, int x, int y, const char *str, uint32_t color) {
    uint64_t coords = ((uint64_t)x & 0xFFFFFFFF) | ((uint64_t)y << 32);
    syscall5(SYS_GUI, GUI_CMD_DRAW_STRING_BITMAP, (uint64_t)win, coords, (uint64_t)str, (uint64_t)color);
}

void ui_mark_dirty(ui_window_t win, int x, int y, int w, int h) {
    uint64_t params[4] = { (uint64_t)x, (uint64_t)y, (uint64_t)w, (uint64_t)h };
    syscall3(SYS_GUI, GUI_CMD_MARK_DIRTY, (uint64_t)win, (uint64_t)params);
}

void ui_draw_image(ui_window_t win, int x, int y, int w, int h, uint32_t *image_data) {
    uint64_t params[4] = { (uint64_t)x, (uint64_t)y, (uint64_t)w, (uint64_t)h };
    syscall4(SYS_GUI, GUI_CMD_DRAW_IMAGE, (uint64_t)win, (uint64_t)params, (uint64_t)image_data);
}

void ui_draw_image_opaque(ui_window_t win, int x, int y, int w, int h, const uint32_t *image_data) {
    uint64_t params[4] = { (uint64_t)x, (uint64_t)y, (uint64_t)w, (uint64_t)h };
    syscall4(SYS_GUI, GUI_CMD_DRAW_IMAGE_OPAQUE, (uint64_t)win, (uint64_t)params, (uint64_t)image_data);
}

void ui_draw_image_opaque_strided(ui_window_t win, int x, int y, int w, int h, int stride, const uint32_t *image_data) {
    uint64_t params[5] = { (uint64_t)x, (uint64_t)y, (uint64_t)w, (uint64_t)h, (uint64_t)stride };
    syscall4(SYS_GUI, GUI_CMD_DRAW_IMAGE_OPAQUE_STRIDED, (uint64_t)win, (uint64_t)params, (uint64_t)image_data);
}

void ui_present_image_opaque_strided(ui_window_t win, int x, int y, int w, int h, int stride, const uint32_t *image_data) {
    uint64_t params[5] = { (uint64_t)x, (uint64_t)y, (uint64_t)w, (uint64_t)h, (uint64_t)stride };
    syscall4(SYS_GUI, GUI_CMD_PRESENT_IMAGE_OPAQUE_STRIDED, (uint64_t)win, (uint64_t)params, (uint64_t)image_data);
}

uint32_t ui_get_string_width(const char *str) {
    return (uint32_t)syscall3(SYS_GUI, GUI_CMD_GET_STRING_WIDTH, (uint64_t)str, 0);
}

uint32_t ui_get_font_height(void) {
    return (uint32_t)syscall3(SYS_GUI, GUI_CMD_GET_FONT_HEIGHT, 0, 0);
}

void ui_get_screen_size(uint64_t *out_w, uint64_t *out_h) {
    syscall3(SYS_GUI, GUI_CMD_GET_SCREEN_SIZE, (uint64_t)out_w, (uint64_t)out_h);
}

void ui_draw_string_scaled(ui_window_t win, int x, int y, const char *str, uint32_t color, float scale) {
    uint64_t coords = ((uint64_t)x & 0xFFFFFFFF) | ((uint64_t)y << 32);
    // Pack color into lower 32, scale (as uint32_t representation) into upper 32
    uint32_t scale_bits = ui_float_bits(scale);
    uint64_t packed_arg5 = ((uint64_t)scale_bits << 32) | (color & 0xFFFFFFFF);
    syscall5(SYS_GUI, GUI_CMD_DRAW_STRING_SCALED, (uint64_t)win, coords, (uint64_t)str, packed_arg5);
}

void ui_draw_string_scaled_sloped(ui_window_t win, int x, int y, const char *str, uint32_t color, float scale, float slope) {
    uint64_t coords = ((uint64_t)x & 0xFFFFFFFF) | ((uint64_t)y << 32);
    // Pack color into lower 32, scale (as uint32_t representation) into upper 32
    uint32_t scale_bits = ui_float_bits(scale);
    uint32_t slope_bits = ui_float_bits(slope);
    uint64_t packed_arg5 = ((uint64_t)scale_bits << 32) | (color & 0xFFFFFFFF);
    
    syscall6(SYS_GUI, GUI_CMD_DRAW_STRING_SCALED_SLOPED, (uint64_t)win, coords, (uint64_t)str, packed_arg5, (uint64_t)slope_bits);
}

uint32_t ui_get_string_width_scaled(const char *str, float scale) {
    uint32_t scale_bits = ui_float_bits(scale);
    return (uint32_t)syscall4(SYS_GUI, GUI_CMD_GET_STRING_WIDTH_SCALED, (uint64_t)str, (uint64_t)scale_bits, 0);
}

uint32_t ui_get_font_height_scaled(float scale) {
    uint32_t scale_bits = ui_float_bits(scale);
    return (uint32_t)syscall3(SYS_GUI, GUI_CMD_GET_FONT_HEIGHT_SCALED, (uint64_t)scale_bits, 0);
}

void ui_window_set_title(ui_window_t win, const char *title) {
    syscall3(SYS_GUI, GUI_CMD_WINDOW_SET_TITLE, (uint64_t)win, (uint64_t)title);
}

void ui_window_set_resizable(ui_window_t win, bool resizable) {
    syscall3(SYS_GUI, GUI_CMD_WINDOW_SET_RESIZABLE, (uint64_t)win, resizable ? 1 : 0);
}

void ui_set_font(ui_window_t win, const char *path) {
    syscall3(SYS_GUI, GUI_CMD_SET_FONT, (uint64_t)win, (uint64_t)path);
}
