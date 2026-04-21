#include "syscall.h"



uint64_t syscall0(uint64_t sys_num) {
    uint64_t ret;
    asm volatile("int $0x80"
                 : "=a"(ret)
                 : "a"(sys_num)
                 : "rcx", "r11", "memory");
    return ret;
}

uint64_t syscall1(uint64_t sys_num, uint64_t arg1) {
    uint64_t ret;
    asm volatile("int $0x80"
                 : "=a"(ret)
                 : "a"(sys_num), "D"(arg1)
                 : "rcx", "r11", "memory");
    return ret;
}

uint64_t syscall2(uint64_t sys_num, uint64_t arg1, uint64_t arg2) {
    uint64_t ret;
    asm volatile("int $0x80"
                 : "=a"(ret)
                 : "a"(sys_num), "D"(arg1), "S"(arg2)
                 : "rcx", "r11", "memory");
    return ret;
}

uint64_t syscall3(uint64_t sys_num, uint64_t arg1, uint64_t arg2, uint64_t arg3) {
    uint64_t ret;
    asm volatile("int $0x80"
                 : "=a"(ret)
                 : "a"(sys_num), "D"(arg1), "S"(arg2), "d"(arg3)
                 : "rcx", "r11", "memory", "r10", "r8", "r9");
    return ret;
}

uint64_t syscall4(uint64_t sys_num, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4) {
    uint64_t ret;
    register uint64_t r10 asm("r10") = arg4;
    asm volatile("int $0x80"
                 : "=a"(ret)
                 : "a"(sys_num), "D"(arg1), "S"(arg2), "d"(arg3), "r"(r10)
                 : "rcx", "r11", "memory", "r8", "r9");
    return ret;
}

uint64_t syscall5(uint64_t sys_num, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5) {
    uint64_t ret;
    register uint64_t r10 asm("r10") = arg4;
    register uint64_t r8  asm("r8") = arg5;
    asm volatile("int $0x80"
                 : "=a"(ret)
                 : "a"(sys_num), "D"(arg1), "S"(arg2), "d"(arg3), "r"(r10), "r"(r8)
                 : "rcx", "r11", "memory", "r9");
    return ret;
}

uint64_t syscall6(uint64_t sys_num, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5, uint64_t arg6) {
    uint64_t ret;
    register uint64_t r10 asm("r10") = arg4;
    register uint64_t r8  asm("r8") = arg5;
    register uint64_t r9  asm("r9") = arg6;
    asm volatile("int $0x80"
                 : "=a"(ret)
                 : "a"(sys_num), "D"(arg1), "S"(arg2), "d"(arg3), "r"(r10), "r"(r8), "r"(r9)
                 : "rcx", "r11", "memory");
    return ret;
}


void sys_exit(int status) {
    syscall1(SYS_EXIT, (uint64_t)status);
    while (1); 
}

int sys_write(int fd, const char *buf, int len) {
    return (int)syscall3(SYS_WRITE, (uint64_t)fd, (uint64_t)buf, (uint64_t)len);
}

void *sys_sbrk(int incr) {
    return (void *)syscall1(SYS_SBRK, (uint64_t)incr);
}

int sys_system(int cmd, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4) {
    return (int)syscall5(SYS_SYSTEM, (uint64_t)cmd, arg1, arg2, arg3, arg4);
}

int sys_open(const char *path, const char *mode) {
    return (int)syscall3(SYS_FS, FS_CMD_OPEN, (uint64_t)path, (uint64_t)mode);
}

int sys_read(int fd, void *buf, uint32_t len) {
    return (int)syscall4(SYS_FS, FS_CMD_READ, (uint64_t)fd, (uint64_t)buf, (uint64_t)len);
}

int sys_write_fs(int fd, const void *buf, uint32_t len) {
    return (int)syscall4(SYS_FS, FS_CMD_WRITE, (uint64_t)fd, (uint64_t)buf, (uint64_t)len);
}

void sys_close(int fd) {
    syscall2(SYS_FS, FS_CMD_CLOSE, (uint64_t)fd);
}

