// Copyright (c) 2023-2026 Christiaan (chris@boreddev.nl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
#include "lapic.h"
#include "platform.h"
#include "spinlock.h"
#include "io.h"
#include "vmm.h"
#include <stddef.h>
#include <stdbool.h>

extern void serial_write(const char *str);
extern void serial_write_num(uint32_t n);

// LAPIC base address via HHDM (in xAPIC mode)
static volatile uint32_t *lapic_base = 0;
static spinlock_t lapic_lock = SPINLOCK_INIT;
static uint32_t lapic_ticks_per_ms = 0;

static bool is_x2apic(void) {
    return (rdmsr(0x1B) & (1ULL << 10)) != 0;
}

bool try_enable_x2apic(void) {
    if (is_x2apic()) return true;
    uint32_t eax, ebx, ecx, edx;
    asm volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(1));
    if (ecx & (1 << 21)) {
        uint64_t base = rdmsr(0x1B);
        wrmsr(0x1B, base | (1ULL << 11) | (1ULL << 10));
        return is_x2apic();
    }
    return false;
}

static inline void lapic_write(uint32_t reg_offset, uint32_t val) {
    if (is_x2apic()) {
        wrmsr(0x800 + (reg_offset >> 4), (uint64_t)val);
    } else if (lapic_base) {
        lapic_base[reg_offset / 4] = val;
    }
}

static inline uint32_t lapic_read(uint32_t reg_offset) {
    if (is_x2apic()) {
        return (uint32_t)rdmsr(0x800 + (reg_offset >> 4));
    } else if (lapic_base) {
        return lapic_base[reg_offset / 4];
    }
    return 0;
}

void lapic_enable(void) {
    try_enable_x2apic();

    // 1. Accept all interrupt priorities
    lapic_write(0x080, 0);

    // 2. Enable LAPIC: Bit 8 = Software Enable, Vector = 0xFF (spurious)
    lapic_write(0x0F0, 0x1FF);

    // 3. Configure Local Interrupt pins (LINT0 / LINT1)
    // Bit 8 of IA32_APIC_BASE indicates if this processor is the BSP
    bool is_bsp = (rdmsr(0x1B) & (1ULL << 8)) != 0;
    if (is_bsp) {
        // BSP: Enable Virtual Wire Mode so 8259 PIC legacy interrupts (like IRQ 1 keyboard)
        // are routed through the local APIC to CPU 0.
        // LINT0: Delivery Mode = 111b (ExtINT), Unmasked (bit 16 = 0) => 0x00000700
        lapic_write(0x350, 0x00000700);
        // LINT1: Delivery Mode = 100b (NMI), Unmasked (bit 16 = 0) => 0x00000400
        lapic_write(0x360, 0x00000400);
    } else {
        // AP: Mask LINT0, set LINT1 to NMI
        lapic_write(0x350, 0x00010000);
        lapic_write(0x360, 0x00000400);
        // Mask local timer so AP only responds to scheduling IPIs
        lapic_write(0x320, 0x00010000 | 32);
    }
}

static uint32_t lapic_calibrate_timer(void) {
    uint64_t rflags;
    asm volatile("pushfq; popq %0; cli" : "=r"(rflags));

    lapic_write(0x3E0, 0x03);
    lapic_write(0x320, 0x00010000 | 32);


    outb(0x43, 0x30);
    io_wait();
    outb(0x40, 11932 & 0xFF);
    io_wait();
    outb(0x40, (11932 >> 8) & 0xFF);
    io_wait();

    uint32_t timeout = 10000000;
    while (--timeout) {
        outb(0x43, 0x00);
        uint8_t lo = inb(0x40);
        uint8_t hi = inb(0x40);
        uint16_t cur = ((uint16_t)hi << 8) | lo;
        if (cur <= 11932) {
            break;
        }
    }

    // Start LAPIC timer counting down from max
    lapic_write(0x380, 0xFFFFFFFF);

    uint16_t prev = 11932;
    while (--timeout) {
        outb(0x43, 0x00);
        uint8_t lo = inb(0x40);
        uint8_t hi = inb(0x40);
        uint16_t cur = ((uint16_t)hi << 8) | lo;
        if (cur > prev) {
            break;
        }
        prev = cur;
    }

    uint32_t elapsed = 0xFFFFFFFF - lapic_read(0x390);

    extern void pit_setup(void);
    pit_setup();

    asm volatile("pushq %0; popfq" : : "r"(rflags));

    if (timeout > 0 && elapsed > 1000 && elapsed < 0xFF000000) {
        return elapsed / 10;
    }

    uint32_t rtc_to = 20000000;
    outb(0x70, 0x0A);
    while ((inb(0x71) & 0x80) && --rtc_to) asm volatile("pause");
    outb(0x70, 0x0A);
    while (!(inb(0x71) & 0x80) && --rtc_to) asm volatile("pause");
    outb(0x70, 0x0A);
    while ((inb(0x71) & 0x80) && --rtc_to) asm volatile("pause");

    lapic_write(0x380, 0xFFFFFFFF);

    outb(0x70, 0x0A);
    while (!(inb(0x71) & 0x80) && --rtc_to) asm volatile("pause");
    outb(0x70, 0x0A);
    while ((inb(0x71) & 0x80) && --rtc_to) asm volatile("pause");

    uint32_t rtc_elapsed = 0xFFFFFFFF - lapic_read(0x390);
    if (rtc_to > 0 && rtc_elapsed > 10000 && rtc_elapsed < 0xFF000000) {
        return rtc_elapsed / 1000;
    }

    // Default fallback
    return 6250;
}

