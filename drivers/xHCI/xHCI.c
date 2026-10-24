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


#include "xHCI.h"
#include "lapic.h"
#include "xHCI_drvr.h"
#include "kconsole.h"
#include "mmu.h"
#include "pci.h"
#include "process.h"
#include "slab.h"
#include "spinlock.h"
#include "vmm.h"
#include "string.h"
#include "xHCI_def.h"
#include "pmm.h"
#include <stdint.h>

#define PAGE_SIZE 4096UL
#define LOW_12 0xFFF
#define BOOT_PUMPS 500000
#define MAKE_WVALUE(type, index) (((uint16_t)(type) << 8) | (uint8_t)(index))
#define XHCI_PAUSE __asm__ volatile("pause");
#define XHCI_FENCE __asm__ volatile("sfence" ::: "memory"); \
    __asm__ volatile("mfence" ::: "memory");
#define LOG32(label, v) do { serial_write(label); serial_write_hex((uint64_t)(v)); serial_write("\n"); } while (0)

static xHCI_PCI_t *xhci_devices = NULL;
static spinlock_t xhci_list_lock = SPINLOCK_INIT;

void xHCI_setup(void) {
    pci_device_t *all = kmalloc(sizeof(pci_device_t) * 32);
    if (!all) return;

    int n = pci_enumerate_devices(all, 32);
    for (int i = 0; i < n; ++i) {
        serial_write("[xHCI] Checking: ");
        serial_write_hex(all[i].class_code); serial_write(":");
        serial_write_hex(all[i].subclass); serial_write(".");
        serial_write_hex(all[i].prog_if); serial_write("\n");
        if (all[i].class_code != 0xC || all[i].subclass != 0x3 || all[i].prog_if != 0x30)
            continue;

        pci_device_t *dev = kmalloc(sizeof(pci_device_t));
        if (!dev) break;
        memcpy(dev, &all[i], sizeof(pci_device_t));

        serial_write("[xHCI] Binding to: ");
        serial_write_num(dev->bus); serial_write(":");
        serial_write_num(dev->device); serial_write(".");
        serial_write_num(dev->function); serial_write("\n");

        xHCI_PCI_t *c = xHCI_create(dev);
        xHCI_initialize(c);
    }

    kfree(all);
    if (!xhci_devices)
        serial_write("[xHCI] No xHCI devices!\n");

    return;
}

void xHCI_pump_boot_ctrlr(void) {
    if (!xhci_devices) {
        serial_write("[xHCI] Boot pump failed (this device has no xHCI support)\n");
        return;
    }

    for (size_t i = 0; i < BOOT_PUMPS; ++i) {
        xHCI_PCI_t *next = xhci_devices;
        do {
            xHCI_poll_evring(next);
            next = next->next_xhci;
        } while (next);

        XHCI_PAUSE
    }
    
    return;
}

void xHCI_pump_device(xHCI_PCI_t *ref) {
    if (!ref) {
        serial_write("[xHCI] Warn: no reference to pump!");
        return;
    }

    for (size_t i = 0; i < BOOT_PUMPS; ++i) {
        xHCI_poll_evring(ref);
        XHCI_PAUSE
    }

    return;
}

xHCI_PCI_t *xHCI_create(pci_device_t *device) {
    xHCI_PCI_t *tmp = kzalloc(sizeof(xHCI_PCI_t));

    tmp->pending_port_setup = XHCI_NO_PENDING_PORT;
    tmp->dev_t = device;
    memcpy(tmp->signature, "xHCI_PCI_t", 11);

    uint64_t lkeys = spinlock_acquire_irqsave(&xhci_list_lock);

    if (!xhci_devices) xhci_devices = tmp;
    else {
        xHCI_PCI_t *startxh = xhci_devices;
        while (startxh->next_xhci) {
            startxh = startxh->next_xhci;
        }

        startxh->next_xhci = tmp;
    }

    spinlock_release_irqrestore(&xhci_list_lock, lkeys);
    
    XHCI_FENCE
    
    return tmp;
}

uint64_t xHCI_get_phys(void *virt) {
    uintptr_t v = (uintptr_t)virt;
    uintptr_t page_base = v & ~(uintptr_t)LOW_12;

    uintptr_t phys_page = mmu_virt_to_phys(mmu_get_kernel_context(), page_base);
    if (!phys_page) {
        serial_write("[xHCI] Error: No mapping for vaddr ");
        serial_write_hex(v);
        serial_write("\n");
        return 0;
    }

    return (uint64_t)phys_page + (v & LOW_12);
}

static const char *xHCI_comp_code_str(uint32_t code) {
    switch (code) {
    case 2:  return "DATA_BUFFER_ERROR";
    case 3:  return "BABBLE_DETECTED";
    case 4:  return "USB_TRANSACTION_ERROR";
    case 5:  return "TRB_ERROR";
    case 6:  return "STALL_ERROR";
    case 7:  return "RESOURCE_ERROR";
    case 14: return "RING_UNDERRUN";
    case 15: return "RING_OVERRUN";
    case 17: return "PARAMETER_ERROR";
    case 19: return "CONTEXT_STATE_ERROR";
    case 26: return "STOPPED";
    default: return "OTHER";
    }
}

static xHCI_TRB_t *xHCI_phys_to_virt(uint64_t phys) {
    void *page = pmm_page_to_vaddr(pmm_paddr_to_page(phys & ~(uint64_t)LOW_12));
    if (!page) return NULL;
    return (xHCI_TRB_t *)((uint8_t *)page + (phys & LOW_12));
}

static void xHCI_dump_trb(const char *tag, xHCI_TRB_t *trb) {
    serial_write("[xHCI] "); 
    serial_write(tag);
    serial_write(": type="); 
    serial_write_num((trb->control >> XHCI_TRB_TYPE_SHIFT) & XHCI_TRB_TYPE_MASK);
    serial_write(" param="); 
    serial_write_hex(trb->param);    
    serial_write(" status="); 
    serial_write_hex(trb->status);
    serial_write(" ctrl="); 
    serial_write_hex(trb->control);
    serial_write("\n");
}

static void xHCI_dump_halt(xHCI_PCI_t *this, xHCI_TRB_t *event, uint32_t slot, uint32_t dci) {
    xHCI_dump_trb("Event", event);

    xHCI_TRB_t *failed = xHCI_phys_to_virt(event->param);
    if (failed) {
        xHCI_dump_trb("Failed TRB", failed);

        uint64_t ring_phys = this->ep_ring_physs[slot][dci];
        if (event->param < ring_phys || event->param >= ring_phys + PAGE_SIZE) {
            serial_write("[xHCI] Failed TRB is outside this endpoint's ring!\n");
        }
        else {
            serial_write("[xHCI] Failed TRB ring index: ");
            serial_write_num((event->param - ring_phys) / sizeof(xHCI_TRB_t));
            serial_write("\n");
        }
    }

    serial_write("[xHCI] Ring enq="); 
    serial_write_num(this->ep_enqueue_ptrs[slot][dci]);
    serial_write(" cycle="); 
    serial_write_num(this->ep_cycle_states[slot][dci]);
    serial_write("\n");

    uint64_t out_phys = this->dcbaap_virt[slot];
    if (!out_phys) return;

    uint8_t *out = (uint8_t *)xHCI_phys_to_virt(out_phys);
    if (!out) return;

    uint32_t csz = (this->cap_regs->hcc_params1 & (1U << 2)) ? 64 : 32;
    volatile uint32_t *ep = (volatile uint32_t *)(out + csz * dci);
    uint64_t deq = ((uint64_t)ep[3] << 32) | ep[2];

    // EP state: 0 disabled, 1 running, 2 halted, 3 stopped, 4 error
    serial_write("[xHCI] EP ctx: state="); serial_write_num(ep[0] & 0x7);
    serial_write(" deq="); serial_write_hex(deq & ~0xFULL);
    serial_write(" dcs="); serial_write_num(deq & 1);
    serial_write("\n");
}

