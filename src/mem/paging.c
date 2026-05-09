// Copyright (c) 2023-2026 Chris (boreddevnl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
#include "paging.h"
#include "memory_manager.h"
#include "platform.h"
#include <stddef.h>

static uint64_t current_pml4_phys = 0;

// Get current CR3 value
static uint64_t read_cr3(void) {
    uint64_t cr3;
    asm volatile("mov %%cr3, %0" : "=r"(cr3));
    return cr3;
}

// Set CR3 value
static void write_cr3(uint64_t cr3) {
    asm volatile("mov %0, %%cr3" : : "r"(cr3));
}

// Helper to allocate a page table and clear it
static uint64_t alloc_page_table_phys(void) {
    void *ptr = kmalloc_aligned(PAGE_SIZE, PAGE_SIZE);
    if (!ptr) return 0;
    
    page_table_t* table = (page_table_t*)ptr;
    
    // Clear table 
    for (int i = 0; i < 512; i++) {
        table->entries[i] = 0;
    }
    
    // Return the physical address of this table
    return v2p((uint64_t)table);
}

void paging_init(void) {

    current_pml4_phys = read_cr3() & PT_ADDR_MASK;
}

uint64_t paging_get_pml4_phys(void) {
    return current_pml4_phys;
}

void paging_switch_directory(uint64_t pml4_phys) {
    current_pml4_phys = pml4_phys;
    write_cr3(pml4_phys);
}

void paging_map_page(uint64_t pml4_phys, uint64_t virtual_addr, uint64_t physical_addr, uint64_t flags) {
    if (!pml4_phys) return;
    
    page_table_t* pml4 = (page_table_t*)p2v(pml4_phys);
    
    // Extract indices
    uint64_t pml4_index = (virtual_addr >> 39) & 0x1FF;
    uint64_t pdpt_index = (virtual_addr >> 30) & 0x1FF;
    uint64_t pd_index   = (virtual_addr >> 21) & 0x1FF;
    uint64_t pt_index   = (virtual_addr >> 12) & 0x1FF;
    
    // Check PML4 entry
    if (!(pml4->entries[pml4_index] & PT_PRESENT)) {
        uint64_t new_table_phys = alloc_page_table_phys();
        if (!new_table_phys) return; // Out of memory
        pml4->entries[pml4_index] = new_table_phys | PT_PRESENT | PT_RW | PT_USER;
    }
    
    // Get PDPT
    page_table_t* pdpt = (page_table_t*)p2v(pml4->entries[pml4_index] & PT_ADDR_MASK);
    if (!(pdpt->entries[pdpt_index] & PT_PRESENT)) {
        uint64_t new_table_phys = alloc_page_table_phys();
        if (!new_table_phys) return;
        pdpt->entries[pdpt_index] = new_table_phys | PT_PRESENT | PT_RW | PT_USER;
    }
    
    // Get PD
    page_table_t* pd = (page_table_t*)p2v(pdpt->entries[pdpt_index] & PT_ADDR_MASK);
    if (!(pd->entries[pd_index] & PT_PRESENT)) {
        uint64_t new_table_phys = alloc_page_table_phys();
        if (!new_table_phys) return;
        pd->entries[pd_index] = new_table_phys | PT_PRESENT | PT_RW | PT_USER;
    }
    
    // Get PT
    page_table_t* pt = (page_table_t*)p2v(pd->entries[pd_index] & PT_ADDR_MASK);
    
    // Set entry in PT
    pt->entries[pt_index] = (physical_addr & PT_ADDR_MASK) | flags;
    
    // Flush TLB for this address
    asm volatile("invlpg (%0)" : : "r"(virtual_addr) : "memory");
}

