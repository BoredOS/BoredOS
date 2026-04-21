// Copyright (c) 2023-2026 Chris (boreddevnl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
#include "process.h"
#include "gdt.h"
#include "idt.h"
#include "paging.h"
#include "io.h"
#include "platform.h"
#include "memory_manager.h"
#include "elf.h"
#include "wm.h"
#include "vfs.h"
#include "spinlock.h"
#include "smp.h"
#include "lapic.h"

extern void cmd_write(const char *str);
extern void serial_write(const char *str);

#define MAX_PROCESSES 16
#define MAX_CPUS_SCHED 32
process_t processes[MAX_PROCESSES] __attribute__((aligned(16)));
static process_t* current_process[MAX_CPUS_SCHED] = {0}; // Per-CPU
static uint32_t next_pid = 0;
static void *free_kernel_stack_later[MAX_CPUS_SCHED] = {0};
static uint64_t free_pml4_later[MAX_CPUS_SCHED] = {0};
static spinlock_t runqueue_lock = SPINLOCK_INIT;
static uint32_t next_cpu_assign = 1; 

static void process_cleanup_inner(process_t *proc);

static void process_close_fd_inner(process_t *proc, int fd) {
    if (!proc || fd < 0 || fd >= MAX_PROCESS_FDS || !proc->fds[fd]) {
        return;
    }

    if (proc->fd_kind[fd] == PROC_FD_KIND_FILE) {
        process_fd_file_ref_t *ref = (process_fd_file_ref_t *)proc->fds[fd];
        if (ref) {
            ref->refs--;
            if (ref->refs <= 0) {
                if (ref->file) vfs_close((vfs_file_t *)ref->file);
                kfree(ref);
            }
        }
    } else if (proc->fd_kind[fd] == PROC_FD_KIND_PIPE_READ || proc->fd_kind[fd] == PROC_FD_KIND_PIPE_WRITE) {
        process_fd_pipe_t *pipe = (process_fd_pipe_t *)proc->fds[fd];
        if (pipe) {
            if (proc->fd_kind[fd] == PROC_FD_KIND_PIPE_READ) pipe->readers--;
            else pipe->writers--;
            if (pipe->readers <= 0 && pipe->writers <= 0) {
                kfree(pipe);
            }
        }
    }

    proc->fds[fd] = NULL;
    proc->fd_kind[fd] = PROC_FD_KIND_NONE;
    proc->fd_flags[fd] = 0;
}

static void process_init_signal_state(process_t *proc) {
    if (!proc) return;
    proc->signal_mask = 0;
    proc->signal_pending = 0;
    for (int i = 0; i < MAX_SIGNALS; i++) {
        proc->signal_handlers[i] = 0;
        proc->signal_action_mask[i] = 0;
        proc->signal_action_flags[i] = 0;
    }
}

void process_init(void) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        processes[i].pid = 0xFFFFFFFF;
    }

    process_t *kernel_proc = &processes[0];
    kernel_proc->pid = next_pid++;
    kernel_proc->is_user = false;
    kernel_proc->is_idle = true;
    kernel_proc->tty_id = -1;
    kernel_proc->kill_pending = false;
    

    kernel_proc->pml4_phys = paging_get_pml4_phys();
    kernel_proc->kernel_stack = 0;
    
    kernel_proc->fpu_initialized = true;

    for (int i = 0; i < MAX_PROCESS_FDS; i++) {
        kernel_proc->fds[i] = NULL;
        kernel_proc->fd_kind[i] = 0;
        kernel_proc->fd_flags[i] = 0;
    }
    kernel_proc->parent_pid = 0;
    kernel_proc->pgid = 0;
    kernel_proc->exited = false;
    kernel_proc->exit_status = 0;
    process_init_signal_state(kernel_proc);
    
    extern void mem_memcpy(void *dest, const void *src, size_t len);
    mem_memcpy(kernel_proc->name, "kernel", 7);
    kernel_proc->ticks = 0;
    kernel_proc->used_memory = 32768; // Kernel stack

    kernel_proc->next = kernel_proc; // Circular linked list
    kernel_proc->cpu_affinity = 0;   // Kernel always on BSP
    mem_memset(kernel_proc->cwd, 0, 1024);
    kernel_proc->cwd[0] = '/';
    current_process[0] = kernel_proc;
}

