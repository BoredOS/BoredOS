// Copyright (c) 2023-2026 Chris (boreddevnl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in this file it has in it, as per the GPL license terms.
#ifndef LOGITECH_B110_H
#define LOGITECH_B110_H

#include "hid.h"
#include <stdint.h>
#include <stdbool.h>

#define LOGITECH_VID 0x046D
#define LOGITECH_B110_PID 0xC05A

bool logitech_b110_probe(usb_device_t *dev);
bool logitech_b110_init(usb_device_t *dev);
void logitech_b110_poll(void);

#endif
