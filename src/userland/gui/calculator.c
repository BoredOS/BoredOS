// Copyright (c) 2023-2026 Chris (boreddevnl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
// BOREDOS_APP_DESC: Graphical calculator utility.
// BOREDOS_APP_ICONS: /Library/images/icons/colloid/calc.png
#include "syscall.h"
#include "libui.h"
#include "../../wm/libwidget.h"
#include <stdbool.h>
#include "stdlib.h"

#define SCALE 1000000LL

#define COLOR_DARK_BG       0xFF1E1E1E
#define COLOR_DARK_PANEL    0xFF2D2D2D
#define COLOR_DARK_TEXT     0xFFF0F0F0
#define COLOR_DARK_BORDER   0xFF3A3A3A

static ui_window_t win_calculator;

static long long calc_acc = 0;
static long long calc_curr = 0;
static char calc_op = 0;
static bool calc_new_entry = true;
static bool calc_error = false;
static bool calc_decimal_mode = false;
static long long calc_decimal_divisor = 10;
static char display_buffer[1024];
static int display_buf_len = 0;

static widget_button_t buttons[20];
static const char *labels[] = {
    "C", "sqr", "rt", "/",
    "7", "8", "9", "*",
    "4", "5", "6", "-",
    "1", "2", "3", "+",
    "0", ".", "BS", "="
};

static void calc_draw_rect(void *user_data, int x, int y, int w, int h, uint32_t color) {
    ui_draw_rect((ui_window_t)user_data, x, y, w, h, color);
}
static void calc_draw_rounded_rect_filled(void *user_data, int x, int y, int w, int h, int r, uint32_t color) {
    ui_draw_rounded_rect_filled((ui_window_t)user_data, x, y, w, h, r, color);
}
static void calc_draw_string(void *user_data, int x, int y, const char *str, uint32_t color) {
    ui_draw_string((ui_window_t)user_data, x, y, str, color);
}

static widget_context_t calc_ctx = {
    .user_data = 0,
    .draw_rect = calc_draw_rect,
    .draw_rounded_rect_filled = calc_draw_rounded_rect_filled,
    .draw_string = calc_draw_string,
    .mark_dirty = NULL
};

static long long isqrt(long long n) {
    if (n < 0) return -1;
    if (n == 0) return 0;
    long long x = n;
    long long y = 1;
    while (x > y) {
        x = (x + y) / 2;
        y = n / x;
    }
    return x;
}

static void fixed_to_str(long long n, char *buf) {
    if (n == 0) {
        buf[0] = '0'; buf[1] = 0; return;
    }
    char temp[64];
    int pos = 0;
    bool neg = n < 0;
    if (neg) n = -n;
    long long int_part = n / SCALE;
    long long frac_part = n % SCALE;
    
    char frac_buf[16];
    int f_idx = 0;
    for(int k=0; k<6; k++) {
        long long div = 100000;
        for(int m=0; m<k; m++) div /= 10;
        frac_buf[f_idx++] = '0' + ((frac_part / div) % 10);
    }
    frac_buf[f_idx] = 0;
    while (f_idx > 0 && frac_buf[f_idx-1] == '0') f_idx--;
    frac_buf[f_idx] = 0;
    if (f_idx > 0) {
        for (int i = f_idx - 1; i >= 0; i--) temp[pos++] = frac_buf[i];
        temp[pos++] = '.';
    }
    if (int_part == 0) {
        temp[pos++] = '0';
    } else {
        while (int_part > 0) {
            temp[pos++] = '0' + (int_part % 10);
            int_part /= 10;
        }
    }
    if (neg) temp[pos++] = '-';
    int j = 0;
    while (pos > 0) buf[j++] = temp[--pos];
    buf[j] = 0;
}

static void update_display(void) {
    if (calc_error) {
        char *err = "Error";
        int i = 0; while(err[i]) { display_buffer[i] = err[i]; i++; }
        display_buffer[i] = 0;
    } else {
        fixed_to_str(calc_curr, display_buffer);
    }
    display_buf_len = 0; while(display_buffer[display_buf_len]) display_buf_len++;
}

static void calculator_paint(void) {
    int w = 180;
    int h = 230;
    ui_draw_rect(win_calculator, 4, 4, w - 8, h - 34, COLOR_DARK_BG);
    ui_draw_rounded_rect_filled(win_calculator, 10, 10, w - 20, 25, 6, COLOR_DARK_PANEL);
    
    int text_w = display_buf_len * 8;
    int text_x = w - 15 - text_w;
    ui_draw_string(win_calculator, text_x, 18, display_buffer, COLOR_DARK_TEXT);
    
    for (int i = 0; i < 20; i++) {
        widget_button_draw(&calc_ctx, &buttons[i]);
    }
}

static void do_op(void) {
    if (calc_op == '+') calc_acc += calc_curr;
    else if (calc_op == '-') calc_acc -= calc_curr;
    else if (calc_op == '*') {
        calc_acc = (calc_acc * calc_curr) / SCALE;
    }
    else if (calc_op == '/') {
        if (calc_curr == 0) calc_error = true;
        else calc_acc = (calc_acc * SCALE) / calc_curr;
    } else {
        calc_acc = calc_curr;
    }
}

