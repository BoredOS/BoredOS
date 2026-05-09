#ifndef BOREDOS_LIBC_SETJMP_H
#define BOREDOS_LIBC_SETJMP_H

#include <stdint.h>

typedef struct boredos_jmp_buf_s {
    uint64_t rbx;
    uint64_t rbp;
    uint64_t r12;
    uint64_t r13;
    uint64_t r14;
    uint64_t r15;
    uint64_t rsp;
    uint64_t rip;
} jmp_buf[1];

int setjmp(jmp_buf env) __attribute__((returns_twice, noinline));
void longjmp(jmp_buf env, int val) __attribute__((noreturn, noinline));

#endif
