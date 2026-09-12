// Copyright (c) 2023-2026 Christiaan (chris@boreddev.nl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
#include "syscall_internal.h"

extern void serial_write(const char *str);
extern void serial_write_num(uint64_t n);

bool is_valid_user_ptr(const void *ptr, size_t size) {
  if (size == 0) return true;
  uint64_t addr = (uint64_t)ptr;
  if (!ptr || addr < 0x1000 || addr >= 0xFFFF800000000000ULL) return false;
  if (addr + size < addr || addr + size > 0xFFFF800000000000ULL) return false;

  process_t *proc = process_get_current();
  if (!proc) return false;
  if (!proc->is_user) return true;

  uint64_t page_start = addr & ~0xFFFULL;
  uint64_t page_end = (addr + size - 1) & ~0xFFFULL;

  if (proc->vmm_space) {
    for (uint64_t p = page_start; p <= page_end; p += 4096) {
      if (mmu_virt_to_phys(proc->vmm_space->mmu_ctx, p) == 0) {
        if (!vma_find(&proc->vmm_space->vma_tree, p)) {
          return false;
        }
      }
      if (p == page_end) break;
    }
    return true;
  }

  if (!proc->pml4_phys) return false;

  mmu_context_t ctx = { .pml4_phys = proc->pml4_phys, .lock = SPINLOCK_INIT };
  for (uint64_t p = page_start; p <= page_end; p += 4096) {
    if (mmu_virt_to_phys(&ctx, p) == 0) {
      return false;
    }
    if (p == page_end) break;
  }
  return true;
}

bool is_valid_user_string(const char *str, size_t max_len) {
  if (!str) return false;
  uint64_t addr = (uint64_t)str;
  if (addr < 0x1000 || addr >= 0xFFFF800000000000ULL) return false;

  process_t *proc = process_get_current();
  if (!proc) return false;
  if (!proc->is_user) return true;
  if (!proc->pml4_phys) return false;

  mmu_context_t ctx = { .pml4_phys = proc->pml4_phys, .lock = SPINLOCK_INIT };
  mmu_context_t *uctx = (proc->vmm_space && proc->vmm_space->mmu_ctx) ? proc->vmm_space->mmu_ctx : &ctx;

  for (size_t i = 0; i < max_len; i++) {
    uint64_t cur_addr = addr + i;
    if (cur_addr >= 0xFFFF800000000000ULL) return false;
    if ((cur_addr & 0xFFF) == 0 || i == 0) {
      uint64_t p = cur_addr & ~0xFFFULL;
      if (mmu_virt_to_phys(uctx, p) == 0) {
        if (!proc->vmm_space || !vma_find(&proc->vmm_space->vma_tree, p)) {
          return false;
        }
      }
    }
    if (str[i] == '\0') {
      return true;
    }
  }
  return false;
}


void syscall_init(void) {
  uint64_t efer = rdmsr(MSR_EFER);
  efer |= 1;
  wrmsr(MSR_EFER, efer);
  uint64_t star = ((uint64_t)0x0013 << 48) | ((uint64_t)0x0008 << 32);
  wrmsr(MSR_STAR, star);
  extern void syscall_entry(void);
  wrmsr(MSR_LSTAR, (uint64_t)syscall_entry);
  wrmsr(MSR_FMASK, 0x200);
}

