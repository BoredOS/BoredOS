#ifndef SYSCALL_USER_H
#define SYSCALL_USER_H

#include "syscall.h"
#include <stddef.h>

static inline void sys_serial_write(const char *str) {
    syscall2(8, 0, (uint64_t)str);
}

#endif
