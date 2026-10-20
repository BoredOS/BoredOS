/*
* BoredOS xHCI Host Controller Driver Definitions
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

#ifndef XHCI_DEF_H
#define XHCI_DEF_H

#define TRB_TYPE_NORMAL                      1U
#define TRB_TYPE_LINK                        6U
#define TRB_TYPE_ENABLE_SLOT                 9ULL
#define TRB_TYPE_ADDRESS_DEVICE              11ULL
#define TRB_TYPE_DISABLE_SLOT                10ULL
#define TRB_TYPE_EVALUATE_CONTEXT            13ULL
#define TRB_TYPE_CONFIGURE_EP                12ULL
#define TRB_CMD_NOOP                         23ULL
#define TRB_TRANSFER_EVENT                   32ULL
#define TRB_TYPE_CMD_COMPLETE                33ULL
#define TRB_TYPE_PORT_STATUS                 34ULL

#define XHCI_PORT_REG_SET_OFFSET             0x400ULL
#define XHCI_PORT_REG_STRIDE                 0x10ULL

#define XHCI_PORTSC_CCS                      (1U << 0)
#define XHCI_PORTSC_PED                      (1U << 1)
#define XHCI_PORTSC_PR                       (1U << 4)
#define XHCI_PORTSC_SPEED_SHIFT              10U
#define XHCI_PORTSC_SPEED_MASK               0xFU
#define XHCI_PORTSC_RW1C_MASK                ((1U << 17) | (1U << 18) | (1U << 19) | \
                                              (1U << 20) | (1U << 21) | (1U << 22) | \
                                              (1U << 23))

#define USB_EP0_MAX_PACKET_DEFAULT           8U
#define USB_EP0_MAX_PACKET_HIGHSPEED         64U
#define USB_EP0_MAX_PACKET_SUPERSPEED        512U
#define USB_REQUEST_DIR_IN                   0x80U
#define USB_SETUP_PACKET_SIZE                8U

#define XHCI_INPUT_ADD_SLOT_CONTEXT          (1U << 0)
#define XHCI_INPUT_ADD_EP0_CONTEXT           (1U << 1)

#define XHCI_SLOT_ROOT_PORT_SHIFT            16U
#define XHCI_SLOT_SPEED_SHIFT                20U
#define XHCI_SLOT_CONTEXT_ENTRIES_SHIFT      27U

#define XHCI_EP_TYPE_OUT_SHIFT               0U
#define XHCI_EP_CERR_SHIFT                   1U
#define XHCI_EP_TYPE_SHIFT                   3U
#define XHCI_EP_TYPE_IN_SHIFT                4U
#define XHCI_EP_MAX_PACKET_SHIFT             16U
#define XHCI_EP_CERR_MAX                     3U
#define XHCI_EP_MAX_ERROR_COUNT              3U
#define XHCI_EP_INDEX_0                      1U
#define XHCI_EP_DEFAULT_TRB_LEN              8U
#define XHCI_CTX_EP0_INDEX                   0U

#define XHCI_NO_PENDING_PORT                 (-1)

#define XHCI_TRB_CYCLE_SHIFT                 0U
#define XHCI_TRB_CYCLE_MASK                  0x1U
#define XHCI_TRB_ISP_ENABLE                  (1U << 2)
#define XHCI_TRB_IOC_SHIFT                   5U
#define XHCI_TRB_IOC_ENABLE                  (1U << 5)
#define XHCI_TRB_IDT_SHIFT                   6U
#define XHCI_TRB_TYPE_SHIFT                  10U
#define XHCI_TRB_TYPE_MASK                   0x3FU
#define XHCI_TRB_TRT_SHIFT                   16U
#define XHCI_TRB_DIR_SHIFT                   16U
#define XHCI_TRB_STATUS_DIR_SHIFT            16U
#define XHCI_TRB_SLOT_ID_SHIFT               24U
#define XHCI_TRB_SLOT_ID_MASK                0xFFU
#define XHCI_TRB_COMP_CODE_SHIFT             24U
#define XHCI_TRB_COMP_CODE_MASK              0xFFU

#define XHCI_TRB_DATA_DIR_OUT                0U
#define XHCI_TRB_DATA_DIR_IN                 1U
#define XHCI_TRB_STATUS_DIR_OUT              0U
#define XHCI_TRB_STATUS_DIR_IN               1U

#define TRB_TRANSFER_LEN_MASK                0xFFFFFFU
#define XHCI_TRB_LINK_TC                     (1U << 1)
#define XHCI_TRB_SIZE_BYTES                  16U

#define XHCI_COMP_SUCCESS                    1U
#define XHCI_CODE_SUCCESS                    1U
#define XHCI_COMP_SHORT_PACKET               13U

#define DESC_TYPE_INTERFACE                  0x04U
#define DESC_TYPE_ENDPOINT                   0x05U
#define CONFIG_HEADER_SIZE                   9U
#define MAX_PARSED_ENDPOINTS                 16

#define XHCI_HC_DOORBELL                     0U
#define XHCI_DB_TARGET_HC                    0U
#define XHCI_DB_TARGET_EP0                   1U

#define USB_TOTAL_xHCI_SLOTS                 256U
#define XHCI_CMD_RING_INDEX_LIMIT            255U
#define XHCI_EVENT_RING_TRBS                 256U
#define DEVICE_CONTEXT_ENTRIES               32U
#define XHCI_DEQ_CYCLE_STATE_START           (1ULL << 0)
#define XHCI_COMMAND_RING_CYCLE_START        1U

#define BAR_INDEX_0                          0U
#define MMIO_MAP_PAGES                       16U

#define XHCI_CAP_HCSPARAMS1                  0x04ULL
#define XHCI_HCSPARAMS1_MAX_SLOTS_MASK       0xFFU
#define XHCI_HCSPARAMS1_MAX_PORTS_SHIFT      24U
#define XHCI_HCSPARAMS1_MAX_PORTS_MASK       0xFFU

#define XHCI_CRCR_RCS                        (1ULL << 0)
#define XHCI_ERST_PRIMARY_SEGMENT            0U
#define XHCI_PRIMARY_INTERRUPTER             0U
#define XHCI_ERST_SINGLE_SEGMENT             1U
#define XHCI_ERDP_EHB                        (1ULL << 3)

#define XHCI_IMAN_IP                         (1U << 0)
#define XHCI_IMAN_IE                         (1U << 1)
#define XHCI_IMOD_1MS_INTERVAL               4000U

#define XHCI_CMD_RUN_STOP                    (1U << 0)
#define XHCI_CMD_HCRST                       (1U << 1)
#define XHCI_CMD_INTE                        (1U << 2)
#define XHCI_CMD_HSEE                        (1U << 3)

#define XHCI_STS_HCHALTED                    (1U << 0)
#define XHCI_STS_CNR                         (1U << 11)

#define XHCI_MAX_EVENTS_PER_INTR             16U

#define ENDPOINT_ID_SHIFT                    16U
#define ENDPOINT_ID_BITMASK                  0x1F0000ULL

#define USB_XHCI_DEVICE_DESC_SIZE           18
#define USB_XHCI_REQ_DIR_IN                 0x80
#define USB_XHCI_REQ_DIR_OUT                0x0
#define USB_XHCI_REQ_SET_TYPE_CONFIG        0x9
#define USB_XHCI_REQ_TYPE_STANDARD          0x0
#define USB_XHCI_REQ_REC_DEV                0x0
#define USB_XHCI_REQ_GET_DESC               0x6
#define USB_XHCI_DESC_TYPE_DEV              0x1
#define USB_XHCI_DESC_TYPE_CONF             0x2
#define USB_XHCI_DESC_TYPE_STR              0x3
#define USB_XHCI_DESC_TYPE_INTERF           0x4
#define USB_XHCI_DESC_TYPE_ENDPOINT         0x5
#define USB_XHCI_DEFAULT_CONF_VAL           1
#define USB_XHCI_EP_NUM_MASK                0xF
#define USB_XHCI_EP_DIR_IN                  0x80
#define USB_XHCI_EP_TYPE_MASK               0x3

#endif