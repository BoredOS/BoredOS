; Copyright (c) 2023-2026 Christiaan (chris@boreddev.nl)
; This software is released under the GNU General Public License v3.0. See LICENSE file for details.
; This header needs to maintain in any file it is present in, as per the GPL license terms.
; userland/crt0.asm
global _start
extern main
extern sys_exit
extern __init_array_start
extern __init_array_end
extern __fini_array_start
extern __fini_array_end

section .text
_start:
    ; The kernel loads the ELF and jumps here.
    ; RSP should point to the 0x800000 stack.

    ; Align the stack to 16 bytes for C functions (System V ABI)
    and rsp, -16

    ; Preserve the process arguments the kernel placed in registers while
    ; running C++ global constructors.
    push rdi
    push rsi
    call __boredos_run_init_array
    pop rsi
    pop rdi

    ; Call main(argc, argv)
    call main

    ; Run C++ global destructors before exiting.
    mov rbx, rax
    call __boredos_run_fini_array
    mov rax, rbx

    ; If main returns, call exit(status)
    mov rdi, rax ; Pass main's return value to exit syscall
    call sys_exit

    ; Fallback halt if exit miraculously returns
.hang:
    jmp .hang

__boredos_run_init_array:
    push rbp
    mov rbp, rsp
    push rbx
    push r12
    lea rbx, [rel __init_array_start]
    lea r12, [rel __init_array_end]
.init_loop:
    cmp rbx, r12
    jae .init_done
    mov rax, [rbx]
    test rax, rax
    jz .init_next
    call rax
.init_next:
    add rbx, 8
    jmp .init_loop
.init_done:
    pop r12
    pop rbx
    pop rbp
    ret

__boredos_run_fini_array:
    push rbp
    mov rbp, rsp
    push rbx
    push r12
    lea rbx, [rel __fini_array_end]
    lea r12, [rel __fini_array_start]
.fini_loop:
    cmp rbx, r12
    jbe .fini_done
    sub rbx, 8
    mov rax, [rbx]
    test rax, rax
    jz .fini_loop
    call rax
    jmp .fini_loop
.fini_done:
    pop r12
    pop rbx
    pop rbp
    ret

section .note.GNU-stack noalloc noexec nowrite progbits
