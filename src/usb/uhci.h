// Copyright (c) 2023-2026 Chris (boreddevnl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in this file it has in it, as per the GPL license terms.
#ifndef UHCI_H
#define UHCI_H

#include "usb.h"
#include <stdint.h>
#include <stdbool.h>

#define UHCI_CMD_REG 0x00
#define UHCI_STATUS_REG 0x02
#define UHCI_INT_EN_REG 0x04
#define UHCI_FRAME_NUM_REG 0x06
#define UHCI_FRAME_BASE_REG 0x08
#define UHCI_SOF_MOD_REG 0x0C
#define UHCI_PORT1_REG 0x10
#define UHCI_PORT2_REG 0x12

#define UHCI_CMD_RUN 0x0001
#define UHCI_CMD_HCRESET 0x0002
#define UHCI_CMD_GRESET 0x0004
#define UHCI_CMD_EGSM 0x0008
#define UHCI_CMD_FGR 0x0010
#define UHCI_CMD_MAXP 0x0080

#define UHCI_STATUS_USBINT 0x0001
#define UHCI_STATUS_USBERRINT 0x0002
#define UHCI_STATUS_RD 0x0004
#define UHCI_STATUS_HSE 0x0008
#define UHCI_STATUS_HCPE 0x0010
#define UHCI_STATUS_HCH 0x0020

#define UHCI_PORT_CCS 0x0001
#define UHCI_PORT_CSC 0x0002
#define UHCI_PORT_PED 0x0004
#define UHCI_PORT_PEC 0x0008
#define UHCI_PORT_PO 0x0010
#define UHCI_PORT_PR 0x0200
#define UHCI_PORT_LS 0x0300

typedef struct {
    uint16_t cmd;
    uint16_t status;
    uint16_t int_en;
    uint16_t frame_num;
    uint32_t frame_base;
    uint16_t sof_mod;
    uint16_t reserved1;
    uint16_t port1;
    uint16_t port2;
} __attribute__((packed)) uhci_regs_t;

typedef struct {
    uint32_t link;
    uint32_t actual_len;
    uint32_t status;
    uint32_t buffer;
    uint32_t next_td;
    uint32_t next_qh;
    uint32_t reserved;
} __attribute__((packed)) uhci_td_t;

typedef struct {
    uint32_t link;
    uint32_t element_link;
} __attribute__((packed)) uhci_qh_t;

typedef struct {
    uint8_t *base;
    uhci_regs_t *regs;
    bool initialized;
} uhci_hc_t;

bool uhci_init(usb_hc_t *hc);
int uhci_control_transfer(usb_hc_t *hc, usb_setup_packet_t *setup, void *data, uint16_t len);
int uhci_interrupt_in(usb_hc_t *hc, uint8_t endpoint, void *data, uint16_t len);
void uhci_reset_port(usb_hc_t *hc, int port);

#endif