int sys_seek(int fd, int offset, int whence) {
    return (int)syscall4(SYS_FS, FS_CMD_SEEK, (uint64_t)fd, (uint64_t)offset, (uint64_t)whence);
}

uint32_t sys_tell(int fd) {
    return (uint32_t)syscall2(SYS_FS, FS_CMD_TELL, (uint64_t)fd);
}

uint32_t sys_size(int fd) {
    return (uint32_t)syscall2(SYS_FS, FS_CMD_SIZE, (uint64_t)fd);
}

int sys_list(const char *path, FAT32_FileInfo *entries, int max_entries) {
    return (int)syscall4(SYS_FS, FS_CMD_LIST, (uint64_t)path, (uint64_t)entries, (uint64_t)max_entries);
}

int sys_get_file_info(const char *path, FAT32_FileInfo *info) {
    return (int)syscall3(SYS_FS, FS_CMD_GET_INFO, (uint64_t)path, (uint64_t)info);
}

int sys_delete(const char *path) {
    return (int)syscall2(SYS_FS, FS_CMD_DELETE, (uint64_t)path);
}

int sys_mkdir(const char *path) {
    return (int)syscall2(SYS_FS, FS_CMD_MKDIR, (uint64_t)path);
}

int sys_exists(const char *path) {
    return (int)syscall2(SYS_FS, FS_CMD_EXISTS, (uint64_t)path);
}

int sys_getcwd(char *buf, int size) {
    return (int)syscall3(SYS_FS, FS_CMD_GETCWD, (uint64_t)buf, (uint64_t)size);
}

int sys_chdir(const char *path) {
    return (int)syscall2(SYS_FS, FS_CMD_CHDIR, (uint64_t)path);
}

int sys_dup(int oldfd) {
    return (int)syscall2(SYS_FS, FS_CMD_DUP, (uint64_t)oldfd);
}

int sys_dup2(int oldfd, int newfd) {
    return (int)syscall3(SYS_FS, FS_CMD_DUP2, (uint64_t)oldfd, (uint64_t)newfd);
}

int sys_pipe(int pipefd[2]) {
    return (int)syscall2(SYS_FS, FS_CMD_PIPE, (uint64_t)pipefd);
}

int sys_fcntl(int fd, int cmd, int val) {
    return (int)syscall4(SYS_FS, FS_CMD_FCNTL, (uint64_t)fd, (uint64_t)cmd, (uint64_t)val);
}

int sys_tty_create(void) {
    return (int)syscall2(SYS_SYSTEM, SYSTEM_CMD_TTY_CREATE, 0);
}

int sys_tty_read_out(int tty_id, char *buf, int len) {
    return (int)syscall5(SYS_SYSTEM, SYSTEM_CMD_TTY_READ_OUT, (uint64_t)tty_id, (uint64_t)buf, (uint64_t)len, 0);
}

int sys_tty_write_in(int tty_id, const char *buf, int len) {
    return (int)syscall5(SYS_SYSTEM, SYSTEM_CMD_TTY_WRITE_IN, (uint64_t)tty_id, (uint64_t)buf, (uint64_t)len, 0);
}

int sys_tty_read_in(char *buf, int len) {
    return (int)syscall4(SYS_SYSTEM, SYSTEM_CMD_TTY_READ_IN, (uint64_t)buf, (uint64_t)len, 0);
}

int sys_spawn(const char *path, const char *args, uint64_t flags, uint64_t tty_id) {
    return (int)syscall5(SYS_SYSTEM, SYSTEM_CMD_SPAWN, (uint64_t)path, (uint64_t)args, flags, (uint64_t)tty_id);
}

int sys_exec(const char *path, const char *args) {
    return (int)syscall4(SYS_SYSTEM, SYSTEM_CMD_EXEC, (uint64_t)path, (uint64_t)args, 0);
}

int sys_waitpid(int pid, int *status, int options) {
    return (int)syscall4(SYS_SYSTEM, SYSTEM_CMD_WAITPID, (uint64_t)pid, (uint64_t)status, (uint64_t)options);
}

