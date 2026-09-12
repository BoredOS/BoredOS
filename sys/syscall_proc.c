// Copyright (c) 2023-2026 Christiaan (chris@boreddev.nl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
#include "syscall_internal.h"
#include "panic.h"

typedef struct {
  uint64_t sa_handler;
  uint64_t sa_mask;
  int sa_flags;
} k_sigaction_t;

#define SA_RESETHAND 0x80000000
#define SIGKILL_NUM 9

uint64_t sys_cmd_spawn_process(const syscall_args_t *args) {
  process_t *proc = process_get_current();
  const char *user_path = (const char *)args->arg2;
  const char *user_args = (const char *)args->arg3;
  uint64_t flags = args->arg4;
  int tty_id = (int)args->arg5;

  if (!user_path || !is_valid_user_string(user_path, 256))
    return (uint64_t)-EFAULT;
  if (user_args && !is_valid_user_string(user_args, 512))
    return (uint64_t)-EFAULT;

  char path_buf[256];
  int pi = 0;
  while (pi < 255 && user_path[pi]) {
    path_buf[pi] = user_path[pi];
    pi++;
  }
  path_buf[pi] = 0;

  char args_buf[512];
  const char *args_ptr = NULL;
  if (user_args) {
    int ai = 0;
    while (ai < 511 && user_args[ai]) {
      args_buf[ai] = user_args[ai];
      ai++;
    }
    args_buf[ai] = 0;
    args_ptr = args_buf;
  }

  int effective_tty = -1;
  if (flags & SPAWN_FLAG_TTY_ID)
    effective_tty = tty_id;
  else if (flags & SPAWN_FLAG_INHERIT_TTY)
    effective_tty = proc ? proc->tty_id : -1;

  process_t *child =
      process_create_elf(path_buf, args_ptr, flags, effective_tty);
  if (!child)
    return -1;
  return (uint64_t)child->pid;
}

static uint64_t sys_cmd_exec_process(const syscall_args_t *args) {
  const char *user_path = (const char *)args->arg2;
  const char *user_args = (const char *)args->arg3;
  if (!user_path || !is_valid_user_string(user_path, 256))
    return (uint64_t)-EFAULT;
  if (user_args && !is_valid_user_string(user_args, 512))
    return (uint64_t)-EFAULT;

  char path_buf[256];
  int pi = 0;
  while (pi < 255 && user_path[pi]) {
    path_buf[pi] = user_path[pi];
    pi++;
  }
  path_buf[pi] = 0;

  char args_buf[512];
  const char *args_ptr = NULL;
  if (user_args) {
    int ai = 0;
    while (ai < 511 && user_args[ai]) {
      args_buf[ai] = user_args[ai];
      ai++;
    }
    args_buf[ai] = 0;
    args_ptr = args_buf;
  }

  return process_exec_replace_current(args->regs, path_buf, args_ptr);
}

static uint64_t sys_cmd_fork_process(const syscall_args_t *args) {
  extern process_t *process_duplicate(registers_t * parent_regs);
  process_t *child = process_duplicate(args->regs);
  if (!child)
    return -1;
  return child->pid;
}

uint64_t sys_cmd_clone_process(const syscall_args_t *args) {
  extern process_t *process_create_thread(registers_t *parent_regs, uint64_t entry_point, uint64_t user_sp, uint64_t flags);
  uint64_t entry_point = args->arg1;
  uint64_t user_sp = args->arg2;
  uint64_t flags = args->arg3;
  process_t *child = process_create_thread(args->regs, entry_point, user_sp, flags);
  if (!child)
    return (uint64_t)-1;
  return child->pid;
}

uint64_t sys_cmd_get_pid(const syscall_args_t *args) {
  (void)args;
  process_t *proc = process_get_current();
  if (!proc) return (uint64_t)-1;
  return (uint64_t)proc->pid;
}

uint64_t sys_cmd_gettid(const syscall_args_t *args) {
  (void)args;
  return process_get_current_pid();
}

uint64_t handle_sys_set_tid_address(const syscall_args_t *args) {
  (void)args;
  return process_get_current_pid();
}

uint64_t handle_sys_exit_group(const syscall_args_t *args) {
  process_t *proc = process_get_current();
  if (proc) {
    process_terminate_with_status(proc, (int)args->arg2);
  }
  return 0;
}

