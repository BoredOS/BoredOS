#ifndef BOREDOS_LIBC_SYS_WAIT_H
#define BOREDOS_LIBC_SYS_WAIT_H

#include "../sys/types.h"

#define WNOHANG 1

#define WEXITSTATUS(status) (((status) >> 8) & 0xff)
#define WIFEXITED(status)   ((((status) & 0x7f) == 0) ? 1 : 0)
#define WTERMSIG(status)    ((status) & 0x7f)
#define WIFSIGNALED(status) (((status) & 0x7f) != 0)

pid_t waitpid(pid_t pid, int *status, int options);

#endif