process_t* process_create(void (*entry_point)(void), bool is_user) {
    uint64_t rflags = spinlock_acquire_irqsave(&runqueue_lock);
    
    process_t *new_proc = NULL;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (processes[i].pid == 0xFFFFFFFF) {
            new_proc = &processes[i];
            break;
        }
    }

    if (!new_proc) {
        spinlock_release_irqrestore(&runqueue_lock, rflags);
        return NULL;
    }
    
    process_t *parent = process_get_current();

    new_proc->pid = next_pid++;
    new_proc->is_user = is_user;
    new_proc->tty_id = -1;
    new_proc->kill_pending = false;
    new_proc->parent_pid = parent ? parent->pid : 0;
    new_proc->pgid = parent ? parent->pgid : new_proc->pid;
    new_proc->exited = false;
    new_proc->exit_status = 0;
    process_init_signal_state(new_proc);
    
    if (parent) {
        extern void mem_memcpy(void *dest, const void *src, size_t len);
        mem_memcpy(new_proc->cwd, parent->cwd, 1024);
        new_proc->tty_id = parent->tty_id;
    } else {
        mem_memset(new_proc->cwd, 0, 1024);
        new_proc->cwd[0] = '/';
    }
    
    // 1. Setup Page Table
    if (is_user) {
        new_proc->pml4_phys = paging_create_user_pml4_phys();
    } else {
        new_proc->pml4_phys = paging_get_pml4_phys();
    }
    
    if (!new_proc->pml4_phys) {
        spinlock_release_irqrestore(&runqueue_lock, rflags);
        return NULL;
    }
    
    // 2. Allocate aligned stack
    void* user_stack = kmalloc_aligned(4096, 4096);
    void* kernel_stack = kmalloc_aligned(32768, 32768); // Needed for when user interrupts to Ring 0
    
    if (is_user) {
        // Map user stack to 0x800000
        paging_map_page(new_proc->pml4_phys, 0x800000, v2p((uint64_t)user_stack), PT_PRESENT | PT_RW | PT_USER);
        
        // Allocate code page aligned and copy code
        void* code = kmalloc_aligned(4096, 4096);
        for(int i=0; i<128; i++) ((uint8_t*)code)[i] = ((uint8_t*)entry_point)[i];
        
        paging_map_page(new_proc->pml4_phys, 0x400000, v2p((uint64_t)code), PT_PRESENT | PT_RW | PT_USER);
        
        // Build initial stack frame for iretq
        // Stack grows down, start at top
        uint64_t* stack_ptr = (uint64_t*)((uint64_t)kernel_stack + 32768);
        
        *(--stack_ptr) = 0x1B;          // SS (User Data)
        *(--stack_ptr) = 0x800000 + 4096; // RSP
        *(--stack_ptr) = 0x202;         // RFLAGS (IF=1)
        *(--stack_ptr) = 0x23;          // CS (User Code)
        *(--stack_ptr) = 0x400000;      // RIP
        *(--stack_ptr) = 0;             // int_no
        *(--stack_ptr) = 0;             // err_code
        
        // Push 15 zeros for general purpose registers (r15 -> rax)
        for (int i = 0; i < 15; i++) *(--stack_ptr) = 0;
        
        // Push 512 bytes for SSE/FPU state (fxsave_region)
        // Zero it out for safety
        stack_ptr = (uint64_t*)((uint64_t)stack_ptr - 512);
        for (int i = 0; i < 512/8; i++) stack_ptr[i] = 0;
        
        new_proc->kernel_stack = (uint64_t)kernel_stack + 32768;
        new_proc->rsp = (uint64_t)stack_ptr;
    } else {
        // Kernel thread
        uint64_t* stack_ptr = (uint64_t*)((uint64_t)kernel_stack + 32768);
        *(--stack_ptr) = 0x10;          // SS (Kernel Data)
        stack_ptr--;
        *stack_ptr = (uint64_t)stack_ptr; // RSP
        *(--stack_ptr) = 0x202;         // RFLAGS
        *(--stack_ptr) = 0x08;          // CS (Kernel Code)
        *(--stack_ptr) = (uint64_t)entry_point; // RIP
        *(--stack_ptr) = 0;             // int_no
        *(--stack_ptr) = 0;             // err_code
        
        // Push 15 zeros for general purpose registers (r15 -> rax)
        for (int i = 0; i < 15; i++) *(--stack_ptr) = 0;
        
        // Push 512 bytes for SSE/FPU state (fxsave_region)
        stack_ptr = (uint64_t*)((uint64_t)stack_ptr - 512);
        // Zero it out for safety
        for (int i = 0; i < 512/8; i++) stack_ptr[i] = 0;

        new_proc->kernel_stack = (uint64_t)kernel_stack + 32768;
        new_proc->rsp = (uint64_t)stack_ptr;
        kfree(user_stack); // Unused for kernel threads
    }

    // Initialize FPU state for new process
    asm volatile("fninit");
    new_proc->fpu_initialized = true;
    
    new_proc->cpu_affinity = 0; // Non-ELF processes stay on BSP
    
    // Add to linked list
    new_proc->next = current_process[0]->next;
    current_process[0]->next = new_proc;
    
    spinlock_release_irqrestore(&runqueue_lock, rflags);
    return new_proc;
}

