// Copyright (c) 2023-2026 Chris (boreddevnl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
#include "ps2.h"
#include "io.h"
#include "wm.h"
#include "network.h"
#include "lapic.h"
#include "smp.h"
#include <stdbool.h>
#include "input/keymap.h"

extern void serial_print(const char *s);
extern void serial_print_hex(uint64_t n);

// --- Timer Handler ---
volatile uint64_t kernel_ticks = 0;

uint64_t timer_handler(registers_t *regs) {
    if (smp_this_cpu_id() == 0) {
        kernel_ticks++;
        wm_timer_tick();
        network_process_frames();
        
        extern void k_beep_process(void);
        k_beep_process();
    }
    
    outb(0x20, 0x20); 
    extern uint64_t process_schedule(uint64_t current_rsp);
    uint64_t new_rsp = process_schedule((uint64_t)regs);
    
    if (smp_cpu_count() > 1) {
        lapic_send_ipi_all();
    }
    
    return new_rsp;
}

// --- Keyboard ---
static bool shift_pressed = false;
static bool caps_lock_on = false;
bool ps2_ctrl_pressed = false;
static bool extended_scancode = false;

static void ps2_kbd_wait_write(void) {
    uint32_t timeout = 100000;
    while (timeout--) {
        if ((inb(0x64) & 2) == 0) return;
    }
}

static void ps2_update_leds(void) {
    uint8_t led_status = 0;
    if (caps_lock_on) led_status |= 4;
    
    ps2_kbd_wait_write();
    outb(0x60, 0xED);
    ps2_kbd_wait_write();
    outb(0x60, led_status);
}

uint64_t keyboard_handler(registers_t *regs) {
    uint8_t scancode = inb(0x60);

    if (scancode == 0xE0) {
        extended_scancode = true;
        outb(0x20, 0x20);
        return (uint64_t)regs;
    }

    if (scancode == 0x1D) {
        ps2_ctrl_pressed = true;
        extended_scancode = false; 
    } else if (scancode == 0x9D) {
        ps2_ctrl_pressed = false;
        extended_scancode = false;
    }



    if (scancode == 0x2A || scancode == 0x36) { // Shift Down
        shift_pressed = true;
    } else if (scancode == 0xAA || scancode == 0xB6) { // Shift Up
        shift_pressed = false;
    } else if (scancode == 0x3A) { // Caps Lock Down
        caps_lock_on = !caps_lock_on;
        ps2_update_leds();
    } else if (!(scancode & 0x80)) { // Key Press
        if (extended_scancode) {
            extended_scancode = false;
            switch (scancode) {
                case 0x48: wm_handle_key(17, true); break; // Up arrow
                case 0x50: wm_handle_key(18, true); break; // Down arrow
                case 0x4B: wm_handle_key(19, true); break; // Left arrow
                case 0x4D: wm_handle_key(20, true); break; // Right arrow
            }
        } else {
            char c = keymap_translate(scancode, shift_pressed, caps_lock_on);
            if (c) {
                if (caps_lock_on) {
                    if (c >= 'a' && c <= 'z') c -= 32;
                    else if (c >= 'A' && c <= 'Z') c += 32;
                }
                wm_handle_key(c, true);
            }
        }
    } else if (scancode & 0x80) { // Key release
        if (extended_scancode) {
            extended_scancode = false;
            switch (scancode & 0x7F) { // Strip the release bit
                case 0x48: wm_handle_key(17, false); break; // Up arrow
                case 0x50: wm_handle_key(18, false); break; // Down arrow
                case 0x4B: wm_handle_key(19, false); break; // Left arrow
                case 0x4D: wm_handle_key(20, false); break; // Right arrow
            }
        } else {
            uint8_t base_scancode = scancode & 0x7F;
            char c = keymap_translate(base_scancode, shift_pressed, caps_lock_on);
            if (c) {
                if (caps_lock_on) {
                    if (c >= 'a' && c <= 'z') c -= 32;
                    else if (c >= 'A' && c <= 'Z') c += 32;
                }
                wm_handle_key(c, false);
            }
        }
    }

    outb(0x20, 0x20); // EOI
    return (uint64_t)regs;
}

// --- Mouse ---
static uint8_t mouse_cycle = 0;
static int8_t mouse_byte[4];
static bool mouse_has_wheel = false;

void mouse_wait(uint8_t type) {
    uint32_t timeout = 100000;
    if (type == 0) { // Write
        while (timeout--) {
            if ((inb(0x64) & 2) == 0) return;
        }
    } else { // Read
        while (timeout--) {
            if ((inb(0x64) & 1) == 1) return;
        }
    }
}

void mouse_write(uint8_t write) {
    mouse_wait(0);
    outb(0x64, 0xD4);
    mouse_wait(0);
    outb(0x60, write);
}

uint8_t mouse_read(void) {
    mouse_wait(1);
    return inb(0x60);
}

void mouse_init(void) {
    uint8_t status;
    
    // Enable Aux Device
    mouse_wait(0);
    outb(0x64, 0xA8);
    
    // Enable Interrupts
    mouse_wait(0);
    outb(0x64, 0x20);
    mouse_wait(1);
    status = inb(0x60) | 2;
    mouse_wait(0);
    outb(0x64, 0x60);
    mouse_wait(0);
    outb(0x60, status);
    
    // Set Defaults
    mouse_write(0xF6);
    mouse_read();

    // Enable Wheel
    mouse_write(0xF3); mouse_read(); mouse_write(200); mouse_read();
    mouse_write(0xF3); mouse_read(); mouse_write(100); mouse_read();
    mouse_write(0xF3); mouse_read(); mouse_write(80); mouse_read();
    
    mouse_write(0xF2);
    mouse_read();
    uint8_t id = mouse_read();
    if (id == 3) mouse_has_wheel = true;
    
    // Enable Streaming
    mouse_write(0xF4);
    mouse_read();
}

uint64_t mouse_handler(registers_t *regs) {
    uint8_t status = inb(0x64);
    if (!(status & 0x20)) {
        outb(0x20, 0x20);
        outb(0xA0, 0x20);
        return (uint64_t)regs; 
    }

    uint8_t b = inb(0x60);
    
    if (mouse_cycle == 0) {
        if ((b & 0x08) == 0) {
            // Out of sync
        } else {
            mouse_byte[0] = b;
            mouse_cycle++;
        }
    } else if (mouse_cycle == 1) {
        mouse_byte[1] = b;
        mouse_cycle++;
    } else if (mouse_cycle == 2) {
        mouse_byte[2] = b;
        if (mouse_has_wheel) {
            mouse_cycle++;
        } else {
            mouse_cycle = 0;
            int8_t dx = mouse_byte[1];
            int8_t dy = mouse_byte[2];
            wm_handle_mouse(dx, -dy, mouse_byte[0] & 0x07, 0);
        }
    } else if (mouse_cycle == 3) {
        mouse_byte[3] = b;
        mouse_cycle = 0;
        int8_t dx = mouse_byte[1];
        int8_t dy = mouse_byte[2];
        int8_t dz = mouse_byte[3];
        wm_handle_mouse(dx, -dy, mouse_byte[0] & 0x07, -dz);
    }

    outb(0x20, 0x20);
    outb(0xA0, 0x20);
    return (uint64_t)regs;
}

void ps2_init(void) {
    mouse_init();
}

bool ps2_shift_pressed(void) {
    return shift_pressed;
}
