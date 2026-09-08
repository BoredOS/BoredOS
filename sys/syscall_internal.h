// Copyright (c) 2023-2026 Christiaan (chris@boreddev.nl)
// This software is released under the GNU General Public License v3.0. See
// LICENSE file for details. This header needs to maintain in any file it is
// present in, as per the GPL license terms.
#ifndef SYSCALL_INTERNAL_H
#define SYSCALL_INTERNAL_H

#include "syscall.h"
#include "gdt.h"
#include "slab.h"
#include "process.h"
#include "vfs.h"
#include "shm.h"
#include "errno.h"

#include "mmu.h"
#include "pmm.h"
#include "vmm.h"
#include "vma.h"

#include "cmd.h"
#include "disk.h"
#include "graphics.h"
#include "keycodes.h"
#include "keymap.h"
#include "io.h"
#include "kutils.h"
#include "network.h"
#include "pagecache.h"
#include "pci.h"
#include "platform.h"
#include "smp.h"
#include "tty.h"
#include "pty.h"
#include "unix_socket.h"
#include "wait_queue.h"
#include "work_queue.h"
#include <string.h>

#define SPAWN_FLAG_TERMINAL 0x1
#define SPAWN_FLAG_INHERIT_TTY 0x2
#define SPAWN_FLAG_TTY_ID 0x4

#define MSR_FS_BASE 0xC0000100

#define O_RDONLY 0x0000
#define O_WRONLY 0x0001
#define O_RDWR 0x0002
#define O_APPEND 0x0400
#define O_NONBLOCK 0x0800
#define F_GETFL 3
#define F_SETFL 4

#define SA_RESETHAND 0x80000000
#define SIGKILL_NUM 9

#define PROT_READ 0x1
#define PROT_WRITE 0x2
#define PROT_EXEC 0x4
#define MAP_SHARED 0x01
#define MAP_PRIVATE 0x02
#define MAP_FIXED 0x10
#define MAP_ANONYMOUS 0x20
#define MAP_FAILED ((void *)-1)

#define PR_SET_CHILD_SUBREAPER 36
#define PR_GET_CHILD_SUBREAPER 37

#define LINUX_REBOOT_MAGIC1 0xfee1dead
#define LINUX_REBOOT_MAGIC2 672274793
#define LINUX_REBOOT_CMD_RESTART 0x01234567
#define LINUX_REBOOT_CMD_HALT 0xcdef0123
#define LINUX_REBOOT_CMD_POWER_OFF 0x4321fedc

#define AT_FDCWD -100

struct linux_dirent64 {
  uint64_t d_ino;
  int64_t  d_off;
  unsigned short d_reclen;
  unsigned char  d_type;
  char     d_name[];
};

struct statfs {
  uint64_t f_type;
  uint64_t f_bsize;
  uint64_t f_blocks;
  uint64_t f_bfree;
  uint64_t f_bavail;
  uint64_t f_files;
  uint64_t f_ffree;
  struct { int val[2]; } f_fsid;
  uint64_t f_namelen;
  uint64_t f_frsize;
  uint64_t f_flags;
  uint64_t f_spare[4];
};

typedef struct {
  registers_t *regs;
  uint64_t arg1;
  uint64_t arg2;
  uint64_t arg3;
  uint64_t arg4;
  uint64_t arg5;
  uint64_t arg6;
} syscall_args_t;

typedef uint64_t (*syscall_handler_fn)(const syscall_args_t *args);

// Validation helpers
bool is_valid_user_ptr(const void *ptr, size_t size);
bool is_valid_user_string(const char *str, size_t max_len);

// FD table helpers
int fs_alloc_fd_slot(process_t *proc, int start);
int fs_mode_to_flags(const char *mode);
void fd_addref(process_t *proc, int fd);

// Pipe helpers
process_fd_pipe_t *fs_create_pipe_state(void);
void fs_pipe_drop_reader(process_fd_pipe_t **pipe);
void fs_pipe_drop_writer(process_fd_pipe_t **pipe);
void poll_cleanup(process_t *proc);