static uint64_t sys_cmd_waitpid(const syscall_args_t *args) {
  process_t *proc = process_get_current();
  int pid = (int)args->arg2;
  int *status = (int *)args->arg3;
  int options = (int)args->arg4;
  if (!proc)
    return -1;

  int st = 0;
  int res = process_waitpid(proc->pid, pid, options, &st);
  if (res == -2) {
    if (options & 1)
      return 0; // WNOHANG
    return (uint64_t)-2;
  }
  if (res < 0)
    return (uint64_t)-1;
  if (status) {
    if (!is_valid_user_ptr(status, sizeof(int))) return (uint64_t)-EFAULT;
    *status = st;
  }
  return (uint64_t)res;
}

static uint64_t sys_cmd_kill_signal(const syscall_args_t *args) {
  int pid = (int)args->arg2;
  int sig = (int)args->arg3;

  if (sig < 0 || sig >= MAX_SIGNALS)
    return (uint64_t)-1;

  if (pid < -1) {
    int res = signal_send_to_pgrp(-pid, sig);
    return res < 0 ? (uint64_t)res : 0;
  }

  if (pid == -1) {
    int max_pids = 1024;
    uint32_t *pids = (uint32_t *)kmalloc(max_pids * sizeof(uint32_t));
    if (!pids) return (uint64_t)-1;
    extern int process_get_all_pids(uint32_t *pids_out, int max_pids);
    int n = process_get_all_pids(pids, max_pids);
    process_t *cur = process_get_current();
    uint32_t my_pid = cur ? cur->pid : 0;
    for (int i = 0; i < n; i++) {
      if (pids[i] <= 1 || pids[i] == my_pid) continue;
      process_t *p = process_get_by_pid(pids[i]);
      if (!p) continue;
      bool is_user = p->is_user;
      bool is_dead = (p->state == PROC_STATE_ZOMBIE || p->exited);
      process_put(p);
      if (!is_user || is_dead) continue;
      signal_send_to_pid((int)pids[i], sig);
    }
    kfree(pids);
    return 0;
  }

  process_t *target = process_get_by_pid((uint32_t)pid);
  if (!target)
    return (uint64_t)-ESRCH;

  if (target->state == PROC_STATE_ZOMBIE || target->exited) {
    process_put(target);
    return 0;
  }

  process_t *cur = process_get_current();
  if (cur && cur->euid != 0 && cur->euid != target->uid && cur->uid != target->uid) {
    process_put(target);
    return (uint64_t)-EPERM;
  }

  if (sig == 0) {
    process_put(target);
    return 0;
  }

  // PID 1 protection:
  // Cannot be killed by SIGKILL or SIGSTOP.
  // If an unhandled fatal hardware/crash signal (SIGSEGV, SIGILL, SIGFPE, SIGBUS) hits PID 1, panic.
  // Other unhandled signals are ignored rather than terminating init.
  // it shall be protected by it's mother. a mother shall allways protect her child.
  // (especially the brightest one)
  if (target->pid == 1) {
    if (sig == 9 || sig == 19) {
      process_put(target);
      return 0;
    }
    if (target->signal_handlers[sig] == 0) {
      if (sig == 11 /* SIGSEGV */ || sig == 4 /* SIGILL */ || sig == 8 /* SIGFPE */ || sig == 7 /* SIGBUS */) {
        process_put(target);
        kernel_panic(NULL, "Kernel panic - not syncing: Attempted to kill init! (Fatal crash signal received)");
      }
      process_put(target);
      return 0;
    }
  }

  if (sig == 9) {
    process_terminate_with_status(target, sig & 0x7f);
    process_put(target);
    return 0;
  }

  if (target->signal_handlers[sig] == 1 ||
      (target->signal_handlers[sig] == 0 && (sig == 17 || sig == 28 || sig == 23))) {
    process_put(target);
    return 0;
  }

  if (target->signal_handlers[sig] == 0) {
    process_terminate_with_status(target, sig & 0x7f);
    process_put(target);
    return 0;
  }

  target->signal_pending |= (1ULL << (uint32_t)sig);
  if (target->state == PROC_STATE_BLOCKED) {
    target->state = PROC_STATE_RUNNING;
    target->sleep_until = 0;
  }
  process_put(target);
  return 0;
}