process_t* process_create_elf(const char* filepath, const char* args_str, bool terminal_proc, int tty_id) {
    uint64_t rflags = spinlock_acquire_irqsave(&runqueue_lock);
    process_t *new_proc = NULL;
    
    // Find an available slot
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (processes[i].pid == 0xFFFFFFFF) {
            new_proc = &processes[i];
            break;
        }
    }

    if (!new_proc) {
        spinlock_release_irqrestore(&runqueue_lock, rflags);
        return NULL;
    }
    
    new_proc->pid = next_pid++;
    new_proc->is_user = true;
    spinlock_release_irqrestore(&runqueue_lock, rflags);
    
    // 1. Setup Page Table
    new_proc->pml4_phys = paging_create_user_pml4_phys();
    if (!new_proc->pml4_phys) return NULL;

    for (int i = 0; i < MAX_PROCESS_FDS; i++) {
        new_proc->fds[i] = NULL;
        new_proc->fd_kind[i] = 0;
        new_proc->fd_flags[i] = 0;
    }
    new_proc->gui_event_head = 0;
    new_proc->gui_event_tail = 0;
    new_proc->ui_window = NULL;
    new_proc->heap_start = 0x20000000; // 512MB mark
    new_proc->heap_end = 0x20000000;
    new_proc->is_terminal_proc = terminal_proc;
    new_proc->tty_id = tty_id;
    new_proc->kill_pending = false;
    new_proc->exited = false;
    new_proc->exit_status = 0;
    process_init_signal_state(new_proc);

    process_t *parent = process_get_current();
    if (parent) {
        extern void mem_memcpy(void *dest, const void *src, size_t len);
        mem_memcpy(new_proc->cwd, parent->cwd, 1024);
        new_proc->parent_pid = parent->pid;
        new_proc->pgid = parent->pgid;
    } else {
        extern void mem_memset(void *dest, int val, size_t len);
        mem_memset(new_proc->cwd, 0, 1024);
        new_proc->cwd[0] = '/';
        new_proc->parent_pid = 0;
        new_proc->pgid = new_proc->pid;
    }

    // 2. Load ELF executable
    size_t elf_load_size = 0;
    uint64_t entry_point = elf_load(filepath, new_proc->pml4_phys, &elf_load_size);
    if (entry_point == 0) {
        serial_write("[PROC] Failed to load ELF: ");
        serial_write(filepath);
        serial_write("\n");
        return NULL;
    }

    // Set process name from filepath
    int last_slash = -1;
    for (int i = 0; filepath[i]; i++) if (filepath[i] == '/') last_slash = i;
    const char *filename = (last_slash == -1) ? filepath : (filepath + last_slash + 1);
    int ni = 0;
    while (filename[ni] && ni < 63) {
        new_proc->name[ni] = filename[ni];
        ni++;
    }
    new_proc->name[ni] = 0;
    new_proc->ticks = 0;

    // 3. Allocate generic User stack and Kernel stack for interrupts
    // Increase to 256KB to prevent stack smashing on heavy networking
    size_t user_stack_size = 262144;
    void* stack = kmalloc_aligned(user_stack_size, 4096);
    void* kernel_stack = kmalloc_aligned(65536, 65536); 
    
    // Map User stack to 0x800000
    for (uint64_t i = 0; i < (user_stack_size / 4096); i++) {
        paging_map_page(new_proc->pml4_phys, 0x800000 - user_stack_size + (i * 4096), v2p((uint64_t)stack + (i * 4096)), PT_PRESENT | PT_RW | PT_USER);
    }

 
    int argc = 1;
    char *args_buf = (char *)stack + user_stack_size;
    uint64_t user_args_buf = 0x800000;

    // Copy filepath as argv[0]
    int path_len = 0;
    while (filepath[path_len]) path_len++;
    args_buf -= (path_len + 1);
    user_args_buf -= (path_len + 1);
    for (int i = 0; i <= path_len; i++) args_buf[i] = filepath[i];
    
    uint64_t argv_ptrs[32];
    argv_ptrs[0] = user_args_buf;

    if (args_str) {
        int i = 0;
        while (args_str[i] && argc < 31) {
            // Skip spaces
            while (args_str[i] == ' ') i++;
            if (!args_str[i]) break;

            int arg_start = i;
            bool in_quotes = false;
            
            if (args_str[i] == '"') {
                in_quotes = true;
                i++;
                arg_start = i;
                while (args_str[i] && args_str[i] != '"') i++;
            } else {
                while (args_str[i] && args_str[i] != ' ') i++;
            }
            
            int arg_len = i - arg_start;

            args_buf -= (arg_len + 1);
            user_args_buf -= (arg_len + 1);
            
            for (int k = 0; k < arg_len; k++) {
                args_buf[k] = args_str[arg_start + k];
            }
            args_buf[arg_len] = '\0';
            
            argv_ptrs[argc++] = user_args_buf;
            
            if (in_quotes && args_str[i] == '"') i++; // Skip closing quote
        }
    }
    argv_ptrs[argc] = 0; // Null terminator for argv

    // Align stack to 8 bytes before pushing argv array
    uint64_t current_user_sp = user_args_buf;
    current_user_sp &= ~7ULL;
    args_buf = (char *)((uint64_t)stack + (current_user_sp - (0x800000 - user_stack_size)));

    // Push argv array
    int argv_size = (argc + 1) * sizeof(uint64_t);
    args_buf -= argv_size;
    current_user_sp -= argv_size;
    
    uint64_t actual_argv_ptr = current_user_sp; // Store the true pointer to argv array
    
    uint64_t *user_argv_array = (uint64_t *)args_buf;
    for (int i = 0; i <= argc; i++) {
        user_argv_array[i] = argv_ptrs[i];
    }
    
    // Align stack to 16 bytes. crt0.asm does `and rsp, -16`, but it's good practice
    current_user_sp &= ~15ULL;

    // 4. Build Stack Frame for context switch via IRETQ
    uint64_t* stack_ptr = (uint64_t*)((uint64_t)kernel_stack + 65536);
    *(--stack_ptr) = 0x1B;            // SS (User Mode Data)
    *(--stack_ptr) = current_user_sp; // RSP (Updated user stack pointer)
    *(--stack_ptr) = 0x202;           // RFLAGS (Interrupts Enabled)
    *(--stack_ptr) = 0x23;            // CS (User Mode Code)
    *(--stack_ptr) = entry_point;     // RIP
    *(--stack_ptr) = 0;               // err_code
    *(--stack_ptr) = 0;               // int_no
    // 15 General purpose registers
    *(--stack_ptr) = 0;                // RAX
    *(--stack_ptr) = 0;                // RBX
    *(--stack_ptr) = 0;                // RCX
    *(--stack_ptr) = 0;                // RDX
    *(--stack_ptr) = actual_argv_ptr;  // RSI = actual argv array
    *(--stack_ptr) = argc;             // RDI = argc
    *(--stack_ptr) = 0;                // RBP
    *(--stack_ptr) = 0;                // R8
    *(--stack_ptr) = 0;                // R9
    *(--stack_ptr) = 0;                // R10
    *(--stack_ptr) = 0;                // R11
    *(--stack_ptr) = 0;                // R12
    *(--stack_ptr) = 0;                // R13
    *(--stack_ptr) = 0;                // R14
    *(--stack_ptr) = 0;                // R15
    
    // Space for 512-byte fxsave_region
    stack_ptr = (uint64_t*)((uint64_t)stack_ptr - 512);
    // Initialize with a clean FPU state
    asm volatile("fninit");
    asm volatile("fxsave %0" : "=m"(*stack_ptr));

    new_proc->kernel_stack = (uint64_t)kernel_stack + 65536;
    new_proc->kernel_stack_alloc = kernel_stack;
    new_proc->user_stack_alloc = stack;
    new_proc->rsp = (uint64_t)stack_ptr;
    new_proc->used_memory = elf_load_size + user_stack_size + 65536;

    // Initialize FPU state for new process
    asm volatile("fninit");
    new_proc->fpu_initialized = true;

    // Assign to an AP core via round-robin (if SMP is active)
    uint32_t cpu_count = smp_cpu_count();
    if (cpu_count > 1) {
        new_proc->cpu_affinity = next_cpu_assign;
        next_cpu_assign++;
        if (next_cpu_assign >= cpu_count) next_cpu_assign = 1; // Wrap, skip CPU 0
    } else {
        new_proc->cpu_affinity = 0;
    }

    // Add to linked list (Critical Section)
    rflags = spinlock_acquire_irqsave(&runqueue_lock);
    new_proc->next = current_process[0]->next;
    current_process[0]->next = new_proc;
    spinlock_release_irqrestore(&runqueue_lock, rflags);
    
    serial_write("[PROC] Exec: ");
    serial_write(filepath);
    serial_write("\n");
    return new_proc;
}

