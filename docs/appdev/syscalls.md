# BoredOS System Call Reference

BoredOS implements a standard Linux x86_64 ABI system call interface. Standard Linux numbers and calling conventions are used across the kernel and libc (mlibc).

## 1. System Calls Table

| Number | Symbol | Arguments | Description |
| :--- | :--- | :--- | :--- |
| 0 | `SYS_READ` | `int fd, void *buf, size_t count` | Read bytes from file descriptor. |
| 1 | `SYS_WRITE` | `int fd, const void *buf, size_t count` | Write bytes to file descriptor. |
| 2 | `SYS_OPEN` | `const char *path, int flags, mode_t mode` | Open file or device node. |
| 3 | `SYS_CLOSE` | `int fd` | Close file descriptor. |
| 4 | `SYS_STAT` | `const char *path, struct stat *statbuf` | Get file metadata. |
| 7 | `SYS_POLL` | `struct pollfd *fds, nfds_t nfds, int timeout` | Poll file descriptors for I/O readiness. |
| 8 | `SYS_LSEEK` | `int fd, off_t offset, int whence` | Reposition read/write file offset (`SEEK_SET`, `SEEK_CUR`, `SEEK_END`). |
| 9 | `SYS_MMAP` | `void *addr, size_t len, int prot, int flags, int fd, off_t offset` | Map memory pages or files into address space. |
| 10 | `SYS_MPROTECT` | `void *addr, size_t len, int prot` | Change memory protection permissions. |
| 11 | `SYS_MUNMAP` | `void *addr, size_t len` | Unmap memory pages from address space. |
| 12 | `SYS_BRK` | `void *addr` | Change data segment size (heap break). |
| 13 | `SYS_RT_SIGACTION` | `int sig, const struct sigaction *act, struct sigaction *oact` | Examine and change signal actions. |
| 14 | `SYS_RT_SIGPROCMASK` | `int how, const sigset_t *set, sigset_t *oset` | Change blocked signal mask. |
| 16 | `SYS_IOCTL` | `int fd, unsigned long request, void *argp` | Device control operation (`TCGETS`, `TCSETS`, `TIOCSCTTY`, `TIOCGWINSZ`, etc.). |
| 22 | `SYS_PIPE` | `int pipefd[2]` | Create unidirectional pipe pair. |
| 24 | `SYS_SCHED_YIELD` | *none* | Yield remaining CPU timeslice. |
| 32 | `SYS_DUP` | `int oldfd` | Duplicate a file descriptor. |
| 33 | `SYS_DUP2` | `int oldfd, int newfd` | Duplicate a file descriptor to a specified index. |
| 35 | `SYS_NANOSLEEP` | `const struct timespec *req, struct timespec *rem` | High precision sleep. |
| 39 | `SYS_GETPID` | *none* | Get current process ID. |
| 41 | `SYS_SOCKET` | `int domain, int type, int protocol` | Create communication endpoint (`AF_INET`, `AF_UNIX`, `SOCK_STREAM`, `SOCK_DGRAM`). |
| 42 | `SYS_CONNECT` | `int fd, const struct sockaddr *addr, socklen_t len` | Connect socket to target address. |
| 43 | `SYS_ACCEPT` | `int fd, struct sockaddr *addr, socklen_t *len` | Accept incoming connection on socket. |
| 44 | `SYS_SENDTO` | `int fd, const void *buf, size_t len, int flags, const struct sockaddr *dest, socklen_t addrlen` | Send data on socket. |
| 45 | `SYS_RECVFROM` | `int fd, void *buf, size_t len, int flags, struct sockaddr *src, socklen_t *addrlen` | Receive data from socket. |
| 46 | `SYS_SENDMSG` | `int fd, const struct msghdr *msg, int flags` | Send message over socket. |
| 47 | `SYS_RECVMSG` | `int fd, struct msghdr *msg, int flags` | Receive message from socket. |
| 49 | `SYS_BIND` | `int fd, const struct sockaddr *addr, socklen_t len` | Bind socket to local address/port. |
| 50 | `SYS_LISTEN` | `int fd, int backlog` | Listen for incoming socket connections. |
| 51 | `SYS_GETSOCKNAME` | `int fd, struct sockaddr *addr, socklen_t *len` | Get socket local address. |
| 52 | `SYS_GETPEERNAME` | `int fd, struct sockaddr *addr, socklen_t *len` | Get socket peer address. |
| 53 | `SYS_SOCKETPAIR` | `int domain, int type, int protocol, int sv[2]` | Create pair of connected sockets. |
| 54 | `SYS_SETSOCKOPT` | `int fd, int level, int optname, const void *optval, socklen_t optlen` | Set socket option. |
| 55 | `SYS_GETSOCKOPT` | `int fd, int level, int optname, void *optval, socklen_t *optlen` | Get socket option. |
| 56 | `SYS_CLONE` | `unsigned long flags, void *child_stack, void *ptid, void *ctid, void *tls` | Clone process / thread. |
| 57 | `SYS_FORK` | *none* | Create child process with COW address space. |
| 59 | `SYS_EXECVE` | `const char *path, char *const argv[], char *const envp[]` | Execute ELF binary. |
| 60 | `SYS_EXIT` | `int status` | Terminate current process. |
| 61 | `SYS_WAIT4` | `pid_t pid, int *wstatus, int options, void *rusage` | Wait for child process state change. |
| 62 | `SYS_KILL` | `pid_t pid, int sig` | Send signal to process. |
| 72 | `SYS_FCNTL` | `int fd, int cmd, int val` | Manipulate file descriptor properties (`F_GETFL`, `F_SETFL`, `F_DUPFD`). |
| 73 | `SYS_RT_SIGPENDING` | `sigset_t *set` | Check pending signals. |
| 79 | `SYS_GETCWD` | `char *buf, size_t size` | Get current working directory. |
| 80 | `SYS_CHDIR` | `const char *path` | Change current working directory. |
| 83 | `SYS_MKDIR` | `const char *path, mode_t mode` | Create directory. |
| 87 | `SYS_UNLINK` | `const char *path` | Delete filesystem node. |
| 96 | `SYS_GETTIMEOFDAY` | `struct timeval *tv, struct timezone *tz` | Get current clock time. |
| 100 | `SYS_TIMES` | `struct tms *buf` | Get process execution times. |
| 137 | `SYS_STATFS` | `const char *path, struct statfs *buf` | Get filesystem statistics by path. |
| 138 | `SYS_FSTATFS` | `int fd, struct statfs *buf` | Get filesystem statistics by open file descriptor. |
| 157 | `SYS_PRCTL` | `int option, unsigned long arg2, unsigned long arg3, unsigned long arg4, unsigned long arg5` | Process control operations (e.g., `PR_SET_CHILD_SUBREAPER`). |
| 158 | `SYS_ARCH_PRCTL` | `int code, unsigned long addr` | Set architecture-specific state (such as `ARCH_SET_FS`). |
| 162 | `SYS_SYNC` | *none* | Flush all filesystem caches and dirty buffers to disk. |
| 165 | `SYS_MOUNT` | `const char *dev_name, const char *dir_name, const char *type, unsigned long flags, const void *data` | Mount a filesystem. |
| 166 | `SYS_UMOUNT2` | `const char *target, int flags` | Unmount a filesystem. |
| 169 | `SYS_REBOOT` | `int magic1, int magic2, int cmd, void *arg` | Standard reboot/shutdown command (`LINUX_REBOOT_CMD_RESTART`, `LINUX_REBOOT_CMD_POWER_OFF`). |
| 186 | `SYS_GETTID` | *none* | Get thread ID. |
| 202 | `SYS_FUTEX` | `uint32_t *uaddr, int op, uint32_t val` | Fast userspace locking primitive (`FUTEX_WAIT`, `FUTEX_WAKE`). |
| 217 | `SYS_GETDENTS64` | `int fd, struct linux_dirent64 *dirp, size_t count` | Read directory entries in 64-bit Linux format. |
| 218 | `SYS_SET_TID_ADDRESS` | `int *tidptr` | Set clear-child-tid address. |
| 228 | `SYS_CLOCK_GETTIME` | `int clockid, struct timespec *tp` | Read clock timestamp. |
| 229 | `SYS_CLOCK_GETRES` | `int clockid, struct timespec *res` | Get clock resolution. |
| 231 | `SYS_EXIT_GROUP` | `int status` | Terminate all threads in thread group. |
| 269 | `SYS_FACCESSAT` | `int dirfd, const char *pathname, int mode, int flags` | Check file access permissions and existence. |
| 306 | `SYS_SYNCFS` | `int fd` | Synchronize filesystem cache containing open file. |

---