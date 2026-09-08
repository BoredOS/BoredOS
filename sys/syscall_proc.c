// Copyright (c) 2023-2026 Christiaan (chris@boreddev.nl)
// This software is released under the GNU General Public License v3.0. See
// LICENSE file for details. This header needs to maintain in any file it is
// present in, as per the GPL license terms.
#include "syscall_internal.h"

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

  bool terminal_proc = (flags & SPAWN_FLAG_TERMINAL) != 0;
  int effective_tty = -1;
  if (flags & SPAWN_FLAG_TTY_ID)
    effective_tty = tty_id;
  else if (flags & SPAWN_FLAG_INHERIT_TTY)
    effective_tty = proc ? proc->tty_id : -1;

  process_t *child =
      process_create_elf(path_buf, args_ptr, terminal_proc, effective_tty);
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
  process_t *target;
  if (pid == -1) {
    target = process_get_current();
  } else {
    target = process_get_by_pid((uint32_t)pid);
  }
  if (!target)
    return -1;
  if (sig == 0) {
    if (pid != -1) process_put(target);
    return 0;
  }
  if (sig <= 0 || sig >= MAX_SIGNALS) {
    if (pid != -1) process_put(target);
    return -1;
  }

  if (sig == 9) {
    process_terminate_with_status(target, sig & 0x7f);
    if (pid != -1) process_put(target);
    return 0;
  }

  if (target->signal_handlers[sig] == 1 ||
      (target->signal_handlers[sig] == 0 && (sig == 17 || sig == 28 || sig == 23))) {
    if (pid != -1) process_put(target);
    return 0;
  }

  if (target->signal_handlers[sig] == 0) {
    process_terminate_with_status(target, sig & 0x7f);
    if (pid != -1) process_put(target);
    return 0;
  }

  target->signal_pending |= (1ULL << (uint32_t)sig);
  if (target->state == PROC_STATE_BLOCKED) {
    target->state = PROC_STATE_RUNNING;
    target->sleep_until = 0;
  }
  if (pid != -1) process_put(target);
  return 0;
}

int signal_send_to_pid(int pid, int sig) {
  syscall_args_t args = {
    .arg2 = (uint64_t)pid,
    .arg3 = (uint64_t)sig
  };
  return (int)sys_cmd_kill_signal(&args);
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