process_t* process_get_current_for_cpu(uint32_t cpu_id) {
    if (cpu_id >= MAX_CPUS_SCHED) return NULL;
    return current_process[cpu_id];
}

void process_set_current_for_cpu(uint32_t cpu_id, process_t* p) {
    if (cpu_id >= MAX_CPUS_SCHED) return;
    current_process[cpu_id] = p;
}

process_t* process_get_current(void) {
    uint32_t cpu = smp_this_cpu_id();
    return current_process[cpu];
}

uint32_t process_get_current_pid(void) {
    process_t *p = process_get_current();
    return p ? p->pid : 0;
}

uint64_t process_schedule(uint64_t current_rsp) {
    uint32_t my_cpu = smp_this_cpu_id();
    uint64_t rflags = spinlock_acquire_irqsave(&runqueue_lock);

    void *cleanup_stack = NULL;
    uint64_t cleanup_pml4 = 0;

    if (free_kernel_stack_later[my_cpu]) {
        cleanup_stack = free_kernel_stack_later[my_cpu];
        free_kernel_stack_later[my_cpu] = NULL;
    }
    if (free_pml4_later[my_cpu]) {
        cleanup_pml4 = free_pml4_later[my_cpu];
        free_pml4_later[my_cpu] = 0;
    }

    process_t *cur = current_process[my_cpu];
    
    if (!cur || !cur->next || cur == cur->next) {
        spinlock_release_irqrestore(&runqueue_lock, rflags);

        // Perform cleanup outside the lock
        if (cleanup_stack) kfree(cleanup_stack);
        if (cleanup_pml4) {
            extern void paging_destroy_user_pml4_phys(uint64_t pml4_phys);
            paging_destroy_user_pml4_phys(cleanup_pml4);
        }

        return current_rsp;
    }
        
    // Save context
    cur->rsp = current_rsp;

    if (cur->kill_pending && cur->pid != 0xFFFFFFFF && cur->pid != 0) {
        process_cleanup_inner(cur);

        process_t *prev = cur;
        while (prev->next != cur) {
            prev = prev->next;
        }

        if (prev != cur) {
            prev->next = cur->next;

            process_t *next_proc = cur->next;
            while (next_proc != cur) {
                if (next_proc->cpu_affinity == my_cpu && next_proc->pid != 0xFFFFFFFF && !next_proc->kill_pending) break;
                next_proc = next_proc->next;
            }

            if (next_proc == cur || next_proc->cpu_affinity != my_cpu) {
                for (int i = 0; i < MAX_PROCESSES; i++) {
                    if (processes[i].pid == 0 || (processes[i].cpu_affinity == my_cpu && processes[i].is_user == false)) {
                        next_proc = &processes[i];
                        break;
                    }
                }
            }

            current_process[my_cpu] = next_proc;

            cur->exited = true;
            cur->cpu_affinity = 0xFFFFFFFF;
            cur->ui_window = NULL;
            cur->is_terminal_proc = false;
            cur->kill_pending = false;

            free_kernel_stack_later[my_cpu] = cur->kernel_stack_alloc;
            cur->kernel_stack_alloc = NULL;
            free_pml4_later[my_cpu] = cur->pml4_phys;
            cur->pml4_phys = 0;

            if (current_process[my_cpu]->is_user && current_process[my_cpu]->kernel_stack) {
                tss_set_stack_cpu(my_cpu, current_process[my_cpu]->kernel_stack);
                cpu_state_t *cpu_state = smp_get_cpu(my_cpu);
                if (cpu_state) {
                    cpu_state->kernel_syscall_stack = current_process[my_cpu]->kernel_stack;
                }
            }

            paging_switch_directory(current_process[my_cpu]->pml4_phys);

            current_process[my_cpu]->ticks++;
            uint64_t next_rsp = current_process[my_cpu]->rsp;

            spinlock_release_irqrestore(&runqueue_lock, rflags);
            if (cleanup_stack) kfree(cleanup_stack);
            if (cleanup_pml4) {
                extern void paging_destroy_user_pml4_phys(uint64_t pml4_phys);
                paging_destroy_user_pml4_phys(cleanup_pml4);
            }

            return next_rsp;
        }
    }

    // Switch to next ready process assigned to this CPU
    extern uint32_t wm_get_ticks(void);
    uint32_t now = wm_get_ticks();
    
    process_t *start = cur;
    process_t *next_proc = cur->next;
    
    while (next_proc != start) {
        // Only consider processes assigned to our CPU and not terminated
        if (next_proc->cpu_affinity == my_cpu && next_proc->pid != 0xFFFFFFFF && !next_proc->kill_pending) {
            if (next_proc->pid == 0 || next_proc->sleep_until == 0 || next_proc->sleep_until <= now) {
                break;
            }
        }
        next_proc = next_proc->next;
    }
    
    if (next_proc->cpu_affinity != my_cpu || next_proc->pid == 0xFFFFFFFF) {
        if (cur && cur->pid == 0xFFFFFFFF) {
            for (int i = 0; i < MAX_PROCESSES; i++) {
                if (processes[i].pid == 0 || (processes[i].cpu_affinity == my_cpu && processes[i].is_user == false)) {
                    next_proc = &processes[i];
                    break;
                }
            }
        } else {
            spinlock_release_irqrestore(&runqueue_lock, rflags);

            if (cleanup_stack) kfree(cleanup_stack);
            if (cleanup_pml4) {
                extern void paging_destroy_user_pml4_phys(uint64_t pml4_phys);
                paging_destroy_user_pml4_phys(cleanup_pml4);
            }

            return current_rsp;
        }
    }
    
    current_process[my_cpu] = next_proc;
    
    if (current_process[my_cpu]->is_user && current_process[my_cpu]->kernel_stack) {
        tss_set_stack_cpu(my_cpu, current_process[my_cpu]->kernel_stack);
        cpu_state_t *cpu_state = smp_get_cpu(my_cpu);
        if (cpu_state) {
            cpu_state->kernel_syscall_stack = current_process[my_cpu]->kernel_stack;
        }
    }
    
    // Switch page table
    paging_switch_directory(current_process[my_cpu]->pml4_phys);
    
    current_process[my_cpu]->ticks++;
    uint64_t next_rsp = current_process[my_cpu]->rsp;

    spinlock_release_irqrestore(&runqueue_lock, rflags);
    if (cleanup_stack) kfree(cleanup_stack);
    if (cleanup_pml4) {
        extern void paging_destroy_user_pml4_phys(uint64_t pml4_phys);
        paging_destroy_user_pml4_phys(cleanup_pml4);
    }

    return next_rsp;
}