/*
    According to xHCI spec 4.22.1:

    A system configuration may contain support in the BIOS (Pre-OS software)
    for control of the xHC. The OS Handoff synchronization capability provides the mechanisms
    to allow a BIOS to enable SMI support for xHC events, and also a set of registers are used
    to implement a semaphore to synchronize ownership changes of the xHC.

    Funnily enough Intel found it necessary to add "The hand-off mechanism should
    be clean and precise and each participant shall adhere to the protocol defined
    below"... I thought that was funny.
*/
void xHCI_bios_reclaim(xHCI_PCI_t *this) {
    const uint32_t hcc_params1 = this->cap_regs->hcc_params1;
    uint32_t xECP = (hcc_params1 >> UCDCAP_HPXS) & UCDCAP_HPXM;

    if (!xECP) return; // no need for a handoff (the registers don't exist/Pre OS software has no support)

    uint32_t cap_off = xECP * 4;

    while (cap_off != 0) {
        volatile uint32_t *cap_reg = (volatile uint32_t *)(this->mmio_base_virt + cap_off);
        uint32_t val = *cap_reg;
        uint8_t cap_id = val & UCDCAP_CIM;

        if (cap_id == UCDCAP_CID_UL) {
            if (!(val & UCDCAP_LEGBO))
                goto handoff_next; // next cap reg

            *cap_reg = val | UCDCAP_LEGOO; // Set register is owned by the OS

            size_t timeout = 10000; // random high value.
            while ((*cap_reg & UCDCAP_LEGBO) && timeout > 0) {
                --timeout;
                XHCI_PAUSE
            }

            if (*cap_reg & UCDCAP_LEGBO) {
                serial_write("[xHCI] Warning: BIOS/UEFI refused to handoff a cap register!");
            }
        }

        handoff_next: ;
        uint8_t next = (val >> UCDCAP_CNS) & UCDCAP_CNM;
        if (next == 0) break;
        cap_off += next * 4;
    }
}

void xHCI_wrap_trb(xHCI_PCI_t *this, xHCI_TRB_t *ring_base, uint64_t *enqueue, uint8_t slot_id, bool cycle) {
    xHCI_TRB_t *link = &ring_base[XHCI_CMD_RING_INDEX_LIMIT];
    link->param = this->ep_ring_physs[slot_id][XHCI_EP_INDEX_0];
    link->status = 0;

    link->control = (TRB_TYPE_LINK << XHCI_TRB_TYPE_SHIFT) |
                    XHCI_TRB_LINK_TC |
                    ((cycle ? 1U : 0U) << XHCI_TRB_CYCLE_SHIFT);
    
    XHCI_FENCE
    *enqueue = 0;
    cycle = !cycle;
}

inline void *zkpalloc(void) {
    page_t *page = pmm_alloc_page(PMM_ZONE_DMA32); // Intel xHCI controllers are fine with > 4GB physical ram, but AMD controllers aren't so friendly. (Supposedly)
    
    if (!page) {
        serial_write("[xHCI] Warning: pmm_alloc_page returned NULL!\n");
        return NULL;
    }

    void *kview = pmm_page_to_vaddr(page);
    if (!kview) {
        return NULL;
    }

    memset(kview, 0, PMM_PAGE_SIZE);
    
    return kview;
}

static void xHCI_free_kpage(void *kview) {
    if (!kview) return;
    pmm_free_page(pmm_vaddr_to_page(kview));
}

static void xHCI_req_dev_desc(xHCI_PCI_t *this, uint8_t slot, uint16_t len, uint32_t next_state) {
    uint64_t phys = xHCI_get_phys(this->descriptor_buffer[slot]);
    this->slot_states[slot] = next_state;

    uint8_t req_type = USB_REQUEST_DIR_IN | USB_XHCI_REQ_TYPE_STANDARD | USB_XHCI_REQ_REC_DEV;
    xHCI_send_req(this, slot, req_type, USB_XHCI_REQ_GET_DESC,
                  MAKE_WVALUE(USB_XHCI_DESC_TYPE_DEV, 0), 0, len, phys);
}

static void xHCI_eval_ep0_mps(xHCI_PCI_t *this, uint8_t slot, uint16_t mps) {
    if (!this->scratchpad) this->scratchpad = zkpalloc();
    xHCI_ICC_t *ictx = (xHCI_ICC_t *)this->scratchpad;
    memset(ictx, 0, PAGE_SIZE);

    ictx->add_flags = XHCI_INPUT_ADD_EP0_CONTEXT;
    ictx->endpoints[XHCI_CTX_EP0_INDEX].ep_info2 = ((uint32_t)mps << XHCI_EP_MAX_PACKET_SHIFT);

    this->slot_states[slot] = xSS_STATE_EVAL_EP0;
    xHCI_send_command(this, TRB_TYPE_EVALUATE_CONTEXT, xHCI_get_phys(ictx), slot);
}

// Fun fact, my intial implementation would assume a slot speed, which did work...
// most of the time. Unfortunately, on full speed or USB 2.0 devices, this doesn't work
// (sometimes) and will cause a BABBLE_DETECTED error on the xHC. Annoying!
static uint8_t xHCI_slot_speed(xHCI_PCI_t *this, uint8_t slot) {
    for (size_t p = 0; p < this->max_ports; ++p) {
        if (this->port_slot_mapping[p] != slot) continue;
        volatile uint32_t *portsc = (volatile uint32_t *)((uintptr_t)this->op_regs
            + XHCI_PORT_REG_SET_OFFSET + p * XHCI_PORT_REG_STRIDE);
        return get_port_speed(*portsc);
    }
    return 0;
}

// Address a new device for the xHCI controller. I've made memory allocation failures
// just destroy the controller, since its kind of hard to recover the device (too much work, basically)
// and its only gonna happen if the kernel refuses to give the controller memory, for whatever reason?
void xHCI_addr_dev(xHCI_PCI_t *this, uint8_t slotd, int root_port_idx) {
    this->pending_port_setup = XHCI_NO_PENDING_PORT;

    xHCI_DC_t *output_ctx = zkpalloc();

    serial_write("[xHCI] Addressing new device\n");

    if (!output_ctx) {
        serial_write("[xHCI] Error: Failed to allocate output_ctx\n");
        xHCI_cleanup(this);
        return;
    }

    this->dcbaap_virt[slotd] = xHCI_get_phys(output_ctx);
    
    xHCI_ICC_t *input_ctx = zkpalloc();

    if (!input_ctx) {
        serial_write("[xHCI] Error: Failed to allocate input_ctx\n");
        xHCI_cleanup(this);
        return;
    }

    uint64_t ictx_phys = xHCI_get_phys(input_ctx);

    uint64_t opr_base = (uint64_t)this->op_regs;
    uint64_t port_base = opr_base + XHCI_PORT_REG_SET_OFFSET + (root_port_idx * XHCI_PORT_REG_STRIDE);

    volatile uint32_t *portsc_ptr = (volatile uint32_t *)port_base;
    
    uint32_t portsc = *portsc_ptr;
    uint8_t speed_id = get_port_speed(portsc);

    uint16_t max_packet_size = 0;
    switch (speed_id) {
        case xSID_SuperSpeed:
        case xSID_SuperSpeedPlus:
            max_packet_size = USB_EP0_MAX_PACKET_SUPERSPEED;
            break;
        case xSID_HighSpeed:
            max_packet_size = USB_EP0_MAX_PACKET_HIGHSPEED;
            break;
        default:
            max_packet_size = USB_EP0_MAX_PACKET_DEFAULT;
            break;
    }

    serial_write("[xHCI] Identified port speed: ");
    serial_write_num(speed_id);
    serial_write("\n\t -> MPS: ");
    serial_write_num(max_packet_size);
    serial_write("\n");

    input_ctx->add_flags = XHCI_INPUT_ADD_SLOT_CONTEXT | XHCI_INPUT_ADD_EP0_CONTEXT;
    uint32_t cont_entr = 1;
    input_ctx->slot.info = 
        (cont_entr << XHCI_SLOT_CONTEXT_ENTRIES_SHIFT) |
        (speed_id << XHCI_SLOT_SPEED_SHIFT);

    uint32_t xhci_port_num = root_port_idx + 1;
    input_ctx->slot.info2 = (xhci_port_num << XHCI_SLOT_ROOT_PORT_SHIFT);

    uint64_t *transfer_ring = zkpalloc();
    uint64_t tr_phys = xHCI_get_phys(transfer_ring);

    size_t max_trbs = PAGE_SIZE / sizeof(xHCI_TRB_t);
    xHCI_TRB_t *link_trb = (xHCI_TRB_t *)(&transfer_ring[(max_trbs - 1) * (sizeof(xHCI_TRB_t) / 8)]);
    link_trb->param = tr_phys;
    link_trb->control = (TRB_TYPE_LINK << XHCI_TRB_TYPE_SHIFT) | XHCI_TRB_LINK_TC | (1U << XHCI_TRB_CYCLE_SHIFT);

    this->ep_rings[slotd][XHCI_EP_INDEX_0] = transfer_ring;
    this->ep_cycle_states[slotd][XHCI_EP_INDEX_0] = 1;
    this->ep_enqueue_ptrs[slotd][XHCI_EP_INDEX_0] = 0;
    this->ep_ring_physs[slotd][XHCI_EP_INDEX_0] = tr_phys;

    input_ctx->endpoints[XHCI_CTX_EP0_INDEX].ep_info2 = 
        (XHCI_EP_MAX_ERROR_COUNT << XHCI_EP_CERR_SHIFT) |
        (xET_Control << XHCI_TRB_TYPE_SHIFT) |
        (max_packet_size << XHCI_EP_MAX_PACKET_SHIFT);

    input_ctx->endpoints[XHCI_CTX_EP0_INDEX].deq_ptr = tr_phys | XHCI_DEQ_CYCLE_STATE_START;

    xHCI_send_command(this, TRB_TYPE_ADDRESS_DEVICE, ictx_phys, slotd);

    return;
}