uint64_t paging_create_user_pml4_phys(void) {
    // 1. Allocate a new physical PML4
    uint64_t new_pml4_phys = alloc_page_table_phys();
    if (!new_pml4_phys) return 0;
    
    page_table_t* new_pml4 = (page_table_t*)p2v(new_pml4_phys);
    
    // 2. Clone the higher-half kernel mappings from the active PML4
    // In x86_64, indices 256-511 are the higher half.
    uint64_t kernel_pml4_phys = paging_get_pml4_phys();
    if (kernel_pml4_phys) {
        page_table_t* kernel_pml4 = (page_table_t*)p2v(kernel_pml4_phys);
        for (int i = 256; i < 512; i++) {
            new_pml4->entries[i] = kernel_pml4->entries[i];
        }
    }
    
    // The lower half (0-255) is left empty for the user process to use
    return new_pml4_phys;
}

void paging_destroy_user_pml4_phys(uint64_t pml4_phys) {
    if (!pml4_phys) return;
    page_table_t* pml4 = (page_table_t*)p2v(pml4_phys);
    
    // Only traverse lower half (user space, indices 0-255)
    for (int pml4_idx = 0; pml4_idx < 256; pml4_idx++) {
        if (pml4->entries[pml4_idx] & PT_PRESENT) {
            page_table_t* pdpt = (page_table_t*)p2v(pml4->entries[pml4_idx] & PT_ADDR_MASK);
            
            for (int pdpt_idx = 0; pdpt_idx < 512; pdpt_idx++) {
                if (pdpt->entries[pdpt_idx] & PT_PRESENT) {
                    page_table_t* pd = (page_table_t*)p2v(pdpt->entries[pdpt_idx] & PT_ADDR_MASK);
                    
                    for (int pd_idx = 0; pd_idx < 512; pd_idx++) {
                        if (pd->entries[pd_idx] & PT_PRESENT) {
                            page_table_t* pt = (page_table_t*)p2v(pd->entries[pd_idx] & PT_ADDR_MASK);
                            
                            for (int pt_idx = 0; pt_idx < 512; pt_idx++) {
                                if (pt->entries[pt_idx] & PT_PRESENT) {
                                    uint64_t phys_addr = pt->entries[pt_idx] & PT_ADDR_MASK;
                                    extern void kfree(void* ptr);
                                    kfree((void*)p2v(phys_addr));
                                }
                            }
                            extern void kfree(void* ptr);
                            kfree((void*)pt);
                        }
                    }
                    extern void kfree(void* ptr);
                    kfree((void*)pd);
                }
            }
            extern void kfree(void* ptr);
            kfree((void*)pdpt);
        }
    }
    // Finally free the pml4 itself
    extern void kfree(void* ptr);
    kfree((void*)pml4);
}

uint64_t paging_virt2phys(uint64_t pml4_phys, uint64_t virtual_addr) {
    if (!pml4_phys) return 0;
    
    if (virtual_addr >= 0xFFFF800000000000ULL) {
        return v2p(virtual_addr);
    }
    
    page_table_t* pml4 = (page_table_t*)p2v(pml4_phys);
    uint64_t pml4_index = (virtual_addr >> 39) & 0x1FF;
    if (!(pml4->entries[pml4_index] & PT_PRESENT)) return 0;
    
    page_table_t* pdpt = (page_table_t*)p2v(pml4->entries[pml4_index] & PT_ADDR_MASK);
    uint64_t pdpt_index = (virtual_addr >> 30) & 0x1FF;
    if (!(pdpt->entries[pdpt_index] & PT_PRESENT)) return 0;
    
    page_table_t* pd = (page_table_t*)p2v(pdpt->entries[pdpt_index] & PT_ADDR_MASK);
    uint64_t pd_index = (virtual_addr >> 21) & 0x1FF;
    if (!(pd->entries[pd_index] & PT_PRESENT)) return 0;
    
    page_table_t* pt = (page_table_t*)p2v(pd->entries[pd_index] & PT_ADDR_MASK);
    uint64_t pt_index = (virtual_addr >> 12) & 0x1FF;
    if (!(pt->entries[pt_index] & PT_PRESENT)) return 0;
    
    return (pt->entries[pt_index] & PT_ADDR_MASK) | (virtual_addr & 0xFFF);
}