process_t* process_get_by_pid(uint32_t pid) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (processes[i].pid == pid && !processes[i].exited) return &processes[i];
    }
    return NULL;
}

void process_kill_by_tty(int tty_id) {
    if (tty_id < 0) return;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (processes[i].pid != 0xFFFFFFFF && processes[i].pid != 0 && processes[i].tty_id == tty_id) {
            process_terminate(&processes[i]);
        }
    }
}

static void process_cleanup_inner(process_t *proc) {
    if (!proc || proc->pid == 0xFFFFFFFF) return;

    // 1. Cleanup side effects
    if (proc->ui_window) {
        wm_remove_window((Window *)proc->ui_window);
        proc->ui_window = NULL;
    }
    
    for (int i = 0; i < MAX_PROCESS_FDS; i++) {
        process_close_fd_inner(proc, i);
    }
    
    extern void cmd_process_finished(void);
    cmd_process_finished();
    
    extern void network_cleanup(void);
    network_cleanup();
    
    extern void network_cleanup_pcb(void *pcb);
    // TODO: We need per-process PCB tracking to call this safely
    // For now, let's NOT call global network_cleanup
}

void process_terminate(process_t *to_delete) {
    process_terminate_with_status(to_delete, 0);
}

void process_terminate_with_status(process_t *to_delete, int status) {
    if (!to_delete || to_delete->pid == 0xFFFFFFFF || to_delete->pid == 0) return;

    uint32_t cpu_count = smp_cpu_count();
    for (uint32_t c = 0; c < cpu_count && c < MAX_CPUS_SCHED; c++) {
        if (current_process[c] == to_delete) {
            to_delete->kill_pending = true;
            to_delete->exit_status = status;
            to_delete->exited = true;
            return;
        }
    }

    uint64_t rflags = spinlock_acquire_irqsave(&runqueue_lock);

    process_cleanup_inner(to_delete);

    // 2. Find previous process in circular list
    process_t *prev = to_delete;
    while (prev->next != to_delete) {
        prev = prev->next;
    }

    if (prev == to_delete) {
        // Only one process (should be kernel), cannot terminate.
        spinlock_release_irqrestore(&runqueue_lock, rflags);
        return;
    }

    // 3. Remove current from list
    prev->next = to_delete->next;
    
    // Update per-CPU current_process if this was the current on any CPU
    for (uint32_t c = 0; c < cpu_count && c < MAX_CPUS_SCHED; c++) {
        if (current_process[c] == to_delete) {
            process_t *np = to_delete->next;
            while (np != to_delete) {
                if (np->cpu_affinity == c && np->pid != 0xFFFFFFFF) break;
                np = np->next;
            }
            if (np == to_delete || np->cpu_affinity != c) {
                for (int i = 0; i < MAX_PROCESSES; i++) {
                    if (processes[i].pid == 0 || (processes[i].cpu_affinity == c && processes[i].is_user == false)) {
                        np = &processes[i]; break;
                    }
                }
            }
            current_process[c] = np;
        }
    }

    // Mark slot as free
    to_delete->cpu_affinity = 0xFFFFFFFF;
    to_delete->kill_pending = false;
    to_delete->exited = true;
    to_delete->exit_status = status;

    if (to_delete->user_stack_alloc) kfree(to_delete->user_stack_alloc);
    if (to_delete->kernel_stack_alloc) kfree(to_delete->kernel_stack_alloc);
    
    extern void paging_destroy_user_pml4_phys(uint64_t pml4_phys);
    if (to_delete->pml4_phys && to_delete->is_user) {
        paging_destroy_user_pml4_phys(to_delete->pml4_phys);
    }
    
    to_delete->user_stack_alloc = NULL;
    to_delete->kernel_stack_alloc = NULL;
    to_delete->pml4_phys = 0;

    spinlock_release_irqrestore(&runqueue_lock, rflags);
}

