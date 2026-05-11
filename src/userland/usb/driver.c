// Copyright (c) 2023-2026 Chris (boreddevnl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in this file it has in it, as per the GPL license terms.
#include "driver.h"
#include <string.h>
#include <stdio.h>

static usb_driver_t *driver_registry[MAX_DRIVERS];
static int driver_count = 0;
static driver_instance_t active_instances[MAX_DRIVERS];
static int instance_count = 0;

void driver_manager_init(void) {
    memset(driver_registry, 0, sizeof(driver_registry));
    memset(active_instances, 0, sizeof(active_instances));
    driver_count = 0;
    instance_count = 0;
}

int driver_register(usb_driver_t *driver) {
    if (!driver || driver_count >= MAX_DRIVERS) return -1;
    
    for (int i = 0; i < driver_count; i++) {
        if (driver_registry[i]->vendor_id == driver->vendor_id &&
            driver_registry[i]->product_id == driver->product_id) {
            return -1;
        }
    }
    
    driver_registry[driver_count++] = driver;
    driver->loaded = false;
    return 0;
}

int driver_unregister(usb_driver_t *driver) {
    if (!driver) return -1;
    
    for (int i = 0; i < driver_count; i++) {
        if (driver_registry[i] == driver) {
            for (int j = i; j < driver_count - 1; j++) {
                driver_registry[j] = driver_registry[j + 1];
            }
            driver_count--;
            driver->loaded = false;
            return 0;
        }
    }
    
    return -1;
}

int driver_load_for_device(usb_device_t *dev) {
    if (!dev) return -1;
    
    for (int i = 0; i < driver_count; i++) {
        usb_driver_t *driver = driver_registry[i];
        
        if (driver->vendor_id == dev->vendor_id &&
            driver->product_id == dev->product_id) {
            
            if (instance_count >= MAX_DRIVERS) return -1;
            
            if (driver->probe && !driver->probe(dev)) {
                continue;
            }
            
            if (driver->init && driver->init(dev) != 0) {
                continue;
            }
            
            active_instances[instance_count].device = *dev;
            active_instances[instance_count].driver = driver;
            active_instances[instance_count].active = true;
            instance_count++;
            
            driver->loaded = true;
            printf("[DRIVER] Loaded %s for VID:PID %04X:%04X\n", 
                   driver->name, dev->vendor_id, dev->product_id);
            return 0;
        }
    }
    
    return -1;
}

void driver_unload_for_device(usb_device_t *dev) {
    if (!dev) return;
    
    for (int i = 0; i < instance_count; i++) {
        if (active_instances[i].active &&
            active_instances[i].device.vendor_id == dev->vendor_id &&
            active_instances[i].device.product_id == dev->product_id) {
            
            if (active_instances[i].driver->deinit) {
                active_instances[i].driver->deinit(dev);
            }
            
            active_instances[i].driver->loaded = false;
            active_instances[i].active = false;
            
            for (int j = i; j < instance_count - 1; j++) {
                active_instances[j] = active_instances[j + 1];
            }
            instance_count--;
            
            printf("[DRIVER] Unloaded driver for VID:PID %04X:%04X\n",
                   dev->vendor_id, dev->product_id);
            return;
        }
    }
}

void driver_hotplug_poll(void) {
    extern int usb_get_device_count(void);
    extern usb_device_t* usb_get_device(int index);
    
    int current_count = usb_get_device_count();
    
    for (int i = 0; i < current_count; i++) {
        usb_device_t *dev = usb_get_device(i);
        if (!dev) continue;
        
        bool found = false;
        for (int j = 0; j < instance_count; j++) {
            if (active_instances[j].active &&
                active_instances[j].device.vendor_id == dev->vendor_id &&
                active_instances[j].device.product_id == dev->product_id) {
                found = true;
                break;
            }
        }
        
        if (!found && !dev->initialized) {
            driver_load_for_device(dev);
            dev->initialized = true;
        }
    }
    
    for (int i = 0; i < instance_count; i++) {
        bool still_present = false;
        for (int j = 0; j < current_count; j++) {
            usb_device_t *dev = usb_get_device(j);
            if (dev &&
                active_instances[i].device.vendor_id == dev->vendor_id &&
                active_instances[i].device.product_id == dev->product_id) {
                still_present = true;
                break;
            }
        }
        
        if (!still_present) {
            driver_unload_for_device(&active_instances[i].device);
            i--;
        }
    }
}

void driver_poll_all(void) {
    for (int i = 0; i < instance_count; i++) {
        if (active_instances[i].active && active_instances[i].driver->poll) {
            active_instances[i].driver->poll(&active_instances[i].device);
        }
    }
}