void xHCI_send_req(xHCI_PCI_t *this, uint8_t slot_id, uint8_t req_type, uint8_t req, uint16_t value, 
    uint16_t index, uint16_t length, uint64_t buffer_phys) {
    
    xHCI_USP_t setup = {
        .request_type = req_type,
        .request = req,
        .value = value,
        .index = index,
        .length = length
    };

    uint64_t setup_val_raw;
    memcpy(&setup_val_raw, &setup, sizeof(uint64_t));

    uint32_t trt = xTT_NoDataStage;

    if (length > 0) {
        bool is_input = (req_type & USB_REQUEST_DIR_IN);
        trt = is_input ? xTT_InDataStage : xTT_OutDataStage;
    }

    xHCI_TRB_t *ring_base = (xHCI_TRB_t *)this->ep_rings[slot_id][XHCI_EP_INDEX_0];
    uint64_t enqueue = this->ep_enqueue_ptrs[slot_id][XHCI_EP_INDEX_0];
    uint8_t cycle = this->ep_cycle_states[slot_id][XHCI_EP_INDEX_0];

    xHCI_TRB_t *setup_trb = &ring_base[enqueue];
    setup_trb->param = setup_val_raw;
    setup_trb->status = USB_SETUP_PACKET_SIZE;

    setup_trb->control = 
        (xTRBT_SetupStage << XHCI_TRB_TYPE_SHIFT) |
        (trt << XHCI_TRB_TRT_SHIFT) |
        (1U << XHCI_TRB_IDT_SHIFT) |
        ((cycle ? 1U : 0U) << XHCI_TRB_CYCLE_SHIFT);

    ++enqueue;

    if (enqueue >= XHCI_CMD_RING_INDEX_LIMIT) {
        xHCI_wrap_trb(this, ring_base, &enqueue, slot_id, cycle);
    }

    if (length > 0) {
        xHCI_TRB_t *data_trb = &ring_base[enqueue];

        data_trb->param = buffer_phys;
        data_trb->status = length;

        bool is_input = (req_type & USB_REQUEST_DIR_IN);
        uint32_t dir_flag = is_input ? XHCI_TRB_DATA_DIR_IN : XHCI_TRB_DATA_DIR_OUT;

        data_trb->control = 
            (xTRBT_DataStage << XHCI_TRB_TYPE_SHIFT) |
            (dir_flag << XHCI_TRB_DIR_SHIFT) |
            ((cycle ? 1U : 0U) << XHCI_TRB_CYCLE_SHIFT);
        
        ++enqueue;
    }

    if (enqueue >= XHCI_CMD_RING_INDEX_LIMIT) {
        xHCI_wrap_trb(this, ring_base, &enqueue, slot_id, cycle);
    }

    uint32_t status_dir = XHCI_TRB_STATUS_DIR_OUT;
    if (length == 0 || (req_type & USB_REQUEST_DIR_IN) == 0) {
        status_dir = XHCI_TRB_STATUS_DIR_IN;
    }

    xHCI_TRB_t *status_trb = &ring_base[enqueue];
    status_trb->param = 0;
    status_trb->status = 0;

    status_trb->control = 
        (xTRBT_StatStage << XHCI_TRB_TYPE_SHIFT) |
        (status_dir << XHCI_TRB_STATUS_DIR_SHIFT) |
        (1U << XHCI_TRB_IOC_SHIFT) |
        ((cycle ? 1U : 0U) << XHCI_TRB_CYCLE_SHIFT);

    ++enqueue;

    if (enqueue >= XHCI_CMD_RING_INDEX_LIMIT) {
        xHCI_wrap_trb(this, ring_base, &enqueue, slot_id, cycle);
    }

    XHCI_FENCE

    this->ep_enqueue_ptrs[slot_id][XHCI_EP_INDEX_0] = enqueue;
    this->ep_cycle_states[slot_id][XHCI_EP_INDEX_0] = cycle;
    
    XHCI_FENCE

    this->db_regs[slot_id].value = XHCI_DB_TARGET_EP0;

    return;
}

void xHCI_cleanup(xHCI_PCI_t *this) {
    if (this->slot_states) {
        kfree((void *)this->slot_states);
    }

    if (this->attached_drvrs) {
        kfree(this->attached_drvrs);
    }

    if (this->ep_rings) {
        for (size_t i = 0; i < USB_TOTAL_xHCI_SLOTS; ++i) {
            if (this->ep_rings[i])
                kfree(this->ep_rings[i]);
        }

        kfree(this->ep_rings);
    }

    if (this->ep_cycle_states) {
        for (size_t i = 0; i < USB_TOTAL_xHCI_SLOTS; ++i) {
            if (this->ep_cycle_states[i])
                kfree(this->ep_cycle_states[i]);
        }

        kfree(this->ep_cycle_states);
    }

    if (this->ep_enqueue_ptrs) {
        for (size_t i = 0; i < USB_TOTAL_xHCI_SLOTS; ++i) {
            if (this->ep_enqueue_ptrs[i])
                kfree(this->ep_enqueue_ptrs[i]);
        }

        kfree(this->ep_enqueue_ptrs);
    }
    
    if (this->ep_rings) {
        for (size_t i = 0; i < USB_TOTAL_xHCI_SLOTS; ++i) {
            if (this->ep_rings[i])
                kfree(this->ep_rings[i]);
        }

        kfree(this->ep_rings);
    }

    if (this->pending_config_value) {
        kfree(this->pending_config_value);
    }

    if (this->descriptor_buffer) {
        kfree(this->descriptor_buffer);
    }

    if (this->config_desc_buffer) {
        kfree(this->config_desc_buffer);
    }

    if (this->port_slot_mapping) {
        kfree(this->port_slot_mapping);
    }

    serial_write("[xHCI] Cleanup complete\n");
    return;
}

#define SZ_PTR 8

