// Copyright (c) 2023-2026 Christiaan (chris@boreddev.nl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
#include "syscall_internal.h"

struct user_iovec {
  void *iov_base;
  size_t iov_len;
};

struct user_msghdr {
  void *msg_name;
  uint32_t msg_namelen;
  struct user_iovec *msg_iov;
  size_t msg_iovlen;
  void *msg_control;
  size_t msg_controllen;
  int msg_flags;
};

struct user_cmsghdr {
  size_t cmsg_len;
  int cmsg_level;
  int cmsg_type;
};

static int fs_copy_unix_path(const void *addr, uint64_t addrlen, char *path_out,
                             size_t path_out_size) {
  extern void serial_write(const char *str);
  extern void serial_write_num(uint64_t n);

  const uint8_t *raw = (const uint8_t *)addr;
  size_t i;

  if (!addr || !path_out || path_out_size == 0 || addrlen < sizeof(uint16_t)) {
    serial_write("[fs_copy_unix_path] invalid arguments or short addrlen\n");
    return -1;
  }
  uint16_t family = *(const uint16_t *)addr;
  if (family != 1) {
    serial_write("[fs_copy_unix_path] family != 1 (AF_UNIX), family=");
    serial_write_num(family);
    serial_write("\n");
    return -1;
  }

  raw += sizeof(uint16_t);
  size_t offset = 0;
  if (addrlen > sizeof(uint16_t) && raw[0] == '\0') {
    offset = 1;
  }

  size_t limit = (offset == 1) ? 108 : (addrlen - sizeof(uint16_t));

  for (i = 0; i + 1 < path_out_size && i < limit; i++) {
    path_out[i] = (char)raw[i + offset];
    if (path_out[i] == '\0')
      break;
  }
  path_out[i] = '\0';
  return path_out[0] ? 0 : -1;
}

static uint64_t fs_cmd_unix_socket_create(const syscall_args_t *args) {
  process_t *proc = process_get_current();
  int domain = (int)args->arg2;
  int type = (int)args->arg3;
  int protocol = (int)args->arg4;

  if (!proc || (domain != 1 && domain != 2 && domain != 10 && domain != 17) || (type != 1 && type != 2 && type != 3))
    return -1;

  int fd = fs_alloc_fd_slot(proc, 0);
  if (fd < 0) return -1;

  process_fd_socket_t *sock = process_socket_create();
  if (!sock) return -1;

  sock->domain = (uint8_t)domain;
  sock->type = (uint8_t)type;
  sock->protocol = (uint8_t)protocol;

  if (domain == AF_UNIX) {
    extern int unix_socket_create(void *sock, int type);
    unix_socket_create(sock, type);
  } else if (domain == AF_PACKET || type == SOCK_RAW) {
    extern void raw_tap_register(void *sock);
    raw_tap_register(sock);
  } else {
    extern int network_is_initialized(void);
    extern int network_init(void);
    if (!network_is_initialized()) {
      network_init();
    }
  }

  proc->fds[fd] = sock;
  proc->fd_kind[fd] = PROC_FD_KIND_SOCKET;
  proc->fd_flags[fd] = O_RDWR;
  return fd;
}

static uint64_t fs_cmd_unix_socket_bind(const syscall_args_t *args) {
  process_t *proc = process_get_current();
  int fd = (int)args->arg2;
  const void *addr = (const void *)args->arg3;
  uint64_t addrlen = args->arg4;

  if (!proc || fd < 0 || fd >= MAX_PROCESS_FDS || !proc->fds[fd] ||
      proc->fd_kind[fd] != PROC_FD_KIND_SOCKET || !addr) {
    return -1;
  }
  process_fd_socket_t *sock = (process_fd_socket_t *)proc->fds[fd];
  if (!sock) return -1;

  if (sock->domain == AF_UNIX) {
    char path[108];
    if (fs_copy_unix_path(addr, addrlen, path, sizeof(path)) < 0) return -1;
    extern int unix_socket_bind(void *sock, const char *path);
    return unix_socket_bind(sock, path);
  } else if (sock->domain == AF_INET6) {
    if (addrlen < 24) return -EINVAL;
    uint16_t sin6_port = *(const uint16_t *)((const char *)addr + 2);
    uint16_t port = ((sin6_port & 0xFF) << 8) | ((sin6_port >> 8) & 0xFF);
    int bind_err = network_socket_bind_v6(sock, (const ipv6_address_t *)((const char *)addr + 8), port);
    if (bind_err < 0) return bind_err;
    sock->is_bound = 1;
    return 0;
  } else {
    if (addrlen < 8) return -EINVAL;
    uint16_t sin_port = *(const uint16_t *)((const char *)addr + 2);
    uint16_t port = ((sin_port & 0xFF) << 8) | ((sin_port >> 8) & 0xFF);
    uint32_t ip_val = *(const uint32_t *)((const char *)addr + 4);
    int bind_err = network_socket_bind(sock, ip_val, port);
    if (bind_err < 0) return bind_err;
    sock->is_bound = 1;
    return 0;
  }
}

