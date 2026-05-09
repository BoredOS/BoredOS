#ifndef SYSCALL_H
#define SYSCALL_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// Standard syscalls available from Kernel mode
#define SYS_EXIT  0
#define SYS_WRITE 1
#define SYS_GUI   3
#define SYS_FS    4
#define SYS_SYSTEM 5
#define SYS_KILL   10
#define SYS_SBRK  9

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

// System Commands (via SYS_SYSTEM)
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
#define SYSTEM_CMD_NETWORK_HAS_IP 30
#define SYSTEM_CMD_GET_SHELL_CONFIG 28
#define SYSTEM_CMD_NETWORK_GET_NIC_NAME 48
#define SYSTEM_CMD_SET_KEYBOARD_LAYOUT 49
#define SYSTEM_CMD_GET_KEYBOARD_LAYOUT 51
#define SYSTEM_CMD_SET_TEXT_COLOR 29
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
#define SYSTEM_CMD_SLEEP 46
#define SYSTEM_CMD_SET_RAW_MODE 41
#define SYSTEM_CMD_TCP_RECV_NB 42
#define SYSTEM_CMD_YIELD 43
#define SYSTEM_CMD_SET_RESOLUTION 47
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

#define SPAWN_FLAG_TERMINAL 0x1
#define SPAWN_FLAG_INHERIT_TTY 0x2
#define SPAWN_FLAG_TTY_ID 0x4
#define SPAWN_FLAG_BACKGROUND 0x8

// ELF app metadata (mirrors src/sys/elf.h, kept in sync manually)
#define BOREDOS_APP_METADATA_MAX_APP_NAME   64
#define BOREDOS_APP_METADATA_MAX_DESCRIPTION 192
#define BOREDOS_APP_METADATA_MAX_IMAGES     4
#define BOREDOS_APP_METADATA_MAX_IMAGE_PATH 160

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint16_t version;
    uint16_t image_count;
    uint16_t reserved;
    char app_name[BOREDOS_APP_METADATA_MAX_APP_NAME];
    char description[BOREDOS_APP_METADATA_MAX_DESCRIPTION];
    char images[BOREDOS_APP_METADATA_MAX_IMAGES][BOREDOS_APP_METADATA_MAX_IMAGE_PATH];
} boredos_app_metadata_t;

// Internal assembly entry into Ring 0
extern uint64_t syscall0(uint64_t sys_num);
extern uint64_t syscall1(uint64_t sys_num, uint64_t arg1);
extern uint64_t syscall2(uint64_t sys_num, uint64_t arg1, uint64_t arg2);
extern uint64_t syscall3(uint64_t sys_num, uint64_t arg1, uint64_t arg2, uint64_t arg3);
extern uint64_t syscall4(uint64_t sys_num, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4);
extern uint64_t syscall5(uint64_t sys_num, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5);

// Public API
void sys_exit(int status);
int sys_write(int fd, const char *buf, int len);
void *sys_sbrk(int incr);
void sys_kill(int pid);
int sys_system(int cmd, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4);

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

int sys_get_os_info(os_info_t *info);

// FS API
int sys_open(const char *path, const char *mode);
int sys_read(int fd, void *buf, uint32_t len);
int sys_write_fs(int fd, const void *buf, uint32_t len);
void sys_close(int fd);
int sys_seek(int fd, int offset, int whence);
uint32_t sys_tell(int fd);
uint32_t sys_size(int fd);
int sys_delete(const char *path);
int sys_mkdir(const char *path);
int sys_exists(const char *path);
int sys_getcwd(char *buf, int size);
int sys_chdir(const char *path);
int sys_dup(int oldfd);
int sys_dup2(int oldfd, int newfd);
int sys_pipe(int pipefd[2]);
int sys_fcntl(int fd, int cmd, int val);

int sys_tty_create(void);
int sys_tty_read_out(int tty_id, char *buf, int len);
int sys_tty_write_in(int tty_id, const char *buf, int len);
int sys_tty_read_in(char *buf, int len);
int sys_spawn(const char *path, const char *args, uint64_t flags, uint64_t tty_id);
int sys_exec(const char *path, const char *args);
int sys_waitpid(int pid, int *status, int options);
int sys_kill_signal(int pid, int sig);
int sys_sigaction(int sig, const void *act, void *oldact);
int sys_sigprocmask(int how, const unsigned long *set, unsigned long *oldset);
int sys_sigpending(unsigned long *set);
int sys_tty_set_fg(int tty_id, int pid);
int sys_tty_get_fg(int tty_id);
int sys_tty_kill_fg(int tty_id);
int sys_tty_kill_all(int tty_id);
int sys_tty_destroy(int tty_id);

