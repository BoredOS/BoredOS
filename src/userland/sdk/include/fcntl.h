#ifndef BOREDOS_LIBC_FCNTL_H
#define BOREDOS_LIBC_FCNTL_H

#include "sys/types.h"

#define O_RDONLY 0x0000
#define O_WRONLY 0x0001
#define O_RDWR   0x0002
#define O_ACCMODE 0x0003
#define O_CREAT  0x0040
#define O_EXCL   0x0080
#define O_TRUNC  0x0200
#define O_APPEND 0x0400
#define O_NONBLOCK 0x0800

#define F_GETFL 3
#define F_SETFL 4

#define FD_CLOEXEC 1

int open(const char *pathname, int flags, ...);
int fcntl(int fd, int cmd, ...);
int dup(int oldfd);
int dup2(int oldfd, int newfd);
int pipe(int pipefd[2]);

#endif