// Initializes the xHCI device by allocating all its memory
// TODO: If you want, implement MSI-X, it isn't super hard to
// do but for some reason I had issues in my own endeavours with MSI-X
// on real hardware, which makes a poll a lazy but effective solution.
// The good news is the current system is very easy to use with MSI-X,
// simply put a task to sleep then wake it as soon as the interrupt fires.
// You'll need to figure out a way to know which MSI-X vector correlates to what
// xHCI controller. (Or NVMe in future)
void xHCI_initialize(xHCI_PCI_t *this) {
    serial_write("[xHCI] Initializing\n");
    serial_write("[xHCI] This: "); serial_write_hex((uint64_t)this); serial_write("\n");

    this->xHCI_lock = SPINLOCK_INIT;
    this->early_boot = true;
    
    this->port_slot_mapping = kzalloc(USB_TOTAL_xHCI_SLOTS * sizeof(uint8_t));
    
    this->ep_rings = kzalloc(USB_TOTAL_xHCI_SLOTS * SZ_PTR);
    this->ep_cycle_states = kzalloc(USB_TOTAL_xHCI_SLOTS * SZ_PTR);
    this->ep_enqueue_ptrs = kzalloc(USB_TOTAL_xHCI_SLOTS * SZ_PTR);
    this->ep_ring_physs = kzalloc(USB_TOTAL_xHCI_SLOTS * SZ_PTR);
    this->config_desc_buffer = kzalloc(USB_TOTAL_xHCI_SLOTS * SZ_PTR);
    this->descriptor_buffer = kzalloc(USB_TOTAL_xHCI_SLOTS * SZ_PTR);
    this->pending_config_value = kzalloc(USB_TOTAL_xHCI_SLOTS * SZ_PTR);

    for (size_t i = 0; i < USB_TOTAL_xHCI_SLOTS; ++i) {
        this->ep_rings[i] = kzalloc(DEVICE_CONTEXT_ENTRIES * SZ_PTR);
        this->ep_cycle_states[i] = kzalloc(DEVICE_CONTEXT_ENTRIES * SZ_PTR);
        this->ep_enqueue_ptrs[i] = kzalloc(DEVICE_CONTEXT_ENTRIES * SZ_PTR);
        this->ep_ring_physs[i] = kzalloc(DEVICE_CONTEXT_ENTRIES * SZ_PTR);
    }

    this->attached_drvrs = kzalloc(USB_TOTAL_xHCI_SLOTS * SZ_PTR);
    this->slot_states = kzalloc(USB_TOTAL_xHCI_SLOTS * SZ_PTR);

    this->mmio_base_phys = pci_get_bar(this->dev_t, BAR_INDEX_0);
    
    if (!this->mmio_base_phys) {
        serial_write("[xHCI] PCI BAR0 is 0. Failed initialization!\n");
        xHCI_cleanup(this);
        return;
    }

    vmm_space_t *kernel_space = vmm_get_kernel_space();
    size_t map_size = MMIO_MAP_PAGES * PAGE_SIZE;

    vmm_down_write(&kernel_space->mmap_sem);
    uintptr_t virt_addr = vma_find_unmapped_area(
        &kernel_space->vma_tree, 
        kernel_space->mmap_base, 
        kernel_space->mmap_end, 
        map_size, 
        PAGE_SIZE
    );

    if (!virt_addr) {
        vmm_up_write(&kernel_space->mmap_sem);
        serial_write("[xHCI] Error: Failed to find unmapped virtual memory area\n");
        xHCI_cleanup(this);
        return;
    }

    vm_area_t *vma = vma_create(virt_addr, virt_addr + map_size, VMA_TYPE_DEVICE, VMA_FLAG_READ | VMA_FLAG_WRITE);
    
    if (!vma) {
        vmm_up_write(&kernel_space->mmap_sem);
        serial_write("[xHCI] Error: Failed to create VMA structure\n");
        xHCI_cleanup(this);
        return;
    }

    vma_insert(&kernel_space->vma_head, &kernel_space->vma_tree, vma);
    vmm_up_write(&kernel_space->mmap_sem);

    uint32_t mmu_flags = MMU_PROT_READ | MMU_PROT_WRITE | MMU_FLAG_NOCACHE;
    mmu_map_pages(kernel_space->mmu_ctx, virt_addr, this->mmio_base_phys, MMIO_MAP_PAGES, mmu_flags);

    this->mmio_base_virt = virt_addr;
    serial_write("[xHCI] Allocated MMIO @ ");
    serial_write_hex(this->mmio_base_virt);
    serial_write("\n");

    pci_enable_mmio(this->dev_t);
    pci_enable_bus_mastering(this->dev_t);

    volatile uint32_t cap_length_reg = *(volatile uint32_t *)(this->mmio_base_virt);
    uint8_t cap_length = (uint8_t)(cap_length_reg & 0xFF);

    volatile uint32_t *hcs_param_ptr = (volatile uint32_t *)(this->mmio_base_virt + XHCI_CAP_HCSPARAMS1);
    uint32_t hcs_params1 = *hcs_param_ptr;

    this->max_slots = hcs_params1 & XHCI_HCSPARAMS1_MAX_PORTS_MASK;
    this->max_ports = (hcs_params1 >> XHCI_HCSPARAMS1_MAX_PORTS_SHIFT) & XHCI_HCSPARAMS1_MAX_SLOTS_MASK;

    this->cap_regs = (xHCI_CR_t *)(this->mmio_base_virt);
    this->op_regs = (xHCI_OR_t *)(this->mmio_base_virt + cap_length);
    this->run_regs = (xHCI_RR_t *)(this->mmio_base_virt + this->cap_regs->rts_off);
    this->db_regs = (xHCI_DR_t *)(this->mmio_base_virt + this->cap_regs->db_off);

    this->op_regs->config = this->max_slots;

    serial_write("[xHCI] Executing BIOS handoff\n");
    xHCI_bios_reclaim(this);
    serial_write("[xHCI] Resetting controller\n");
    xHCI_reset_controller(this);
    // serial_write("[xHCI] Enabling MSI-X\n");
    // TODO: Add MSI-X interrupts here. Basically find the MSI-X device in PCI, enable it, point it to a IDT vector. Find the MSI device, disable it (if it exists).

    serial_write("[xHCI] Allocating DCBAAP\n");
    this->op_regs->config = this->max_slots;
    this->dcbaap_virt = zkpalloc();
    this->op_regs->dcbaap = xHCI_get_phys(this->dcbaap_virt);

    uint32_t hcs2 = this->cap_regs->hcs_params2;
    uint32_t max_scratchpad = (hcs2 >> 27) & 0x1F;

    if (max_scratchpad > 0) {
        serial_write("[xHCI] Device requested scratchpad buffers\n");

        uint64_t *sp_array = zkpalloc();
        if (!sp_array) {
            // OOM
            serial_write("[xHCI] Failed to allocate scratchpad\n");
            xHCI_cleanup(this);
            return;
        }

        for (uint32_t i = 0; i < max_scratchpad; ++i) {
            void *sp_page = zkpalloc();
            if (!sp_page) {
                serial_write("[xHCI] Failed sp allocation\n");\
                xHCI_cleanup(this);
                return;
            }

            sp_array[i] = xHCI_get_phys(sp_page);
        }

        this->dcbaap_virt[0] = xHCI_get_phys(sp_array);
    }

    serial_write("[xHCI] Allocating command ring\n");

    this->cmd_ring_virt = zkpalloc();
    if (!this->cmd_ring_virt) {
        serial_write("[xHCI] Error: failed to alloc cmd ring virt\n");
        return;
    }

    this->op_regs->crcr = xHCI_get_phys(this->cmd_ring_virt) | XHCI_CRCR_RCS;

    this->event_ring_virt = zkpalloc();
    uint64_t event_ring_phys = xHCI_get_phys(this->event_ring_virt);

    this->erst_virt = zkpalloc();

    if (!this->erst_virt) {
        serial_write("[xHCI] Error: Failed to allocatate ERST!\n");
        return;
    }

    this->erst_virt[XHCI_ERST_PRIMARY_SEGMENT].base_addr = event_ring_phys;
    this->erst_virt[XHCI_ERST_PRIMARY_SEGMENT].size = XHCI_EVENT_RING_TRBS;

    uint64_t erst_phys = xHCI_get_phys(this->erst_virt);

    __asm__ volatile("mfence" ::: "memory");

    this->run_regs->irs[XHCI_PRIMARY_INTERRUPTER].erst_size = XHCI_ERST_SINGLE_SEGMENT;
    this->run_regs->irs[XHCI_PRIMARY_INTERRUPTER].erst_ba = erst_phys;
    this->run_regs->irs[XHCI_PRIMARY_INTERRUPTER].erdp = event_ring_phys;
    this->run_regs->irs[XHCI_PRIMARY_INTERRUPTER].iman |= (XHCI_IMAN_IP | XHCI_IMAN_IE);
    this->run_regs->irs[XHCI_PRIMARY_INTERRUPTER].imod = XHCI_IMOD_1MS_INTERVAL;

    serial_write("[xHCI] Starting controller\n");

    LOG32("[xHCI] HCSPARAMS1=", hcs_params1);
    LOG32("[xHCI] HCCPARAMS1=", this->cap_regs->hcc_params1);
    LOG32("[xHCI] max_slots=", this->max_slots);
    LOG32("[xHCI] max_ports=", this->max_ports);
    LOG32("[xHCI] USBCMD=", this->op_regs->usb_cmd);
    LOG32("[xHCI] USBSTS=", this->op_regs->usb_sts);
    LOG32("[xHCI] dcbaap phys=", this->op_regs->dcbaap);
    LOG32("[xHCI] crcr   phys=", xHCI_get_phys(this->cmd_ring_virt));
    LOG32("[xHCI] evring phys=", event_ring_phys);
    LOG32("[xHCI] erst   phys=", erst_phys);
    for (size_t i = 0; i < this->max_ports; ++i) {
        volatile uint32_t *p = (volatile uint32_t *)((uintptr_t)this->op_regs
            + XHCI_PORT_REG_SET_OFFSET + i * XHCI_PORT_REG_STRIDE);
        serial_write("[xHCI] port "); serial_write_num(i + 1);
        LOG32(" PORTSC=", *p);
    }

    this->op_regs->usb_cmd = (XHCI_CMD_RUN_STOP | XHCI_CMD_INTE | XHCI_CMD_HSEE);
    while (this->op_regs->usb_sts & XHCI_STS_HCHALTED) {
        XHCI_PAUSE
    }

    this->cmd_ring_base = (xHCI_TRB_t *)this->cmd_ring_virt;
    this->cmd_ring_enqueue_ptr = 0;
    this->cmd_ring_cycle_state = XHCI_COMMAND_RING_CYCLE_START;
    this->event_ring_cycle_state = XHCI_COMMAND_RING_CYCLE_START;

    uint64_t ikeys = spinlock_acquire_irqsave(&this->xHCI_lock);
    xHCI_check_ports(this);
    spinlock_release_irqrestore(&this->xHCI_lock, ikeys);

    // Technically UB
    process_t *xhci_worker = process_create((void (*)(void))xHCI_worker, false);

    uint64_t *top_of_stack = (uint64_t *)xhci_worker->kernel_stack;
    uint64_t *rdi_slot = top_of_stack - (7 + 1 + 5);
    *rdi_slot = (uint64_t)this;
    // if there is a better way of doing the above don't kill me ):

    XHCI_FENCE

    serial_write("[xHCI] Initialization complete\n");
}

