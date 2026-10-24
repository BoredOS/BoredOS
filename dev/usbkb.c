/*
* BoredOS HID USB Keyboard
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

#include "pmm.h"
#include "tty.h"
#include "xHCI.h"
#include "usbms.h"
#include "usbkb.h"
#include "kconsole.h"
#include "slab.h"
#include "xHCI_drvr.h"
#include "process.h"
#include <string.h>

typedef struct {
    char normal;
    char shifted;
} KeyPair;

static const KeyPair usb_keymap[256] = {
    [0x4] = {'a', 'A'}, [0x5] = {'b', 'B'}, [0x6] = {'c', 'C'},
    [0x7] = {'d', 'D'}, [0x8] = {'e', 'E'}, [0x9] = {'f', 'F'},
    [0xA] = {'g', 'G'}, [0xB] = {'h', 'H'}, [0xC] = {'i', 'I'},
    [0xD] = {'j', 'J'}, [0xE] = {'k', 'K'}, [0xF] = {'l', 'L'},
    [0x10] = {'m', 'M'}, [0x11] = {'n', 'N'}, [0x12] = {'o', 'O'},
    [0x13] = {'p', 'P'}, [0x14] = {'q', 'Q'}, [0x15] = {'r', 'R'},
    [0x16] = {'s', 'S'}, [0x17] = {'t', 'T'}, [0x18] = {'u', 'U'},
    [0x19] = {'v', 'V'}, [0x1A] = {'w', 'W'}, [0x1B] = {'x', 'X'},
    [0x1C] = {'y', 'Y'}, [0x1D] = {'z', 'Z'},

    [0x1E] = {'1', '!'}, [0x1F] = {'2', '@'}, [0x20] = {'3', '#'},
    [0x21] = {'4', '$'}, [0x22] = {'5', '%'}, [0x23] = {'6', '^'},
    [0x24] = {'7', '&'}, [0x25] = {'8', '*'}, [0x26] = {'9', '('},
    [0x27] = {'0', ')'},

    [0x28] = {'\n', '\n'},
    [0x29] = {0x1B, 0x1B},
    [0x2A] = {'\b', '\b'},
    [0x2B] = {'\t', '\t'},
    [0x2C] = {' ', ' '},
    [0x2D] = {'-', '_'},
    [0x2E] = {'=', '+'},
    [0x2F] = {'[', '{'},
    [0x30] = {']', '}'},
    [0x31] = {'\\', '|'},
    [0x33] = {';', ':'},
    [0x34] = {'\'', '"'},
    [0x35] = {'`', '~'},
    [0x36] = {',', '<'},
    [0x37] = {'.', '>'},
    [0x38] = {'/', '?'},

    [0x54] = {'/', '/'},  
    [0x55] = {'*', '*'},  
    [0x56] = {'-', '-'},
    [0x57] = {'+', '+'},  
    [0x58] = {'\n', '\n'}, 
    [0x59] = {'1', '1'},
    [0x5A] = {'2', '2'},  
    [0x5B] = {'3', '3'},  
    [0x5C] = {'4', '4'},
    [0x5D] = {'5', '5'},  
    [0x5E] = {'6', '6'},  
    [0x5F] = {'7', '7'},
    [0x60] = {'8', '8'},  
    [0x61] = {'9', '9'},  
    [0x62] = {'0', '0'},
    [0x63] = {'.', '.'}
};

KeyPair usb_keymap_get(uint8_t scancode) {
    return usb_keymap[scancode];
}

void kbusb_process_report(USB_kbbr_t *report, USB_kbd_t *this) {
    uint8_t mods = report->modifiers;
    bool ctrl_pressed  = (mods & (MOD_LEFT_CTRL | MOD_RIGHT_CTRL)) != 0;
    bool alt_pressed   = (mods & (MOD_LEFT_ALT | MOD_RIGHT_ALT)) != 0;
    bool shift_pressed = (mods & (MOD_LEFT_SHIFT | MOD_RIGHT_SHIFT)) != 0;

    int tty_id = tty_get_active_id();

    for (int i = 0; i < 6; ++i) {
        uint8_t code = report->keycodes[i];
        if (code == 0) continue;

        bool is_new_press = true;
        for (int j = 0; j < 6; ++j) {
            if (this->last_report.keycodes[j] == code) {
                is_new_press = false;
                break;
            }
        }

        if (is_new_press) {
            tty_push_key(tty_id, code);

            if (ctrl_pressed && alt_pressed && code >= XUSB_KEY_F1 && code <= XUSB_KEY_F10) {
                int target = code - XUSB_KEY_F1;
                tty_switch(target);
                continue;
            }

            if (tty_get_blit_enabled()) {
                KeyPair pair = usb_keymap_get(code);
                char base_char = shift_pressed ? pair.shifted : pair.normal;

                if (code == XUSB_KEY_UP) {
                    tty_push_char(tty_id, 0x1B);
                    tty_push_char(tty_id, '[');
                    tty_push_char(tty_id, 'A');
                } else if (code == XUSB_KEY_DOWN) {
                    tty_push_char(tty_id, 0x1B);
                    tty_push_char(tty_id, '[');
                    tty_push_char(tty_id, 'B');
                } else if (code == XUSB_KEY_LEFT) {
                    tty_push_char(tty_id, 0x1B);
                    tty_push_char(tty_id, '[');
                    tty_push_char(tty_id, 'D');
                } else if (code == XUSB_KEY_RIGHT) {
                    tty_push_char(tty_id, 0x1B);
                    tty_push_char(tty_id, '[');
                    tty_push_char(tty_id, 'C');
                } 
                else if (ctrl_pressed && !alt_pressed) {
                    char lower_c = (base_char >= 'A' && base_char <= 'Z') ? base_char + 32 : base_char;
                    if (lower_c == 'c') {
                        int fg_pid = tty_get_foreground(tty_id);
                        if (fg_pid > 0) {
                            extern process_t *process_get_by_pid(uint32_t pid);
                            process_t *proc = process_get_by_pid((uint32_t)fg_pid);
                            if (proc) {
                                proc->signal_pending |= SIGINT;
                                extern void process_put(process_t *proc);
                                process_put(proc);
                            }
                        } else {
                            tty_push_char(tty_id, 0x3);
                        }
                    } else if (base_char >= 'a' && base_char <= 'z') {
                        tty_push_char(tty_id, (uint8_t)(base_char - 0x60));
                    } else if (base_char >= 'A' && base_char <= 'Z') {
                        tty_push_char(tty_id, (uint8_t)(base_char - 0x40));
                    } else if (base_char == '[') {
                        tty_push_char(tty_id, 0x1B);
                    } else if (base_char == '\\') {
                        tty_push_char(tty_id, 0x1C);
                    } else if (base_char == ']') {
                        tty_push_char(tty_id, 0x1D);
                    } else if (base_char == '^') {
                        tty_push_char(tty_id, 0x1E);
                    } else if (base_char == '_') {
                        tty_push_char(tty_id, 0x1F);
                    } else if (base_char == '?') {
                        tty_push_char(tty_id, 0x7F);
                    }
                }
                else if (alt_pressed) {
                    tty_push_char(tty_id, 0x1B);
                    char utf8_buf[5];
                    int utf8_len = text_encode_utf8((uint32_t)base_char, utf8_buf);
                    for (int k = 0; k < utf8_len; k++) {
                        tty_push_char(tty_id, (uint8_t)utf8_buf[k]);
                    }
                }
                else if (base_char != 0) {
                    char utf8_buf[5];
                    int utf8_len = text_encode_utf8((uint32_t)base_char, utf8_buf);
                    for (int k = 0; k < utf8_len; k++) {
                        tty_push_char(tty_id, (uint8_t)utf8_buf[k]);
                    }
                }
            }
        }
    }

    this->last_report = *report;
}

void kbusb_initialize(xHCI_EXTD_t *this, xHCI_PCI_t *host, uint8_t slot, void *ep_ptr, int ep_count) {
    USB_kbd_t *this_keyboard = kzalloc(sizeof(USB_kbd_t));
    this_keyboard->xHCI_ctrl = host;

    this->local_data = this_keyboard;
    this_keyboard->slot_id = slot;

    xHCI_PE_t *parsed_eps = (xHCI_PE_t *)(ep_ptr);

    for (int i = 0; i < ep_count; i++) {
        uint8_t ep_addr = parsed_eps[i].address;
        bool is_in = (ep_addr & XUSB_EP_DIR_MASK) != 0;
        uint8_t ep_type = parsed_eps[i].attributes & XUSB_EP_ATTR_MASK;

        if (ep_type == XUSB_EP_ATTR_INT && is_in) {
            this_keyboard->int_in = ep_addr;
            break; 
        }
    }

    if (!this_keyboard->int_in) {
        serial_write("[USBKB] Error: Keyboard missing int-in endpoint?\n");
        return;
    }

    xHCI_send_req(host, slot, 0x21, 0xB, 0, 0, 0, 0); // Boot mode config
    xHCI_send_req(host, slot, 0x21, 0xA, 0, 0, 0, 0);
    
    page_t *allocd = pmm_alloc_page(PMM_ZONE_DMA32);;
    this_keyboard->report_phys = pmm_page_to_paddr(allocd);
    this_keyboard->report_virt = pmm_page_to_vaddr(allocd);

    memset(this_keyboard->report_virt, 0, sizeof(USB_kbbr_t));
    memset(&this_keyboard->last_report, 0, sizeof(USB_kbbr_t));

    serial_write("[USBKB] Configured boot keyboard driver\n");
}

void kbusb_start(xHCI_EXTD_t *this) {
    serial_write("[USBKB] Starting HID USB Keyboard polling loop\n");
    USB_kbd_t *this_usb = this->local_data;

    xHCI_queue_transfer(this_usb->xHCI_ctrl, this_usb->slot_id, this_usb->int_in, this_usb->report_phys, sizeof(USB_kbbr_t));
}

void kbusb_on_int(xHCI_EXTD_t *this, uint32_t bytes_transferred, uint32_t endpoint_id, uint64_t param_event) {
    (void)param_event;
    USB_kbd_t *this_usb = this->local_data;

    if (endpoint_id != this_usb->int_in) {
        return;
    }

    if (bytes_transferred <= sizeof(USB_kbbr_t)) {
        kbusb_process_report(this_usb->report_virt, this_usb);
    }

    xHCI_queue_transfer(this_usb->xHCI_ctrl, this_usb->slot_id, this_usb->int_in, this_usb->report_phys, sizeof(USB_kbbr_t));
}

void usbkb_init(void) {
    serial_write("[USBKB] Submitting keyboard boot HID driver\n");

    xHCI_EXTD_t *xHCI_dev = kzalloc(sizeof(xHCI_EXTD_t));
    xHCI_dev->initialize = kbusb_initialize;
    xHCI_dev->start = kbusb_start;
    xHCI_dev->on_int = kbusb_on_int;

    xHCI_CCSCPR_t classes = {
        .cc = XHCI_CL_USB_HID,
        .prot = XHCI_PR_HID_KB,
        .sc = XHCI_BOOT_SUBCL,
        .match = XHCI_MATCH_SC | XHCI_MATCH_PROT
    };

    xHCI_submit_new_drvr(classes, xHCI_dev);
}