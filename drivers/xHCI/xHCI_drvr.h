/*
* BoredOS xHCI Host Controller Driver API
*
* Copyright (C) 2026 Zyperium <zzyperium@gmail.com>
*
* This software is dual-licensed under the MIT License and the GNU General 
* Public License v3.0 (or, at your option, any later version). You may choose:
* to use, redistribute, and/or modify this file under the terms of either license.
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

#ifndef XHCI_DRVR_H
#define XHCI_DRVR_H
#include <stdint.h>

#ifndef XHCI_H
typedef struct xHCI_PCI_t xHCI_PCI_t;
typedef struct xHCI_EXTD_t xHCI_EXTD_t;
#endif

struct xHCI_EXTD_t {
    void (*destroy)(xHCI_EXTD_t *this);
    void (*initialize)(xHCI_EXTD_t *this, xHCI_PCI_t *host, uint8_t _slot, void *endpoints, int ep_count);
    void (*on_int)(xHCI_EXTD_t *this, uint32_t bytes_transferred, uint32_t endpoint_id, uint64_t param_event);
    void (*start)(xHCI_EXTD_t *this);
    void *local_data;
};

// Match flags, class code is always matched but subclasses/protocols are only matched when
// their flag is set (non-0)
#define XHCI_MATCH_SC       (1u << 0)
#define XHCI_MATCH_PROT     (1u << 1)

// ClassCode SubClass Protocol
typedef struct {
    uint8_t cc;
    uint8_t sc;
    uint8_t prot;
    uint8_t match;
} xHCI_CCSCPR_t;

#define XHCI_CL_USB_HUB     0x9
#define XHCI_CL_USB_HID     0x3
#define XHCI_CL_USB_STOR    0x8

#define XHCI_BOOT_SUBCL     0x1

#define XHCI_PR_HID_KB      0x1
#define XHCI_PR_HID_MS      0x2

#define XHCI_MAX_DRVRS      16

int xHCI_submit_new_drvr(xHCI_CCSCPR_t type, xHCI_EXTD_t *tmpl);

xHCI_EXTD_t *xHCI_load_new_drvr(uint8_t cc, uint8_t sc, uint8_t prot);

void xHCI_unload_drvr(xHCI_EXTD_t *inst);

#endif