static uint64_t fs_cmd_unix_socket_listen(const syscall_args_t *args) {
  process_t *proc = process_get_current();
  int fd = (int)args->arg2;
  int backlog = (int)args->arg3;

  if (!proc || fd < 0 || fd >= MAX_PROCESS_FDS || !proc->fds[fd] ||
      proc->fd_kind[fd] != PROC_FD_KIND_SOCKET)
    return -1;

  process_fd_socket_t *sock = (process_fd_socket_t *)proc->fds[fd];
  if (!sock) return -1;

  if (sock->domain == AF_UNIX) {
    extern int unix_socket_listen(void *sock, int backlog);
    return unix_socket_listen(sock, backlog);
  } else {
    if (network_socket_listen(sock, backlog) < 0) return -1;
    sock->is_listening = 1;
    return 0;
  }
}

static uint64_t fs_cmd_unix_socket_connect(const syscall_args_t *args) {
  process_t *proc = process_get_current();
  int fd = (int)args->arg2;
  const void *addr = (const void *)args->arg3;
  uint64_t addrlen = args->arg4;

  if (!proc || fd < 0 || fd >= MAX_PROCESS_FDS || !proc->fds[fd] ||
      proc->fd_kind[fd] != PROC_FD_KIND_SOCKET || !addr) {
    return -1;
  }
  process_fd_socket_t *sock = (process_fd_socket_t *)proc->fds[fd];
  if (!sock) return -1;

  if (sock->domain == AF_UNIX) {
    char path[108];
    if (fs_copy_unix_path(addr, addrlen, path, sizeof(path)) < 0) return -1;
    extern int unix_socket_connect(void *sock, const char *path);
    return unix_socket_connect(sock, path);
  } else if (sock->domain == AF_INET6) {
    if (addrlen < 24) return -1;
    uint16_t sin6_port = *(const uint16_t *)((const char *)addr + 2);
    uint16_t port = ((sin6_port & 0xFF) << 8) | ((sin6_port >> 8) & 0xFF);
    return network_socket_connect_v6(sock, (const ipv6_address_t *)((const char *)addr + 8), port);
  } else {
    if (addrlen < 8) return -1;
    uint16_t sin_port = *(const uint16_t *)((const char *)addr + 2);
    uint16_t port = ((sin_port & 0xFF) << 8) | ((sin_port >> 8) & 0xFF);
    uint32_t ip_val = *(const uint32_t *)((const char *)addr + 4);
    if (network_socket_connect(sock, ip_val, port) < 0) return -1;
    sock->is_connected = 1;
    return 0;
  }
}