uint64_t process_terminate_current(void) {
    uint64_t rflags = spinlock_acquire_irqsave(&runqueue_lock);

    uint32_t my_cpu = smp_this_cpu_id();
    process_t *cur = current_process[my_cpu];

    if (!cur || cur->pid == 0) {
        spinlock_release_irqrestore(&runqueue_lock, rflags);
        return 0;
    }
    
    process_cleanup_inner(cur);
    cur->exited = true;
    cur->exit_status = 0;

    // 2. Find previous process in circular list
    process_t *prev = cur;
    while (prev->next != cur) {
        prev = prev->next;
    }

    // 3. Remove current from list
    process_t *to_delete = cur;
    
    if (prev == cur) {
        // Only one process (should be kernel), cannot terminate.
        spinlock_release_irqrestore(&runqueue_lock, rflags);
        return to_delete->rsp;
    }

    prev->next = to_delete->next;
    
    process_t *next_proc = to_delete->next;
    while (next_proc != to_delete) {
        if (next_proc->cpu_affinity == my_cpu && next_proc->pid != 0xFFFFFFFF) break;
        next_proc = next_proc->next;
    }
    
    if (next_proc == to_delete || next_proc->cpu_affinity != my_cpu) {
        for (int i = 0; i < MAX_PROCESSES; i++) {
            if (processes[i].pid == 0 || (processes[i].cpu_affinity == my_cpu && processes[i].is_user == false)) {
                next_proc = &processes[i]; break;
            }
        }
    }
    
    current_process[my_cpu] = next_proc;
    
    // Mark slot as free
    to_delete->cpu_affinity = 0xFFFFFFFF;
    to_delete->ui_window = NULL;
    to_delete->is_terminal_proc = false;
    to_delete->kill_pending = false;

    // 4. Load context for the NEXT process
    if (current_process[my_cpu]->is_user && current_process[my_cpu]->kernel_stack) {
        tss_set_stack_cpu(my_cpu, current_process[my_cpu]->kernel_stack);
        cpu_state_t *cpu_state = smp_get_cpu(my_cpu);
        if (cpu_state) {
            cpu_state->kernel_syscall_stack = current_process[my_cpu]->kernel_stack;
        }
    }
    
    paging_switch_directory(current_process[my_cpu]->pml4_phys);

    free_kernel_stack_later[my_cpu] = to_delete->kernel_stack_alloc;
    to_delete->kernel_stack_alloc = NULL;
    free_pml4_later[my_cpu] = to_delete->pml4_phys;
    to_delete->pml4_phys = 0;
    
    uint64_t next_rsp = current_process[my_cpu]->rsp;
    spinlock_release_irqrestore(&runqueue_lock, rflags);
    return next_rsp;
}

