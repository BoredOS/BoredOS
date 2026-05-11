// Copyright (c) 2023-2026 Chris (boreddevnl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in this file it has in it, as per the GPL license terms.
#include "uhci.h"
#include "syscall.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static uhci_hc_t uhci_controller;
static uint8_t *frame_list;
static uhci_qh_t *qh_pool;
static uhci_td_t *td_pool;
static int td_idx = 0;

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
    
    frame_list = (uint8_t *)malloc(FRAME_LIST_SIZE * 4);
    if (!frame_list) {
        printf("[UHCI] Failed to allocate frame list\n");
        return false;
    }
    
    memset(frame_list, 0, FRAME_LIST_SIZE * 4);
    
    qh_pool = (uhci_qh_t *)malloc(sizeof(uhci_qh_t) * POOL_SIZE);
    td_pool = (uhci_td_t *)malloc(sizeof(uhci_td_t) * POOL_SIZE);
    
    if (!qh_pool || !td_pool) {
        printf("[UHCI] Failed to allocate pools\n");
        return false;
    }
    
    memset(qh_pool, 0, sizeof(uhci_qh_t) * POOL_SIZE);
    memset(td_pool, 0, sizeof(uhci_td_t) * POOL_SIZE);
    
    for (int i = 0; i < FRAME_LIST_SIZE; i++) {
        frame_list[i * 4 + 0] = 0x01;
        frame_list[i * 4 + 1] = 0x00;
        frame_list[i * 4 + 2] = 0x00;
        frame_list[i * 4 + 3] = 0x00;
    }
    
    uint32_t frame_list_phys = p2v((uint64_t)frame_list);
    outl((uint32_t)&uhci_controller.regs->frame_base, frame_list_phys);
    
    outw((uint32_t)&uhci_controller.regs->cmd, UHCI_CMD_RUN | UHCI_CMD_MAXP);
    
    uint16_t port1 = inw((uint32_t)&uhci_controller.regs->port1);
    if (port1 & UHCI_PORT_CCS) {
        printf("[UHCI] Device detected on port 1\n");
        outw((uint32_t)&uhci_controller.regs->port1, port1 | UHCI_PORT_PED);
    }
    
    uint16_t port2 = inw((uint32_t)&uhci_controller.regs->port2);
    if (port2 & UHCI_PORT_CCS) {
        printf("[UHCI] Device detected on port 2\n");
        outw((uint32_t)&uhci_controller.regs->port2, port2 | UHCI_PORT_PED);
    }
    
    uhci_controller.initialized = true;
    printf("[UHCI] Initialization complete\n");
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
    if (!uhci_controller.initialized) return -1;
    
    uhci_td_t *td_setup = &td_pool[td_idx++ % POOL_SIZE];
    uhci_td_t *td_data = (len > 0) ? &td_pool[td_idx++ % POOL_SIZE] : NULL;
    uhci_td_t *td_status = &td_pool[td_idx++ % POOL_SIZE];
    
    uint8_t *setup_buf = (uint8_t *)kmalloc(8);
    if (!setup_buf) return -1;
    
    setup_buf[0] = setup->bmRequestType;
    setup_buf[1] = setup->bRequest;
    setup_buf[2] = setup->wValue & 0xFF;
    setup_buf[3] = (setup->wValue >> 8) & 0xFF;
    setup_buf[4] = setup->wIndex & 0xFF;
    setup_buf[5] = (setup->wIndex >> 8) & 0xFF;
    setup_buf[6] = setup->wLength & 0xFF;
    setup_buf[7] = (setup->wLength >> 8) & 0xFF;
    
    memset(td_setup, 0, sizeof(uhci_td_t));
    td_setup->link = (td_data) ? (p2v((uint64_t)td_data) | 0x4) : (p2v((uint64_t)td_status) | 0x4);
    td_setup->status = 0x00800000 | ((7 & 0x7FF) << 21);
    td_setup->buffer = p2v((uint64_t)setup_buf);
    
    if (td_data && len > 0) {
        memset(td_data, 0, sizeof(uhci_td_t));
        td_data->link = p2v((uint64_t)td_status) | 0x4;
        td_data->status = 0x00800000 | ((len & 0x7FF) << 21);
        td_data->buffer = p2v((uint64_t)data);
    }
    
    memset(td_status, 0, sizeof(uhci_td_t));
    td_status->link = 0x00000001;
    td_status->status = 0x00800000;
    
    uhci_qh_t *qh = &qh_pool[0];
    memset(qh, 0, sizeof(uhci_qh_t));
    qh->element_link = p2v((uint64_t)td_setup) | 0x4;
    qh->link = 0x00000001;
    
    uint32_t qh_phys = p2v((uint64_t)qh);
    frame_list[0] = qh_phys & 0xFF;
    frame_list[1] = (qh_phys >> 8) & 0xFF;
    frame_list[2] = (qh_phys >> 16) & 0xFF;
    frame_list[3] = (qh_phys >> 24) & 0xFF;
    
    outw((uint32_t)&uhci_controller.regs->cmd, inw((uint32_t)&uhci_controller.regs->cmd) | UHCI_CMD_MAXP);
    
    for (volatile int i = 0; i < 100000; i++);
    
    uint32_t status = td_status->status;
    
    frame_list[0] = 0x01;
    frame_list[1] = 0x00;
    frame_list[2] = 0x00;
    frame_list[3] = 0x00;
    
    if (status & 0x80000000) {
        free(setup_buf);
        return len;
    }
    
    free(setup_buf);
    return -1;
}

int uhci_interrupt_in(usb_hc_t *hc, uint8_t endpoint, void *data, uint16_t len) {
    (void)hc;
    (void)endpoint;
    if (!uhci_controller.initialized) return -1;
    
    uhci_td_t *td = &td_pool[td_idx++ % POOL_SIZE];
    
    memset(td, 0, sizeof(uhci_td_t));
    td->link = 0x00000001;
    td->status = 0x00800000 | ((len & 0x7FF) << 21) | (0x69 << 16);
    td->buffer = p2v((uint64_t)data);
    
    uhci_qh_t *qh = &qh_pool[1];
    memset(qh, 0, sizeof(uhci_qh_t));
    qh->element_link = p2v((uint64_t)td) | 0x4;
    qh->link = 0x00000001;
    
    uint32_t qh_phys = p2v((uint64_t)qh);
    frame_list[0] = qh_phys & 0xFF;
    frame_list[1] = (qh_phys >> 8) & 0xFF;
    frame_list[2] = (qh_phys >> 16) & 0xFF;
    frame_list[3] = (qh_phys >> 24) & 0xFF;
    
    outw((uint32_t)&uhci_controller.regs->cmd, inw((uint32_t)&uhci_controller.regs->cmd) | UHCI_CMD_MAXP);
    
    for (volatile int i = 0; i < 50000; i++);
    
    uint32_t status = td->status;
    
    frame_list[0] = 0x01;
    frame_list[1] = 0x00;
    frame_list[2] = 0x00;
    frame_list[3] = 0x00;
    
    if (status & 0x80000000) {
        return len;
    }
    
    return -1;
}