void xHCI_send_command(xHCI_PCI_t *this, uint32_t type, uint64_t param, uint32_t slot_id_or_status) {

    xHCI_TRB_t *command = &this->cmd_ring_base[this->cmd_ring_enqueue_ptr];
    command->param = param;

    uint32_t control = (type << XHCI_TRB_TYPE_SHIFT);

    if (type == TRB_TYPE_ADDRESS_DEVICE || type == TRB_TYPE_CONFIGURE_EP ||
        type == TRB_TYPE_DISABLE_SLOT   || type == TRB_TYPE_EVALUATE_CONTEXT) {
        command->status = 0;
        control |= (slot_id_or_status << XHCI_TRB_SLOT_ID_SHIFT);
    }
    else {
        command->status = slot_id_or_status;
    }

    if (this->cmd_ring_cycle_state) {
        control |= (1U << XHCI_TRB_CYCLE_SHIFT);
    }
    else {
        control &= ~(1U << XHCI_TRB_CYCLE_SHIFT);
    }

    command->control = control;

    ++this->cmd_ring_enqueue_ptr;

    if (this->cmd_ring_enqueue_ptr == XHCI_CMD_RING_INDEX_LIMIT) {
        serial_write("[xHCI] Send command hit cmd ring limit");
        xHCI_TRB_t *link = &this->cmd_ring_base[XHCI_CMD_RING_INDEX_LIMIT];

        link->param = xHCI_get_phys(this->cmd_ring_base);
        link->status = 0;

        link->control = (TRB_TYPE_LINK << XHCI_TRB_TYPE_SHIFT) |
                        XHCI_TRB_LINK_TC |
                        ((this->cmd_ring_cycle_state ? 1U : 0U) << XHCI_TRB_CYCLE_SHIFT);

        XHCI_FENCE

        this->cmd_ring_enqueue_ptr = 0;
        this->cmd_ring_cycle_state = 0;
    }

    XHCI_FENCE

    this->db_regs[XHCI_HC_DOORBELL].value = XHCI_DB_TARGET_HC;

    return;
}

void xHCI_check_ports(xHCI_PCI_t *this) {
    uintptr_t op_base_addr = (uintptr_t)this->op_regs;

    serial_write("[xHCI] Checking ports...\n");

    for (size_t i = 0; i < this->max_ports; ++i) {
        uintptr_t port_base = op_base_addr + XHCI_PORT_REG_SET_OFFSET + (i * XHCI_PORT_REG_STRIDE);
        volatile uint32_t *portsc_reg = (volatile uint32_t *)(port_base);
        uint32_t status = *portsc_reg;
        
        bool connected = status & XHCI_PORTSC_CCS;
        bool enabled = status & XHCI_PORTSC_PED;

        if (!connected) {
            this->ports_ignored &= ~(1U << i);
            this->ports_resetting &= ~(1U << i);
            if (this->port_slot_mapping[i])
                this->port_slot_mapping[i] = 0;
            
            continue;
        }

        if (this->ports_ignored & (1U << i)) continue;

        if (!enabled) {
            if (this->ports_resetting & (1U << i))
                continue;

            xHCI_reset_ports(this, i);
            this->ports_resetting |= (1U << i);
            continue;
        }

        this->ports_resetting &= ~(1U << i);
        if (this->port_slot_mapping[i]) continue;
        if (this->pending_port_setup != XHCI_NO_PENDING_PORT) continue;

        serial_write("[xHCI] New device on port ");
        serial_write_num(i + 1);
        serial_write(". Requesting slot.\n");
        this->pending_port_setup = i;
        xHCI_send_command(this, TRB_TYPE_ENABLE_SLOT, 0, 0);

        XHCI_FENCE
    }

    return;
}

void xHCI_reset_ports(xHCI_PCI_t *this, int port_idx) {
    uintptr_t op_base_addr = (uintptr_t)(this->op_regs);
    uintptr_t port_base = op_base_addr + XHCI_PORT_REG_SET_OFFSET + (port_idx * XHCI_PORT_REG_STRIDE);
    volatile uint32_t *portsc_reg = (volatile uint32_t *)(port_base);

    uint32_t status = *portsc_reg;

    // Reset to default values. CCS, OCA & CAS must be 1, according to xHCI spec.
    const uint32_t PORT_RO_MASK = (0xFU << 10) | (1U << 0) | (1U << 3) | (1U << 24);

    status &= ~PORT_RO_MASK;
    status |= XHCI_PORTSC_RW1C_MASK;
    status |= XHCI_PORTSC_PR;

    *portsc_reg = status;

    XHCI_FENCE

    return;
}