int signal_send_to_pid(int pid, int sig) {
  syscall_args_t args = {
    .arg2 = (uint64_t)pid,
    .arg3 = (uint64_t)sig
  };
  return (int)sys_cmd_kill_signal(&args);
}

typedef struct {
  uint32_t pgrp;
  int sig;
  int count;
} send_pgrp_arg_t;

static void signal_pgrp_cb(process_t *proc, void *arg) {
  send_pgrp_arg_t *a = (send_pgrp_arg_t *)arg;
  if (!proc || !proc->is_user || proc->state == PROC_STATE_ZOMBIE || proc->exited) return;
  if (proc->pgid != a->pgrp && proc->pid != (uint32_t)a->pgrp) return;

  int sig = a->sig;
  if (sig <= 0 || sig >= MAX_SIGNALS) return;

  // Deliver signal directly — we already hold process_table_lock, so we cannot
  // call signal_send_to_pid() which would call process_get_by_pid() and try to
  // re-acquire that same lock (recursive spinlock = deadlock).
  if (proc->signal_handlers[sig] == 1 ||
      (proc->signal_handlers[sig] == 0 && (sig == 17 || sig == 28 || sig == 23))) {
    return; // SIG_IGN or ignored by default
  }
  proc->signal_pending |= (1ULL << (uint32_t)sig);
  if (proc->state == PROC_STATE_BLOCKED) {
    proc->state = PROC_STATE_RUNNING;
    proc->sleep_until = 0;
  }
  a->count++;
}

int signal_send_to_pgrp(int pgrp, int sig) {
  if (pgrp <= 0) return -1;
  send_pgrp_arg_t a = { .pgrp = (uint32_t)pgrp, .sig = sig, .count = 0 };
  process_table_for_each(signal_pgrp_cb, &a);
  return a.count > 0 ? 0 : -ESRCH;
}

static uint64_t sys_cmd_sigaction(const syscall_args_t *args) {
  process_t *proc = process_get_current();
  int sig = (int)args->arg2;
  const k_sigaction_t *act = (const k_sigaction_t *)args->arg3;
  k_sigaction_t *oldact = (k_sigaction_t *)args->arg4;
  if (!proc || sig <= 0 || sig >= MAX_SIGNALS)
    return (uint64_t)-EINVAL;

  if (act && !is_valid_user_ptr(act, sizeof(k_sigaction_t)))
    return (uint64_t)-EFAULT;
  if (oldact && !is_valid_user_ptr(oldact, sizeof(k_sigaction_t)))
    return (uint64_t)-EFAULT;

  if (oldact) {
    oldact->sa_handler = proc->signal_handlers[sig];
    oldact->sa_mask = proc->signal_action_mask[sig];
    oldact->sa_flags = proc->signal_action_flags[sig];
  }
  if (act) {
    if (sig == SIGKILL_NUM && act->sa_handler != 0) {
      return -1;
    }
    proc->signal_handlers[sig] = act->sa_handler;
    proc->signal_action_mask[sig] = act->sa_mask;
    proc->signal_action_flags[sig] = act->sa_flags;
  }
  return 0;
}

static uint64_t sys_cmd_sigprocmask(const syscall_args_t *args) {
  process_t *proc = process_get_current();
  int how = (int)args->arg2;
  const uint64_t *set = (const uint64_t *)args->arg3;
  uint64_t *oldset = (uint64_t *)args->arg4;
  if (!proc)
    return (uint64_t)-EINVAL;

  if (set && !is_valid_user_ptr(set, sizeof(uint64_t)))
    return (uint64_t)-EFAULT;
  if (oldset && !is_valid_user_ptr(oldset, sizeof(uint64_t)))
    return (uint64_t)-EFAULT;

  if (oldset) {
    *oldset = proc->signal_mask;
  }
  if (!set)
    return 0;

  if (how == 0) {
    proc->signal_mask |= *set;
  } else if (how == 1) {
    proc->signal_mask &= ~(*set);
  } else if (how == 2) {
    proc->signal_mask = *set;
  } else {
    return -1;
  }
  proc->signal_mask &= ~(1ULL << SIGKILL_NUM);

  return 0;
}