int process_reap(uint32_t caller_pid, uint32_t pid, int *status_out) {
    process_t *p = NULL;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (processes[i].pid == pid) {
            p = &processes[i];
            break;
        }
    }
    if (!p) return -1;
    if (!p->exited) return -2;
    if (p->parent_pid != caller_pid && caller_pid != 0) return -1;

    if (status_out) {
        *status_out = p->exit_status;
    }

    p->pid = 0xFFFFFFFF;
    p->parent_pid = 0;
    p->pgid = 0;
    p->cpu_affinity = 0xFFFFFFFF;
    p->exited = false;
    p->exit_status = 0;
    p->sleep_until = 0;
    p->ui_window = NULL;
    p->is_terminal_proc = false;
    p->tty_id = -1;
    p->kill_pending = false;
    p->used_memory = 0;
    p->ticks = 0;
    p->next = NULL;
    for (int i = 0; i < MAX_PROCESS_FDS; i++) {
        p->fds[i] = NULL;
        p->fd_kind[i] = PROC_FD_KIND_NONE;
        p->fd_flags[i] = 0;
    }
    process_init_signal_state(p);
    return 0;
}

int process_waitpid(uint32_t caller_pid, int target_pid, int options, int *status_out) {
    process_t *caller = process_get_by_pid(caller_pid);
    int found_child = 0;
    int found_waitable = 0;

    for (int i = 0; i < MAX_PROCESSES; i++) {
        process_t *p = &processes[i];
        int match = 0;

        if (p->pid == 0xFFFFFFFF || p->pid == 0 || p->parent_pid != caller_pid) {
            continue;
        }

        found_child = 1;
        if (target_pid > 0) {
            match = ((int)p->pid == target_pid);
        } else if (target_pid == -1) {
            match = 1;
        } else if (target_pid == 0) {
            match = (caller && p->pgid == caller->pgid);
        } else {
            match = (p->pgid == (uint32_t)(-target_pid));
        }

        if (!match) {
            continue;
        }

        found_waitable = 1;
        if (!p->exited) {
            continue;
        }

        if (process_reap(caller_pid, p->pid, status_out) != 0) {
            return -1;
        }
        return (int)p->pid;
    }

    if (!found_child || !found_waitable) {
        return -1;
    }
    if (options & 1) {
        return 0;
    }
    return -2;
}

