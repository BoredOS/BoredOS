/*
* BoredOS USB HID Keyboard
*
* Copyright (C)2026 Zyperium <zzyperium@gmail.com>
*
* This software is dual-licensed under the MIT License and the GNU General 
* to use, redistribute, and/or modify this file under the terms of either license.
* Public License v3.0 (or, at your option, any later version). You may choose:
*
* Option 1: MIT License
* -----------------------------------------------------------------------------
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to deal
* in the Software without restriction, including without limitation the rights
* to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
* copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in all
* copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
* OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
* SOFTWARE.
*
* Option 2: GNU General Public License v3.0 (or later)
* -----------------------------------------------------------------------------
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program. If not, see <https://www.gnu.org/licenses/>.
*/

#ifndef USBKB_H
#define USBKB_H

#ifndef PACKED
#define PACKED __attribute__((packed))
#endif

#include "xHCI_drvr.h"

// Keyboard boot report
typedef struct {
    uint8_t modifiers;
    uint8_t reserved;
    uint8_t keycodes[6];
} PACKED USB_kbbr_t;

typedef struct {
    xHCI_EXTD_t *xHCI_dev;
    xHCI_PCI_t *xHCI_ctrl;

    uint8_t slot_id;
    uint8_t int_in;

    uint64_t report_phys;
    USB_kbbr_t *report_virt;
    USB_kbbr_t last_report;
} USB_kbd_t;

// Modifier masks
typedef enum {
    MOD_LEFT_CTRL   = (1 << 0),
    MOD_LEFT_SHIFT  = (1 << 1),
    MOD_LEFT_ALT    = (1 << 2),
    MOD_LEFT_GUI    = (1 << 3),
    MOD_RIGHT_CTRL  = (1 << 4),
    MOD_RIGHT_SHIFT = (1 << 5),
    MOD_RIGHT_ALT   = (1 << 6),
    MOD_RIGHT_GUI   = (1 << 7)
} USB_mm_t;

#define XUSB_KEY_UP     0x52
#define XUSB_KEY_DOWN   0x51
#define XUSB_KEY_LEFT   0x50
#define XUSB_KEY_RIGHT  0x4F
#define XUSB_KEY_F1     0x3A
#define XUSB_KEY_F10    0x43

void usbkb_init(void);

#define XUSB_EP_ATTR_INT 0x3

#endif