static uint64_t sys_cmd_sigpending(const syscall_args_t *args) {
  process_t *proc = process_get_current();
  uint64_t *set = (uint64_t *)args->arg2;
  if (!proc)
    return (uint64_t)-EINVAL;
  if (!set || !is_valid_user_ptr(set, sizeof(uint64_t)))
    return (uint64_t)-EFAULT;
  *set = proc->signal_pending;
  return 0;
}

uint64_t handle_sys_fork(const syscall_args_t *args) {
  return sys_cmd_fork_process(args);
}

uint64_t handle_sys_spawn(const syscall_args_t *args) {
  syscall_args_t shifted = *args;
  shifted.arg2 = args->arg1; // path
  shifted.arg3 = args->arg2; // args
  shifted.arg4 = args->arg3; // flags
  shifted.arg5 = args->arg4; // tty_id
  return sys_cmd_spawn_process(&shifted);
}

uint64_t handle_sys_execve(const syscall_args_t *args) {
  syscall_args_t shifted = *args;
  shifted.arg2 = args->arg1; // path
  shifted.arg3 = args->arg2; // args
  return sys_cmd_exec_process(&shifted);
}

uint64_t handle_sys_wait4(const syscall_args_t *args) {
  syscall_args_t shifted = *args;
  shifted.arg2 = args->arg1; // pid
  shifted.arg3 = args->arg2; // status
  shifted.arg4 = args->arg3; // options
  return sys_cmd_waitpid(&shifted);
}

uint64_t handle_sys_kill(const syscall_args_t *args) {
  syscall_args_t shifted = *args;
  shifted.arg2 = args->arg1; // pid
  shifted.arg3 = args->arg2; // sig
  return sys_cmd_kill_signal(&shifted);
}

uint64_t handle_sys_rt_sigaction(const syscall_args_t *args) {
  syscall_args_t shifted = *args;
  shifted.arg2 = args->arg1; // sig
  shifted.arg3 = args->arg2; // act
  shifted.arg4 = args->arg3; // oact
  return sys_cmd_sigaction(&shifted);
}

uint64_t handle_sys_rt_sigprocmask(const syscall_args_t *args) {
  syscall_args_t shifted = *args;
  shifted.arg2 = args->arg1; // how
  shifted.arg3 = args->arg2; // set
  shifted.arg4 = args->arg3; // oset
  return sys_cmd_sigprocmask(&shifted);
}

uint64_t handle_sys_rt_sigpending(const syscall_args_t *args) {
  syscall_args_t shifted = *args;
  shifted.arg2 = args->arg1; // set
  return sys_cmd_sigpending(&shifted);
}

uint64_t handle_sys_pause(const syscall_args_t *args) {
  (void)args;
  process_t *proc = process_get_current();
  if (!proc) return (uint64_t)-1;

  proc->sleep_until = 0;
  proc->state = PROC_STATE_BLOCKED;
  return (uint64_t)-4; // -EINTR
}

uint64_t handle_sys_sched_yield(const syscall_args_t *args) {
  (void)args;
  return 0;
}

uint64_t handle_sys_arch_prctl(const syscall_args_t *args) {
  process_t *proc = process_get_current();
  if (!proc) return (uint64_t)-1;

  if (args->arg1 == 0x1002) { // ARCH_SET_FS
    proc->fs_base = args->arg2;
    wrmsr(MSR_FS_BASE, args->arg2);
    return 0;
  } else if (args->arg1 == 0x1003) { // ARCH_GET_FS
    if (args->arg2) *(uint64_t *)args->arg2 = proc->fs_base;
    return 0;
  }
  return (uint64_t)-1;
}

uint64_t handle_sys_prctl(const syscall_args_t *args) {
  int option = (int)args->arg1;
  unsigned long arg2 = (unsigned long)args->arg2;
  process_t *proc = process_get_current();
  if (!proc) return (uint64_t)-EINVAL;

  if (option == PR_SET_CHILD_SUBREAPER) {
    extern uint32_t reaper_pid;
    if (arg2) {
      reaper_pid = proc->pid;
    } else {
      if (reaper_pid == proc->pid) reaper_pid = 0;
    }
    return 0;
  } else if (option == PR_GET_CHILD_SUBREAPER) {
    extern uint32_t reaper_pid;
    int *uptr = (int *)arg2;
    if (!uptr || !is_valid_user_ptr(uptr, sizeof(int))) return (uint64_t)-EFAULT;
    *uptr = (reaper_pid == proc->pid) ? 1 : 0;
    return 0;
  }
  return (uint64_t)-EINVAL;
}