int sys_kill_signal(int pid, int sig) {
    return (int)syscall4(SYS_SYSTEM, SYSTEM_CMD_KILL_SIGNAL, (uint64_t)pid, (uint64_t)sig, 0);
}

int sys_sigaction(int sig, const void *act, void *oldact) {
    return (int)syscall4(SYS_SYSTEM, SYSTEM_CMD_SIGACTION, (uint64_t)sig, (uint64_t)act, (uint64_t)oldact);
}

int sys_sigprocmask(int how, const unsigned long *set, unsigned long *oldset) {
    return (int)syscall4(SYS_SYSTEM, SYSTEM_CMD_SIGPROCMASK, (uint64_t)how, (uint64_t)set, (uint64_t)oldset);
}

int sys_sigpending(unsigned long *set) {
    return (int)syscall3(SYS_SYSTEM, SYSTEM_CMD_SIGPENDING, (uint64_t)set, 0);
}

int sys_tty_set_fg(int tty_id, int pid) {
    return (int)syscall4(SYS_SYSTEM, SYSTEM_CMD_TTY_SET_FG, (uint64_t)tty_id, (uint64_t)pid, 0);
}

int sys_tty_get_fg(int tty_id) {
    return (int)syscall3(SYS_SYSTEM, SYSTEM_CMD_TTY_GET_FG, (uint64_t)tty_id, 0);
}

int sys_tty_kill_fg(int tty_id) {
    return (int)syscall3(SYS_SYSTEM, SYSTEM_CMD_TTY_KILL_FG, (uint64_t)tty_id, 0);
}

int sys_tty_kill_all(int tty_id) {
    return (int)syscall3(SYS_SYSTEM, SYSTEM_CMD_TTY_KILL_ALL, (uint64_t)tty_id, 0);
}

int sys_tty_destroy(int tty_id) {
    return (int)syscall3(SYS_SYSTEM, SYSTEM_CMD_TTY_DESTROY, (uint64_t)tty_id, 0);
}

void sys_kill(int pid) {
    syscall1(SYS_KILL, (uint64_t)pid);
}

// Network API implementations
int sys_network_init(void) {
    return (int)syscall2(SYS_SYSTEM, SYSTEM_CMD_NETWORK_INIT, 0);
}

int sys_network_dhcp_acquire(void) {
    return (int)syscall2(SYS_SYSTEM, SYSTEM_CMD_NETWORK_DHCP, 0);
}

int sys_network_get_mac(net_mac_address_t *mac) {
    return (int)syscall2(SYS_SYSTEM, SYSTEM_CMD_NETWORK_GET_MAC, (uint64_t)mac);
}

int sys_network_get_nic_name(char *name_out) {
    return (int)syscall2(SYS_SYSTEM, SYSTEM_CMD_NETWORK_GET_NIC_NAME, (uint64_t)name_out);
}

int sys_network_get_ip(net_ipv4_address_t *ip) {
    return (int)syscall2(SYS_SYSTEM, SYSTEM_CMD_NETWORK_GET_IP, (uint64_t)ip);
}

int sys_network_set_ip(const net_ipv4_address_t *ip) {
    return (int)syscall2(SYS_SYSTEM, SYSTEM_CMD_NETWORK_SET_IP, (uint64_t)ip);
}

int sys_network_get_stat(int stat_type) {
    return (int)syscall2(SYS_SYSTEM, SYSTEM_CMD_NETWORK_GET_STATS, (uint64_t)stat_type);
}

int sys_get_dns_server(net_ipv4_address_t *ip) {
    return (int)syscall2(SYS_SYSTEM, SYSTEM_CMD_NETWORK_GET_DNS, (uint64_t)ip);
}

int sys_network_get_gateway(net_ipv4_address_t *ip) {
    return (int)syscall2(SYS_SYSTEM, SYSTEM_CMD_NETWORK_GET_GATEWAY, (uint64_t)ip);
}

int sys_network_get_dns(net_ipv4_address_t *ip) {
    return (int)syscall2(SYS_SYSTEM, SYSTEM_CMD_NETWORK_GET_DNS, (uint64_t)ip);
}

