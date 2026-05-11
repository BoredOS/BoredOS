// Copyright (c) 2023-2026 Chris (boreddevnl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in this file it has in it, as per the GPL license terms.
#include "logitech_b110.h"
#include "../core/io.h"

extern void serial_write(const char *str);
extern void wm_handle_mouse(int dx, int dy, int buttons, int wheel);

static usb_device_t b110_device;
static bool b110_initialized = false;

bool logitech_b110_probe(usb_device_t *dev) {
    if (dev->vendor_id == LOGITECH_VID && dev->product_id == LOGITECH_B110_PID) {
        serial_write("[B110] Logitech B110 mouse detected\n");
        return true;
    }
    return false;
}

bool logitech_b110_init(usb_device_t *dev) {
    if (!logitech_b110_probe(dev)) return false;
    
    b110_device = *dev;
    b110_initialized = hid_init_mouse(dev);
    
    if (b110_initialized) {
        serial_write("[LT-B110] Mouse USB driver initialized\n");
    }
    
    return b110_initialized;
}

void logitech_b110_poll(void) {
    if (!b110_initialized) return;
    
    hid_mouse_report_t report;
    if (hid_mouse_get_report(&b110_device, &report) > 0) {
        int buttons = 0;
        if (report.buttons & 0x01) buttons |= 0x01;
        if (report.buttons & 0x02) buttons |= 0x02;
        if (report.buttons & 0x04) buttons |= 0x04;
        
        wm_handle_mouse(report.x, report.y, buttons, report.wheel);
    }
}
