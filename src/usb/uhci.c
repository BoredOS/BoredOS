// Copyright (c) 2023-2026 Chris (boreddevnl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in this file it has in it, as per the GPL license terms.
#include "uhci.h"
#include "../core/io.h"
#include "../core/platform.h"
#include "../mem/memory_manager.h"
#include "../dev/pci.h"
#include <string.h>

extern void serial_write(const char *str);
extern void serial_write_hex(uint64_t n);

static uhci_hc_t uhci_controller;
static uint8_t *frame_list;
static uhci_qh_t *qh_pool;
static uhci_td_t *td_pool;

#define FRAME_LIST_SIZE 1024
#define POOL_SIZE 64

bool uhci_init(usb_hc_t *hc) {
    serial_write("[UHCI] Initializing UHCI controller\n");
    
    uint32_t bar = hc->bar0 & 0xFFFFFFF0;
    if (bar == 0) {
        serial_write("[UHCI] Invalid BAR0\n");
        return false;
    }
    
    uhci_controller.base = (uint8_t *)p2v(bar);
    uhci_controller.regs = (uhci_regs_t *)uhci_controller.base;
    
    uint16_t cmd = inw((uint32_t)&uhci_controller.regs->cmd);
    outw((uint32_t)&uhci_controller.regs->cmd, cmd & ~UHCI_CMD_RUN);
    
    for (volatile int i = 0; i < 10000; i++);
    
    outw((uint32_t)&uhci_controller.regs->cmd, cmd | UHCI_CMD_HCRESET);
    
    for (volatile int i = 0; i < 10000; i++);
    
    frame_list = (uint8_t *)kmalloc(FRAME_LIST_SIZE * 4);
    if (!frame_list) {
        serial_write("[UHCI] Failed to allocate frame list\n");
        return false;
    }
    
    memset(frame_list, 0, FRAME_LIST_SIZE * 4);
    
    qh_pool = (uhci_qh_t *)kmalloc(sizeof(uhci_qh_t) * POOL_SIZE);
    td_pool = (uhci_td_t *)kmalloc(sizeof(uhci_td_t) * POOL_SIZE);
    
    if (!qh_pool || !td_pool) {
        serial_write("[UHCI] Failed to allocate pools\n");
        return false;
    }
    
    memset(qh_pool, 0, sizeof(uhci_qh_t) * POOL_SIZE);
    memset(td_pool, 0, sizeof(uhci_td_t) * POOL_SIZE);
    
    uint32_t frame_list_phys = v2p((uint64_t)frame_list);
    outl((uint32_t)&uhci_controller.regs->frame_base, frame_list_phys);
    
    outw((uint32_t)&uhci_controller.regs->cmd, UHCI_CMD_RUN | UHCI_CMD_MAXP);
    
    uint16_t port1 = inw((uint32_t)&uhci_controller.regs->port1);
    if (port1 & UHCI_PORT_CCS) {
        serial_write("[UHCI] Device detected on port 1\n");
        outw((uint32_t)&uhci_controller.regs->port1, port1 | UHCI_PORT_PED);
    }
    
    uint16_t port2 = inw((uint32_t)&uhci_controller.regs->port2);
    if (port2 & UHCI_PORT_CCS) {
        serial_write("[UHCI] Device detected on port 2\n");
        outw((uint32_t)&uhci_controller.regs->port2, port2 | UHCI_PORT_PED);
    }
    
    uhci_controller.initialized = true;
    serial_write("[UHCI] Initialization complete\n");
    return true;
}

void uhci_reset_port(usb_hc_t *hc, int port) {
    (void)hc;
    uhci_hc_t *controller = &uhci_controller;
    uint16_t port_reg = (port == 1) ? (uint32_t)&controller->regs->port1 : (uint32_t)&controller->regs->port2;
    
    uint16_t status = inw(port_reg);
    outw(port_reg, status | UHCI_PORT_PR);
    
    for (volatile int i = 0; i < 10000; i++);
    
    status = inw(port_reg);
    outw(port_reg, status & ~UHCI_PORT_PR);
    
    for (volatile int i = 0; i < 10000; i++);
    
    status = inw(port_reg);
    outw(port_reg, status | UHCI_PORT_PED);
}

int uhci_control_transfer(usb_hc_t *hc, usb_setup_packet_t *setup, void *data, uint16_t len) {
    (void)hc;
    (void)setup;
    (void)data;
    (void)len;
    return -1;
}

int uhci_interrupt_in(usb_hc_t *hc, uint8_t endpoint, void *data, uint16_t len) {
    (void)hc;
    (void)endpoint;
    (void)data;
    (void)len;
    return -1;
}