static uint64_t fs_cmd_unix_socket_accept(const syscall_args_t *args) {
  process_t *proc = process_get_current();
  int fd = (int)args->arg2;
  void *addr = (void *)args->arg3;
  uint64_t *addrlen = (uint64_t *)args->arg4;

  if (!proc || fd < 0 || fd >= MAX_PROCESS_FDS || !proc->fds[fd] ||
      proc->fd_kind[fd] != PROC_FD_KIND_SOCKET)
    return -1;
  process_fd_socket_t *sock = (process_fd_socket_t *)proc->fds[fd];
  if (!sock || !sock->is_listening)
    return -1;

  if (sock->domain == AF_UNIX) {
    int nonblock = (proc->fd_flags[fd] & O_NONBLOCK) ? 1 : 0;
    extern void* unix_socket_accept(void *sock, int nonblock);
    process_fd_socket_t *client = (process_fd_socket_t *)unix_socket_accept(sock, nonblock);
    if (!client) return (uint64_t)-2;

    int newfd = fs_alloc_fd_slot(proc, 0);
    if (newfd < 0) {
      process_socket_release(client);
      return -1;
    }
    proc->fds[newfd] = client;
    proc->fd_kind[newfd] = PROC_FD_KIND_SOCKET;
    proc->fd_flags[newfd] = O_RDWR;
    return newfd;
  } else {
    int nonblock = (proc->fd_flags[fd] & O_NONBLOCK) ? 1 : 0;
    while (1) {
      uint64_t flags = spinlock_acquire_irqsave(&sock->lock);
      if (sock->accept_head) {
        accept_queue_entry_t *entry = sock->accept_head;
        sock->accept_head = entry->next;
        if (!sock->accept_head) sock->accept_tail = NULL;
        sock->accept_queue_count--;
        spinlock_release_irqrestore(&sock->lock, flags);

        process_fd_socket_t *client = (process_fd_socket_t *)entry->client_sock;
        kfree_null(entry);

        int newfd = fs_alloc_fd_slot(proc, 0);
        if (newfd < 0) {
          process_socket_release(client);
          return -1;
        }

        proc->fds[newfd] = client;
        proc->fd_kind[newfd] = PROC_FD_KIND_SOCKET;
        proc->fd_flags[newfd] = O_RDWR;

        if (addr && addrlen && *addrlen >= 8) {
          uint8_t *a_bytes = (uint8_t *)addr;
          *(uint16_t *)a_bytes = AF_INET;
          uint16_t remote_port = 0; uint32_t remote_ip = 0;
          extern void network_socket_get_remote_info(void *sock, uint16_t *port, uint32_t *ip);
          network_socket_get_remote_info(client, &remote_port, &remote_ip);
          *(uint16_t *)(a_bytes + 2) = ((remote_port & 0xFF) << 8) | ((remote_port >> 8) & 0xFF);
          *(uint32_t *)(a_bytes + 4) = remote_ip;
        }
        return newfd;
      }
      spinlock_release_irqrestore(&sock->lock, flags);

      if (nonblock) return (uint64_t)-2;
      wait_queue_wait(&sock->accept_waitq);
    }
  }
}

uint64_t handle_sys_socket(const syscall_args_t *args) {
  syscall_args_t shifted = *args;
  shifted.arg2 = args->arg1; // domain
  shifted.arg3 = args->arg2; // type
  shifted.arg4 = args->arg3; // protocol
  return fs_cmd_unix_socket_create(&shifted);
}

uint64_t handle_sys_connect(const syscall_args_t *args) {
  syscall_args_t shifted = *args;
  shifted.arg2 = args->arg1; // sockfd
  shifted.arg3 = args->arg2; // addr
  shifted.arg4 = args->arg3; // addrlen
  return fs_cmd_unix_socket_connect(&shifted);
}

uint64_t handle_sys_accept(const syscall_args_t *args) {
  syscall_args_t shifted = *args;
  shifted.arg2 = args->arg1; // sockfd
  shifted.arg3 = args->arg2; // addr
  shifted.arg4 = args->arg3; // addrlen
  return fs_cmd_unix_socket_accept(&shifted);
}

uint64_t handle_sys_bind(const syscall_args_t *args) {
  syscall_args_t shifted = *args;
  shifted.arg2 = args->arg1; // sockfd
  shifted.arg3 = args->arg2; // addr
  shifted.arg4 = args->arg3; // addrlen
  return fs_cmd_unix_socket_bind(&shifted);
}

uint64_t handle_sys_listen(const syscall_args_t *args) {
  syscall_args_t shifted = *args;
  shifted.arg2 = args->arg1; // sockfd
  shifted.arg3 = args->arg2; // backlog
  return fs_cmd_unix_socket_listen(&shifted);
}