#define SYSCALL_TABLE_SIZE 351
static const syscall_handler_fn syscall_table[SYSCALL_TABLE_SIZE] = {
    [SYS_READ] = handle_sys_read,
    [SYS_WRITE] = handle_sys_write,
    [SYS_OPEN] = handle_sys_open,
    [SYS_CLOSE] = handle_sys_close,
    [SYS_STAT] = handle_sys_stat,
    [SYS_FSTAT] = handle_sys_fstat,
    [SYS_LSTAT] = handle_sys_lstat,
    [SYS_POLL] = handle_sys_poll,
    [SYS_LSEEK] = handle_sys_lseek,
    [SYS_MMAP] = handle_sys_mmap,
    [SYS_MPROTECT] = handle_sys_mprotect,
    [SYS_MUNMAP] = handle_sys_munmap,
    [SYS_BRK] = handle_sys_brk,
    [SYS_RT_SIGACTION] = handle_sys_rt_sigaction,
    [SYS_RT_SIGPROCMASK] = handle_sys_rt_sigprocmask,
    [SYS_IOCTL] = handle_sys_ioctl,
    [SYS_PIPE] = handle_sys_pipe,
    [SYS_SCHED_YIELD] = handle_sys_sched_yield,
    [SYS_DUP] = handle_sys_dup,
    [SYS_DUP2] = handle_sys_dup2,
    [SYS_PAUSE] = handle_sys_pause,
    [SYS_NANOSLEEP] = handle_sys_nanosleep,
    [SYS_GETPID] = sys_cmd_get_pid,
    [SYS_SOCKET] = handle_sys_socket,
    [SYS_CONNECT] = handle_sys_connect,
    [SYS_ACCEPT] = handle_sys_accept,
    [SYS_SENDTO] = handle_sys_sendto,
    [SYS_RECVFROM] = handle_sys_recvfrom,
    [SYS_SENDMSG] = handle_sys_sendmsg,
    [SYS_RECVMSG] = handle_sys_recvmsg,
    [SYS_BIND] = handle_sys_bind,
    [SYS_LISTEN] = handle_sys_listen,
    [SYS_GETSOCKNAME] = handle_sys_getsockname,
    [SYS_GETPEERNAME] = handle_sys_getpeername,
    [SYS_SOCKETPAIR] = handle_sys_socketpair,
    [SYS_SETSOCKOPT] = handle_sys_setsockopt,
    [SYS_GETSOCKOPT] = handle_sys_getsockopt,
    [SYS_CLONE] = sys_cmd_clone_process,
    [SYS_FORK] = handle_sys_fork,
    [SYS_EXECVE] = handle_sys_execve,
    [SYS_WAIT4] = handle_sys_wait4,
    [SYS_KILL] = handle_sys_kill,
    [SYS_FCNTL] = handle_sys_fcntl,
    [SYS_RT_SIGPENDING] = handle_sys_rt_sigpending,
    [SYS_GETCWD] = handle_sys_getcwd,
    [SYS_CHDIR] = handle_sys_chdir,
    [SYS_MKDIR] = handle_sys_mkdir,
    [SYS_UNLINK] = handle_sys_unlink,
    [SYS_GETTIMEOFDAY] = handle_sys_gettimeofday,
    [SYS_TIMES] = handle_sys_times,
    [SYS_GETUID] = handle_sys_getuid,
    [SYS_GETGID] = handle_sys_getgid,
    [SYS_SETUID] = handle_sys_setuid,
    [SYS_SETGID] = handle_sys_setgid,
    [SYS_GETEUID] = handle_sys_geteuid,
    [SYS_GETEGID] = handle_sys_getegid,
    [SYS_SETREUID] = handle_sys_setreuid,
    [SYS_SETREGID] = handle_sys_setregid,
    [SYS_SETRESUID] = handle_sys_setresuid,
    [SYS_GETRESUID] = handle_sys_getresuid,
    [SYS_SETRESGID] = handle_sys_setresgid,
    [SYS_GETRESGID] = handle_sys_getresgid,
    [SYS_STATFS] = handle_sys_statfs,
    [SYS_FSTATFS] = handle_sys_fstatfs,
    [SYS_PRCTL] = handle_sys_prctl,
    [SYS_ARCH_PRCTL] = handle_sys_arch_prctl,
    [SYS_SYNC] = handle_sys_sync,
    [SYS_SETTIMEOFDAY] = handle_sys_settimeofday,
    [SYS_MOUNT] = handle_sys_mount,
    [SYS_UMOUNT2] = handle_sys_umount2,
    [SYS_REBOOT] = handle_sys_reboot,
    [SYS_GETTID] = sys_cmd_gettid,
    [SYS_FUTEX] = handle_sys_futex,
    [SYS_GETDENTS64] = handle_sys_getdents64,
    [SYS_SET_TID_ADDRESS] = handle_sys_set_tid_address,
    [SYS_CLOCK_SETTIME] = handle_sys_clock_settime,
    [SYS_CLOCK_GETTIME] = handle_sys_clock_gettime,
    [SYS_CLOCK_GETRES] = handle_sys_clock_getres,
    [SYS_EXIT_GROUP] = handle_sys_exit_group,
    [SYS_FACCESSAT] = handle_sys_faccessat,
    [SYS_SYNCFS] = handle_sys_syncfs,
    [SYS_SPAWN] = handle_sys_spawn,
};