int process_exec_replace_current(registers_t *regs, const char* filepath, const char* args_str) {
    process_t *proc = process_get_current();
    if (!proc || !proc->is_user || !regs || !filepath) return -1;

    uint64_t new_pml4 = paging_create_user_pml4_phys();
    if (!new_pml4) return -1;

    size_t elf_load_size = 0;
    uint64_t entry_point = elf_load(filepath, new_pml4, &elf_load_size);
    if (entry_point == 0) {
        extern void paging_destroy_user_pml4_phys(uint64_t pml4_phys);
        paging_destroy_user_pml4_phys(new_pml4);
        return -1;
    }

    size_t user_stack_size = 262144;
    void* stack = kmalloc_aligned(user_stack_size, 4096);
    if (!stack) {
        extern void paging_destroy_user_pml4_phys(uint64_t pml4_phys);
        paging_destroy_user_pml4_phys(new_pml4);
        return -1;
    }

    for (uint64_t i = 0; i < (user_stack_size / 4096); i++) {
        paging_map_page(new_pml4, 0x800000 - user_stack_size + (i * 4096), v2p((uint64_t)stack + (i * 4096)), PT_PRESENT | PT_RW | PT_USER);
    }

    int argc = 1;
    char *args_buf = (char *)stack + user_stack_size;
    uint64_t user_args_buf = 0x800000;

    int path_len = 0;
    while (filepath[path_len]) path_len++;
    args_buf -= (path_len + 1);
    user_args_buf -= (path_len + 1);
    for (int i = 0; i <= path_len; i++) args_buf[i] = filepath[i];

    uint64_t argv_ptrs[32];
    argv_ptrs[0] = user_args_buf;

    if (args_str) {
        int i = 0;
        while (args_str[i] && argc < 31) {
            while (args_str[i] == ' ') i++;
            if (!args_str[i]) break;

            int arg_start = i;
            bool in_quotes = false;
            if (args_str[i] == '"') {
                in_quotes = true;
                i++;
                arg_start = i;
                while (args_str[i] && args_str[i] != '"') i++;
            } else {
                while (args_str[i] && args_str[i] != ' ') i++;
            }

            int arg_len = i - arg_start;
            args_buf -= (arg_len + 1);
            user_args_buf -= (arg_len + 1);
            for (int k = 0; k < arg_len; k++) args_buf[k] = args_str[arg_start + k];
            args_buf[arg_len] = '\0';
            argv_ptrs[argc++] = user_args_buf;
            if (in_quotes && args_str[i] == '"') i++;
        }
    }
    argv_ptrs[argc] = 0;

    uint64_t current_user_sp = user_args_buf;
    current_user_sp &= ~7ULL;
    args_buf = (char *)((uint64_t)stack + (current_user_sp - (0x800000 - user_stack_size)));

    int argv_size = (argc + 1) * (int)sizeof(uint64_t);
    args_buf -= argv_size;
    current_user_sp -= argv_size;
    uint64_t actual_argv_ptr = current_user_sp;
    uint64_t *user_argv_array = (uint64_t *)args_buf;
    for (int i = 0; i <= argc; i++) user_argv_array[i] = argv_ptrs[i];

    current_user_sp &= ~15ULL;

    if (proc->user_stack_alloc) {
        kfree(proc->user_stack_alloc);
    }
    if (proc->pml4_phys) {
        extern void paging_destroy_user_pml4_phys(uint64_t pml4_phys);
        paging_destroy_user_pml4_phys(proc->pml4_phys);
    }

    proc->pml4_phys = new_pml4;
    proc->user_stack_alloc = stack;
    proc->used_memory = elf_load_size + user_stack_size + 65536;
    proc->heap_start = 0x20000000;
    proc->heap_end = 0x20000000;
    proc->sleep_until = 0;
    process_init_signal_state(proc);

    int last_slash = -1;
    for (int i = 0; filepath[i]; i++) if (filepath[i] == '/') last_slash = i;
    const char *filename = (last_slash == -1) ? filepath : (filepath + last_slash + 1);
    int ni = 0;
    while (filename[ni] && ni < 63) {
        proc->name[ni] = filename[ni];
        ni++;
    }
    proc->name[ni] = 0;

    regs->rip = entry_point;
    regs->rdi = argc;
    regs->rsi = actual_argv_ptr;
    regs->rsp = current_user_sp;
    regs->rax = 0;
    regs->rbx = 0;
    regs->rcx = 0;
    regs->rdx = 0;
    regs->r8 = 0;
    regs->r9 = 0;
    regs->r10 = 0;
    regs->r11 = 0;
    regs->r12 = 0;
    regs->r13 = 0;
    regs->r14 = 0;
    regs->r15 = 0;
    regs->rbp = 0;

    paging_switch_directory(proc->pml4_phys);
    return 0;
}

// SMP: IPI handler called on AP cores when BSP broadcasts scheduling IPI
uint64_t sched_ipi_handler(registers_t *regs) {
    lapic_eoi(); // Acknowledge the IPI
    
    // Run the scheduler for this CPU
    return process_schedule((uint64_t)regs);
}

void process_push_gui_event(process_t *proc, gui_event_t *ev) {
    if (!proc) return;

    // Coalesce PAINT events: if a PAINT event is already in the queue, don't add another
    if (ev->type == 1) { // GUI_EVENT_PAINT
        int curr = proc->gui_event_head;
        while (curr != proc->gui_event_tail) {
            if (proc->gui_events[curr].type == 1) {
                return; // Already has a paint event pending
            }
            curr = (curr + 1) % MAX_GUI_EVENTS;
        }
    }

    int next_tail = (proc->gui_event_tail + 1) % MAX_GUI_EVENTS;
    // Drop event if queue is full
    if (next_tail == proc->gui_event_head) {
        extern void serial_write(const char *str);
        return;
    }
    proc->gui_events[proc->gui_event_tail] = *ev;
    proc->gui_event_tail = next_tail;
}

process_t* process_get_by_ui_window(void *win) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (processes[i].pid != 0xFFFFFFFF && processes[i].ui_window == win) {
            return &processes[i];
        }
    }
    return NULL;
}