uint64_t handle_sys_getuid(const syscall_args_t *args) {
  (void)args;
  process_t *proc = process_get_current();
  if (!proc) return 0;
  return (uint64_t)proc->uid;
}

uint64_t handle_sys_getgid(const syscall_args_t *args) {
  (void)args;
  process_t *proc = process_get_current();
  if (!proc) return 0;
  return (uint64_t)proc->gid;
}

uint64_t handle_sys_geteuid(const syscall_args_t *args) {
  (void)args;
  process_t *proc = process_get_current();
  if (!proc) return 0;
  return (uint64_t)proc->euid;
}

uint64_t handle_sys_getegid(const syscall_args_t *args) {
  (void)args;
  process_t *proc = process_get_current();
  if (!proc) return 0;
  return (uint64_t)proc->egid;
}

uint64_t handle_sys_setuid(const syscall_args_t *args) {
  process_t *proc = process_get_current();
  if (!proc) return (uint64_t)-ESRCH;
  uid_t new_uid = (uid_t)args->arg1;
  if (proc->euid == 0) {
    proc->uid = new_uid;
    proc->euid = new_uid;
    proc->suid = new_uid;
    return 0;
  }
  if (new_uid == proc->uid || new_uid == proc->suid) {
    proc->euid = new_uid;
    return 0;
  }
  return (uint64_t)-EPERM;
}

uint64_t handle_sys_setgid(const syscall_args_t *args) {
  process_t *proc = process_get_current();
  if (!proc) return (uint64_t)-ESRCH;
  gid_t new_gid = (gid_t)args->arg1;
  if (proc->euid == 0) {
    proc->gid = new_gid;
    proc->egid = new_gid;
    proc->sgid = new_gid;
    return 0;
  }
  if (new_gid == proc->gid || new_gid == proc->sgid) {
    proc->egid = new_gid;
    return 0;
  }
  return (uint64_t)-EPERM;
}

uint64_t handle_sys_setreuid(const syscall_args_t *args) {
  process_t *proc = process_get_current();
  if (!proc) return (uint64_t)-ESRCH;
  uid_t ruid = (uid_t)args->arg1;
  uid_t euid = (uid_t)args->arg2;
  uid_t cur_ruid = proc->uid;
  uid_t cur_euid = proc->euid;
  uid_t cur_suid = proc->suid;

  if (ruid != (uid_t)-1) {
    if (cur_euid != 0 && ruid != cur_ruid && ruid != cur_euid) {
      return (uint64_t)-EPERM;
    }
    proc->uid = ruid;
  }
  if (euid != (uid_t)-1) {
    if (cur_euid != 0 && euid != cur_ruid && euid != cur_euid && euid != cur_suid) {
      proc->uid = cur_ruid;
      return (uint64_t)-EPERM;
    }
    proc->euid = euid;
  }
  if (ruid != (uid_t)-1 || (euid != (uid_t)-1 && euid != cur_ruid)) {
    proc->suid = proc->euid;
  }
  return 0;
}

uint64_t handle_sys_setregid(const syscall_args_t *args) {
  process_t *proc = process_get_current();
  if (!proc) return (uint64_t)-ESRCH;
  gid_t rgid = (gid_t)args->arg1;
  gid_t egid = (gid_t)args->arg2;
  gid_t cur_rgid = proc->gid;
  gid_t cur_egid = proc->egid;
  gid_t cur_sgid = proc->sgid;

  if (rgid != (gid_t)-1) {
    if (proc->euid != 0 && rgid != cur_rgid && rgid != cur_egid) {
      return (uint64_t)-EPERM;
    }
    proc->gid = rgid;
  }
  if (egid != (gid_t)-1) {
    if (proc->euid != 0 && egid != cur_rgid && egid != cur_egid && egid != cur_sgid) {
      proc->gid = cur_rgid;
      return (uint64_t)-EPERM;
    }
    proc->egid = egid;
  }
  if (rgid != (gid_t)-1 || (egid != (gid_t)-1 && egid != cur_rgid)) {
    proc->sgid = proc->egid;
  }
  return 0;
}