static uint64_t syscall_handler_inner(registers_t *regs) {
  uint64_t syscall_num = regs->rax;

  syscall_args_t args = {
      .regs = regs,
      .arg1 = regs->rdi,
      .arg2 = regs->rsi,
      .arg3 = regs->rdx,
      .arg4 = regs->r10,
      .arg5 = regs->r8,
      .arg6 = regs->r9,
  };

  if (syscall_num < SYSCALL_TABLE_SIZE && syscall_table[syscall_num]) {
    return syscall_table[syscall_num](&args);
  }

  return 0;
}

static uint64_t syscall_maybe_deliver_signal(registers_t *regs, process_t *proc) {
  if (!proc || !proc->is_user || (regs->cs & 0x3) == 0)
    return (uint64_t)regs;

  uint64_t pending = proc->signal_pending & ~proc->signal_mask;
  if (!pending)
    return (uint64_t)regs;

  int sig = -1;
  for (int i = 1; i < MAX_SIGNALS; i++) {
    if (pending & (1ULL << (uint32_t)i)) {
      sig = i;
      break;
    }
  }
  if (sig < 0)
    return (uint64_t)regs;

  proc->signal_pending &= ~(1ULL << (uint32_t)sig);
  uint64_t handler = proc->signal_handlers[sig];
  int flags = proc->signal_action_flags[sig];

  if (handler == 1 || (handler == 0 && (sig == 17 /* SIGCHLD */ || sig == 28 /* SIGWINCH */ || sig == 23 /* SIGURG */))) {
    return (uint64_t)regs;
  }

  if (handler == 0 || sig == 9) {
    process_terminate_with_status(proc, sig & 0x7f);
    return process_schedule((uint64_t)regs);
  }

  if (flags & SA_RESETHAND) {
    proc->signal_handlers[sig] = 0;
    proc->signal_action_mask[sig] = 0;
    proc->signal_action_flags[sig] = 0;
  }
  /* Validate handler is a user-space address and the user stack
   * looks sane before writing into user memory. Reject/terminate
   * the process if the handler or stack pointer is invalid to
   * avoid corrupting kernel stack / descriptor frames. */
  const uint64_t KERNEL_BASE = 0xFFFFFFFF80000000ULL;
  if (handler == 1) {
    return (uint64_t)regs;
  }
  /* Handler must be in user-space (not in kernel direct map). */
  if (handler >= KERNEL_BASE) {
    return process_terminate_current_with_status(128 + 11, (uint64_t)regs);
  }

  uint64_t new_rsp = regs->rsp - sizeof(uint64_t);
  if (new_rsp >= KERNEL_BASE) {
    return process_terminate_current_with_status(128 + 11, (uint64_t)regs);
  }
  /* Ensure the target user address is mapped in the process page tables
   * and write to the underlying physical frame via p2v(). This avoids
   * accidentally writing into kernel memory if regs->rsp was corrupted
   * or pointed at an unmapped address. */
  mmu_context_t fallback_ctx = { .pml4_phys = proc->pml4_phys, .lock = SPINLOCK_INIT };
  mmu_context_t *uctx = (proc->vmm_space && proc->vmm_space->mmu_ctx) ? proc->vmm_space->mmu_ctx : &fallback_ctx;
  uint64_t phys = mmu_virt_to_phys(uctx, new_rsp);
  if (!phys) {
    return process_terminate_current_with_status(128 + 11, (uint64_t)regs);
  }
  uint64_t *target = (uint64_t *)p2v(phys);
  *target = regs->rip;
  regs->rsp = new_rsp;
  regs->rip = handler;
  regs->rdi = (uint64_t)sig;
  return (uint64_t)regs;
}