uint64_t handle_sys_sendto(const syscall_args_t *args) {
  int fd = (int)args->arg1;
  const void *buf = (const void *)args->arg2;
  size_t len = (size_t)args->arg3;
  int flags = (int)args->arg4;
  const void *dest_addr = (const void *)args->arg5;
  uint64_t addrlen = args->arg6;
  (void)flags;

  process_t *proc = process_get_current();
  if (!proc || fd < 0 || fd >= MAX_PROCESS_FDS || !proc->fds[fd] ||
      proc->fd_kind[fd] != PROC_FD_KIND_SOCKET) {
    return -1;
  }
  process_fd_socket_t *sock = (process_fd_socket_t *)proc->fds[fd];
  if (!sock) return -1;

  if (sock->domain == 17) {
    extern int nic_send_packet(const void *data, size_t length);
    return nic_send_packet(buf, len) == 0 ? (uint64_t)len : (uint64_t)-1;
  } else if (sock->domain == 1) {
    char path[108] = {0};
    if (dest_addr && addrlen > 2) {
      fs_copy_unix_path(dest_addr, addrlen, path, sizeof(path));
    }
    int nonblock = (proc->fd_flags[fd] & O_NONBLOCK) ? 1 : 0;
    extern int unix_socket_send(void *sock, const void *data, size_t len, int nonblock, const int *pass_fds, int pass_fd_count, const char *dest_path);
    int ret = unix_socket_send(sock, buf, len, nonblock, NULL, 0, path[0] ? path : NULL);
    if (ret == -2) return (uint64_t)-2;
    return (uint64_t)ret;
  } else if (sock->domain == 2) {
    int nonblock = ((flags & 0x40) || (proc->fd_flags[fd] & O_NONBLOCK)) ? 1 : 0;
    if (sock->type == 1) {
      // SOCK_STREAM (TCP) connected send — dest_addr may be NULL
      extern int network_socket_send(void *sock, const void *data, size_t len, int nonblock);
      int ret = network_socket_send(sock, buf, len, nonblock);
      if (ret == -2) return (uint64_t)-2;
      return (uint64_t)ret;
    } else if (sock->type == 2 || sock->type == 3) {
      if (addrlen < 8 || !dest_addr) return -1;
      uint16_t family = *(const uint16_t *)dest_addr;
      if (family != 2) return -1;
      uint16_t sin_port = *(const uint16_t *)((const char *)dest_addr + 2);
      uint16_t port = ((sin_port & 0xFF) << 8) | ((sin_port >> 8) & 0xFF);
      uint32_t ip_val = *(const uint32_t *)((const char *)dest_addr + 4);

      extern int network_socket_sendto(void *sock, const void *data, size_t len, uint32_t dest_ip, uint16_t dest_port);
      return (uint64_t)network_socket_sendto(sock, buf, len, ip_val, port);
    }
  }
  return -1;
}