int sys_udp_send(const net_ipv4_address_t *dest_ip, uint16_t dest_port, uint16_t src_port, const void *data, size_t data_len) {
    uint32_t ports = (dest_port & 0xFFFF) | ((src_port & 0xFFFF) << 16);
    return (int)syscall5(SYS_SYSTEM, SYSTEM_CMD_UDP_SEND, (uint64_t)dest_ip, (uint64_t)ports, (uint64_t)data, (uint64_t)data_len);
}

int sys_icmp_ping(const net_ipv4_address_t *dest_ip) {
    return (int)syscall2(SYS_SYSTEM, SYSTEM_CMD_ICMP_PING, (uint64_t)dest_ip);
}

int sys_network_is_initialized(void) {
    return (int)syscall2(SYS_SYSTEM, SYSTEM_CMD_NETWORK_IS_INIT, 0);
}

int sys_network_has_ip(void) {
    return (int)syscall2(SYS_SYSTEM, SYSTEM_CMD_NETWORK_HAS_IP, 0);
}

uint64_t sys_get_shell_config(const char *key) {
    return (uint64_t)sys_system(SYSTEM_CMD_GET_SHELL_CONFIG, (uint64_t)key, 0, 0, 0);
}

void sys_set_text_color(uint32_t color) {
    sys_system(SYSTEM_CMD_SET_TEXT_COLOR, (uint64_t)color, 0, 0, 0);
}

int sys_tcp_connect(const net_ipv4_address_t *ip, uint16_t port) {
    return (int)syscall3(SYS_SYSTEM, SYSTEM_CMD_TCP_CONNECT, (uint64_t)ip, (uint64_t)port);
}

int sys_tcp_send(const void *data, size_t len) {
    return (int)syscall3(SYS_SYSTEM, SYSTEM_CMD_TCP_SEND, (uint64_t)data, (uint64_t)len);
}

int sys_tcp_recv(void *buf, size_t max_len) {
    return (int)syscall3(SYS_SYSTEM, SYSTEM_CMD_TCP_RECV, (uint64_t)buf, (uint64_t)max_len);
}

int sys_tcp_recv_nb(void *buf, size_t max_len) {
    return (int)syscall3(SYS_SYSTEM, SYSTEM_CMD_TCP_RECV_NB, (uint64_t)buf, (uint64_t)max_len);
}

int sys_tcp_close(void) {
    return (int)syscall2(SYS_SYSTEM, SYSTEM_CMD_TCP_CLOSE, 0);
}

int sys_dns_lookup(const char *name, net_ipv4_address_t *out_ip) {
    return (int)syscall3(SYS_SYSTEM, SYSTEM_CMD_DNS_LOOKUP, (uint64_t)name, (uint64_t)out_ip);
}

int sys_set_dns_server(const net_ipv4_address_t *ip) {
    return (int)syscall2(SYS_SYSTEM, SYSTEM_CMD_SET_DNS, (uint64_t)ip);
}

void sys_network_force_unlock(void) {
    syscall2(SYS_SYSTEM, SYSTEM_CMD_NET_UNLOCK, 0);
}

void sys_yield(void) {
    syscall1(SYS_SYSTEM, SYSTEM_CMD_YIELD);
}

void sys_parallel_run(void (*fn)(void*), void **args, int count) {
    syscall5(SYS_SYSTEM, SYSTEM_CMD_PARALLEL_RUN, (uint64_t)fn, (uint64_t)args, (uint64_t)count, 0);
}

// ELF metadata API
int sys_get_elf_metadata(const char *path, boredos_app_metadata_t *out_metadata) {
    return (int)syscall4(SYS_SYSTEM, SYSTEM_CMD_GET_ELF_METADATA,
                         (uint64_t)path, (uint64_t)out_metadata, 0);
}

int sys_get_elf_primary_image(const char *path, char *out_path, size_t out_path_size) {
    return (int)syscall5(SYS_SYSTEM, SYSTEM_CMD_GET_ELF_PRIMARY_IMAGE,
                         (uint64_t)path, (uint64_t)out_path, (uint64_t)out_path_size, 0);
}

