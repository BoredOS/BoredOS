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
#include "input/keyboard.h"
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
static void ps2_kbd_wait_write(void) {
    uint32_t timeout = 100000;
    while (timeout--) {
        if ((inb(0x64) & 2) == 0) return;
    }
}

static void ps2_update_leds(void) {
    uint8_t led_status = 0;
    uint32_t mods = keyboard_get_modifiers();

    if (mods & KB_MOD_CAPS)   led_status |= 4;
    if (mods & KB_MOD_NUM)    led_status |= 2;
    if (mods & KB_MOD_SCROLL) led_status |= 1;

    ps2_kbd_wait_write();
    outb(0x60, 0xED);
    ps2_kbd_wait_write();
    outb(0x60, led_status);
}

uint64_t keyboard_handler(registers_t *regs) {
    uint8_t scancode = inb(0x60);

    keyboard_event_t ev;
    if (keyboard_handle_set1_scancode(scancode, &ev)) {
        // Update LEDs if a lock key state changed
        if (ev.keycode == KEY_CAPS_LOCK || ev.keycode == KEY_NUM_LOCK || ev.keycode == KEY_SCROLL_LOCK) {
            if (ev.pressed) ps2_update_leds();
        }

        wm_handle_key_event(ev.keycode, ev.codepoint, ev.mods, ev.pressed);
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
    keymap_init();
    keyboard_init();
    mouse_init();
    ps2_update_leds();
}

bool ps2_shift_pressed(void) {
    return keyboard_shift_pressed();
}