uint64_t handle_sys_recvfrom(const syscall_args_t *args) {
  int fd = (int)args->arg1;
  void *buf = (void *)args->arg2;
  size_t len = (size_t)args->arg3;
  int flags = (int)args->arg4;
  void *src_addr = (void *)args->arg5;
  uint32_t *addrlen_ptr = (uint32_t *)args->arg6;

  process_t *proc = process_get_current();
  if (!proc || fd < 0 || fd >= MAX_PROCESS_FDS || !proc->fds[fd] ||
      proc->fd_kind[fd] != PROC_FD_KIND_SOCKET) {
    return -1;
  }
  process_fd_socket_t *sock = (process_fd_socket_t *)proc->fds[fd];
  if (!sock) return -1;

  int nonblock = ((flags & 0x40) || (proc->fd_flags[fd] & O_NONBLOCK)) ? 1 : 0;

  if (sock->domain == AF_PACKET) {
    extern int network_socket_recvfrom(void *sock, void *buf, size_t max_len, int nonblock, uint32_t *from_ip, uint16_t *from_port);
    int ret = network_socket_recvfrom(sock, buf, len, nonblock, NULL, NULL);
    if (ret == -2) return (uint64_t)-2;
    if (ret >= 0 && src_addr && addrlen_ptr && *addrlen_ptr >= 18) {
      *(uint16_t *)src_addr = AF_PACKET;
      *addrlen_ptr = 18;
    }
    return (uint64_t)ret;
  } else if (sock->domain == AF_UNIX) {
    extern int unix_socket_recv(void *sock, void *data, size_t len, int nonblock, void **out_objs, uint8_t *out_kinds, int *out_flags, int *out_fd_count);
    int ret = unix_socket_recv(sock, buf, len, nonblock, NULL, NULL, NULL, NULL);
    if (ret == -2) return (uint64_t)-2;
    return (uint64_t)ret;
  } else if (sock->domain == AF_INET) {
    if (sock->type == SOCK_STREAM) {
      // SOCK_STREAM (TCP) recv
      extern int network_socket_recv(void *sock, void *buf, size_t len, int nonblock);
      int ret = network_socket_recv(sock, buf, len, nonblock);
      if (ret == -2) return (uint64_t)-2;
      return (uint64_t)ret;
    } else if (sock->type == SOCK_DGRAM || sock->type == SOCK_RAW) {
      uint32_t from_ip = 0;
      uint16_t from_port = 0;
      extern int network_socket_recvfrom(void *sock, void *buf, size_t max_len, int nonblock, uint32_t *from_ip, uint16_t *from_port);
      int ret = network_socket_recvfrom(sock, buf, len, nonblock, &from_ip, &from_port);
      if (ret == -2) {
        return (uint64_t)-2;
      }
      if (ret >= 0 && src_addr && addrlen_ptr && *addrlen_ptr >= 8) {
        *(uint16_t *)src_addr = AF_INET;
        uint16_t sin_port = ((from_port & 0xFF) << 8) | ((from_port >> 8) & 0xFF);
        *(uint16_t *)((char *)src_addr + 2) = sin_port;
        *(uint32_t *)((char *)src_addr + 4) = from_ip;
        *addrlen_ptr = 8;
      }
      return (uint64_t)ret;
    }
  }
  return -1;
}

uint64_t handle_sys_setsockopt(const syscall_args_t *args) {
  int fd = (int)args->arg1;
  int level = (int)args->arg2;
  int optname = (int)args->arg3;
  const void *optval = (const void *)args->arg4;
  size_t optlen = (size_t)args->arg5;

  process_t *proc = process_get_current();
  if (!proc || fd < 0 || fd >= MAX_PROCESS_FDS || !proc->fds[fd] || proc->fd_kind[fd] != PROC_FD_KIND_SOCKET)
    return -1;

  process_fd_socket_t *sock = (process_fd_socket_t *)proc->fds[fd];
  extern int network_setsockopt(void *s, int level, int optname, const void *optval, size_t optlen);
  return network_setsockopt(sock, level, optname, optval, optlen);
}

uint64_t handle_sys_getsockopt(const syscall_args_t *args) {
  int fd = (int)args->arg1;
  int level = (int)args->arg2;
  int optname = (int)args->arg3;
  void *optval = (void *)args->arg4;
  size_t *optlen = (size_t *)args->arg5;

  process_t *proc = process_get_current();
  if (!proc || fd < 0 || fd >= MAX_PROCESS_FDS || !proc->fds[fd] || proc->fd_kind[fd] != PROC_FD_KIND_SOCKET)
    return -1;

  process_fd_socket_t *sock = (process_fd_socket_t *)proc->fds[fd];
  extern int network_getsockopt(void *s, int level, int optname, void *optval, size_t *optlen);
  return network_getsockopt(sock, level, optname, optval, optlen);
}

uint64_t handle_sys_socketpair(const syscall_args_t *args) {
  int domain = (int)args->arg1;
  int type = (int)args->arg2;
  int protocol = (int)args->arg3;
  int *sv = (int *)args->arg4;
  (void)protocol;

  if (domain != 1 || !sv) return -1;

  process_t *proc = process_get_current();
  if (!proc) return -1;

  int fd1 = fs_alloc_fd_slot(proc, 0);
  if (fd1 < 0) return -1;
  process_fd_socket_t *sock1 = process_socket_create();
  if (!sock1) return -1;
  proc->fds[fd1] = sock1;
  proc->fd_kind[fd1] = PROC_FD_KIND_SOCKET;
  proc->fd_flags[fd1] = O_RDWR;

  int fd2 = fs_alloc_fd_slot(proc, 0);
  if (fd2 < 0) {
    proc->fds[fd1] = NULL;
    process_socket_release(sock1);
    return -1;
  }
  process_fd_socket_t *sock2 = process_socket_create();
  if (!sock2) {
    proc->fds[fd1] = NULL;
    process_socket_release(sock1);
    return -1;
  }
  proc->fds[fd2] = sock2;
  proc->fd_kind[fd2] = PROC_FD_KIND_SOCKET;
  proc->fd_flags[fd2] = O_RDWR;

  extern int unix_socketpair(void *sock1, void *sock2, int type);
  if (unix_socketpair(sock1, sock2, type) < 0) {
    proc->fds[fd1] = NULL; process_socket_release(sock1);
    proc->fds[fd2] = NULL; process_socket_release(sock2);
    return -1;
  }

  sv[0] = fd1;
  sv[1] = fd2;
  return 0;
}