void xHCI_conf_iface_ep(xHCI_PCI_t *this, uint8_t slot_id, xHCI_PE_t *endpoints, int ep_count) {
    if (!this->scratchpad) {
        this->scratchpad = zkpalloc();
    }

    xHCI_ICC_t *input_ctx = (xHCI_ICC_t *)(this->scratchpad);
    uint64_t in_phys = xHCI_get_phys(input_ctx);

    input_ctx->add_flags = XHCI_INPUT_ADD_SLOT_CONTEXT;
    uint32_t highest_dci = 1;

    for (int i = 0; i < ep_count; ++i) {
        uint8_t ep_num = endpoints[i].address & USB_XHCI_EP_NUM_MASK;
        bool is_in = endpoints[i].address & USB_XHCI_EP_DIR_IN;
        uint8_t dci = (ep_num * 2) + (is_in ? 1 : 0);

        if (dci > highest_dci) highest_dci = dci;

        input_ctx->add_flags |= (1U << dci);

        uint64_t *ring_virt = (uint64_t *)(zkpalloc());
        uint64_t ring_phys = xHCI_get_phys(ring_virt);

        this->ep_rings[slot_id][dci] = ring_virt;
        this->ep_cycle_states[slot_id][dci] = XHCI_COMMAND_RING_CYCLE_START;
        this->ep_enqueue_ptrs[slot_id][dci] = 0;
        this->ep_ring_physs[slot_id][dci] = ring_phys;

        uint8_t usb_type = endpoints[i].attributes & USB_XHCI_EP_TYPE_MASK;
        uint8_t type_base = is_in ? XHCI_EP_TYPE_IN_SHIFT : XHCI_EP_TYPE_OUT_SHIFT;
        uint8_t xhci_ep_type = type_base + usb_type;

        volatile xHCI_EC_t *ep_ctx = &input_ctx->endpoints[dci - 1];

        ep_ctx->ep_info2 = ((uint32_t)(xhci_ep_type) << XHCI_EP_TYPE_SHIFT) | ((uint32_t)(endpoints[i].max_packet_size) << XHCI_EP_MAX_PACKET_SHIFT);

        ep_ctx->ep_info2 |= (XHCI_EP_CERR_MAX << XHCI_EP_CERR_SHIFT);
        ep_ctx->deq_ptr = ring_phys | XHCI_DEQ_CYCLE_STATE_START;
        ep_ctx->tx_info = XHCI_EP_DEFAULT_TRB_LEN;

        XHCI_FENCE
    }

    serial_write("[xHCI] Endpoints configured\n");

    int root_port = 0;
    for (int i = 0; i < this->max_ports; ++i) {
        if (this->port_slot_mapping[i] == slot_id) {
            root_port = i;
            break;
        }
    }

    uintptr_t op_base_addr = (uintptr_t)(this->op_regs);
    uintptr_t port_base = op_base_addr + XHCI_PORT_REG_SET_OFFSET + (root_port * XHCI_PORT_REG_STRIDE);

    uint32_t portsc = *(volatile uint32_t *)(port_base);
    uint8_t speed_id = (portsc >> XHCI_PORTSC_SPEED_SHIFT) & XHCI_PORTSC_SPEED_MASK;

    input_ctx->slot.info = (highest_dci << XHCI_SLOT_CONTEXT_ENTRIES_SHIFT) | ((uint32_t)(speed_id) << XHCI_SLOT_SPEED_SHIFT);
    input_ctx->slot.info2 = ((uint32_t)(root_port + 1) << XHCI_SLOT_ROOT_PORT_SHIFT);
    this->slot_states[slot_id] = xSS_STATE_CONFIGURING_EP;

    XHCI_FENCE

    serial_write("[xHCI] Sending config endpoint command\n");
    xHCI_send_command(this, TRB_TYPE_CONFIGURE_EP, in_phys, slot_id);
}

void xHCI_handle_TTCC(xHCI_PCI_t *this, xHCI_TRB_t *event) {
    uint32_t code = (event->status >> XHCI_TRB_COMP_CODE_SHIFT) & XHCI_TRB_COMP_CODE_MASK;

    uint8_t allocd_slot = (event->control >> XHCI_TRB_SLOT_ID_SHIFT) & XHCI_TRB_SLOT_ID_MASK;
    if (!allocd_slot) {
        for (uint32_t i = 1; i < USB_TOTAL_xHCI_SLOTS; ++i) {
            if (this->slot_states[i] != xSS_STATE_CONFIGURING_EP && this->slot_states[i] != xSS_STATE_ADDRESSING) continue;
            
            allocd_slot = i;
            break;
        }
    }

    XHCI_FENCE

    if (code == XHCI_CODE_SUCCESS && allocd_slot != 0) {
        uint64_t off = event->param & LOW_12;
        void *param_virt = (uint8_t *)pmm_page_to_vaddr(pmm_paddr_to_page(event->param & ~LOW_12)) + off;
        xHCI_TRB_t *completed_cmd_trb = (xHCI_TRB_t *)param_virt;
        uint8_t cmd_type = (completed_cmd_trb->control >> XHCI_TRB_TYPE_SHIFT) & XHCI_TRB_TYPE_MASK;

        serial_write("[xHCI] CMD done: type="); serial_write_num(cmd_type);
        serial_write(" slot="); serial_write_num(allocd_slot);
        serial_write(" state="); serial_write_num((uint64_t)this->slot_states[allocd_slot]);
        serial_write("\n");

        switch(cmd_type) {
        case TRB_TYPE_ENABLE_SLOT:
            serial_write("[xHCI] Slot assigned: ");
            serial_write_num(allocd_slot);
            serial_write("\n");

            this->slot_states[allocd_slot] = xSS_STATE_ADDRESSING;
            this->port_slot_mapping[this->pending_port_setup] = allocd_slot;

            xHCI_addr_dev(this, allocd_slot, this->pending_port_setup);
            this->pending_port_setup = XHCI_NO_PENDING_PORT;

            serial_write("[xHCI] Slot enabled\n");
            break;
        case TRB_TYPE_ADDRESS_DEVICE:
            this->descriptor_buffer[allocd_slot] = kzalloc(USB_XHCI_DEVICE_DESC_SIZE);
            if (xHCI_slot_speed(this, allocd_slot) == 1)
                xHCI_req_dev_desc(this, allocd_slot, 8, xSS_STATE_GET_DEV_DESC_HEAD);
            else
                xHCI_req_dev_desc(this, allocd_slot, USB_XHCI_DEVICE_DESC_SIZE, xSS_STATE_GET_DEV_DESC);
            break;
        case TRB_TYPE_EVALUATE_CONTEXT:
            xHCI_req_dev_desc(this, allocd_slot, USB_XHCI_DEVICE_DESC_SIZE, xSS_STATE_GET_DEV_DESC);
            break;
        case TRB_TYPE_CONFIGURE_EP: {
            serial_write("[xHCI] Endpoints configured, setting device config\n");
            this->slot_states[allocd_slot] = xSS_STATE_SETTING_CONFIG;

            uint8_t req_type = USB_XHCI_REQ_DIR_OUT | USB_XHCI_REQ_TYPE_STANDARD | USB_XHCI_REQ_REC_DEV;
            uint16_t conf_val = this->pending_config_value[allocd_slot] ? this->pending_config_value[allocd_slot] : USB_XHCI_DEFAULT_CONF_VAL;

            xHCI_send_req(this, allocd_slot, req_type, USB_XHCI_REQ_SET_TYPE_CONFIG, conf_val, 0, 0, 0);
            break;
        }
        case TRB_TYPE_DISABLE_SLOT: {
            if (this->slot_states[allocd_slot] != xSS_STATE_DISABLING) {
                serial_write("[xHCI] Warning: Disable Slot completion for slot ");
                serial_write_num(allocd_slot);
                serial_write(" not in DISABLING state, ignoring\n");
                break;
            }

            serial_write("[xHCI] TRB to disable slot received, freeing context for slot");
            serial_write_num(allocd_slot); serial_write("\n");

            uint64_t out_phys = this->dcbaap_virt[allocd_slot];
            if (out_phys) {
                void *out_virt = (uint8_t *)pmm_page_to_vaddr(pmm_paddr_to_page(out_phys & ~LOW_12))
                                 + (out_phys & LOW_12);
                xHCI_free_kpage(out_virt);
            }
            this->dcbaap_virt[allocd_slot] = 0;

            for (size_t dci = 0; dci < 32; ++dci) {
                if (this->ep_rings[allocd_slot][dci]) {
                    xHCI_free_kpage(this->ep_rings[allocd_slot][dci]);
                    this->ep_rings[allocd_slot][dci] = NULL;
                }
                this->ep_cycle_states[allocd_slot][dci] = 0;
                this->ep_enqueue_ptrs[allocd_slot][dci] = 0;
                this->ep_ring_physs[allocd_slot][dci]   = 0;
            }

            if (this->descriptor_buffer[allocd_slot]) {
                kfree((void *)this->descriptor_buffer[allocd_slot]);
                this->descriptor_buffer[allocd_slot] = NULL;
            }
            if (this->config_desc_buffer[allocd_slot]) {
                kfree(this->config_desc_buffer[allocd_slot]);
                this->config_desc_buffer[allocd_slot] = NULL;
            }

            this->attached_drvrs[allocd_slot] = NULL;
            this->pending_config_value[allocd_slot] = 0;
            this->slot_states[allocd_slot] = xSS_STATE_NONE;
            break;
        }
        }

        if (this->slot_states[allocd_slot] != xSS_STATE_CONFIGURED) {
            XHCI_FENCE
            
            return;
        }

        if (this->attached_drvrs[allocd_slot]) {
            xHCI_check_ports(this);
        }

        XHCI_FENCE

        return;
    }

    serial_write("[xHCI] Warning: Command failed. Code: ");
    serial_write_num(code);
    serial_write("\n");
}

