/*
* BoredOS xHCI Host Controller Driver
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

#ifndef XHCI_H
#define XHCI_H
#include "pci.h"
#include "pmm.h"
#include "spinlock.h"
#include "xHCI_def.h"

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

// These basically all need to be packed.
#ifndef PACKED
#define PACKED __attribute__((packed))
#endif

// xHCI Parsed endpoint
typedef struct {
    uint8_t address;
    uint8_t attributes;
    uint16_t max_packet_size;
} xHCI_PE_t;

// Event ring segment table entry
typedef struct {
    uint64_t base_addr;
    uint32_t size;
    uint32_t reserved;
} PACKED xHCI_ERST_t;

// xHCI Capability registers
typedef struct {
    uint8_t cap_length;
    uint8_t  reserved;
    uint16_t hci_version;
    uint32_t hcs_params1;
    uint32_t hcs_params2;
    uint32_t hcs_params3;
    uint32_t hcc_params1;
    uint32_t db_off;
    uint32_t rts_off;
    uint32_t hcc_params2;
} PACKED xHCI_CR_t;

// xHCI Port register set
typedef struct {
    volatile uint32_t portsc;
    volatile uint32_t portpmsc;
    volatile uint32_t portli;
    volatile uint32_t porthlpmc;
} PACKED xHCI_PRS_t;

// xHCI Operational registers
typedef struct {
    volatile uint32_t usb_cmd;
    volatile uint32_t usb_sts;
    volatile uint32_t page_size;
    uint8_t           reserved1[8];
    volatile uint32_t dn_ctrl;
    volatile uint64_t crcr;
    uint8_t           reserved2[16];
    volatile uint64_t dcbaap;
    volatile uint32_t config;
    uint8_t           reserved3[964];
    xHCI_PRS_t        *ports;
} PACKED xHCI_OR_t;

// xHCI Interruptable register set
typedef struct {
    volatile uint32_t iman;
    volatile uint32_t imod;
    volatile uint32_t erst_size;
    uint32_t          reserved;
    volatile uint64_t erst_ba;
    volatile uint64_t erdp;
} PACKED xHCI_IRS_t;

// xHCI Runtime registers.
typedef struct {
    volatile uint32_t microframe_index;
    uint8_t           reserved[28];
    xHCI_IRS_t        irs[1024];
} PACKED xHCI_RR_t;

// xHCI Doorbell registers
typedef struct {
    volatile uint32_t value;
} PACKED xHCI_DR_t;

// xHCI Transfer request block
typedef struct {
    volatile uint64_t param;
    volatile uint32_t status;
    volatile uint32_t control;
} PACKED xHCI_TRB_t;

// xHCI Endpoint context
typedef struct {
    volatile uint32_t ep_info;
    volatile uint32_t ep_info2;
    volatile uint64_t deq_ptr;
    volatile uint32_t tx_info;
    volatile uint32_t reserved[3];
} PACKED xHCI_EC_t;

// xHCI Slot context
typedef struct {
    volatile uint32_t info;
    volatile uint32_t info2;
    volatile uint32_t tt_id;
    volatile uint32_t state;
    volatile uint32_t reserved[4];
} PACKED xHCI_SC_t;

// xHCI Device context
typedef struct {
    xHCI_SC_t slot;
    xHCI_EC_t endpoints[31];
} PACKED xHCI_DC_t;

// xHCI Input control context
typedef struct {
    volatile uint32_t drop_flags;
    volatile uint32_t add_flags;
    volatile uint32_t reserved[5];
    volatile uint32_t config;

    xHCI_SC_t slot;
    xHCI_EC_t endpoints[31];
} PACKED xHCI_ICC_t;

// xHCI USB Setup packet
typedef struct {
    uint8_t  request_type;
    uint8_t  request;
    uint16_t value;
    uint16_t index;
    uint16_t length;
} PACKED xHCI_USP_t;

// xHCI USB Device descriptor
typedef struct {
    uint8_t  length;
    uint8_t  descriptor_type;
    uint16_t bcd_usb;
    uint8_t  device_class;
    uint8_t  device_subclass;
    uint8_t  device_protocol;
    uint8_t  max_packet_size;
    uint16_t id_vendor;
    uint16_t id_product;
    uint16_t bcd_device;
    uint8_t  manufacturer_index;
    uint8_t  product_index;
    uint8_t  serial_index;
    uint8_t  num_configurations;
} PACKED xHCI_UDD_t;

// xHCI USB Endpoint descriptor
typedef struct {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bEndpointAddress;
    uint8_t  bmAttributes;
    uint16_t wMaxPacketSize;
    uint8_t  bInterval;
} PACKED xHCI_UED_t;

// xHCI USB Config descriptor
typedef struct {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint16_t wTotalLength;
    uint8_t  bNumInterfaces;
    uint8_t  bConfigurationValue;
    uint8_t  iConfiguration;
    uint8_t  bmAttributes;
    uint8_t  bMaxPower;
} PACKED xHCI_UCD_t;

// xHCI USB Interface descriptor
typedef struct {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint8_t bInterfaceNumber;
    uint8_t bAlternateSetting;
    uint8_t bNumEndpoints;
    uint8_t bInterfaceClass;
    uint8_t bInterfaceSubClass;
    uint8_t bInterfaceProtocol;
    uint8_t iInterface;
} PACKED xHCI_UID_t;

typedef struct {
    void     *virt;
    uint64_t  phys;
    page_t   *page;
    uint8_t   order;
} xHCI_DMA_t;

// ENUMS -> 

typedef enum {
    // Capability ID USB Legacy
    UCDCAP_CID_UL   = 1,

    // HCC Params XECP shift
    UCDCAP_HPXS     = 16,
    // HCC Params XECP mask
    UCDCAP_HPXM     = 0xFFFF,

    // USB is owned by the BIOS/UEFI
    UCDCAP_LEGBO    = (1 << 16),
    // USB is owned by the OS (us)
    UCDCAP_LEGOO    = (1 << 24),

    // CAP_NEXT_SHIFT
    UCDCAP_CNS      = 8,
    // CAP_NEXT_MASK
    UCDCAP_CNM      = 0xFF,
    // CAP_ID_MASK
    UCDCAP_CIM      = 0xFF
} UCDCAP;

// xHCI speed definitions
typedef enum {
    xSID_FullSpeed = 1,
    xSID_LowSpeed,
    xSID_HighSpeed,
    xSID_SuperSpeed,
    xSID_SuperSpeedPlus,
} xSID;

// xHCI Endpoint types
typedef enum {
    xET_Invalid,
    xET_IsochOut,
    xET_BulkOut,
    xET_IntOut,
    xET_Control,
    xET_IsochIn,
    xET_InterruptIn
} xET;

// xHCI Transfer type
typedef enum {
    xTT_NoDataStage,
    xTT_OutDataStage = 2,
    xTT_InDataStage
} xTT;

// xHCI Transfer request block type
typedef enum {
    xTRBT_Normal        = 1,
    xTRBT_SetupStage    = 2,
    xTRBT_DataStage     = 3,
    xTRBT_StatStage     = 4,
    xTRBT_Link          = 6,
    xTRBT_ComNoOp       = 23
} xTRBT;

typedef enum {
    xSS_STATE_NONE,
    xSS_STATE_ADDRESSING,
    xSS_STATE_GET_DEV_DESC,
    xSS_STATE_GET_CONFIG_DESC_HEADER,
    xSS_STATE_GET_CONFIG_DESC_FULL,
    xSS_STATE_CONFIGURING_EP,
    xSS_STATE_SETTING_CONFIG,
    xSS_STATE_CONFIGURED,
    xSS_STATE_DISABLING,
    xSS_STATE_EVAL_EP0,
    xSS_STATE_GET_DEV_DESC_HEAD
} xSS;

// forward declare the xHCI driver conf:
typedef struct xHCI_EXTD_t xHCI_EXTD_t;

// Main struct (discovered from PCI enumeration)
typedef struct {
    char signature[16]; // xHCI_EXTD_t
    pci_device_t *dev_t;
    uint8_t slot_to_start;
    uint64_t mmio_base_phys;
    uint64_t mmio_base_virt;
    uint32_t ports_resetting;
    uint32_t ports_ignored;

    uint8_t max_slots;
    uint8_t max_ports;
    uint8_t cmd_ring_cycle_state;
    uint64_t cmd_ring_enqueue_ptr;
    uint64_t event_ring_dequeue_ptr;
    uint8_t event_ring_cycle_state;

    int pending_port_setup;
    uint8_t *port_slot_mapping;

    xHCI_CR_t *cap_regs;
    xHCI_OR_t *op_regs;
    xHCI_RR_t *run_regs;
    xHCI_DR_t *db_regs;

    xHCI_ERST_t *erst_virt;
    xHCI_TRB_t *cmd_ring_base;

    uint64_t *dcbaap_virt;
    uint64_t *cmd_ring_virt;
    uint64_t *event_ring_virt;

    // [slot_id][dci]
    uint64_t ***ep_rings;
    uint8_t **ep_cycle_states;
    uint64_t **ep_enqueue_ptrs;
    uint64_t **ep_ring_physs;

    void **descriptor_buffer;
    void *scratchpad;
    void *next_xhci;

    volatile xSS *slot_states;
    uint8_t **config_desc_buffer;
    uint8_t *pending_config_value;
    xHCI_EXTD_t **attached_drvrs;

    spinlock_t xHCI_lock;
    volatile bool early_boot;
} xHCI_PCI_t;

void xHCI_setup(void);
xHCI_PCI_t *xHCI_create(pci_device_t *ref);
void xHCI_initialize(xHCI_PCI_t *this);
void xHCI_send_command(xHCI_PCI_t *this, uint32_t type, uint64_t param, uint32_t slot_id_or_status);
void xHCI_check_ports(xHCI_PCI_t *this);
void xHCI_reset_ports(xHCI_PCI_t *this, int port_idx);
void xHCI_bios_reclaim(xHCI_PCI_t *this);
void xHCI_addr_dev(xHCI_PCI_t *this, uint8_t slotd, int root_port_idx);
void xHCI_cleanup(xHCI_PCI_t *this);

void xHCI_send_req(xHCI_PCI_t *this, uint8_t slot_id, uint8_t req_type, uint8_t req, uint16_t value, uint16_t index, uint16_t length, uint64_t buffer_phys);
void xHCI_conf_ep(xHCI_PCI_t *this, uint8_t slot_id, uint8_t endpoint_address, uint8_t endpoint_type, uint16_t max_packet_size);
void xHCI_queue_transfer(xHCI_PCI_t *this, uint8_t slot_id, uint8_t endpoint_address, uint64_t buffer_phys, uint32_t buffer_size);
void xHCI_conf_iface_ep(xHCI_PCI_t *this, uint8_t slot_id, xHCI_PE_t *endpoints, int ep_count);
void xHCI_worker(void *xpciaddr);

bool xHCI_poll_evring(xHCI_PCI_t *this);
void xHCI_pump_boot_ctrlr(void);
void xHCI_pump_device(xHCI_PCI_t *ref);

inline uint8_t get_port_speed(uint32_t portsc) {
    return (portsc >> XHCI_PORTSC_SPEED_SHIFT) & XHCI_PORTSC_SPEED_MASK;
}

inline void xHCI_reset_controller(xHCI_PCI_t *this) {
    if (this->op_regs->usb_cmd & XHCI_CMD_RUN_STOP) {
        this->op_regs->usb_cmd &= ~XHCI_CMD_RUN_STOP;
        
        while (!(this->op_regs->usb_sts & XHCI_STS_HCHALTED)) {
            __asm__ volatile("pause");
        }
    }

    this->op_regs->usb_cmd |= XHCI_CMD_HCRST;
    while (this->op_regs->usb_cmd & XHCI_CMD_HCRST) {
        __asm__ volatile("pause");
    }

    while (this->op_regs->usb_sts & XHCI_STS_CNR) {
        __asm__ volatile("pause");
    }
}

#undef PACKED
#endif