uint64_t syscall_handler_c(registers_t *regs) {
  uint64_t syscall_num = regs->rax;

  switch (syscall_num) {
    case SYS_GETPID: {
      process_t *cur = process_get_current();
      regs->rax = cur ? (uint64_t)cur->pid : (uint64_t)-1;
      if (__builtin_expect(cur && !cur->kill_pending && !(cur->signal_pending & ~cur->signal_mask), 1)) {
        return (uint64_t)regs;
      }
      if (cur && cur->kill_pending) {
        return process_terminate_current_with_status(cur->exit_status ? cur->exit_status : 1, (uint64_t)regs);
      }
      return syscall_maybe_deliver_signal(regs, cur);
    }
    case SYS_GETTID: {
      process_t *cur = process_get_current();
      regs->rax = cur ? (uint64_t)cur->pid : 0;
      if (__builtin_expect(cur && !cur->kill_pending && !(cur->signal_pending & ~cur->signal_mask), 1)) {
        return (uint64_t)regs;
      }
      if (cur && cur->kill_pending) {
        return process_terminate_current_with_status(cur->exit_status ? cur->exit_status : 1, (uint64_t)regs);
      }
      return syscall_maybe_deliver_signal(regs, cur);
    }
    case SYS_GETUID: {
      process_t *cur = process_get_current();
      regs->rax = cur ? (uint64_t)cur->uid : 0;
      if (__builtin_expect(cur && !cur->kill_pending && !(cur->signal_pending & ~cur->signal_mask), 1)) {
        return (uint64_t)regs;
      }
      if (cur && cur->kill_pending) {
        return process_terminate_current_with_status(cur->exit_status ? cur->exit_status : 1, (uint64_t)regs);
      }
      return syscall_maybe_deliver_signal(regs, cur);
    }
    case SYS_GETEUID: {
      process_t *cur = process_get_current();
      regs->rax = cur ? (uint64_t)cur->euid : 0;
      if (__builtin_expect(cur && !cur->kill_pending && !(cur->signal_pending & ~cur->signal_mask), 1)) {
        return (uint64_t)regs;
      }
      if (cur && cur->kill_pending) {
        return process_terminate_current_with_status(cur->exit_status ? cur->exit_status : 1, (uint64_t)regs);
      }
      return syscall_maybe_deliver_signal(regs, cur);
    }
    case SYS_GETGID: {
      process_t *cur = process_get_current();
      regs->rax = cur ? (uint64_t)cur->gid : 0;
      if (__builtin_expect(cur && !cur->kill_pending && !(cur->signal_pending & ~cur->signal_mask), 1)) {
        return (uint64_t)regs;
      }
      if (cur && cur->kill_pending) {
        return process_terminate_current_with_status(cur->exit_status ? cur->exit_status : 1, (uint64_t)regs);
      }
      return syscall_maybe_deliver_signal(regs, cur);
    }
    case SYS_GETEGID: {
      process_t *cur = process_get_current();
      regs->rax = cur ? (uint64_t)cur->egid : 0;
      if (__builtin_expect(cur && !cur->kill_pending && !(cur->signal_pending & ~cur->signal_mask), 1)) {
        return (uint64_t)regs;
      }
      if (cur && cur->kill_pending) {
        return process_terminate_current_with_status(cur->exit_status ? cur->exit_status : 1, (uint64_t)regs);
      }
      return syscall_maybe_deliver_signal(regs, cur);
    }
    case SYS_EXIT: {
      int status = (int)regs->rdi;
      return process_terminate_current_with_status((status & 0xff) << 8, (uint64_t)regs);
    }
    default:
      break;
  }

  // Normal syscalls
  regs->rax = syscall_handler_inner(regs);

  process_t *cur_proc = process_get_current();
  if (cur_proc && cur_proc->kill_pending) {
    return process_terminate_current_with_status(cur_proc->exit_status ? cur_proc->exit_status : 1, (uint64_t)regs);
  }

  if (cur_proc && cur_proc->state == PROC_STATE_BLOCKED) {
    return process_schedule((uint64_t)regs);
  }

  if (syscall_num == SYS_SCHED_YIELD) {
    regs->rax = 0;
    return process_schedule((uint64_t)regs);
  }

  return syscall_maybe_deliver_signal(regs, cur_proc);
}
