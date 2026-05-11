// Copyright (c) 2023-2026 Chris (boreddevnl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it has in it, as per the GPL license terms.
#include "usb.h"
#include "uhci.h"
#include "syscall.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static usb_hc_t usb_controllers[8];
static int usb_controller_count = 0;
static usb_device_t usb_devices[USB_MAX_DEVICES];
static int usb_device_count = 0;
static uint8_t device_address_counter = 1;

usb_hc_type_t usb_detect_controller(usb_hc_t *hc) {
    uint32_t class_code = pci_read_config(hc->bus, hc->device, hc->function, 0x08) >> 24;
    uint32_t prog_if = pci_read_config(hc->bus, hc->device, hc->function, 0x09) & 0xFF;
    
    if (class_code == 0x0C) {
        if (prog_if == 0x00) return USB_HC_UHCI;
        if (prog_if == 0x10) return USB_HC_OHCI;
        if (prog_if == 0x20) return USB_HC_EHCI;
        if (prog_if == 0x30) return USB_HC_XHCI;
    }
    
    return USB_HC_UNKNOWN;
}

void usb_init(void) {
    printf("[USB] Initializing USB stack...\n");
    
    int idx = 0;
    
    for (int bus = 0; bus < 256; bus++) {
        for (int device = 0; device < 32; device++) {
            for (int function = 0; function < 8; function++) {
                uint32_t vendor_id = pci_read_config(bus, device, function, 0x00) & 0xFFFF;
                if (vendor_id == 0xFFFF) continue;
                
                uint32_t class_code = pci_read_config(bus, device, function, 0x08) >> 24;
                
                if (class_code == 0x0C && idx < 8) {
                    usb_controllers[idx].bus = bus;
                    usb_controllers[idx].device = device;
                    usb_controllers[idx].function = function;
                    usb_controllers[idx].bar0 = pci_read_config(bus, device, function, 0x10);
                    usb_controllers[idx].bar1 = pci_read_config(bus, device, function, 0x14);
                    usb_controllers[idx].bar2 = pci_read_config(bus, device, function, 0x18);
                    usb_controllers[idx].bar3 = pci_read_config(bus, device, function, 0x1C);
                    usb_controllers[idx].bar4 = pci_read_config(bus, device, function, 0x20);
                    
                    usb_hc_type_t type = usb_detect_controller(&usb_controllers[idx]);
                    const char *type_str = "UNKNOWN";
                    if (type == USB_HC_UHCI) type_str = "UHCI";
                    else if (type == USB_HC_OHCI) type_str = "OHCI";
                    else if (type == USB_HC_EHCI) type_str = "EHCI";
                    else if (type == USB_HC_XHCI) type_str = "XHCI";
                    
                    printf("[USB] Found controller: %s\n", type_str);
                    
                    if (type == USB_HC_UHCI) {
                        if (uhci_init(&usb_controllers[idx])) {
                            printf("[USB] UHCI initialized\n");
                        }
                    }
                    
                    idx++;
                }
            }
        }
    }
    
    usb_controller_count = idx;
    printf("[USB] Total controllers found: %d\n", usb_controller_count);
}

bool usb_enumerate_device(usb_device_t *dev) {
    if (usb_device_count >= USB_MAX_DEVICES) return false;
    
    usb_setup_packet_t setup;
    uint8_t desc[18];
    
    setup.bmRequestType = USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_DEVICE;
    setup.bRequest = USB_REQ_GET_DESCRIPTOR;
    setup.wValue = (USB_DESC_DEVICE << 8) | 0;
    setup.wIndex = 0;
    setup.wLength = 18;
    
    if (usb_control_transfer(dev, &setup, desc) < 0) {
        return false;
    }
    
    dev->vendor_id = desc[8] | (desc[9] << 8);
    dev->product_id = desc[10] | (desc[11] << 8);
    dev->device_class = desc[4];
    dev->device_subclass = desc[5];
    dev->device_protocol = desc[6];
    dev->max_packet_size = desc[7];
    
    setup.bmRequestType = USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_DEVICE;
    setup.bRequest = USB_REQ_SET_ADDRESS;
    setup.wValue = device_address_counter;
    setup.wIndex = 0;
    setup.wLength = 0;
    
    if (usb_control_transfer(dev, &setup, NULL) < 0) {
        return false;
    }
    
    dev->config_value = 1;
    dev->interface_number = 0;
    dev->endpoint_in = 0x81;
    dev->endpoint_out = 0x01;
    
    usb_devices[usb_device_count++] = *dev;
    device_address_counter++;
    
    return true;
}

int usb_control_transfer(usb_device_t *dev, usb_setup_packet_t *setup, void *data) {
    (void)dev;
    if (usb_controller_count == 0) return -1;
    
    usb_hc_t *hc = &usb_controllers[0];
    uint16_t len = setup->wLength;
    
    return uhci_control_transfer(hc, setup, data, len);
}

int usb_interrupt_in(usb_device_t *dev, uint8_t endpoint, void *data, uint16_t len) {
    (void)dev;
    if (usb_controller_count == 0) return -1;
    
    usb_hc_t *hc = &usb_controllers[0];
    
    return uhci_interrupt_in(hc, endpoint, data, len);
}

void usb_enumerate_devices(void) {
    printf("[USB] Starting device enumeration\n");
    
    if (usb_controller_count == 0) {
        printf("[USB] No controllers available\n");
        return;
    }
    
    for (int port = 1; port <= 2; port++) {
        usb_device_t dev;
        memset(&dev, 0, sizeof(usb_device_t));
        
        if (usb_enumerate_device(&dev)) {
            printf("[USB] Device enumerated: VID=%04X PID=%04X\n", dev.vendor_id, dev.product_id);
        }
    }
}

int usb_get_device_count(void) {
    return usb_device_count;
}

usb_device_t* usb_get_device(int index) {
    if (index < 0 || index >= usb_device_count) return NULL;
    return &usb_devices[index];
}