static void handle_button_click(int idx) {
    const char *labels_map[] = {
        "C", "s", "r", "/",
        "7", "8", "9", "*",
        "4", "5", "6", "-",
        "1", "2", "3", "+",
        "0", ".", "B", "="
    };
    char lbl = labels_map[idx][0];
    
    if (lbl >= '0' && lbl <= '9') {
        if (calc_new_entry || calc_error) {
            calc_curr = (lbl - '0') * SCALE;
            calc_new_entry = false;
            calc_decimal_mode = false;
        } else {
            if (calc_decimal_mode) {
                 if (calc_decimal_divisor <= SCALE) {
                     long long digit_val = ((long long)(lbl - '0') * SCALE) / calc_decimal_divisor;
                     if (calc_curr >= 0) calc_curr += digit_val;
                     else calc_curr -= digit_val;
                     calc_decimal_divisor *= 10;
                 }
            } else {
                if (calc_curr >= 0) calc_curr = calc_curr * 10 + (lbl - '0') * SCALE;
                else calc_curr = calc_curr * 10 - (lbl - '0') * SCALE;
            }
        }
        calc_error = false;
    } else if (lbl == '.') {
        if (calc_new_entry) {
            calc_curr = 0;
            calc_new_entry = false;
        }
        if (!calc_decimal_mode) {
            calc_decimal_mode = true;
            calc_decimal_divisor = 10;
        }
    } else if (lbl == 'C') {
        calc_curr = 0; calc_acc = 0; calc_op = 0;
        calc_new_entry = true; calc_error = false; calc_decimal_mode = false;
    } else if (lbl == 'B') {
        if (!calc_new_entry && !calc_error) {
            if (calc_decimal_mode) {
                if (calc_decimal_divisor > 10) {
                    calc_decimal_divisor /= 10;
                    long long unit = SCALE / calc_decimal_divisor;
                    calc_curr = (calc_curr / (unit * 10)) * (unit * 10);
                } else {
                    calc_decimal_mode = false;
                    calc_decimal_divisor = 10;
                    calc_curr = (calc_curr / SCALE) * SCALE;
                }
            } else {
                calc_curr = (calc_curr / SCALE / 10) * SCALE;
            }
        }
    } else if (lbl == 's') { // sqr
        calc_curr = (calc_curr * calc_curr) / SCALE; calc_new_entry = true;
    } else if (lbl == 'r') { // rt
        long long s = isqrt(calc_curr);
        if (s == -1) calc_error = true;
        else calc_curr = s * 1000;
        calc_new_entry = true;
    } else if (lbl == '=') {
        do_op();
        calc_curr = calc_acc; calc_op = 0; calc_new_entry = true; calc_decimal_mode = false;
    } else {
        if (!calc_new_entry) {
            if (calc_op) do_op();
            else calc_acc = calc_curr;
        }
        calc_op = lbl; calc_new_entry = true; calc_decimal_mode = false;
    }
}

int main(void) {
    win_calculator = ui_window_create("Calculator", 200, 200, 180, 230);
    calc_ctx.user_data = (void *)win_calculator;
    
    int bw = 35, bh = 25, gap = 5, start_x = 10, start_y = 40;
    for (int i = 0; i < 20; i++) {
        int r = i / 4;
        int c = i % 4;
        widget_button_init(&buttons[i], start_x + c*(bw+gap), start_y + r*(bh+gap), bw, bh, labels[i]);
    }

    calc_curr = 0;
    calc_acc = 0;
    calc_op = 0;
    calc_new_entry = true;
    update_display();
    
    calculator_paint();
    ui_mark_dirty(win_calculator, 0, 0, 180, 230);
    
    gui_event_t ev;
    while (1) {
        if (ui_get_event(win_calculator, &ev)) {
            if (ev.type == GUI_EVENT_PAINT) {
                calculator_paint();
                ui_mark_dirty(win_calculator, 0, 0, 180, 230);
            } else if (ev.type == GUI_EVENT_CLICK || ev.type == GUI_EVENT_MOUSE_DOWN || ev.type == GUI_EVENT_MOUSE_UP) {
                bool needs_paint = false;
                for (int i=0; i<20; i++) {
                    if (widget_button_handle_mouse(&buttons[i], ev.arg1, ev.arg2, ev.type == GUI_EVENT_MOUSE_DOWN, ev.type == GUI_EVENT_CLICK, NULL)) {
                        needs_paint = true;
                        if (ev.type == GUI_EVENT_CLICK) {
                            handle_button_click(i);
                            update_display();
                        }
                    }
                }
                if (needs_paint) {
                    calculator_paint();
                    ui_mark_dirty(win_calculator, 0, 0, 180, 230);
                }
            } else if (ev.type == GUI_EVENT_CLOSE) {
                sys_exit(0);
            }
        } else {
            sleep(10);
        }
    }
    
    return 0;
}
