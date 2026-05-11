// Copyright (c) 2023-2026 Chris (boreddevnl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in this file it has in it, as per the GPL license terms.
#include "hid.h"
#include "logitech_b110.h"
#include "../core/io.h"
#include <string.h>

extern void serial_write(const char *str);

static usb_device_t hid_mouse_devices[8];
static int hid_mouse_count = 0;

bool hid_parse_mouse_descriptor(const uint8_t *desc, int len, hid_mouse_desc_t *out) {
    if (!desc || !out || len < 10) return false;
    
    memset(out, 0, sizeof(hid_mouse_desc_t));
    
    int i = 0;
    while (i < len) {
        uint8_t prefix = desc[i++];
        uint8_t size = prefix & 0x03;
        uint8_t type = (prefix >> 2) & 0x03;
        uint8_t tag = prefix >> 4;
        
        uint32_t value = 0;
        if (size == 1) {
            value = desc[i++];
        } else if (size == 2) {
            value = desc[i];
            i++;
            value |= (desc[i] << 8);
            i++;
        } else if (size == 3) {
            value = desc[i];
            i++;
            value |= (desc[i] << 8);
            i++;
            value |= (desc[i] << 16);
            i++;
            value |= (desc[i] << 24);
            i++;
        }
        
        if (type == 1) {
            if (tag == 0x04) out->usage_page = value;
            if (tag == 0x08) out->usage = value;
        }
        
        if (type == 2) {
            if (tag == 0x01 && value == 0x30) out->max_x = 32767;
            if (tag == 0x01 && value == 0x31) out->max_y = 32767;
            if (tag == 0x09 && value == 0x38) out->has_wheel = true;
        }
    }
    
    out->button_count = 3;
    out->usage_page = 0x01;
    out->usage = 0x02;
    
    return true;
}

bool hid_init_mouse(usb_device_t *dev) {
    if (hid_mouse_count >= 8) return false;
    
    usb_setup_packet_t setup;
    
    setup.bmRequestType = USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE;
    setup.bRequest = USB_REQ_SET_PROTOCOL;
    setup.wValue = 0;
    setup.wIndex = dev->interface_number;
    setup.wLength = 0;
    
    usb_control_transfer(dev, &setup, NULL);
    
    setup.bmRequestType = USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE;
    setup.bRequest = USB_REQ_SET_IDLE;
    setup.wValue = 0;
    setup.wIndex = dev->interface_number;
    setup.wLength = 0;
    
    usb_control_transfer(dev, &setup, NULL);
    
    hid_mouse_devices[hid_mouse_count++] = *dev;
    dev->initialized = true;
    
    serial_write("[HID] Mouse initialized\n");
    return true;
}

int hid_mouse_get_report(usb_device_t *dev, hid_mouse_report_t *report) {
    if (!dev || !report) return -1;
    
    uint8_t buffer[8];
    int len = usb_interrupt_in(dev, dev->endpoint_in, buffer, sizeof(buffer));
    
    if (len < 3) return -1;
    
    report->buttons = buffer[0];
    report->x = (int8_t)buffer[1];
    report->y = (int8_t)buffer[2];
    report->wheel = (len >= 4) ? (int8_t)buffer[3] : 0;
    
    return len;
}

void usb_hid_init(void) {
    serial_write("[HID] Initializing HID subsystem\n");
    
    int device_count = usb_get_device_count();
    
    for (int i = 0; i < device_count; i++) {
        usb_device_t *dev = usb_get_device(i);
        
        if (dev && dev->device_class == USB_CLASS_HID) {
            serial_write("[HID] Found HID device\n");
            
            if (logitech_b110_probe(dev)) {
                serial_write("[HID] Initializing LT-B110\n");
                logitech_b110_init(dev);
            }
        }
    }
}
