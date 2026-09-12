// Copyright (c) 2023-2026 Christiaan (chris@boreddev.nl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
#ifndef IDT_H
#define IDT_H

#include <stdint.h>

void idt_init(void);
void idt_set_gate(uint8_t vector, void *isr, uint16_t cs, uint8_t flags);
void idt_register_interrupts(void);
void idt_load(void);
void pit_setup(void);

struct registers_t;
void idt_register_irq_handler(int irq, uint64_t (*handler)(struct registers_t *regs));

// ISR wrappers defined in assembly
extern void isr0_wrapper(void);  // Timer (IRQ 0)
extern void isr1_wrapper(void);  // Keyboard (IRQ 1)
extern void isr2_wrapper(void);  // Cascade (IRQ 2)
extern void isr3_wrapper(void);  // COM2 (IRQ 3)
extern void isr4_wrapper(void);  // COM1 (IRQ 4)
extern void isr5_wrapper(void);  // PCI (IRQ 5)
extern void isr6_wrapper(void);  // Floppy / PCI (IRQ 6)
extern void isr7_wrapper(void);  // Spurious / Real IRQ 7
extern void isr8_wrapper(void);  // RTC (IRQ 8)
extern void isr9_wrapper(void);  // PCI (IRQ 9)
extern void isr10_wrapper(void); // PCI (IRQ 10)
extern void isr11_wrapper(void); // PCI (IRQ 11)
extern void isr12_wrapper(void); // Mouse (IRQ 12)
extern void isr13_wrapper(void); // Coprocessor (IRQ 13)
extern void isr14_wrapper(void); // Primary ATA (IRQ 14)
extern void isr15_wrapper(void); // Spurious / Real IRQ 15
extern void isr_spurious_lapic_wrapper(void);
extern void isr_default_wrapper(void);
extern void isr128_wrapper(void);
extern void isr_sched_ipi_wrapper(void);
extern void isr_tlb_ipi_wrapper(void);

#endif