typedef struct {
    char name[256];
    uint32_t size;
    uint8_t is_directory;
    uint32_t start_cluster;
    uint16_t write_date;
    uint16_t write_time;
} FAT32_FileInfo;

int sys_list(const char *path, FAT32_FileInfo *entries, int max_entries);
int sys_get_file_info(const char *path, FAT32_FileInfo *info);

typedef struct {
    uint32_t pid;
    char name[64];
    uint64_t ticks;
    size_t used_memory;
    uint32_t is_idle;
} ProcessInfo;

// Network API
typedef struct { uint8_t bytes[6]; } net_mac_address_t;
typedef struct { uint8_t bytes[4]; } net_ipv4_address_t;

int sys_network_init(void);
int sys_network_dhcp_acquire(void);
int sys_network_get_mac(net_mac_address_t *mac);
int sys_network_get_nic_name(char *name_out);
int sys_network_get_ip(net_ipv4_address_t *ip);
int sys_network_set_ip(const net_ipv4_address_t *ip);
int sys_network_get_stat(int stat_type);
int sys_network_get_gateway(net_ipv4_address_t *ip);
int sys_network_get_dns(net_ipv4_address_t *ip);
int sys_get_dns_server(net_ipv4_address_t *ip);
int sys_udp_send(const net_ipv4_address_t *dest_ip, uint16_t dest_port, uint16_t src_port, const void *data, size_t data_len);
int sys_icmp_ping(const net_ipv4_address_t *dest_ip);
int sys_network_is_initialized(void);
int sys_network_has_ip(void);
uint64_t sys_get_shell_config(const char *key);
void sys_set_text_color(uint32_t color);

int sys_tcp_connect(const net_ipv4_address_t *ip, uint16_t port);
int sys_tcp_send(const void *data, size_t len);
int sys_tcp_recv(void *buf, size_t max_len);
int sys_tcp_recv_nb(void *buf, size_t max_len);
int sys_tcp_close(void);
int sys_dns_lookup(const char *name, net_ipv4_address_t *out_ip);
int sys_set_dns_server(const net_ipv4_address_t *ip);
void sys_network_force_unlock(void);
void sys_yield(void);

// ELF metadata API
int sys_get_elf_metadata(const char *path, boredos_app_metadata_t *out_metadata);
int sys_get_elf_primary_image(const char *path, char *out_path, size_t out_path_size);

// Disk Management Syscalls

#define SYSTEM_CMD_DISK_GET_COUNT  100
#define SYSTEM_CMD_DISK_GET_INFO   101
#define SYSTEM_CMD_DISK_MOUNT      105
#define SYSTEM_CMD_DISK_UMOUNT     106
#define SYSTEM_CMD_DISK_RESCAN     107
#define SYSTEM_CMD_DISK_SYNC       109  

#define SYSTEM_CMD_DISK_WRITE_GPT      102
#define SYSTEM_CMD_DISK_WRITE_MBR      103
#define SYSTEM_CMD_DISK_MKFS_FAT32     104
#define SYSTEM_CMD_DISK_REPLACE_KERNEL 108

typedef struct {
    char     devname[16];
    char     label[32];
    uint32_t type;           
    uint32_t total_sectors;
    bool     is_partition;
    bool     is_fat32;
    bool     is_esp;
    uint32_t lba_offset;
} disk_info_t;

typedef struct {
    uint32_t lba_start;
    uint32_t sector_count;
    uint8_t  part_type;
    uint8_t  flags;
    char     label[36];
} partition_spec_t;

#define PART_FLAG_ESP       0x01
#define MIN_INSTALL_SECTORS 2097152  

int sys_disk_get_count(void);
int sys_disk_get_info(int index, disk_info_t *out);
int sys_disk_write_gpt(const char *devname, partition_spec_t *parts, int count);
int sys_disk_write_mbr(const char *devname, partition_spec_t *parts, int count);
int sys_disk_mkfs_fat32(const char *devname, const char *label);
int sys_disk_mount(const char *devname, const char *mountpoint);
int sys_disk_umount(const char *mountpoint);
int sys_disk_sync(const char *mountpoint);
int sys_disk_rescan(const char *devname);
int sys_disk_replace_kernel(const char *src_path, const char *esp_mountpoint);

#endif
