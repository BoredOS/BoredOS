// Copyright (c) 2023-2026 Chris (boreddevnl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in this file it has in it, as per the GPL license terms.
#ifndef HID_H
#define HID_H

#include "usb.h"
#include <stdint.h>
#include <stdbool.h>

#define HID_REPORT_DESC_MAX 256

typedef struct {
    uint8_t usage_page;
    uint8_t usage;
    uint16_t max_x;
    uint16_t max_y;
    uint8_t button_count;
    bool has_wheel;
} hid_mouse_desc_t;

typedef struct {
    int8_t x;
    int8_t y;
    int8_t wheel;
    uint8_t buttons;
} hid_mouse_report_t;

bool hid_parse_mouse_descriptor(const uint8_t *desc, int len, hid_mouse_desc_t *out);
bool hid_init_mouse(usb_device_t *dev);
int hid_mouse_get_report(usb_device_t *dev, hid_mouse_report_t *report);

void usb_hid_init(void);

#endif
