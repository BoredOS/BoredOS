# Kernel Memory Manager Architecture

BoredOS utilizes a highly optimized, two-tier kernel memory manager (`memory_manager.c`) designed for performance, concurrency safety, and long-term stability. The API provides the standard POSIX-like `kmalloc`, `krealloc`, and `kfree` functions used universally throughout the kernel.

## 1. High-Level Design

The memory manager delegates allocation requests to one of two internal sub-systems based on the requested size and alignment parameters:

1. **Slab Allocator**: Optimally handles all small allocations (<= 512 bytes) with an alignment restriction of <= 8 bytes.
2. **Block-List Allocator**: Handles large allocations (> 512 bytes) and any request requiring aggressive alignment (such as page-aligned buffers).

All operations within the memory manager are secured by a global interrupt-safe spinlock (`mm_lock`), rendering the memory subsystem completely atomic and safe to use from any CPU or interrupt handler without triggering a race condition.

---

## 2. The Slab Allocator (Small Objects)

For frequent, small data structures, the overhead of standard heap fragmentation is unacceptable. The Slab Allocator addresses this by pre-allocating blocks of identical size.

### Classes & Geometry
There are 7 active slab classes defined by `slab_sizes[]`: `8, 16, 32, 64, 128, 256, 512` bytes. 
Whenever an allocation requests a size within these bounds, it is rounded up to the nearest valid class.

Each active slab page maps precisely to one standard system `PAGE_SIZE` (4096 bytes). 
- The page header (`SlabPage`) is embedded at the very top (byte offset 0).
- The rest of the page is sliced seamlessly into perfectly sized object slots.

### Intrusive LIFO Free-List
To minimize metadata overhead, the Slab Allocator uses an *intrusive* LIFO (Last-In-First-Out) free-list to track empty object slots. The first 8 bytes of any unallocated slot act as a `next` pointer to the next free slot in that page. When a pointer is freed, it is immediately pushed back to the head of this list, making it the most likely candidate for the *next* allocation. This maximizes CPU cache locality.

### Guardrails & Safety
The Slab Allocator implements highly restrictive checks to guard against fatal kernel errors:
- **Canonical Address Checks:** The allocator verifies that the freelist head remains in the higher-half address space (`0xFFFF000000000000` or above), proactively detecting structural corruption.
- **Strict Pointer Admittance:** Before freeing a pointer to a slab, the allocator validates a dual magic-number footprint, limits the pointer's bounds to verify it belongs geographically to the page, and executes a linked-list walk. 
- **Double-Free Detection:** When a slab is freed, the allocator walks the internal free-list. If the freed pointer is already in the free-list, the allocator intercepts the double-free attempt before the internal state can be damaged.

---

## 3. The Block-List Allocator (Large Objects)

If an allocation is larger than 512 bytes, the memory manager falls back to the Block-List allocator. 

### First-Fit Search & Splitting
The Block Allocator tracks all system memory chunks using an array of `MemBlock` structs ordered dynamically by address.
- It iterates through the array utilizing a **First-Fit Search**. The first contiguous, unallocated block that satisfies the `size` requirement is immediately claimed.
- If the requested alignment dictates it, the allocator splits the parent block. It yields up to three new fragments: `[head padding | exact requested allocation | tail remainder]`.

### Bootstrapping & Heap Migration
To avoid infinite recursion when allocating memory to track new memory blocks, the block list is initially statically allocated in a `.bss` array (`_bootstrap_blocks`) with an initial capacity of 64 `MemBlocks`.

When the system runs out of capacity to track new blocks, the block list calls `grow_block_list()`, which reallocates the array space into the primary heap. It utilizes a `growing` lock-flag to prevent recursive faults while performing this relocation.

### Coalescing
Upon `kfree()`, the chunk is marked as unallocated. The allocator inspects its immediate left and right address neighbors. If they are also free, the adjacent blocks are merged (coalesced) into one continuous block to reduce overall memory fragmentation. 

---

## 4. API Caveats & Contracts

### Alignment guarantees
`kmalloc` inherently returns a naturally aligned pointer (minimum 8-byte boundary) sufficient to satisfy scalar types natively on x86-64 without fetching faults. `kmalloc_aligned` can be utilized for strict power-of-two alignment boundaries (e.g., page directories that demand 4096 alignment).

### Resizing limits
`krealloc` accepts an existing allocated pointer and transforms it to meet a new size requirement. To prevent memory starvation over long lifetimes, `krealloc` employs aggressive optimization strategies depending on the allocator layer:
- **Block Allocator (Shrink-in-Place):** Large blocks actively support shrink-in-place maneuvers. If the reduction saves at least 32 bytes, the unused trailing memory is sliced off, injected into the free pool, and physically coalesced with adjacent free neighbors. The original pointer remains identical.
- **Slab Allocator (Down-Migration):** Since slab slots have rigid geometries, true shrink-in-place is impossible. However, if a pointer shrinks enough to cleanly fall into a smaller slab class, `krealloc` triggers an internal copy-migration. This instantly relinquishes the highly-contested larger slab slot back to the system.

---

## 5. Telemetry & Metrics
The `memory_get_stats()` API exports complete transparency over the current topological state of the system memory map. It calculates variables such as peak memory, overall fragmentation % (the ratio of stranded memory outside the largest single block), and explicit slab efficiency counters.
