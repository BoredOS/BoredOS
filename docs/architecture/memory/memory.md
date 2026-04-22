<div align="center">
  <h1>Memory Management</h1>
  <p><em>Physical and Virtual Memory coordination in x86_64 Long Mode.</em></p>
</div>

---

Memory management in BoredOS is split into physical and virtual layers, designed to support both kernel operations and userland isolation on the x86_64 architecture.

## Physical Memory Management (PMM)

The PMM is responsible for tracking which physical RAM frames (usually 4KB each) are free and which are in use.

1.  **Memory Map**: During boot, Limine provides a memory map detailing the available, reserved, and unusable physical memory regions.
2.  **Bitmap Allocator**: The core PMM uses a bitmap-based allocation strategy. Each bit in the bitmap represents a single physical page (frame). If a bit is `1`, the page is in use; if `0`, it is free.
3.  **Allocation**: When a new page is requested (e.g., for userland space or kernel heap), the PMM scans the bitmap for the first available zero bit, marks it as used, and returns the physical address.
4.  **SMP Safety**: In a multi-core environment, the PMM and VMM are protected by **Spinlocks** to prevent two CPUs from allocating the same frame or modifying page tables simultaneously.

> [!NOTE]
> 4KB frame sizes strike a balance between allocation speed and minimal memory fragmentation, fitting directly with the page tables.

## Virtual Memory Management (VMM) and Paging

BoredOS uses 4-level paging (PML4), a requirement for x86_64 long mode, dividing the virtual address space between the kernel and userland.

-   **Kernel Space**: The kernel relies on a higher-half design where its code, data, and heap are mapped to high addresses (typically above `0xFFFF800000000000`).
-   **Per-CPU Structures**: Each CPU core maintains its own architectural state in memory:
    *   **Per-CPU GDT**: Each core is initialized with its own Global Descriptor Table.
    *   **Per-CPU TSS**: Each core has a dedicated Task State Segment containing the `RSP0` pointer for its own kernel stack, ensuring safe interrupt handling across cores.
-   **User Space**: Userland applications are loaded into lower virtual addresses.
-   **Page Faults**: The `mem/` subsystem registers an Interrupt Service Routine (ISR) for page faults (Interrupt 14). If a process accesses unmapped memory, the handler determines whether to allocate a new frame or terminate the process.

## Kernel Heap

Dynamic allocation within the kernel (`kmalloc` and `kfree`) is layered on top of the physical allocator. The kernel maintains its own heap area in virtual memory. When the heap requires more space, it requests physical frames from the PMM and maps them into the kernel's virtual address space.

> [!IMPORTANT]
> The kernel heap is a shared resource; therefore, all `kmalloc` and `kfree` operations are guarded by a global spinlock to ensure thread safety during multi-core execution.

---
