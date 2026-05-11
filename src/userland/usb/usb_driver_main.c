// Copyright (c) 2023-2026 Chris (boreddevnl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in this file it has in it, as per the GPL license terms.
#include "usb.h"
#include "driver.h"
#include "logitech_b110.h"
#include <stdio.h>
#include <unistd.h>

extern void wm_handle_mouse(int dx, int dy, uint8_t buttons, int wheel);

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    
    printf("[USB] Starting userspace USB driver\n");
    
    driver_manager_init();
    
    printf("[USB] Initializing USB stack\n");
    usb_init();
    
    printf("[USB] Enumerating devices\n");
    usb_enumerate_devices();
    
    printf("[USB] Registering drivers\n");
    driver_register(logitech_b110_get_driver());
    
    printf("[USB] Loading drivers for detected devices\n");
    driver_hotplug_poll();
    
    printf("[USB] Entering main loop\n");
    while (1) {
        driver_hotplug_poll();
        driver_poll_all();
        usleep(10000);
    }
    
    return 0;
}
