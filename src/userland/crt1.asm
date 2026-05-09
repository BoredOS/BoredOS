; Copyright (c) 2023-2026 Chris (boreddevnl)
; This software is released under the GNU General Public License v3.0. See LICENSE file for details.
; This header needs to maintain in any file it is present in, as per the GPL license terms.
; userland/crt0.asm
global _start
extern main
extern sys_exit

section .text
_start:
    ; The kernel loads the ELF and jumps here.
    ; RSP should point to the 0x800000 stack.

    ; Align the stack to 16 bytes for C functions (System V ABI)
    and rsp, -16

    ; Call main(argc, argv)
    call main

    ; If main returns, call exit(status)
    mov rdi, rax ; Pass main's return value to exit syscall
    call sys_exit

    ; Fallback halt if exit miraculously returns
.hang:
    jmp .hang
