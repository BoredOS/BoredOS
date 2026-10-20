/*
* BoredOS SCSI USB Mass Storage Driver
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


#ifndef USBMS_H
#define USBMS_H
#include "spinlock.h"
#include "xHCI_drvr.h"

#ifndef PACKED
#define PACKED __attribute__((packed))
#endif

#ifndef XHCI_H
typedef struct xHCI_PCI_t xHCI_PCI_t;
#endif

typedef struct {
    uint32_t signature;
    uint32_t tag;
    uint32_t data_transfer_len;
    uint8_t flags;
    uint8_t lun;
    uint8_t cmd_len;
    uint8_t scsi_cmd[16];
} PACKED USB_cbw_t;

typedef struct {
    uint32_t signature;
    uint32_t tag;
    uint32_t data_residue;
    uint8_t status;
} PACKED USB_csw_t;

// Like every other SCSI thing, this is also big endian
typedef struct {
    uint32_t max_lba;
    uint32_t block_len;
} PACKED USB_scsi_cap_t;

typedef enum {
    USB_UNCONFIGURED,
    USB_WAITING_DATA,
    USB_RECEIVED_DATA,
    USB_IDLE,
    USB_WAIT_CSW,
    USB_WAIT_CBW
} USB_states;

typedef struct {
    xHCI_EXTD_t *xHCI_dev;
    xHCI_PCI_t *xHCI_ctrl;
    
    uint8_t slot_id;
    uint8_t bulk_in_ep;
    uint8_t bulk_out_ep;

    USB_cbw_t *current_cbw;
    USB_csw_t *current_csw;
    uint64_t cbw_physical;
    uint64_t csw_physical;

    uint8_t *bounce_virt;
    uint64_t bounce_phys;
    uint64_t current_len;

    spinlock_t transfers_lock;
    volatile USB_states state;
} USB_ms_t;

void usbms_init(void);

#define XUSB_VALID_STATUS   0
#define XUSB_BULK_BUFF_SIZE 13
#define XUSB_TOTAL_BOUNCE   16
#define XUSB_EP_DIR_MASK    0x80
#define XUSB_EP_ATTR_BULK   0x2
#define XUSB_EP_ATTR_MASK   0x3
#define XUSB_SCSI_MASK      0xFF

#define XUSB_CBW_SIG        0x43425355U
#define XUSB_CSW_SIG        0x53425355U
#define XUSB_CBW_SIZE       31
#define XUSB_CSW_SIZE       13
#define XUSB_CBW_IN         0x80
#define XUSB_CBW_OUT        0x0
#define XUSB_CBW_TAG        0xCA11
#define XUSB_MAX_LEN_CBD    0x10
#define XUSB_SCSI_R10       0x28
#define XUSB_SCSI_W10       0x2A
#define XUSB_SCSI_L10       0xA
#define XUSB_SCSI_C10       0x25
#define XUSB_SCSI_SEC       512

#define XUSB_SCSI_IO        0
#define XUSB_SCSI_LBA       2
#define XUSB_SCSI_SECT      7
#define XUSB_SCSI_SIZE      8

#define XUSB_MAX_DEVICES 16

#undef PACKED
#endif