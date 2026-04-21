# Syscall Reference

This page documents the current syscall surface in BoredOS as implemented in:
- `src/sys/syscall.h` (kernel command IDs)
- `src/userland/libc/syscall.h` (userland wrappers)

Use libc wrappers when possible instead of calling raw syscall numbers directly.

## Top-Level Syscall Numbers

| Number | Name | Purpose |
|---|---|---|
| 0 | `SYS_EXIT` (userland header) | Terminate current process |
| 1 | `SYS_WRITE` | Write to stdout/tty path |
| 3 | `SYS_GUI` | Window manager and drawing commands |
| 4 | `SYS_FS` | Filesystem and fd commands |
| 5 | `SYS_SYSTEM` | System-wide command multiplexer |
| 9 | `SYS_SBRK` (userland header) | Heap break management |
| 10 | `SYS_KILL` (userland header) | Kill process by PID |
| 60 | `SYS_EXIT` (kernel header) | Internal kernel syscall number map |

Notes:
- Some numbers differ between kernel and userland headers for historical reasons. For app code, rely on wrapper functions in `src/userland/libc/syscall.c`.
- `SYS_GUI`, `SYS_FS`, and `SYS_SYSTEM` are command multiplexers.

## FS Command IDs (`SYS_FS`)

| ID | Macro | Meaning |
|---|---|---|
| 1 | `FS_CMD_OPEN` | Open file |
| 2 | `FS_CMD_READ` | Read from fd |
| 3 | `FS_CMD_WRITE` | Write to fd |
| 4 | `FS_CMD_CLOSE` | Close fd |
| 5 | `FS_CMD_SEEK` | Seek in file |
| 6 | `FS_CMD_TELL` | Current offset |
| 7 | `FS_CMD_LIST` | Directory listing |
| 8 | `FS_CMD_DELETE` | Delete file |
| 9 | `FS_CMD_SIZE` | File size |
| 10 | `FS_CMD_MKDIR` | Create directory |
| 11 | `FS_CMD_EXISTS` | Path exists check |
| 12 | `FS_CMD_GETCWD` | Get cwd |
| 13 | `FS_CMD_CHDIR` | Change cwd |
| 14 | `FS_CMD_GET_INFO` | File metadata |
| 15 | `FS_CMD_DUP` | `dup` fd |
| 16 | `FS_CMD_DUP2` | `dup2` fd |
| 17 | `FS_CMD_PIPE` | Create pipe |
| 18 | `FS_CMD_FCNTL` | `fcntl` flags ops |

## SYSTEM Command IDs (`SYS_SYSTEM`)

### Desktop and display

| ID | Macro | Meaning |
|---|---|---|
| 1 | `SYSTEM_CMD_SET_BG_COLOR` | Set desktop background color |
| 2 | `SYSTEM_CMD_SET_BG_PATTERN` | Set desktop background pattern |
| 3 | `SYSTEM_CMD_SET_WALLPAPER` | Legacy wallpaper command slot |
| 4 | `SYSTEM_CMD_SET_DESKTOP_PROP` | Set desktop behavior property |
| 5 | `SYSTEM_CMD_SET_MOUSE_SPEED` | Set mouse speed |
| 7 | `SYSTEM_CMD_GET_DESKTOP_PROP` | Get desktop property |
| 8 | `SYSTEM_CMD_GET_MOUSE_SPEED` | Get mouse speed |
| 9 | `SYSTEM_CMD_GET_WALLPAPER_THUMB` | Legacy wallpaper thumb slot |
| 10 | `SYSTEM_CMD_CLEAR_SCREEN` | Clear text console |
| 29 | `SYSTEM_CMD_SET_TEXT_COLOR` | Set console text color |
| 31 | `SYSTEM_CMD_SET_WALLPAPER_PATH` | Set wallpaper from path |
| 40 | `SYSTEM_CMD_SET_FONT` | Set active font |
| 47 | `SYSTEM_CMD_SET_RESOLUTION` | Set display mode |

### Time, power, and system state

| ID | Macro | Meaning |
|---|---|---|
| 11 | `SYSTEM_CMD_RTC_GET` | Read RTC datetime |
| 12 | `SYSTEM_CMD_REBOOT` | Reboot machine |
| 13 | `SYSTEM_CMD_SHUTDOWN` | Power off machine |
| 14 | `SYSTEM_CMD_BEEP` | PC speaker beep |
| 15 | `SYSTEM_CMD_GET_MEM_INFO` | Return total/used memory |
| 16 | `SYSTEM_CMD_GET_TICKS` | Return scheduler/WM tick count |
| 28 | `SYSTEM_CMD_GET_SHELL_CONFIG` | Read shell config value |
| 32 | `SYSTEM_CMD_RTC_SET` | Set RTC datetime |
| 41 | `SYSTEM_CMD_SET_RAW_MODE` | Terminal raw-mode control |
| 43 | `SYSTEM_CMD_YIELD` | Yield scheduler timeslice |
| 46 | `SYSTEM_CMD_SLEEP` | Sleep current process |

### Network

