// Copyright (c) 2023-2026 Chris (boreddevnl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it has in it, as per the GPL license terms.
#ifndef USB_H
#define USB_H

#include <stdint.h>
#include <stdbool.h>

#define USB_MAX_DEVICES 128
#define USB_MAX_ENDPOINTS 16

typedef struct {
    uint8_t bus;
    uint8_t device;
    uint8_t function;
    uint32_t bar0;
    uint32_t bar1;
    uint32_t bar2;
    uint32_t bar3;
    uint32_t bar4;
} usb_hc_t;

typedef enum {
    USB_HC_UHCI,
    USB_HC_OHCI,
    USB_HC_EHCI,
    USB_HC_XHCI,
    USB_HC_UNKNOWN
} usb_hc_type_t;

typedef struct {
    uint16_t vendor_id;
    uint16_t product_id;
    uint8_t device_class;
    uint8_t device_subclass;
    uint8_t device_protocol;
    uint8_t config_value;
    uint8_t interface_number;
    uint8_t alternate_setting;
    uint8_t endpoint_in;
    uint8_t endpoint_out;
    uint16_t max_packet_size;
    bool initialized;
} usb_device_t;

typedef struct {
    uint8_t bmRequestType;
    uint8_t bRequest;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;
} __attribute__((packed)) usb_setup_packet_t;

#define USB_REQ_GET_STATUS        0x00
#define USB_REQ_CLEAR_FEATURE     0x01
#define USB_REQ_SET_FEATURE       0x03
#define USB_REQ_SET_ADDRESS       0x05
#define USB_REQ_GET_DESCRIPTOR    0x06
#define USB_REQ_SET_DESCRIPTOR    0x07
#define USB_REQ_GET_CONFIGURATION 0x08
#define USB_REQ_SET_CONFIGURATION 0x09
#define USB_REQ_GET_INTERFACE     0x0A
#define USB_REQ_SET_INTERFACE     0x0B
#define USB_REQ_SET_IDLE          0x0A
#define USB_REQ_SET_PROTOCOL      0x0B

#define USB_DESC_DEVICE           0x01
#define USB_DESC_CONFIG           0x02
#define USB_DESC_STRING           0x03
#define USB_DESC_INTERFACE        0x04
#define USB_DESC_ENDPOINT         0x05
#define USB_DESC_HID              0x21

#define USB_DIR_OUT               0x00
#define USB_DIR_IN                0x80

#define USB_TYPE_STANDARD         0x00
#define USB_TYPE_CLASS            0x20
#define USB_TYPE_VENDOR           0x40

#define USB_RECIP_DEVICE          0x00
#define USB_RECIP_INTERFACE       0x01
#define USB_RECIP_ENDPOINT        0x02

#define USB_CLASS_HID             0x03
#define USB_CLASS_MASS_STORAGE    0x08

void usb_init(void);
usb_hc_type_t usb_detect_controller(usb_hc_t *hc);
bool usb_enumerate_device(usb_device_t *dev);
int usb_control_transfer(usb_device_t *dev, usb_setup_packet_t *setup, void *data);
int usb_interrupt_in(usb_device_t *dev, uint8_t endpoint, void *data, uint16_t len);
void usb_enumerate_devices(void);
int usb_get_device_count(void);
usb_device_t* usb_get_device(int index);

#endif