static void xHCI_discard_slot(xHCI_PCI_t *this, uint8_t slot) {
    serial_write("[xHCI] DISCARD slot "); serial_write_num(slot);
    serial_write(" state="); serial_write_num((uint64_t)this->slot_states[slot]);
    serial_write(" caller="); serial_write_hex((uint64_t)__builtin_return_address(0));
    serial_write("\n");

    for (size_t p = 0; p < this->max_ports; ++p) {
        if (this->port_slot_mapping[p] == slot) {
            this->ports_ignored |= (1U << p);
            break;
        }
    }
    this->slot_states[slot] = xSS_STATE_DISABLING;
    // Refer to xHCI section 3.3.3
    xHCI_send_command(this, TRB_TYPE_DISABLE_SLOT, 0, slot);
}

void xHCI_handle_TTE(xHCI_PCI_t *this, xHCI_TRB_t *event, uint32_t slot, uint64_t keys) {
    uint32_t transfer_len = event->status & TRB_TRANSFER_LEN_MASK;
    uint32_t completion_code = (event->status >> XHCI_TRB_COMP_CODE_SHIFT) & XHCI_TRB_COMP_CODE_MASK;

    if (completion_code != XHCI_COMP_SUCCESS && completion_code != XHCI_COMP_SHORT_PACKET) {
        serial_write("[xHCI] Warning: Transfer error on slot ");
        serial_write_num(slot);
        serial_write(", with code ");
        serial_write_hex(completion_code);
        serial_write(" ("); serial_write(xHCI_comp_code_str(completion_code)); serial_write(")");
        serial_write(", dci "); serial_write_num((event->control >> ENDPOINT_ID_SHIFT) & 0x1F);
        serial_write(", residual "); serial_write_num(transfer_len);
        serial_write("\n");

        xHCI_dump_halt(this, event, slot, (event->control >> ENDPOINT_ID_SHIFT) & 0x1F);


        if (this->slot_states[slot] != xSS_STATE_CONFIGURED &&
            this->slot_states[slot] != xSS_STATE_DISABLING &&
            ((event->control >> ENDPOINT_ID_SHIFT) & 0x1F) == 1) {
            xHCI_discard_slot(this, slot);
        }
        return;
    }

    switch(this->slot_states[slot]) {
    case xSS_STATE_GET_DEV_DESC_HEAD: {
        uint8_t *desc = (uint8_t *)this->descriptor_buffer[slot];
        uint8_t mps0 = desc[7];
        if (mps0 == 16 || mps0 == 32 || mps0 == 64)
            xHCI_eval_ep0_mps(this, slot, mps0);
        else
            xHCI_req_dev_desc(this, slot, USB_XHCI_DEVICE_DESC_SIZE, xSS_STATE_GET_DEV_DESC);
        break;
    }
    case xSS_STATE_GET_DEV_DESC: {
        kfree((void *)this->descriptor_buffer[slot]);
        this->descriptor_buffer[slot] = NULL;
        this->config_desc_buffer[slot] = kzalloc(CONFIG_HEADER_SIZE);
        uint64_t phys = xHCI_get_phys(this->config_desc_buffer[slot]);

        this->slot_states[slot] = xSS_STATE_GET_CONFIG_DESC_HEADER;
        uint8_t req_type = USB_XHCI_REQ_DIR_IN | USB_XHCI_REQ_TYPE_STANDARD | USB_XHCI_REQ_REC_DEV;
        uint16_t wval = MAKE_WVALUE(USB_XHCI_DESC_TYPE_CONF, 0);
        
        xHCI_send_req(this, slot, req_type, USB_XHCI_REQ_GET_DESC, wval, 0x0, CONFIG_HEADER_SIZE, phys);
        break;
    }
    case xSS_STATE_GET_CONFIG_DESC_HEADER: {
        xHCI_UCD_t *cfg = (xHCI_UCD_t *)(this->config_desc_buffer[slot]);
        uint16_t total_len = cfg->wTotalLength;
        kfree(this->config_desc_buffer[slot]);

        this->config_desc_buffer[slot] = kzalloc(total_len);
        uint64_t phys = xHCI_get_phys(this->config_desc_buffer[slot]);

        this->slot_states[slot] = xSS_STATE_GET_CONFIG_DESC_FULL;
        uint8_t req_type = USB_XHCI_REQ_DIR_IN | USB_XHCI_REQ_TYPE_STANDARD | USB_XHCI_REQ_REC_DEV;
        uint16_t wval = MAKE_WVALUE(USB_XHCI_DESC_TYPE_CONF, 0);

        xHCI_send_req(this, slot, req_type, USB_XHCI_REQ_GET_DESC, wval, 0x0, total_len, phys);
        break;
    }
    case xSS_STATE_GET_CONFIG_DESC_FULL: {
        xHCI_UCD_t *cfg = (xHCI_UCD_t *)(this->config_desc_buffer[slot]);
        uint8_t *walker = this->config_desc_buffer[slot] + cfg->bLength;
        uint8_t *end = this->config_desc_buffer[slot] + cfg->wTotalLength;
        xHCI_EXTD_t *match = NULL;
        xHCI_PE_t endpoints[MAX_PARSED_ENDPOINTS];
        int ep_count = 0;
        while (walker < end) {
            uint8_t length = walker[0];
            uint8_t desc_type = walker[1];
            if (desc_type == USB_XHCI_DESC_TYPE_INTERF) {
                if (match) break;
                xHCI_UID_t *iface = (xHCI_UID_t *)(walker);
                match = xHCI_load_new_drvr(
                    iface->bInterfaceClass, 
                    iface->bInterfaceSubClass, 
                    iface->bInterfaceProtocol
                );
            }
            else if (desc_type == USB_XHCI_DESC_TYPE_ENDPOINT && match) {
                if (ep_count < MAX_PARSED_ENDPOINTS) {
                    xHCI_UED_t *ep_desc = (xHCI_UED_t *)(walker);
                    endpoints[ep_count].address = ep_desc->bEndpointAddress;
                    endpoints[ep_count].attributes = ep_desc->bmAttributes;
                    endpoints[ep_count].max_packet_size = ep_desc->wMaxPacketSize;
                    ++ep_count;
                }
            }

            walker += length;
        }

        if (match && ep_count > 0) {
            serial_write("[xHCI] Driver match found, ");
            serial_write_num(ep_count);
            serial_write(" endpoints to configure\n");

            this->attached_drvrs[slot] = match;
            match->initialize(match, this, slot, endpoints, ep_count);
            xHCI_conf_iface_ep(this, slot, endpoints, ep_count);
        }

        if (!match || ep_count == 0) {
            serial_write("[xHCI] No driver for slot "); serial_write_num(slot);
            serial_write("\n");
            xHCI_discard_slot(this, slot);
        }

        kfree(this->config_desc_buffer[slot]);
        this->config_desc_buffer[slot] = NULL;
        break;
    }
    case xSS_STATE_CONFIGURED: {
        if (!this->attached_drvrs[slot]) {
            break;
        }

        uint32_t dci = (event->control >> ENDPOINT_ID_SHIFT) & 0x1F;

        uint8_t ep_num = dci / 2;
        bool is_in = (dci % 2) != 0;
        uint8_t raw_ep_addr = ep_num | (is_in ? USB_XHCI_EP_DIR_IN : 0);

        xHCI_EXTD_t *drvr = this->attached_drvrs[slot];

        spinlock_release_irqrestore(&this->xHCI_lock, keys); // stops reentrancy issues, though this just allows people to write bad code?
        drvr->on_int(drvr, transfer_len, raw_ep_addr, event->param);
        spinlock_acquire_irqsave(&this->xHCI_lock);

        break;
    }
    case xSS_STATE_SETTING_CONFIG: {
        this->slot_states[slot] = xSS_STATE_CONFIGURED;
        serial_write("[xHCI] Device fully configured & ready\n");

        xHCI_EXTD_t *drvr = this->attached_drvrs[slot];

        spinlock_release_irqrestore(&this->xHCI_lock, keys);

        if (this->early_boot) {
            drvr->start(drvr);
        }
        else {
            uint64_t nkeys = irq_save();

            process_t *new_subproc = process_create((void (*)(void))drvr->start, false);
            
            uint64_t *top_of_stack = (uint64_t *)new_subproc->kernel_stack;
            uint64_t *rdi_slot = top_of_stack - 13;
            *rdi_slot = (uint64_t)drvr;
            irq_restore(nkeys);
        }

        spinlock_acquire_irqsave(&this->xHCI_lock);

        xHCI_check_ports(this);
        
        break;
    }
    default:
        break;
    }

    XHCI_FENCE
}