uint64_t handle_sys_getsockname(const syscall_args_t *args) {
  int fd = (int)args->arg1;
  void *addr = (void *)args->arg2;
  uint32_t *addrlen = (uint32_t *)args->arg3;
  (void)addr; (void)addrlen;
  process_t *proc = process_get_current();
  if (!proc || fd < 0 || fd >= MAX_PROCESS_FDS || !proc->fds[fd] || proc->fd_kind[fd] != PROC_FD_KIND_SOCKET)
    return -1;
  return 0;
}

uint64_t handle_sys_getpeername(const syscall_args_t *args) {
  int fd = (int)args->arg1;
  void *addr = (void *)args->arg2;
  uint32_t *addrlen = (uint32_t *)args->arg3;
  process_t *proc = process_get_current();
  if (!proc || fd < 0 || fd >= MAX_PROCESS_FDS || !proc->fds[fd] || proc->fd_kind[fd] != PROC_FD_KIND_SOCKET)
    return -1;
  process_fd_socket_t *sock = (process_fd_socket_t *)proc->fds[fd];
  if (sock->domain == AF_INET && addr && addrlen && *addrlen >= 8) {
    uint16_t port = 0; uint32_t ip = 0;
    extern void network_socket_get_remote_info(void *sock, uint16_t *port, uint32_t *ip);
    network_socket_get_remote_info(sock, &port, &ip);
    *(uint16_t *)addr = AF_INET;
    *(uint16_t *)((char *)addr + 2) = ((port & 0xFF) << 8) | ((port >> 8) & 0xFF);
    *(uint32_t *)((char *)addr + 4) = ip;
    *addrlen = 8;
    return 0;
  }
  return 0;
}

uint64_t handle_sys_sendmsg(const syscall_args_t *args) {
  int fd = (int)args->arg1;
  const struct user_msghdr *msg = (const struct user_msghdr *)args->arg2;
  int flags = (int)args->arg3;
  (void)flags;

  process_t *proc = process_get_current();
  if (!proc || fd < 0 || fd >= MAX_PROCESS_FDS || !proc->fds[fd] || proc->fd_kind[fd] != PROC_FD_KIND_SOCKET || !msg)
    return -1;

  process_fd_socket_t *sock = (process_fd_socket_t *)proc->fds[fd];

  int pass_fds[16];
  int pass_fd_count = 0;
  if (sock->domain == 1 && msg->msg_control && msg->msg_controllen >= sizeof(struct user_cmsghdr)) {
    const struct user_cmsghdr *cmsg = (const struct user_cmsghdr *)msg->msg_control;
    if (cmsg->cmsg_len >= sizeof(struct user_cmsghdr)) {
      const int *fd_payload = (const int *)((const char *)msg->msg_control + sizeof(struct user_cmsghdr));
      size_t payload_bytes = cmsg->cmsg_len - sizeof(struct user_cmsghdr);
      pass_fd_count = (int)(payload_bytes / sizeof(int));
      if (pass_fd_count > 16) pass_fd_count = 16;
      for (int i = 0; i < pass_fd_count; i++) pass_fds[i] = fd_payload[i];
    }
  }

  size_t total_sent = 0;
  int fds_passed_already = 0;
  for (size_t i = 0; i < msg->msg_iovlen; i++) {
    if (!msg->msg_iov[i].iov_base || msg->msg_iov[i].iov_len == 0) continue;
    if (sock->domain == 1) {
      extern int unix_socket_send(void *sock, const void *data, size_t len, int nonblock, const int *pass_fds, int pass_fd_count, const char *dest_path);
      int cur_fds_cnt = fds_passed_already ? 0 : pass_fd_count;
      int ret = unix_socket_send(sock, msg->msg_iov[i].iov_base, msg->msg_iov[i].iov_len, 0, cur_fds_cnt > 0 ? pass_fds : NULL, cur_fds_cnt, NULL);
      if (ret > 0) {
        total_sent += ret;
        if (cur_fds_cnt > 0) fds_passed_already = 1;
      }
    } else {
      extern int network_socket_send(void *sock, const void *data, size_t len, int nonblock);
      int ret = network_socket_send(sock, msg->msg_iov[i].iov_base, msg->msg_iov[i].iov_len, 0);
      if (ret > 0) total_sent += ret;
    }
  }
  return (uint64_t)total_sent;
}

