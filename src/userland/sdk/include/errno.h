#ifndef BOREDOS_LIBC_ERRNO_H
#define BOREDOS_LIBC_ERRNO_H

extern int errno;

#define EDOM 33
#define E2BIG 7
#define ERANGE 34
#define EINVAL 22
#define EISDIR 21
#define ENOENT 2
#define ENOMEM 12
#define EACCES 13
#define EBADF 9
#define EIO 5
#define EEXIST 17
#define EAGAIN 11
#define EINTR 4
#define ECHILD 10
#define ENOTSUP 95
#define EPIPE 32
#define ENOTDIR 20
#define EBUSY 16
#define ESPIPE 29
#define ENOSYS 38

#endif