// Try to avoid invoking this directly
bool xHCI_poll_evring(xHCI_PCI_t *this) {
    xHCI_TRB_t *evring = (xHCI_TRB_t *)this->event_ring_virt;
    bool handled_events = false;

    __asm__ volatile("mfence" ::: "memory");

    for (size_t i = 0; i < XHCI_MAX_EVENTS_PER_INTR; ++i) {
        size_t peek_idx = this->event_ring_dequeue_ptr;

        __asm__ volatile("mfence" ::: "memory");

        if (peek_idx < XHCI_EVENT_RING_TRBS &&
            (evring[peek_idx].control & XHCI_TRB_CYCLE_MASK) != this->event_ring_cycle_state)
            break;

        uint64_t keys = spinlock_acquire_irqsave(&this->xHCI_lock);
        
        xHCI_TRB_t *event_slot = &evring[this->event_ring_dequeue_ptr];

        __asm__ volatile("mfence" ::: "memory");

        if ((event_slot->control & XHCI_TRB_CYCLE_MASK) != this->event_ring_cycle_state) {
            spinlock_release_irqrestore(&this->xHCI_lock, keys);
            break;
        }

        xHCI_TRB_t ev = *event_slot;
        xHCI_TRB_t *event = &ev;

        ++this->event_ring_dequeue_ptr;
        if (this->event_ring_dequeue_ptr >= XHCI_EVENT_RING_TRBS) {
            this->event_ring_dequeue_ptr = XHCI_ERST_PRIMARY_SEGMENT;
            this->event_ring_cycle_state = !this->event_ring_cycle_state;
        }

        {
            uint64_t base = this->erst_virt[XHCI_ERST_PRIMARY_SEGMENT].base_addr;
            uint64_t erdp = base + this->event_ring_dequeue_ptr * sizeof(xHCI_TRB_t);
            this->run_regs->irs[XHCI_PRIMARY_INTERRUPTER].erdp = (erdp & ~0xFULL) | XHCI_ERDP_EHB;
        }

        handled_events = true;

        uint32_t type = (event->control >> XHCI_TRB_TYPE_SHIFT) & XHCI_TRB_TYPE_MASK;
        uint32_t slot = (event->control >> XHCI_TRB_SLOT_ID_SHIFT) & XHCI_TRB_SLOT_ID_MASK;

        switch (type) {
        case TRB_TYPE_CMD_COMPLETE:
            xHCI_handle_TTCC(this, event);
            break;
        case TRB_TYPE_PORT_STATUS: {
            uint8_t port_id = (event->param >> 24) & 0xFF;
            int port_idx = (int)port_id - 1;

            serial_write("[xHCI] Port status change on port ");
            serial_write_num(port_id);
            serial_write("\n");

            if (port_idx >= 0) this->ports_resetting &= ~(1U << port_idx);
            xHCI_check_ports(this);
            break;
        }
        case TRB_TRANSFER_EVENT:
            xHCI_handle_TTE(this, event, slot, keys);
            break;
        default: 
            break;
        }

        spinlock_release_irqrestore(&this->xHCI_lock, keys);
    }

    if (handled_events) {
        uint64_t ekeys = spinlock_acquire_irqsave(&this->xHCI_lock);
        uint64_t event_ring_base_phys = this->erst_virt[XHCI_ERST_PRIMARY_SEGMENT].base_addr;
        uint64_t erdp_phys = event_ring_base_phys + (this->event_ring_dequeue_ptr * sizeof(xHCI_TRB_t));

        uint64_t new_erdp = (erdp_phys & ~0xFULL) | XHCI_ERDP_EHB;

        this->run_regs->irs[XHCI_PRIMARY_INTERRUPTER].erdp = new_erdp;
        this->run_regs->irs[XHCI_PRIMARY_INTERRUPTER].iman |= XHCI_IMAN_IP;
        spinlock_release_irqrestore(&this->xHCI_lock, ekeys);
    }

    XHCI_FENCE

    return handled_events;
}

void xHCI_worker(void *xpciaddr) {
    xHCI_PCI_t *selfdev = (xHCI_PCI_t *)(xpciaddr);

    if (!selfdev) {
        serial_write("[xHCI] Warning: Failed to receive xHCI_PCI_t pointer from creator. Aborting.\n");
        
        process_get_current()->kill_pending = true;
        for (;;) ;
    }

    // Check that xpciaddr actually points to a real xHCI device, rather than some corrupted
    // conglomorate.
    if (strncmp(selfdev->signature, "xHCI_PCI_t", 11) == 0) {
        serial_write("[xHCI] Verified xHCI device.\n");
    }
    else {
        serial_write("[xHCI] Failed to verify xHCI device.\n");
        process_get_current()->kill_pending = true;
        for (;;) ;
    }

    selfdev->early_boot = false;
    for (;;) {
        __asm__ volatile("sti");
        xHCI_poll_evring(selfdev);
        XHCI_PAUSE
    } // do work
}

// NOTE: This is a bulk transfer, not an int transfer, which is why it works for mass storage.
// I've done this because its easier, and it works perfectly fine for regular mice & keyboards,
// it just has locking issues, since they'll fight over locks.
void xHCI_queue_transfer(xHCI_PCI_t *this, uint8_t slot_id, uint8_t endpoint_address, uint64_t buffer_phys, uint32_t buffer_size) {
    uint64_t key = spinlock_acquire_irqsave(&this->xHCI_lock);

    XHCI_FENCE

    uint8_t ep_num = endpoint_address & USB_XHCI_EP_NUM_MASK;
    bool is_in = (endpoint_address & USB_XHCI_EP_DIR_IN);
    uint8_t dci = (ep_num * 2) + (is_in ? 1 : 0);

    xHCI_TRB_t *ring = (xHCI_TRB_t *)(this->ep_rings[slot_id][dci]);
    if (!ring) {
        spinlock_release_irqrestore(&this->xHCI_lock, key);
        return;
    }

    uint64_t ring_phys = this->ep_ring_physs[slot_id][dci];
    uint64_t idx = this->ep_enqueue_ptrs[slot_id][dci];

    uint8_t cycle = this->ep_cycle_states[slot_id][dci];

    if (idx == XHCI_CMD_RING_INDEX_LIMIT) {
        xHCI_TRB_t *link = &ring[XHCI_CMD_RING_INDEX_LIMIT];

        link->param = ring_phys;
        link->status = 0;

        uint32_t link_ctrl = (TRB_TYPE_LINK << XHCI_TRB_TYPE_SHIFT) | XHCI_TRB_LINK_TC | ((cycle ? 1U : 0U) << XHCI_TRB_CYCLE_SHIFT);
        link->control = link_ctrl;

        XHCI_FENCE

        idx = 0;
        cycle = !cycle;
    }

    xHCI_TRB_t *trb = &ring[idx];

    trb->param = buffer_phys;
    trb->status = buffer_size;

    uint32_t control_val = (TRB_TYPE_NORMAL << XHCI_TRB_TYPE_SHIFT) | XHCI_TRB_IOC_ENABLE;
    uint32_t final_control = control_val | ((cycle ? 1U : 0U) << XHCI_TRB_CYCLE_SHIFT);

    XHCI_FENCE

    trb->control = final_control;

    XHCI_FENCE

    ++idx;

    if (idx == XHCI_CMD_RING_INDEX_LIMIT) {
        xHCI_TRB_t *link = &ring[XHCI_CMD_RING_INDEX_LIMIT];

        link->param = ring_phys;
        link->status = 0;

        uint32_t link_ctrl = (TRB_TYPE_LINK << XHCI_TRB_TYPE_SHIFT) | XHCI_TRB_LINK_TC | ((cycle ? 1U : 0U) << XHCI_TRB_CYCLE_SHIFT);
        link->control = link_ctrl;

        XHCI_FENCE

        idx = 0;
        cycle = !cycle;
    }

    this->ep_enqueue_ptrs[slot_id][dci] = idx;
    this->ep_cycle_states[slot_id][dci] = cycle;

    XHCI_FENCE

    this->db_regs[slot_id].value = dci;

    spinlock_release_irqrestore(&this->xHCI_lock, key);

    XHCI_FENCE

    return;
}