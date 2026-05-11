// Copyright (c) 2023-2026 Chris (boreddevnl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in this file it has in it, as per the GPL license terms.
#ifndef SYSCALL_H
#define SYSCALL_H

#include <stdint.h>

#define SYSTEM_CMD_PCI_READ_CONFIG 78
#define SYSTEM_CMD_PCI_WRITE_CONFIG 79
#define SYSTEM_CMD_IO_INB 80
#define SYSTEM_CMD_IO_OUTB 81
#define SYSTEM_CMD_IO_INW 82
#define SYSTEM_CMD_IO_OUTW 83
#define SYSTEM_CMD_IO_INL 84
#define SYSTEM_CMD_IO_OUTL 85
#define SYSTEM_CMD_MEM_MAP_PHYS 86
#define SYSTEM_CMD_MEM_UNMAP 87
#define SYSTEM_CMD_WM_HANDLE_MOUSE 88

static inline uint64_t syscall(uint64_t num, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5) {
    uint64_t ret;
    asm volatile ("syscall"
                 : "=a"(ret)
                 : "a"(num), "D"(arg1), "S"(arg2), "d"(arg3), "r"(arg4), "r"(arg5)
                 : "rcx", "r11", "memory");
    return ret;
}

static inline uint32_t pci_read_config(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset) {
    return syscall(SYSTEM_CMD_PCI_READ_CONFIG, 0, bus, device, function, offset);
}

static inline void pci_write_config(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint32_t value) {
    syscall(SYSTEM_CMD_PCI_WRITE_CONFIG, 0, bus, device, function, offset);
}

static inline uint8_t inb(uint16_t port) {
    return syscall(SYSTEM_CMD_IO_INB, 0, port, 0, 0, 0);
}

static inline void outb(uint16_t port, uint8_t value) {
    syscall(SYSTEM_CMD_IO_OUTB, 0, port, value, 0, 0);
}

static inline uint16_t inw(uint16_t port) {
    return syscall(SYSTEM_CMD_IO_INW, 0, port, 0, 0, 0);
}

static inline void outw(uint16_t port, uint16_t value) {
    syscall(SYSTEM_CMD_IO_OUTW, 0, port, value, 0, 0);
}

static inline uint32_t inl(uint16_t port) {
    return syscall(SYSTEM_CMD_IO_INL, 0, port, 0, 0, 0);
}

static inline void outl(uint16_t port, uint32_t value) {
    syscall(SYSTEM_CMD_IO_OUTL, 0, port, value, 0, 0);
}

static inline uint64_t p2v(uint64_t phys) {
    return syscall(SYSTEM_CMD_MEM_MAP_PHYS, 0, phys, 0, 0, 0);
}

static inline void mem_unmap(uint64_t virt) {
    syscall(SYSTEM_CMD_MEM_UNMAP, 0, virt, 0, 0, 0);
}

static inline void wm_handle_mouse(int dx, int dy, uint8_t buttons, int wheel) {
    syscall(SYSTEM_CMD_WM_HANDLE_MOUSE, 0, dx, dy, buttons, wheel);
}

#endif
