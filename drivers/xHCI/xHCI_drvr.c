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

#include "xHCI_drvr.h"
#include "kconsole.h"
#include "slab.h"

#include <stddef.h>

typedef struct {
    xHCI_CCSCPR_t type;
    xHCI_EXTD_t *tmpl;
} xHCI_DE_t;

// Currently under the assumption registration happen early (pre mass storage controller)
static xHCI_DE_t drvr_table[XHCI_MAX_DRVRS];
static int drvr_count = 0;

int xHCI_submit_new_drvr(xHCI_CCSCPR_t type, xHCI_EXTD_t *tmpl) { 
    if (!tmpl || !tmpl->initialize || !tmpl->on_int) {
        serial_write("[xHCI] Warning: Missing template or callbacks\n");
        return -1;
    }

    if (drvr_count >= XHCI_MAX_DRVRS) {
        serial_write("[xHCI] Warning: Driver table full\n");
        return -1;
    }

    drvr_table[drvr_count].type = type;
    drvr_table[drvr_count].tmpl = tmpl;
    drvr_count++;

    __asm__ volatile("mfence" ::: "memory");

    serial_write("[xHCI] Registered driver for class 0x");
    serial_write_hex(type.cc);
    serial_write("\n");
    return 0;
}

// 0 = no match
// 1 = weak match
// 2/3 for better
static int xhci_match_score(const xHCI_CCSCPR_t *t, uint8_t cc, uint8_t sc, uint8_t prot) {
    int score = 1;

    if (t->cc != cc)
        return 0;

    if (t->match & XHCI_MATCH_SC) {
        if (t->sc != sc)
            return 0;
        score++;
    }

    if (t->match & XHCI_MATCH_PROT) {
        if (t->prot != prot)
            return 0;
        score++;
    }

    return score;
}

xHCI_EXTD_t *xHCI_load_new_drvr(uint8_t cc, uint8_t sc, uint8_t prot) {
    int best = -1;
    int best_score = 0;

    for (int i = 0; i < drvr_count; ++i) {
        int s = xhci_match_score(&drvr_table[i].type, cc, sc, prot);
        if (s > best_score) {
            best_score = s;
            best = i;
        }
    }

    if (best < 0) {
        if (cc == XHCI_CL_USB_HUB)
            serial_write("[xHCI] Discovered USB hub (no driver)\n");
        else if (cc == XHCI_CL_USB_HID)
            serial_write("[xHCI] Discovered HID device (no driver)\n");
        else {
            serial_write("[xHCI] Discovered unknown USB device: ");
            serial_write_hex(cc);
            serial_write(", ");
            serial_write_hex(sc);
            serial_write(", ");
            serial_write_hex(prot);  
            serial_write("\n");
        }
        return NULL;
    }

    xHCI_EXTD_t *inst = kzalloc(sizeof(xHCI_EXTD_t));
    if (!inst) {
        serial_write("[xHCI] Out of memory instantiating driver\n");
        return NULL;
    }

    *inst = *drvr_table[best].tmpl;
    inst->local_data = NULL;

    serial_write("[xHCI] Driver match for class 0x");
    serial_write_hex(cc);
    serial_write(", instance at 0x");
    serial_write_hex((uint64_t)inst);
    serial_write("\n");

    return inst;
}

void xHCI_unload_drvr(xHCI_EXTD_t *inst) {
    if (!inst)
        return;

    if (inst->destroy)
        inst->destroy(inst);

    kfree(inst);
}