// File / IO Syscall Handlers (syscall_file.c)
uint64_t handle_sys_read(const syscall_args_t *args);
uint64_t handle_sys_write(const syscall_args_t *args);
uint64_t handle_sys_open(const syscall_args_t *args);
uint64_t handle_sys_close(const syscall_args_t *args);
uint64_t handle_sys_stat(const syscall_args_t *args);
uint64_t handle_sys_lseek(const syscall_args_t *args);
uint64_t handle_sys_poll(const syscall_args_t *args);
uint64_t handle_sys_ioctl(const syscall_args_t *args);
uint64_t handle_sys_pipe(const syscall_args_t *args);
uint64_t handle_sys_dup(const syscall_args_t *args);
uint64_t handle_sys_dup2(const syscall_args_t *args);
uint64_t handle_sys_fcntl(const syscall_args_t *args);
uint64_t handle_sys_getcwd(const syscall_args_t *args);
uint64_t handle_sys_chdir(const syscall_args_t *args);
uint64_t handle_sys_mkdir(const syscall_args_t *args);
uint64_t handle_sys_unlink(const syscall_args_t *args);
uint64_t handle_sys_statfs(const syscall_args_t *args);
uint64_t handle_sys_fstatfs(const syscall_args_t *args);
uint64_t handle_sys_getdents64(const syscall_args_t *args);
uint64_t handle_sys_faccessat(const syscall_args_t *args);
uint64_t handle_sys_mount(const syscall_args_t *args);
uint64_t handle_sys_umount2(const syscall_args_t *args);
uint64_t handle_sys_sync(const syscall_args_t *args);
uint64_t handle_sys_syncfs(const syscall_args_t *args);

// Process / Signal Syscall Handlers (syscall_proc.c)
uint64_t handle_sys_fork(const syscall_args_t *args);
uint64_t sys_cmd_clone_process(const syscall_args_t *args);
uint64_t handle_sys_execve(const syscall_args_t *args);
uint64_t handle_sys_wait4(const syscall_args_t *args);
uint64_t handle_sys_exit_group(const syscall_args_t *args);
uint64_t handle_sys_kill(const syscall_args_t *args);
uint64_t handle_sys_rt_sigaction(const syscall_args_t *args);
uint64_t handle_sys_rt_sigprocmask(const syscall_args_t *args);
uint64_t handle_sys_rt_sigpending(const syscall_args_t *args);
uint64_t sys_cmd_get_pid(const syscall_args_t *args);
uint64_t sys_cmd_gettid(const syscall_args_t *args);
uint64_t handle_sys_set_tid_address(const syscall_args_t *args);
uint64_t handle_sys_sched_yield(const syscall_args_t *args);
uint64_t handle_sys_arch_prctl(const syscall_args_t *args);
uint64_t handle_sys_prctl(const syscall_args_t *args);

// Memory Syscall Handlers (syscall_mem.c)
uint64_t handle_sys_mmap(const syscall_args_t *args);
uint64_t handle_sys_munmap(const syscall_args_t *args);
uint64_t handle_sys_mprotect(const syscall_args_t *args);
uint64_t handle_sys_brk(const syscall_args_t *args);

// Network / Socket Syscall Handlers (syscall_net.c)
uint64_t handle_sys_socket(const syscall_args_t *args);
uint64_t handle_sys_connect(const syscall_args_t *args);
uint64_t handle_sys_accept(const syscall_args_t *args);
uint64_t handle_sys_sendto(const syscall_args_t *args);
uint64_t handle_sys_recvfrom(const syscall_args_t *args);
uint64_t handle_sys_sendmsg(const syscall_args_t *args);
uint64_t handle_sys_recvmsg(const syscall_args_t *args);
uint64_t handle_sys_bind(const syscall_args_t *args);
uint64_t handle_sys_listen(const syscall_args_t *args);
uint64_t handle_sys_getsockname(const syscall_args_t *args);
uint64_t handle_sys_getpeername(const syscall_args_t *args);
uint64_t handle_sys_socketpair(const syscall_args_t *args);
uint64_t handle_sys_setsockopt(const syscall_args_t *args);
uint64_t handle_sys_getsockopt(const syscall_args_t *args);

// Time / Futex Syscall Handlers (syscall_time.c)
uint64_t handle_sys_nanosleep(const syscall_args_t *args);
uint64_t handle_sys_gettimeofday(const syscall_args_t *args);
uint64_t handle_sys_times(const syscall_args_t *args);
uint64_t handle_sys_clock_gettime(const syscall_args_t *args);
uint64_t handle_sys_clock_getres(const syscall_args_t *args);
uint64_t handle_sys_futex(const syscall_args_t *args);

// System / Device Syscall Handlers (syscall_system.c)
uint64_t handle_sys_reboot(const syscall_args_t *args);
uint64_t handle_sys_sysctl(const syscall_args_t *args);

#endif // SYSCALL_INTERNAL_H
