/*
* BoredOS USB Mass Storage Device
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

#include "disk.h"
#include "kconsole.h"
#include "pmm.h"
#include "slab.h"
#include "spinlock.h"
#include "xHCI.h"
#include "string.h"
#include "usbms.h"
#include "xHCI_drvr.h"

#define LOW_12 0xFFF

inline void *zkpalloc(void) {
    page_t *page = pmm_alloc_page(PMM_ZONE_DMA32);

    if (!page) {
        serial_write("[USBMS] Warning: pmm_alloc_page returned NULL!\n");
        return NULL;
    }

    void *kview = pmm_page_to_vaddr(page);
    if (!kview) {
        return NULL;
    }

    memset(kview, 0, PMM_PAGE_SIZE);
    
    return kview;
}

uint64_t USB_get_phys(void *virt) {
    page_t *page = pmm_vaddr_to_page(virt);
    uint64_t phys_page = pmm_page_to_paddr(page);
    
    uint64_t offset = (uint64_t)virt & LOW_12;
    
    return phys_page + offset;
}

int usb_read_sectors(USB_ms_t *this, uint32_t sector, uint32_t count, uint8_t *buffer) {
    const uint16_t MAX_SECT_PER_CHUNK = XUSB_TOTAL_BOUNCE * PMM_PAGE_SIZE / SECTOR_SIZE;
    uint32_t sectors_left = count;
    uint32_t curr_lba = sector;
    uint32_t total_read = 0;

    while (sectors_left > 0) {
        uint16_t chunk_sectors = (sectors_left > MAX_SECT_PER_CHUNK) ? MAX_SECT_PER_CHUNK : sectors_left;
        uint32_t chunk_bytes = chunk_sectors * XUSB_SCSI_SEC;
        
        uint64_t keys = spinlock_acquire_irqsave(&this->transfers_lock);

        this->current_len = chunk_bytes;

        this->current_cbw->signature = XUSB_CBW_SIG;
        this->current_cbw->tag = curr_lba;
        this->current_cbw->data_transfer_len = chunk_bytes;
        this->current_cbw->flags = XUSB_CBW_IN;
        this->current_cbw->lun = 0;
        this->current_cbw->cmd_len = XUSB_SCSI_L10;

        memset(this->current_cbw->scsi_cmd, 0, XUSB_MAX_LEN_CBD);

        this->current_cbw->scsi_cmd[XUSB_SCSI_IO] = XUSB_SCSI_R10;

        // CPUs typically are little endian. However, SCSI expects big endian, so this
        // inverts it.
        this->current_cbw->scsi_cmd[XUSB_SCSI_LBA] = (curr_lba >> 24) & XUSB_SCSI_MASK;
        this->current_cbw->scsi_cmd[XUSB_SCSI_LBA + 1] = (curr_lba >> 16) & XUSB_SCSI_MASK;
        this->current_cbw->scsi_cmd[XUSB_SCSI_LBA + 2] = (curr_lba >> 8) & XUSB_SCSI_MASK;
        this->current_cbw->scsi_cmd[XUSB_SCSI_LBA + 3] = curr_lba & XUSB_SCSI_MASK;

        // Do the same for sectors; yet again SCSI expects big endian
        this->current_cbw->scsi_cmd[XUSB_SCSI_SECT] = (chunk_sectors >> 8) & XUSB_SCSI_MASK;
        this->current_cbw->scsi_cmd[XUSB_SCSI_SECT + 1] = chunk_sectors & XUSB_SCSI_MASK;

        this->state = USB_WAIT_CBW;

        __asm__ volatile("sfence" ::: "memory");
        __asm__ volatile("mfence" ::: "memory");

        xHCI_queue_transfer(this->xHCI_ctrl, this->slot_id, this->bulk_out_ep, this->cbw_physical, XUSB_CBW_SIZE);

        while (this->state != USB_RECEIVED_DATA) {
            xHCI_poll_evring(this->xHCI_ctrl);
            
            __asm__ volatile("pause" ::: "memory");
        }
        
        this->state = USB_IDLE;

        memcpy(buffer, this->bounce_virt, chunk_bytes);

        buffer += chunk_bytes;
        curr_lba += chunk_sectors;
        sectors_left -= chunk_sectors;
        total_read += chunk_sectors;

        spinlock_release_irqrestore(&this->transfers_lock, keys);
        __asm__ volatile("sfence" ::: "memory");
        __asm__ volatile("mfence" ::: "memory");
    }

    if (__builtin_return_address(0) == 0) {
        serial_write("[USBMS] Returning to 0?\n");
    }

    return total_read;
}

int usb_write_sectors(USB_ms_t *this, uint64_t sector, uint32_t count, const uint8_t *buffer) {
    uint32_t sectors_left = count;
    uint32_t current_lba = sector;
    uint32_t total_sectors_written = 0;

    const uint16_t MAX_SECT_PER_CHUNK = XUSB_TOTAL_BOUNCE * PMM_PAGE_SIZE / SECTOR_SIZE;

    while (sectors_left > 0) {
        uint16_t chunk_sectors = (sectors_left > MAX_SECT_PER_CHUNK) ? MAX_SECT_PER_CHUNK : sectors_left;
        uint32_t chunk_bytes = chunk_sectors * SECTOR_SIZE;

        uint64_t keys = spinlock_acquire_irqsave(&this->transfers_lock);

        memcpy(this->bounce_virt, buffer, chunk_bytes);
        this->current_len = chunk_bytes;
        this->current_cbw->signature = XUSB_CBW_SIG;
        this->current_cbw->tag = current_lba;
        this->current_cbw->data_transfer_len = chunk_bytes;
        this->current_cbw->lun = 0;
        this->current_cbw->cmd_len = XUSB_SCSI_L10;
        this->current_cbw->flags = 0;
        
        memset(this->current_cbw->scsi_cmd, 0, XUSB_MAX_LEN_CBD);
        this->current_cbw->scsi_cmd[0] = XUSB_SCSI_W10;

        this->current_cbw->scsi_cmd[XUSB_SCSI_LBA] = (current_lba >> 24) & XUSB_SCSI_MASK;
        this->current_cbw->scsi_cmd[XUSB_SCSI_LBA + 1] = (current_lba >> 16) & XUSB_SCSI_MASK;
        this->current_cbw->scsi_cmd[XUSB_SCSI_LBA + 2] = (current_lba >> 8) & XUSB_SCSI_MASK;
        this->current_cbw->scsi_cmd[XUSB_SCSI_LBA + 3] = current_lba & XUSB_SCSI_MASK;

        this->current_cbw->scsi_cmd[XUSB_SCSI_SECT] = (chunk_sectors >> 8) & XUSB_SCSI_MASK;
        this->current_cbw->scsi_cmd[XUSB_SCSI_SECT + 1] = chunk_sectors & XUSB_SCSI_MASK;

        this->state = USB_WAIT_CBW;

        __asm__ volatile("sfence" ::: "memory");
        __asm__ volatile("mfence" ::: "memory");

        xHCI_queue_transfer(this->xHCI_ctrl, this->slot_id, this->bulk_out_ep, this->cbw_physical, XUSB_CBW_SIZE);

        while (this->state != USB_RECEIVED_DATA) {
            xHCI_poll_evring(this->xHCI_ctrl);
            __asm__ volatile("pause" ::: "memory");
        }

        this->state = USB_IDLE;

        buffer += chunk_bytes;
        sectors_left -= chunk_sectors;
        current_lba += chunk_sectors;
        total_sectors_written += chunk_sectors;

        spinlock_release_irqrestore(&this->transfers_lock, keys);
        __asm__ volatile("sfence" ::: "memory");
        __asm__ volatile("mfence" ::: "memory");
    }

    return total_sectors_written;
}

static int usb_disk_read_sector(Disk *disk, uint32_t sector, uint8_t *buffer) {
    USB_ms_t *this_device = disk->driver_data;

    if (disk->is_partition && disk->parent) {
        USB_ms_t *parent_data = (USB_ms_t *)disk->parent->driver_data;
        
        return usb_read_sectors(parent_data, (uint64_t)sector + disk->partition_lba_offset, 1, buffer) == 0;
    }

    return usb_read_sectors(this_device, (uint64_t)sector, 1, buffer) == 0;
}

static int usb_disk_write_sector(Disk *disk, uint32_t sector, const uint8_t *buffer) {
    USB_ms_t *this_device = disk->driver_data;

    if (disk->is_partition && disk->parent) {
        USB_ms_t *parent_data = (USB_ms_t *)disk->parent->driver_data;
        
        return usb_write_sectors(parent_data, (uint64_t)sector + disk->partition_lba_offset, 1, buffer) == 0;
    }

    return usb_write_sectors(this_device, (uint64_t)sector, 1, buffer) == 0;
}

static int usb_disk_read_sectors(Disk *disk, uint32_t sector, uint32_t count, uint8_t *buf) {
    USB_ms_t *dev = disk->driver_data;
    uint64_t lba = sector;
    if (disk->is_partition && disk->parent) {
        dev = disk->parent->driver_data;
        lba += disk->partition_lba_offset;
    }
    return usb_read_sectors(dev, lba, count, buf) == (int)count ? 0 : -1;
}

static int usb_disk_write_sectors(Disk *disk, uint32_t sector, uint32_t count, const uint8_t *buffer) {
    USB_ms_t *this_device = disk->driver_data;

    if (disk->is_partition && disk->parent) {
        USB_ms_t *parent_data = (USB_ms_t *)disk->parent->driver_data;
        
        return usb_write_sectors(parent_data, (uint64_t)sector + disk->partition_lba_offset, count, buffer) == (int)count ? 0 : -1;;
    }

    return usb_write_sectors(this_device, (uint64_t)sector, count, buffer) == (int)count ? 0 : -1;
}

static int usb_disk_sync(Disk *disk) {
    (void)disk;
    serial_write("[USBMS] Disk sync called (unimplemented!)\n");
    return 0;
}

uint64_t usb_aquire_size(USB_ms_t *this) {
    serial_write("[USBMS] Acquiring USB size\n");
    serial_write("[USBMS] My xHCI controller is 0x");
    serial_write_hex((uint64_t)this->xHCI_ctrl);
    serial_write("\n");
    serial_write("[USBMS] Early boot is: ");
    serial_write((this->xHCI_ctrl->early_boot) ? "true\n" : "false\n");

    uint64_t keys = spinlock_acquire_irqsave(&this->transfers_lock);

    this->current_len = XUSB_SCSI_C10;  
    this->current_cbw->signature = XUSB_CBW_SIG;
    this->current_cbw->tag = XUSB_CBW_TAG;
    this->current_cbw->data_transfer_len = XUSB_SCSI_SIZE;
    this->current_cbw->lun = 0;
    this->current_cbw->cmd_len = 10;
    this->current_cbw->flags = XUSB_CBW_IN;

    memset(this->current_cbw->scsi_cmd, 0, XUSB_MAX_LEN_CBD);
    this->current_cbw->scsi_cmd[0] = XUSB_SCSI_C10;
    this->state = USB_WAIT_CBW;

    __asm__ volatile("sfence" ::: "memory");
    __asm__ volatile("mfence" ::: "memory");

    xHCI_queue_transfer(this->xHCI_ctrl, this->slot_id, this->bulk_out_ep, this->cbw_physical, XUSB_CBW_SIZE);

    while (this->state != USB_RECEIVED_DATA) {
        xHCI_poll_evring(this->xHCI_ctrl);
        __asm__ volatile("pause" ::: "memory");
    }

    this->state = USB_IDLE;

    USB_scsi_cap_t *contents = (USB_scsi_cap_t *)(this->bounce_virt);

    uint32_t max_lba = __builtin_bswap32(contents->max_lba);
    uint32_t block_len = __builtin_bswap32(contents->block_len);

    spinlock_release_irqrestore(&this->transfers_lock, keys);

    uint64_t total_sectors = max_lba + 1;

    serial_write("[USBMS] Capacity: ");
    serial_write_hex(total_sectors);
    serial_write(" sectors, block size: ");
    serial_write_hex(block_len);
    serial_write("\n");

    return total_sectors;
}

void usb_start(xHCI_EXTD_t *this) {
    USB_ms_t *this_usb = this->local_data;

    if (this_usb->state != USB_UNCONFIGURED) return;
    this_usb->state = USB_IDLE;

    Disk *this_disk = kzalloc(sizeof(Disk));
    this_disk->driver_data = this_usb;
    this_disk->type = DISK_TYPE_USB;
    this_disk->devname[0] = 0;
    
    this_disk->read_sector = usb_disk_read_sector;
    this_disk->write_sector = usb_disk_write_sector;
    this_disk->read_sectors = usb_disk_read_sectors;
    this_disk->write_sectors = usb_disk_write_sectors;
    this_disk->sync = usb_disk_sync;
    this_disk->partition_lba_offset = 0;
    this_disk->parent = NULL;
    this_disk->is_partition = false;
    this_disk->is_fat32 = false;
    this_disk->total_sectors = usb_aquire_size(this_usb);

    disk_register(this_disk);
    serial_write("[USBMS] Probing partitions on /dev/");
    serial_write(this_disk->devname);
    serial_write("\n");
    disk_rescan(this_disk);

    return;
}

void usb_initialize(xHCI_EXTD_t *this, xHCI_PCI_t *host, uint8_t _slot, void *endpoints, int ep_count) {
    USB_ms_t *this_usb = kzalloc(sizeof(USB_ms_t));
    this->local_data = this_usb;

    this_usb->xHCI_ctrl = host;
    this_usb->slot_id = _slot;

    xHCI_PE_t *parsed_endpoints = (xHCI_PE_t *)(endpoints);

    for (int i = 0; i < ep_count; ++i) {
        uint8_t ep_addr = parsed_endpoints[i].address;
        bool is_in = (ep_addr & XUSB_EP_DIR_MASK);

        if ((parsed_endpoints[i].attributes & XUSB_EP_ATTR_MASK) != 2)
            continue;

        if (is_in)
            this_usb->bulk_in_ep = ep_addr;
        else
            this_usb->bulk_out_ep = ep_addr;
    }

    this_usb->current_cbw = zkpalloc();
    this_usb->current_csw = zkpalloc();

    this_usb->cbw_physical = USB_get_phys(this_usb->current_cbw);
    this_usb->csw_physical = USB_get_phys(this_usb->current_csw);

    page_t *target = pmm_alloc_pages(XUSB_TOTAL_BOUNCE, PMM_ZONE_DMA32);
    this_usb->bounce_virt = pmm_page_to_vaddr(target); // contiguous physical, so hhdm contig too
    this_usb->bounce_phys = pmm_page_to_paddr(target);
    this_usb->state = USB_UNCONFIGURED;
    this_usb->transfers_lock = SPINLOCK_INIT;

    serial_write("[USBMS] Finished initialization\n");

    return;
}

void usb_on_int(xHCI_EXTD_t *this, uint32_t bytes_transferred, uint32_t endpoint_id, uint64_t param_event) {
    (void)bytes_transferred;
    (void)param_event;

    USB_ms_t *this_dev = this->local_data;

    switch (this_dev->state) {
    case USB_WAITING_DATA:
        if (endpoint_id != this_dev->bulk_in_ep && endpoint_id != this_dev->bulk_out_ep)
            break;

        this_dev->state = USB_WAIT_CSW;
        xHCI_queue_transfer(this_dev->xHCI_ctrl, this_dev->slot_id, this_dev->bulk_in_ep, this_dev->csw_physical, XUSB_BULK_BUFF_SIZE);
        break;
    case USB_WAIT_CSW: {
        if (endpoint_id != this_dev->bulk_in_ep)
            break;

        if (this_dev->current_csw->signature != XUSB_CSW_SIG || this_dev->current_csw->status != XUSB_VALID_STATUS) {
            serial_write("[USBMS] Warning: SCSI command failed\n");
        }

        this_dev->state = USB_RECEIVED_DATA;
        break;
    }
    case USB_WAIT_CBW: {
        if (endpoint_id != this_dev->bulk_out_ep) {
            break;
        }

        if (this_dev->current_cbw->data_transfer_len > 0) {
            uint8_t data_ep = (this_dev->current_cbw->flags & XUSB_EP_DIR_MASK) ? this_dev->bulk_in_ep : this_dev->bulk_out_ep;

            this_dev->state = USB_WAITING_DATA;
            xHCI_queue_transfer(this_dev->xHCI_ctrl, this_dev->slot_id, data_ep, this_dev->bounce_phys, this_dev->current_len);
        }
        else {
            this_dev->state = USB_WAIT_CSW;
            xHCI_queue_transfer(this_dev->xHCI_ctrl, this_dev->slot_id, this_dev->bulk_in_ep, this_dev->csw_physical, XUSB_BULK_BUFF_SIZE);
        }
        break;
    }
    default:
        serial_write("[USBMS] Unknown switch during interrupt! (");
        serial_write_hex(this_dev->state);
        serial_write(")\n");
        break;
    }


    __asm__ volatile("sfence" ::: "memory");
    __asm__ volatile("mfence" ::: "memory");
}

void usb_destroy(xHCI_EXTD_t *this) {
    // On unplug event/destructor.
    (void)this;
}

void usbms_init(void) {
    serial_write("[USBMS] Polling xHCI for a mass storage driver\n");

    xHCI_EXTD_t *xHCI_dev = kzalloc(sizeof(xHCI_EXTD_t));
    xHCI_dev->initialize = usb_initialize;
    xHCI_dev->destroy = usb_destroy;
    xHCI_dev->on_int = usb_on_int;
    xHCI_dev->start = usb_start;

    xHCI_CCSCPR_t classes = {
        .cc = XHCI_CL_USB_STOR,
        .prot = 0, // any, mass storage only matches CC
        .sc = 0, // any
        .match = 0 // This match flag actually specifies any
    };

    xHCI_submit_new_drvr(classes, xHCI_dev);

    xHCI_pump_boot_ctrlr();

    serial_write("[USBMS] Finished boot init\n");
}