uint64_t handle_sys_setresuid(const syscall_args_t *args) {
  process_t *proc = process_get_current();
  if (!proc) return (uint64_t)-ESRCH;
  uid_t ruid = (uid_t)args->arg1;
  uid_t euid = (uid_t)args->arg2;
  uid_t suid = (uid_t)args->arg3;
  if (proc->euid == 0) {
    if (ruid != (uid_t)-1) proc->uid = ruid;
    if (euid != (uid_t)-1) proc->euid = euid;
    if (suid != (uid_t)-1) proc->suid = suid;
    return 0;
  }
  if ((ruid != (uid_t)-1 && ruid != proc->uid && ruid != proc->euid && ruid != proc->suid) ||
      (euid != (uid_t)-1 && euid != proc->uid && euid != proc->euid && euid != proc->suid) ||
      (suid != (uid_t)-1 && suid != proc->uid && suid != proc->euid && suid != proc->suid)) {
    return (uint64_t)-EPERM;
  }
  if (ruid != (uid_t)-1) proc->uid = ruid;
  if (euid != (uid_t)-1) proc->euid = euid;
  if (suid != (uid_t)-1) proc->suid = suid;
  return 0;
}

uint64_t handle_sys_getresuid(const syscall_args_t *args) {
  process_t *proc = process_get_current();
  if (!proc) return (uint64_t)-ESRCH;
  uid_t *ruid = (uid_t *)args->arg1;
  uid_t *euid = (uid_t *)args->arg2;
  uid_t *suid = (uid_t *)args->arg3;
  if (ruid && !is_valid_user_ptr(ruid, sizeof(uid_t))) return (uint64_t)-EFAULT;
  if (euid && !is_valid_user_ptr(euid, sizeof(uid_t))) return (uint64_t)-EFAULT;
  if (suid && !is_valid_user_ptr(suid, sizeof(uid_t))) return (uint64_t)-EFAULT;
  if (ruid) *ruid = proc->uid;
  if (euid) *euid = proc->euid;
  if (suid) *suid = proc->suid;
  return 0;
}

uint64_t handle_sys_setresgid(const syscall_args_t *args) {
  process_t *proc = process_get_current();
  if (!proc) return (uint64_t)-ESRCH;
  gid_t rgid = (gid_t)args->arg1;
  gid_t egid = (gid_t)args->arg2;
  gid_t sgid = (gid_t)args->arg3;
  if (proc->euid == 0) {
    if (rgid != (gid_t)-1) proc->gid = rgid;
    if (egid != (gid_t)-1) proc->egid = egid;
    if (sgid != (gid_t)-1) proc->sgid = sgid;
    return 0;
  }
  if ((rgid != (gid_t)-1 && rgid != proc->gid && rgid != proc->egid && rgid != proc->sgid) ||
      (egid != (gid_t)-1 && egid != proc->gid && egid != proc->egid && egid != proc->sgid) ||
      (sgid != (gid_t)-1 && sgid != proc->gid && sgid != proc->egid && sgid != proc->sgid)) {
    return (uint64_t)-EPERM;
  }
  if (rgid != (gid_t)-1) proc->gid = rgid;
  if (egid != (gid_t)-1) proc->egid = egid;
  if (sgid != (gid_t)-1) proc->sgid = sgid;
  return 0;
}

uint64_t handle_sys_getresgid(const syscall_args_t *args) {
  process_t *proc = process_get_current();
  if (!proc) return (uint64_t)-ESRCH;
  gid_t *rgid = (gid_t *)args->arg1;
  gid_t *egid = (gid_t *)args->arg2;
  gid_t *sgid = (gid_t *)args->arg3;
  if (rgid && !is_valid_user_ptr(rgid, sizeof(gid_t))) return (uint64_t)-EFAULT;
  if (egid && !is_valid_user_ptr(egid, sizeof(gid_t))) return (uint64_t)-EFAULT;
  if (sgid && !is_valid_user_ptr(sgid, sizeof(gid_t))) return (uint64_t)-EFAULT;
  if (rgid) *rgid = proc->gid;
  if (egid) *egid = proc->egid;
  if (sgid) *sgid = proc->sgid;
  return 0;
}