void lapic_init(void) {
    if (try_enable_x2apic()) {
        lapic_enable();
        serial_write("[LAPIC] Initialized in x2APIC mode\n");
    } else {
        uint64_t apic_base_msr = rdmsr(0x1B);
        uint64_t apic_phys = apic_base_msr & 0xFFFFF000ULL;
        if (apic_phys == 0) apic_phys = 0xFEE00000ULL;

        lapic_base = (volatile uint32_t *)ioremap(apic_phys, 0x1000);
        if (!lapic_base) {
            extern uint64_t hhdm_offset;
            lapic_base = (volatile uint32_t *)(hhdm_offset + apic_phys);
        }
        lapic_enable();
        serial_write("[LAPIC] Initialized at MMIO 0xFEE00000\n");
    }

    lapic_ticks_per_ms = lapic_calibrate_timer();
    serial_write("[LAPIC] Calibrated timer: ");
    serial_write_num(lapic_ticks_per_ms);
    serial_write(" ticks/ms\n");
}

void lapic_timer_start(void) {
    if (lapic_ticks_per_ms == 0) {
        lapic_ticks_per_ms = 6250;
    }

    // Set divide to 16
    lapic_write(0x3E0, 0x03);

    // Periodic mode (bit 17 = 1), Vector 32, Unmasked (bit 16 = 0) => 0x00020020
    lapic_write(0x320, 0x00020000 | 32);

    // Initial count for 1ms tick (1000Hz)
    lapic_write(0x380, lapic_ticks_per_ms);

    // Mask legacy PIT IRQ 0 on 8259 PIC so we don't get double interrupts
    outb(0x21, inb(0x21) | 0x01);

    serial_write("[LAPIC] Periodic timer started at 1000Hz (vector 32)\n");
}

void lapic_eoi(void) {
    if (is_x2apic()) {
        wrmsr(0x80B, 0);
        return;
    }
    if (lapic_base) {
        lapic_base[0x0B0 / 4] = 0;
    }
}

void lapic_send_ipi_all(void) {
    if (is_x2apic()) {
        uint64_t icr = IPI_SCHED_VECTOR | (0b11ULL << 18);
        uint64_t rflags = spinlock_acquire_irqsave(&lapic_lock);
        wrmsr(0x830, icr);
        spinlock_release_irqrestore(&lapic_lock, rflags);
        return;
    }
    if (!lapic_base) return;
    
    uint32_t icr_low = IPI_SCHED_VECTOR | (0b11 << 18);
    
    uint64_t rflags = spinlock_acquire_irqsave(&lapic_lock);
    lapic_base[0x300 / 4] = icr_low;
    while (lapic_base[0x300 / 4] & (1 << 12)) {}
    spinlock_release_irqrestore(&lapic_lock, rflags);
}

void lapic_send_ipi(uint32_t lapic_id, uint8_t vector) {
    if (is_x2apic()) {
        uint64_t icr = ((uint64_t)lapic_id << 32) | vector;
        uint64_t rflags = spinlock_acquire_irqsave(&lapic_lock);
        wrmsr(0x830, icr);
        spinlock_release_irqrestore(&lapic_lock, rflags);
        return;
    }
    if (!lapic_base) return;
    uint32_t icr_low = (uint32_t)vector; 

    uint64_t rflags = spinlock_acquire_irqsave(&lapic_lock);
    lapic_base[0x310 / 4] = (lapic_id << 24);
    lapic_base[0x300 / 4] = icr_low;
    while (lapic_base[0x300 / 4] & (1 << 12)) {}
    spinlock_release_irqrestore(&lapic_lock, rflags);
}
