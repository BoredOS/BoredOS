// Copyright (c) 2023-2026 Chris (boreddevnl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
#ifndef SYSCALL_H
#define SYSCALL_H

#include <stdint.h>

typedef struct Window Window;
typedef struct registers_t registers_t;

typedef struct {
    char os_name[64];
    char os_version[64];
    char os_codename[64];
    char kernel_name[64];
    char kernel_version[64];
    char build_date[64];
    char build_time[64];
    char build_arch[64];
} os_info_t;

// MSRs used for syscalls in x86_64
#define MSR_EFER       0xC0000080
#define MSR_STAR       0xC0000081
#define MSR_LSTAR      0xC0000082
#define MSR_COMPAT_STAR 0xC0000083
#define MSR_FMASK      0xC0000084

// Syscall Numbers
#define SYS_WRITE 1
#define SYS_GUI   3
#define SYS_FS    4
#define SYS_SYSTEM 5
#define SYS_EXIT  60

// FS Commands
#define FS_CMD_OPEN 1
#define FS_CMD_READ 2
#define FS_CMD_WRITE 3
#define FS_CMD_CLOSE 4
#define FS_CMD_SEEK 5
#define FS_CMD_TELL 6
#define FS_CMD_LIST 7
#define FS_CMD_DELETE 8
#define FS_CMD_SIZE 9
#define FS_CMD_MKDIR 10
#define FS_CMD_EXISTS 11
#define FS_CMD_GETCWD 12
#define FS_CMD_CHDIR 13
#define FS_CMD_GET_INFO 14
#define FS_CMD_DUP 15
#define FS_CMD_DUP2 16
#define FS_CMD_PIPE 17
#define FS_CMD_FCNTL 18

#define SYSTEM_CMD_SET_BG_COLOR 1
#define SYSTEM_CMD_SET_BG_PATTERN 2
#define SYSTEM_CMD_SET_WALLPAPER 3
#define SYSTEM_CMD_SET_DESKTOP_PROP 4
#define SYSTEM_CMD_SET_MOUSE_SPEED 5
#define SYSTEM_CMD_NETWORK_INIT 6
#define SYSTEM_CMD_GET_DESKTOP_PROP 7
#define SYSTEM_CMD_GET_MOUSE_SPEED 8
#define SYSTEM_CMD_GET_WALLPAPER_THUMB 9
#define SYSTEM_CMD_CLEAR_SCREEN 10
#define SYSTEM_CMD_RTC_GET 11
#define SYSTEM_CMD_REBOOT 12
#define SYSTEM_CMD_SHUTDOWN 13
#define SYSTEM_CMD_BEEP 14
#define SYSTEM_CMD_GET_MEM_INFO 15
#define SYSTEM_CMD_GET_TICKS 16
#define SYSTEM_CMD_PCI_LIST 17
#define SYSTEM_CMD_NETWORK_DHCP 18
#define SYSTEM_CMD_NETWORK_GET_MAC 19
#define SYSTEM_CMD_NETWORK_GET_IP 20
#define SYSTEM_CMD_NETWORK_SET_IP 21
#define SYSTEM_CMD_UDP_SEND 22
#define SYSTEM_CMD_NETWORK_GET_STATS 23
#define SYSTEM_CMD_NETWORK_GET_GATEWAY 24
#define SYSTEM_CMD_NETWORK_GET_DNS 25
#define SYSTEM_CMD_ICMP_PING 26
#define SYSTEM_CMD_NETWORK_IS_INIT 27
#define SYSTEM_CMD_GET_SHELL_CONFIG 28
#define SYSTEM_CMD_SET_TEXT_COLOR 29
#define SYSTEM_CMD_NETWORK_HAS_IP 30
#define SYSTEM_CMD_SET_WALLPAPER_PATH 31
#define SYSTEM_CMD_RTC_SET 32
#define SYSTEM_CMD_TCP_CONNECT 33
#define SYSTEM_CMD_TCP_SEND 34
#define SYSTEM_CMD_TCP_RECV 35
#define SYSTEM_CMD_TCP_CLOSE 36
#define SYSTEM_CMD_DNS_LOOKUP 37
#define SYSTEM_CMD_SET_DNS 38
#define SYSTEM_CMD_NET_UNLOCK 39
#define SYSTEM_CMD_SET_FONT 40
#define SYSTEM_CMD_SET_RAW_MODE 41
#define SYSTEM_CMD_TCP_RECV_NB 42
#define SYSTEM_CMD_YIELD 43
#define SYSTEM_CMD_SLEEP 46
#define SYSTEM_CMD_SET_RESOLUTION 47
#define SYSTEM_CMD_NETWORK_GET_NIC_NAME 48
#define SYSTEM_CMD_PARALLEL_RUN 50
#define SYSTEM_CMD_TTY_CREATE 60
#define SYSTEM_CMD_TTY_READ_OUT 61
#define SYSTEM_CMD_TTY_WRITE_IN 62
#define SYSTEM_CMD_TTY_READ_IN 63
#define SYSTEM_CMD_SPAWN 64
#define SYSTEM_CMD_TTY_SET_FG 65
#define SYSTEM_CMD_TTY_GET_FG 66
#define SYSTEM_CMD_TTY_KILL_FG 67
#define SYSTEM_CMD_TTY_KILL_ALL 68
#define SYSTEM_CMD_TTY_DESTROY 69
#define SYSTEM_CMD_EXEC 70
#define SYSTEM_CMD_WAITPID 71
#define SYSTEM_CMD_KILL_SIGNAL 72
#define SYSTEM_CMD_SIGACTION 73
#define SYSTEM_CMD_SIGPROCMASK 74
#define SYSTEM_CMD_SIGPENDING 75
#define SYSTEM_CMD_GET_ELF_METADATA 76
#define SYSTEM_CMD_GET_ELF_PRIMARY_IMAGE 77

void syscall_init(void);
uint64_t syscall_handler_c(registers_t *regs);

// Mouse event helpers for WM
void syscall_send_mouse_move_event(Window *win, int x, int y, uint8_t buttons);
void syscall_send_mouse_down_event(Window *win, int x, int y);
void syscall_send_mouse_up_event(Window *win, int x, int y);

#endif // SYSCALL_H
