// Copyright (c) 2023-2026 Chris (boreddevnl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in this file it has in it, as per the GPL license terms.
#ifndef DRIVER_H
#define DRIVER_H

#include "usb.h"
#include <stdbool.h>

#define MAX_DRIVERS 16

typedef struct {
    uint16_t vendor_id;
    uint16_t product_id;
    char name[64];
    bool (*probe)(usb_device_t *dev);
    int (*init)(usb_device_t *dev);
    void (*deinit)(usb_device_t *dev);
    int (*poll)(usb_device_t *dev);
    bool loaded;
} usb_driver_t;

typedef struct {
    usb_device_t device;
    usb_driver_t *driver;
    bool active;
} driver_instance_t;

void driver_manager_init(void);
int driver_register(usb_driver_t *driver);
int driver_unregister(usb_driver_t *driver);
int driver_load_for_device(usb_device_t *dev);
void driver_unload_for_device(usb_device_t *dev);
void driver_hotplug_poll(void);
void driver_poll_all(void);

#endif