| ID | Macro | Meaning |
|---|---|---|
| 6 | `SYSTEM_CMD_NETWORK_INIT` | Init networking |
| 17 | `SYSTEM_CMD_PCI_LIST` | PCI device list access |
| 18 | `SYSTEM_CMD_NETWORK_DHCP` | DHCP acquire |
| 19 | `SYSTEM_CMD_NETWORK_GET_MAC` | Read NIC MAC |
| 20 | `SYSTEM_CMD_NETWORK_GET_IP` | Read IPv4 |
| 21 | `SYSTEM_CMD_NETWORK_SET_IP` | Set static IPv4 |
| 22 | `SYSTEM_CMD_UDP_SEND` | Send UDP packet |
| 23 | `SYSTEM_CMD_NETWORK_GET_STATS` | Network stats |
| 24 | `SYSTEM_CMD_NETWORK_GET_GATEWAY` | Read gateway |
| 25 | `SYSTEM_CMD_NETWORK_GET_DNS` | Read DNS server |
| 26 | `SYSTEM_CMD_ICMP_PING` | ICMP ping |
| 27 | `SYSTEM_CMD_NETWORK_IS_INIT` | Network initialized flag |
| 30 | `SYSTEM_CMD_NETWORK_HAS_IP` | Has IPv4 address flag |
| 33 | `SYSTEM_CMD_TCP_CONNECT` | TCP connect |
| 34 | `SYSTEM_CMD_TCP_SEND` | TCP send |
| 35 | `SYSTEM_CMD_TCP_RECV` | TCP recv (blocking) |
| 36 | `SYSTEM_CMD_TCP_CLOSE` | TCP close |
| 37 | `SYSTEM_CMD_DNS_LOOKUP` | DNS lookup |
| 38 | `SYSTEM_CMD_SET_DNS` | Set DNS server |
| 39 | `SYSTEM_CMD_NET_UNLOCK` | Force net lock release |
| 42 | `SYSTEM_CMD_TCP_RECV_NB` | TCP recv (non-blocking) |
| 48 | `SYSTEM_CMD_NETWORK_GET_NIC_NAME` | NIC name |

### Process, tty, signals

| ID | Macro | Meaning |
|---|---|---|
| 50 | `SYSTEM_CMD_PARALLEL_RUN` | Dispatch parallel job |
| 60 | `SYSTEM_CMD_TTY_CREATE` | Create tty |
| 61 | `SYSTEM_CMD_TTY_READ_OUT` | Read tty output buffer |
| 62 | `SYSTEM_CMD_TTY_WRITE_IN` | Write tty input buffer |
| 63 | `SYSTEM_CMD_TTY_READ_IN` | Read input for current tty |
| 64 | `SYSTEM_CMD_SPAWN` | Spawn process |
| 65 | `SYSTEM_CMD_TTY_SET_FG` | Set tty foreground PID |
| 66 | `SYSTEM_CMD_TTY_GET_FG` | Get tty foreground PID |
| 67 | `SYSTEM_CMD_TTY_KILL_FG` | Kill tty foreground PID |
| 68 | `SYSTEM_CMD_TTY_KILL_ALL` | Kill tty process group |
| 69 | `SYSTEM_CMD_TTY_DESTROY` | Destroy tty |
| 70 | `SYSTEM_CMD_EXEC` | Exec replace current process |
| 71 | `SYSTEM_CMD_WAITPID` | Wait/reap child |
| 72 | `SYSTEM_CMD_KILL_SIGNAL` | Send signal |
| 73 | `SYSTEM_CMD_SIGACTION` | Set/get handler |
| 74 | `SYSTEM_CMD_SIGPROCMASK` | Signal mask ops |
| 75 | `SYSTEM_CMD_SIGPENDING` | Get pending signals |

### ELF app metadata

| ID | Macro | Meaning |
|---|---|---|
| 76 | `SYSTEM_CMD_GET_ELF_METADATA` | Read full app metadata from an ELF |
| 77 | `SYSTEM_CMD_GET_ELF_PRIMARY_IMAGE` | Read primary icon path from an ELF |

## Common Wrapper API (`src/userland/libc/syscall.h`)

Typical wrappers used by apps:
- Process/system: `sys_exit`, `sys_yield`, `sys_spawn`, `sys_exec`, `sys_waitpid`, `sys_kill_signal`
- Filesystem: `sys_open`, `sys_read`, `sys_write_fs`, `sys_close`, `sys_seek`, `sys_tell`, `sys_size`, `sys_list`
- Network: `sys_network_init`, `sys_network_dhcp_acquire`, `sys_udp_send`, `sys_tcp_connect`, `sys_tcp_recv_nb`, `sys_dns_lookup`
- TTY: `sys_tty_create`, `sys_tty_read_out`, `sys_tty_write_in`, `sys_tty_set_fg`
- ELF metadata: `sys_get_elf_metadata`, `sys_get_elf_primary_image` — see [`elf_metadata.md`](elf_metadata.md) for full usage

## Best Practices

- Do not hardcode numeric command IDs in app code.
- Prefer high-level libc calls (`open`, `read`, `waitpid`, `sigaction`) where available.
- Use `syscall.h` macros when a raw `sys_system` call is still needed.
