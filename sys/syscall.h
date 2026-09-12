// Copyright (c) 2023-2026 Christiaan (chris@boreddev.nl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
#ifndef SYSCALL_H
#define SYSCALL_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct registers_t registers_t;
// MSRs used for syscalls in x86_64
#define MSR_EFER       0xC0000080
#define MSR_STAR       0xC0000081
#define MSR_LSTAR      0xC0000082
#define MSR_COMPAT_STAR 0xC0000083
#define MSR_FMASK      0xC0000084

// Syscall Numbers
typedef enum {
    SYS_READ = 0,
    SYS_WRITE = 1,
    SYS_OPEN = 2,
    SYS_CLOSE = 3,
    SYS_STAT = 4,
    SYS_FSTAT = 5,
    SYS_LSTAT = 6,
    SYS_POLL = 7,
    SYS_LSEEK = 8,
    SYS_MMAP = 9,
    SYS_MPROTECT = 10,
    SYS_MUNMAP = 11,
    SYS_BRK = 12,
    SYS_RT_SIGACTION = 13,
    SYS_RT_SIGPROCMASK = 14,
    SYS_IOCTL = 16,
    SYS_PIPE = 22,
    SYS_SCHED_YIELD = 24,
    SYS_DUP = 32,
    SYS_DUP2 = 33,
    SYS_PAUSE = 34,
    SYS_NANOSLEEP = 35,
    SYS_GETPID = 39,
    SYS_SOCKET = 41,
    SYS_CONNECT = 42,
    SYS_ACCEPT = 43,
    SYS_SENDTO = 44,
    SYS_RECVFROM = 45,
    SYS_SENDMSG = 46,
    SYS_RECVMSG = 47,
    SYS_BIND = 49,
    SYS_LISTEN = 50,
    SYS_GETSOCKNAME = 51,
    SYS_GETPEERNAME = 52,
    SYS_SOCKETPAIR = 53,
    SYS_SETSOCKOPT = 54,
    SYS_GETSOCKOPT = 55,
    SYS_CLONE = 56,
    SYS_FORK = 57,
    SYS_EXECVE = 59,
    SYS_EXIT = 60,
    SYS_WAIT4 = 61,
    SYS_KILL = 62,
    SYS_FCNTL = 72,
    SYS_RT_SIGPENDING = 73,
    SYS_GETCWD = 79,
    SYS_CHDIR = 80,
    SYS_MKDIR = 83,
    SYS_UNLINK = 87,
    SYS_GETTIMEOFDAY = 96,
    SYS_TIMES = 100,
    SYS_GETUID = 102,
    SYS_GETGID = 104,
    SYS_SETUID = 105,
    SYS_SETGID = 106,
    SYS_GETEUID = 107,
    SYS_GETEGID = 108,
    SYS_SETREUID = 113,
    SYS_SETREGID = 114,
    SYS_SETRESUID = 117,
    SYS_GETRESUID = 118,
    SYS_SETRESGID = 119,
    SYS_GETRESGID = 120,
    SYS_STATFS = 137,
    SYS_FSTATFS = 138,
    SYS_PRCTL = 157,
    SYS_ARCH_PRCTL = 158,
    SYS_SYNC = 162,
    SYS_SETTIMEOFDAY = 164,
    SYS_MOUNT = 165,
    SYS_UMOUNT2 = 166,
    SYS_REBOOT = 169,
    SYS_GETTID = 186,
    SYS_FUTEX = 202,
    SYS_GETDENTS64 = 217,
    SYS_SET_TID_ADDRESS = 218,
    SYS_CLOCK_SETTIME = 227,
    SYS_CLOCK_GETTIME = 228,
    SYS_CLOCK_GETRES = 229,
    SYS_EXIT_GROUP = 231,
    SYS_FACCESSAT = 269,
    SYS_SYNCFS = 306,
    SYS_SPAWN = 317
} syscall_t;

// Futex operations (mlibc FutexWait/FutexWake)
#define FUTEX_WAIT 0
#define FUTEX_WAKE 1

void syscall_init(void);
uint64_t syscall_handler_c(registers_t *regs);
int kernel_futex_wait(uint32_t *uaddr, uint32_t expected);
int kernel_futex_wake(uint32_t *uaddr, int count);
int signal_send_to_pid(int pid, int sig);
bool is_valid_user_ptr(const void *ptr, size_t size);

#endif // SYSCALL_H
