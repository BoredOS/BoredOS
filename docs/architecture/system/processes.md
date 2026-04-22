# Process Management & Scheduling

BoredOS implements a lightweight, symmetric multiprocessing (SMP) capable multitasking environment. This document outlines the architecture of the scheduler, process structures, context switching, and ELF binary loading.

## 1. Process Structure (`process_t`)

The core of the process management system is the `process_t` structure, defined in `src/sys/process.h`. Due to kernel memory constraints, BoredOS supports a maximum of 16 concurrent processes (`MAX_PROCESSES`), stored in a statically allocated array.

Key fields include:
- **Identification:** `pid`, `parent_pid`, `pgid` (Process Group ID), and `name`.
- **Memory & Context:**
  - `rsp`: The saved stack pointer during a context switch.
  - `pml4_phys`: The physical address of the Page Map Level 4 table (VMM root) for this process.
  - `kernel_stack` & `user_stack_alloc`: Pointers to allocated stack memory.
- **Scheduler State:** `ticks`, `sleep_until`, `is_idle`, `cpu_affinity`.
- **Resources:**
  - `fds`: File descriptor table tracking open files, pipes, and sockets (up to `MAX_PROCESS_FDS` = 16).
  - `gui_events`: A circular queue for Window Manager events (keyboard, mouse).
- **Signals:** POSIX-like signal tracking via `signal_mask` and `signal_pending`.

## 2. The Scheduler

BoredOS uses a **Preemptive Round-Robin** scheduler implemented as a circular linked list. 

### Symmetric Multiprocessing (SMP)
Each CPU core maintains its own `current_process` pointer (`current_process[my_cpu]`). When a new user process is spawned via `process_create_elf`, the kernel assigns it to an Application Processor (AP) core using a simple round-robin assignment policy (`next_cpu_assign`), avoiding Core 0 (BSP) which is typically reserved for kernel tasks and driver interrupts.

### The `process_schedule` Loop
When the timer interrupt fires, it calls `process_schedule(current_rsp)`:
1. It saves the `current_rsp` into the current process's structure.
2. It handles cleanup of killed processes (`kill_pending`).
3. It traverses the circular linked list (`cur->next`) looking for a process where `cpu_affinity == my_cpu`.
4. It checks if the process is sleeping (`sleep_until > now`).
5. It switches the hardware context:
   - Updates the Task State Segment (TSS) ring 0 stack pointer.
   - Switches the page directory by writing the new `pml4_phys` to `CR3`.
   - Returns the new process's `rsp`, which the interrupt handler then pops into registers.

## 3. Context Switching

Context switching is achieved by manually constructing an interrupt stack frame (IRETQ frame). 

When a process is created, the kernel sets up the top of its kernel stack with:
- `SS` (Stack Segment: `0x1B` for user, `0x10` for kernel)
- `RSP` (The process's stack pointer)
- `RFLAGS` (`0x202` to ensure interrupts are enabled)
- `CS` (Code Segment: `0x23` for user, `0x08` for kernel)
- `RIP` (The entry point of the binary or function)
- Zeroed space for General Purpose Registers and a 512-byte `fxsave` region for FPU/SSE state.

When `process_schedule` returns the new `rsp`, the assembly interrupt stub uses `pop` instructions to restore the general-purpose registers, and finally executes `iretq`, transitioning execution to the new process seamlessly.

## 4. ELF Loading

Userland applications in BoredOS are standard 64-bit ELF binaries. 

The function `process_create_elf` orchestrates this:
1. **Memory Allocation:** Creates a new PML4 page table for the user process.
2. **Parsing:** Calls `elf_load(filepath, pml4, &size)` to parse the ELF headers, allocate required physical memory, and copy the executable segments (text, data, bss) into the process's virtual address space at the locations specified by the ELF program headers.
3. **Stack Setup:** Allocates a 256KB user stack mapped at `0x800000`. 
4. **Argument Passing:** Parses the `args_str` passed to the executable and pushes an `argv` array onto the newly allocated user stack.
5. **Execution:** Sets the stack frame's `RIP` to the ELF entry point and links the process into the scheduler's run queue.

## 5. Process Termination

When a process exits (or is killed), it is not immediately freed. The scheduler sets `kill_pending = true`. The actual destruction of the PML4 table and stack allocations is deferred to the next tick inside `process_schedule` to avoid freeing the memory of the code currently executing the cleanup.