uint64_t handle_sys_recvmsg(const syscall_args_t *args) {
  int fd = (int)args->arg1;
  struct user_msghdr *msg = (struct user_msghdr *)args->arg2;
  int flags = (int)args->arg3;
  (void)flags;

  process_t *proc = process_get_current();
  if (!proc || fd < 0 || fd >= MAX_PROCESS_FDS || !proc->fds[fd] || proc->fd_kind[fd] != PROC_FD_KIND_SOCKET || !msg)
    return -1;

  process_fd_socket_t *sock = (process_fd_socket_t *)proc->fds[fd];

  void *rx_objs[16];
  uint8_t rx_kinds[16];
  int rx_flags[16];
  int rx_fd_count = 0;
  size_t total_recvd = 0;

  for (size_t i = 0; i < msg->msg_iovlen; i++) {
    if (!msg->msg_iov[i].iov_base || msg->msg_iov[i].iov_len == 0) continue;
    if (sock->domain == 1) {
      extern int unix_socket_recv(void *sock, void *data, size_t len, int nonblock, void **out_objs, uint8_t *out_kinds, int *out_flags, int *out_fd_count);
      int ret = unix_socket_recv(sock, msg->msg_iov[i].iov_base, msg->msg_iov[i].iov_len, 0, rx_objs, rx_kinds, rx_flags, &rx_fd_count);
      if (ret > 0) total_recvd += ret;
    } else {
      extern int network_socket_recv(void *sock, void *data, size_t len, int nonblock);
      int ret = network_socket_recv(sock, msg->msg_iov[i].iov_base, msg->msg_iov[i].iov_len, 0);
      if (ret > 0) total_recvd += ret;
    }
  }

  if (sock->domain == 1 && rx_fd_count > 0 && msg->msg_control && msg->msg_controllen >= sizeof(struct user_cmsghdr)) {
    struct user_cmsghdr *cmsg = (struct user_cmsghdr *)msg->msg_control;
    cmsg->cmsg_level = 1; // SOL_SOCKET
    cmsg->cmsg_type = 1;  // SCM_RIGHTS

    int *fd_payload = (int *)((char *)msg->msg_control + sizeof(struct user_cmsghdr));
    int installed = 0;
    for (int i = 0; i < rx_fd_count; i++) {
      int newfd = fs_alloc_fd_slot(proc, 0);
      if (newfd >= 0) {
        proc->fds[newfd] = rx_objs[i];
        proc->fd_kind[newfd] = rx_kinds[i];
        proc->fd_flags[newfd] = rx_flags[i];
        fd_payload[installed++] = newfd;
      } else {
        if (rx_kinds[i] == PROC_FD_KIND_FILE) {
          process_fd_file_ref_t *ref = (process_fd_file_ref_t *)rx_objs[i];
          if (ref && ref->refs > 0) ref->refs--;
        } else if (rx_kinds[i] == PROC_FD_KIND_SOCKET) {
          process_socket_release((process_fd_socket_t *)rx_objs[i]);
        }
      }
    }
    cmsg->cmsg_len = sizeof(struct user_cmsghdr) + installed * sizeof(int);
    msg->msg_controllen = cmsg->cmsg_len;
  }

  return (uint64_t)total_recvd;
}
