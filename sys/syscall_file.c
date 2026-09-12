// Copyright (c) 2023-2026 Christiaan (chris@boreddev.nl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
#include "syscall_internal.h"
#include "fat32.h"
#include "kutils.h"
#undef RB_BLACK
#undef RB_RED
#undef RB_ROOT
#include "ext4fs.h"

int fs_alloc_fd_slot(process_t *proc, int start) {
  for (int i = start; i < MAX_PROCESS_FDS; i++) {
    if (!proc->fds[i])
      return i;
  }
  return -1;
}

int fs_mode_to_flags(const char *mode) {
  if (!mode || !mode[0])
    return O_RDONLY;
  if (mode[0] == 'r') {
    return (mode[1] == '+') ? O_RDWR : O_RDONLY;
  }
  if (mode[0] == 'a') {
    return (mode[1] == '+') ? (O_RDWR | O_APPEND) : (O_WRONLY | O_APPEND);
  }
  if (mode[0] == 'w') {
    return (mode[1] == '+') ? O_RDWR : O_WRONLY;
  }
  return O_RDONLY;
}

process_fd_pipe_t *fs_create_pipe_state(void) {
  process_fd_pipe_t *pipe =
      (process_fd_pipe_t *)kmalloc(sizeof(process_fd_pipe_t));
  if (!pipe)
    return NULL;
  memset(pipe, 0, sizeof(*pipe));
  pipe->readers = 1;
  pipe->writers = 1;
  pipe->lock = SPINLOCK_INIT;
  wait_queue_init(&pipe->read_queue);
  wait_queue_init(&pipe->write_queue);
  return pipe;
}

void fs_pipe_drop_reader(process_fd_pipe_t **pipe) {
  if (!pipe || !*pipe)
    return;

  process_fd_pipe_t *p = *pipe;

  __atomic_fetch_sub(&p->readers, 1, __ATOMIC_RELEASE);
  if (__atomic_load_n(&p->readers, __ATOMIC_ACQUIRE) <= 0 &&
      __atomic_load_n(&p->writers, __ATOMIC_ACQUIRE) <= 0) {
    kfree_null(*pipe);
  }
}

void fs_pipe_drop_writer(process_fd_pipe_t **pipe) {
  if (!pipe || !*pipe)
    return;

  process_fd_pipe_t *p = *pipe;

  __atomic_fetch_sub(&p->writers, 1, __ATOMIC_RELEASE);
  if (__atomic_load_n(&p->readers, __ATOMIC_ACQUIRE) <= 0 &&
      __atomic_load_n(&p->writers, __ATOMIC_ACQUIRE) <= 0) {
    kfree_null(*pipe);
  }
}

void fd_addref(process_t *proc, int fd) {
  if (proc->fd_kind[fd] == PROC_FD_KIND_FILE) {
    process_fd_file_ref_t *ref = (process_fd_file_ref_t *)proc->fds[fd];
    if (ref)
      __atomic_fetch_add(&ref->refs, 1, __ATOMIC_RELAXED);
  } else if (proc->fd_kind[fd] == PROC_FD_KIND_PIPE_READ) {
    process_fd_pipe_t *pipe = (process_fd_pipe_t *)proc->fds[fd];
    if (pipe)
      __atomic_fetch_add(&pipe->readers, 1, __ATOMIC_RELAXED);
  } else if (proc->fd_kind[fd] == PROC_FD_KIND_PIPE_WRITE) {
    process_fd_pipe_t *pipe = (process_fd_pipe_t *)proc->fds[fd];
    if (pipe)
      __atomic_fetch_add(&pipe->writers, 1, __ATOMIC_RELAXED);
  } else if (proc->fd_kind[fd] == PROC_FD_KIND_SOCKET) {
    process_socket_addref((process_fd_socket_t *)proc->fds[fd]);
  }
}

static uint64_t fs_cmd_open(const syscall_args_t *args) {
  process_t *proc = process_get_current();
  const char *path = (const char *)args->arg2;
  const char *mode_arg = (const char *)args->arg3;
  if (!path || !is_valid_user_string(path, 1024))
    return (uint64_t)-EFAULT;

  const char *mode = "r";
  if (mode_arg != NULL) {
    if ((uintptr_t)mode_arg == 1) mode = "w";
    else if ((uintptr_t)mode_arg == 2) mode = "w+";
    else if ((uintptr_t)mode_arg > 4096) mode = mode_arg;
  }

  vfs_file_t *vf = vfs_open(path, mode);

  if (!vf) {
    if (mode && (mode[0] == 'r' && !strchr(mode, '+'))) {
      return (uint64_t)-ENOENT;
    }
    return (uint64_t)-EIO;
  }

  process_fd_file_ref_t *ref =
      (process_fd_file_ref_t *)kmalloc(sizeof(process_fd_file_ref_t));
  if (!ref) {
    vfs_close(vf);
    return (uint64_t)-ENOMEM;
  }
  ref->file = vf;
  ref->refs = 1;

  for (int i = 0; i < MAX_PROCESS_FDS; i++) {
    if (proc->fds[i] == NULL) {
      proc->fds[i] = ref;
      proc->fd_kind[i] = PROC_FD_KIND_FILE;
      proc->fd_flags[i] = fs_mode_to_flags(mode);
      return (uint64_t)i;
    }
  }

  kfree_null(ref);
  vfs_close(vf);
  return (uint64_t)-EMFILE;
}

static uint64_t fs_cmd_read(const syscall_args_t *args) {
  process_t *proc = process_get_current();
  int fd = (int)args->arg2;
  void *buf = (void *)args->arg3;
  uint32_t len = (uint32_t)args->arg4;
  if (fd < 0 || fd >= MAX_PROCESS_FDS || !proc->fds[fd])
    return (uint64_t)-EBADF;

  if (len > 0 && !is_valid_user_ptr(buf, len))
    return (uint64_t)-EFAULT;

  if (proc->fd_kind[fd] == PROC_FD_KIND_FILE) {
    process_fd_file_ref_t *ref = (process_fd_file_ref_t *)proc->fds[fd];
    if (!ref || !ref->file)
      return -1;
    if ((uint64_t)buf < 0xFFFF800000000000ULL && len > 0) {
      volatile uint8_t *p = (volatile uint8_t *)buf;
      for (size_t i = 0; i < len; i += 4096) {
        p[i] = p[i];
      }
      p[len - 1] = p[len - 1];
    }
    return (uint64_t)vfs_read(ref->file, buf, (int)len);
  }

  if (proc->fd_kind[fd] == PROC_FD_KIND_PIPE_READ) {
    process_fd_pipe_t *pipe = (process_fd_pipe_t *)proc->fds[fd];
    if (!pipe || !buf)
      return -1;
    uint8_t *out = (uint8_t *)buf;
    uint32_t n = 0;
    uint64_t pflags = spinlock_acquire_irqsave(&pipe->lock);
    while (n < len) {
      if (pipe->count == 0) {
        if (__atomic_load_n(&pipe->writers, __ATOMIC_ACQUIRE) == 0)
          break;
        if (proc->fd_flags[fd] & O_NONBLOCK) {
          if (n == 0) {
            spinlock_release_irqrestore(&pipe->lock, pflags);
            return (uint64_t)-2;
          }
          break;
        }
        break;
      }
      out[n++] = pipe->data[pipe->read_pos];
      pipe->read_pos = (pipe->read_pos + 1) % sizeof(pipe->data);
      pipe->count--;
    }
    spinlock_release_irqrestore(&pipe->lock, pflags);
    if (n > 0) {
      wait_queue_wake_all(&pipe->write_queue);
    }
    return n;
  }

  if (proc->fd_kind[fd] == PROC_FD_KIND_SOCKET) {
    process_fd_socket_t *sock = (process_fd_socket_t *)proc->fds[fd];
    if (!sock)
      return -1;
    int nonblock = (proc->fd_flags[fd] & O_NONBLOCK) ? 1 : 0;
    if (sock->domain == 1) {
      extern int unix_socket_recv(void *sock, void *data, size_t len, int nonblock, void **out_objs, uint8_t *out_kinds, int *out_flags, int *out_fd_count);
      int ret = unix_socket_recv(sock, buf, len, nonblock, NULL, NULL, NULL, NULL);
      if (ret == -2)
        return (uint64_t)-2;
      return (uint64_t)ret;
    } else {
      extern int network_socket_recv(void *sock, void *buf, size_t max_len, int nonblock);
      int ret = network_socket_recv(sock, buf, len, nonblock);
      if (ret == -2)
        return (uint64_t)-2;
      return (uint64_t)ret;
    }
  }

  return -1;
}

static uint64_t fs_cmd_write(const syscall_args_t *args) {
  process_t *proc = process_get_current();
  int fd = (int)args->arg2;
  const void *buf = (const void *)args->arg3;
  uint32_t len = (uint32_t)args->arg4;
  if (fd < 0 || fd >= MAX_PROCESS_FDS || !proc->fds[fd])
    return -1;

  if (proc->fd_kind[fd] == PROC_FD_KIND_FILE) {
    balance_dirty_pages();
    process_fd_file_ref_t *ref = (process_fd_file_ref_t *)proc->fds[fd];
    if (!ref || !ref->file)
      return -1;
    return (uint64_t)vfs_write(ref->file, buf, (int)len);
  }

  if (proc->fd_kind[fd] == PROC_FD_KIND_PIPE_WRITE) {
    process_fd_pipe_t *pipe = (process_fd_pipe_t *)proc->fds[fd];
    if (!pipe || !buf)
      return -1;
    if (__atomic_load_n(&pipe->readers, __ATOMIC_ACQUIRE) <= 0)
      return (uint64_t)-1;
    const uint8_t *in = (const uint8_t *)buf;
    uint32_t n = 0;
    uint64_t pflags = spinlock_acquire_irqsave(&pipe->lock);
    while (n < len) {
      if (pipe->count == sizeof(pipe->data)) {
        if (proc->fd_flags[fd] & O_NONBLOCK) {
          if (n == 0) {
            spinlock_release_irqrestore(&pipe->lock, pflags);
            return (uint64_t)-2;
          }
          break;
        }
        break;
      }
      pipe->data[pipe->write_pos] = in[n++];
      pipe->write_pos = (pipe->write_pos + 1) % sizeof(pipe->data);
      pipe->count++;
    }
    spinlock_release_irqrestore(&pipe->lock, pflags);
    if (n > 0) {
      wait_queue_wake_all(&pipe->read_queue);
    }
    return n;
  }

  if (proc->fd_kind[fd] == PROC_FD_KIND_SOCKET) {
    process_fd_socket_t *sock = (process_fd_socket_t *)proc->fds[fd];
    if (!sock)
      return -1;
    int nonblock = (proc->fd_flags[fd] & O_NONBLOCK) ? 1 : 0;
    if (sock->domain == 1) {
      extern int unix_socket_send(void *sock, const void *data, size_t len, int nonblock, const int *pass_fds, int pass_fd_count, const char *dest_path);
      int ret = unix_socket_send(sock, buf, len, nonblock, NULL, 0, NULL);
      if (ret == -2)
        return (uint64_t)-2;
      return (uint64_t)ret;
    } else {
      extern int network_socket_send(void *sock, const void *data, size_t len, int nonblock);
      int ret = network_socket_send(sock, buf, len, nonblock);
      if (ret == -2)
        return (uint64_t)-2;
      return (uint64_t)ret;
    }
  }

  return -1;
}

static uint64_t fs_cmd_close(const syscall_args_t *args) {
  process_t *proc = process_get_current();
  int fd = (int)args->arg2;
  if (fd < 0 || fd >= MAX_PROCESS_FDS || !proc->fds[fd])
    return -1;

  process_close_fd_inner(proc, fd);
  return 0;
}

static uint64_t fs_cmd_seek(const syscall_args_t *args) {
  process_t *proc = process_get_current();
  int fd = (int)args->arg2;
  int64_t offset = (int64_t)args->arg3;
  int whence = (int)args->arg4; // 0=SET, 1=CUR, 2=END
  if (fd < 0 || fd >= MAX_PROCESS_FDS) {
    return (uint64_t)-9; // -EBADF
  }
  if (!proc->fds[fd]) {
    if (fd == 0 || fd == 1 || fd == 2) {
      return (uint64_t)-29; // -ESPIPE
    }
    return (uint64_t)-9; // -EBADF
  }
  if (proc->fd_kind[fd] != PROC_FD_KIND_FILE) {
    return (uint64_t)-29; // -ESPIPE
  }
  process_fd_file_ref_t *ref = (process_fd_file_ref_t *)proc->fds[fd];
  if (!ref || !ref->file) {
    return (uint64_t)-9; // -EBADF
  }
  int sret = vfs_seek((vfs_file_t *)ref->file, offset, whence);
  if (sret == -29) {
    return (uint64_t)-29; // -ESPIPE
  }
  if (sret < 0) {
    return (uint64_t)-22; // -EINVAL
  }
  return (uint64_t)vfs_file_position((vfs_file_t *)ref->file);
}

static uint64_t fs_cmd_delete(const syscall_args_t *args) {
  process_t *proc = process_get_current();
  const char *path = (const char *)args->arg2;
  if (!path || !is_valid_user_string(path, 1024))
    return (uint64_t)-EFAULT;
  char normalized[VFS_MAX_PATH];
  vfs_normalize_path(proc ? proc->cwd : "/", path, normalized);
  if (vfs_is_directory(normalized)) {
    return vfs_rmdir(normalized) ? 0 : (uint64_t)-ENOENT;
  }
  if (vfs_delete(normalized))
    return 0;
  return (uint64_t)-ENOENT;
}

static uint64_t fs_cmd_mkdir(const syscall_args_t *args) {
  process_t *proc = process_get_current();
  const char *path = (const char *)args->arg2;
  if (!path || !is_valid_user_string(path, 1024))
    return (uint64_t)-EFAULT;
  char normalized[VFS_MAX_PATH];
  vfs_normalize_path(proc ? proc->cwd : "/", path, normalized);
  if (vfs_exists(normalized)) {
    return (uint64_t)-17; // -EEXIST
  }
  return vfs_mkdir(normalized) ? 0 : (uint64_t)-ENOENT;
}

static uint64_t fs_cmd_getcwd(const syscall_args_t *args) {
  process_t *proc = process_get_current();
  char *buf = (char *)args->arg2;
  size_t size = args->arg3;
  if (!buf || size <= 0 || !is_valid_user_ptr(buf, size))
    return (uint64_t)-EFAULT;
  size_t len = strlen(proc ? proc->cwd : "/");
  if (len >= size)
    return (uint64_t)-ERANGE;
  strcpy(buf, proc ? proc->cwd : "/");
  return (uint64_t)len;
}

static uint64_t fs_cmd_chdir(const syscall_args_t *args) {
  process_t *proc = process_get_current();
  const char *path = (const char *)args->arg2;
  if (!path || !is_valid_user_string(path, 1024))
    return (uint64_t)-EFAULT;
  char normalized[VFS_MAX_PATH];
  vfs_normalize_path(proc ? proc->cwd : "/", path, normalized);
  if (vfs_is_directory(normalized)) {
    if (proc) strcpy(proc->cwd, normalized);
    return 0;
  }
  return (uint64_t)-ENOENT;
}

static uint64_t fs_cmd_dup(const syscall_args_t *args) {
  process_t *proc = process_get_current();
  int oldfd = (int)args->arg2;
  if (oldfd < 0 || oldfd >= MAX_PROCESS_FDS || !proc->fds[oldfd])
    return -1;

  int newfd = fs_alloc_fd_slot(proc, 0);
  if (newfd < 0)
    return -1;

  proc->fds[newfd] = proc->fds[oldfd];
  proc->fd_kind[newfd] = proc->fd_kind[oldfd];
  proc->fd_flags[newfd] = proc->fd_flags[oldfd];
  fd_addref(proc, oldfd);

  return (uint64_t)newfd;
}

static uint64_t fs_cmd_dup2(const syscall_args_t *args) {
  process_t *proc = process_get_current();
  int oldfd = (int)args->arg2;
  int newfd = (int)args->arg3;
  if (oldfd < 0 || oldfd >= MAX_PROCESS_FDS || !proc->fds[oldfd])
    return -1;
  if (newfd < 0 || newfd >= MAX_PROCESS_FDS)
    return -1;
  if (oldfd == newfd)
    return (uint64_t)newfd;

  if (proc->fds[newfd]) {
    syscall_args_t close_args = *args;
    close_args.arg2 = (uint64_t)newfd;
    if (fs_cmd_close(&close_args) != 0)
      return -1;
  }

  proc->fds[newfd] = proc->fds[oldfd];
  proc->fd_kind[newfd] = proc->fd_kind[oldfd];
  proc->fd_flags[newfd] = proc->fd_flags[oldfd];
  fd_addref(proc, oldfd);

  return (uint64_t)newfd;
}

static uint64_t fs_cmd_pipe(const syscall_args_t *args) {
  process_t *proc = process_get_current();
  int *pipefd = (int *)args->arg2;
  if (!pipefd || !is_valid_user_ptr(pipefd, 2 * sizeof(int)))
    return (uint64_t)-EFAULT;

  int rfd = fs_alloc_fd_slot(proc, 0);
  if (rfd < 0)
    return -1;
  int wfd = fs_alloc_fd_slot(proc, rfd + 1);
  if (wfd < 0)
    return -1;

  process_fd_pipe_t *pipe = fs_create_pipe_state();
  if (!pipe)
    return -1;

  proc->fds[rfd] = pipe;
  proc->fd_kind[rfd] = PROC_FD_KIND_PIPE_READ;
  proc->fd_flags[rfd] = O_RDONLY;

  proc->fds[wfd] = pipe;
  proc->fd_kind[wfd] = PROC_FD_KIND_PIPE_WRITE;
  proc->fd_flags[wfd] = O_WRONLY;

  pipefd[0] = rfd;
  pipefd[1] = wfd;
  return 0;
}

static uint64_t fs_cmd_fcntl(const syscall_args_t *args) {
  process_t *proc = process_get_current();
  int fd = (int)args->arg2;
  int cmd = (int)args->arg3;
  int val = (int)args->arg4;
  if (fd < 0 || fd >= MAX_PROCESS_FDS || !proc->fds[fd])
    return -1;

  if (cmd == F_GETFL) {
    return (uint64_t)proc->fd_flags[fd];
  }
  if (cmd == F_SETFL) {
    proc->fd_flags[fd] = (proc->fd_flags[fd] & ~(O_APPEND | O_NONBLOCK)) |
                         (val & (O_APPEND | O_NONBLOCK));
    return 0;
  }
  return -1;
}

void poll_cleanup(process_t *proc) {
  if (!proc)
    return;
  poll_wtable_t *wt = &proc->poll_table;
  for (int i = 0; i < wt->count; i++) {
    if (wt->entries[i].h) {
      wait_queue_remove(wt->entries[i].h, &wt->entries[i].entry);
      wt->entries[i].h = NULL;
    }
  }
  wt->count = 0;
}

static void poll_qproc(wait_queue_head_t *h, poll_table_t *pt) {
  (void)pt;
  if (!h) return;
  process_t *proc = process_get_current();
  if (!proc) return;
  poll_wtable_t *wt = &proc->poll_table;
  for (int i = 0; i < wt->count; i++) {
    if (wt->entries[i].h == h) return;
  }
  if (wt->count < MAX_POLL_ENTRIES) {
    poll_entry_t *pe = &wt->entries[wt->count++];
    pe->h = h;
    pe->entry.proc = proc;
    pe->entry.next = NULL;
    wait_queue_add(h, &pe->entry);
  }
}

static uint64_t fs_cmd_poll(const syscall_args_t *args) {
  struct pollfd *fds = (struct pollfd *)args->arg2;
  int nfds = (int)args->arg3;
  int timeout = (int)args->arg4;

  process_t *proc = process_get_current();
  if (proc) {
    poll_cleanup(proc);
  }

  if (!proc || !fds || nfds <= 0 || nfds > 128) {
    return (uint64_t)-EINVAL;
  }

  if (!is_valid_user_ptr(fds, nfds * sizeof(struct pollfd))) {
    return (uint64_t)-EFAULT;
  }

  // Initialize/reset poll table in process structure
  proc->poll_table.pt.qproc = (timeout != 0) ? poll_qproc : NULL;
  proc->poll_table.count = 0;
  poll_table_t *pt = &proc->poll_table.pt;

  int ready = 0;
  for (int i = 0; i < nfds; i++) {
    int fd = fds[i].fd;
    fds[i].revents = 0;

    int mask = 0;
    if (pty_is_pty_id(fd)) {
      extern int pty_poll_master(int pty_id, struct poll_table *pt);
      mask = pty_poll_master(fd, pt);
    } else {
      if (fd < 0 || fd >= MAX_PROCESS_FDS)
        continue;
      if (!proc->fds[fd]) {
        fds[i].revents = POLLNVAL;
        ready++;
        continue;
      }

      if (proc->fd_kind[fd] == PROC_FD_KIND_FILE) {
        process_fd_file_ref_t *ref = (process_fd_file_ref_t *)proc->fds[fd];
        mask = vfs_poll(ref->file, pt);
      } else if (proc->fd_kind[fd] == PROC_FD_KIND_PIPE_READ ||
                 proc->fd_kind[fd] == PROC_FD_KIND_PIPE_WRITE) {
        process_fd_pipe_t *pipe = (process_fd_pipe_t *)proc->fds[fd];
        if (proc->fd_kind[fd] == PROC_FD_KIND_PIPE_READ) {
          if (pt->qproc && (fds[i].events & POLLIN))
            pt->qproc(&pipe->read_queue, pt);
          if (pipe->count > 0)
            mask |= POLLIN;
          if (pipe->writers == 0)
            mask |= POLLHUP;
        } else {
          if (pt->qproc && (fds[i].events & POLLOUT))
            pt->qproc(&pipe->write_queue, pt);
          if (pipe->count < sizeof(pipe->data))
            mask |= POLLOUT;
          if (pipe->readers == 0)
            mask |= POLLERR;
        }
      } else if (proc->fd_kind[fd] == PROC_FD_KIND_SOCKET) {
        process_fd_socket_t *sock = (process_fd_socket_t *)proc->fds[fd];
        if (sock) {
          if (sock->is_listening) {
            if (sock->domain == AF_UNIX && sock->unpcb) {
              unpcb_t *unp = (unpcb_t *)sock->unpcb;
              if (pt->qproc && (fds[i].events & POLLIN))
                pt->qproc(&unp->accept_waitq, pt);
              if (unp->accept_count > 0)
                mask |= POLLIN;
            } else {
              if (pt->qproc && (fds[i].events & POLLIN))
                pt->qproc(&sock->accept_waitq, pt);
              if (sock->accept_queue_count > 0)
                mask |= POLLIN;
            }
          } else {
            if (pt->qproc && (fds[i].events & POLLIN)) {
              pt->qproc(&sock->rx_sb.waitq, pt);
              pt->qproc(&sock->rx_waitq, pt);
            }
            extern int sockbuf_readable(sockbuf_t *sb);
            if (sockbuf_readable(&sock->rx_sb)) mask |= POLLIN;
            if (sock->domain == AF_UNIX && sock->unpcb) {
              unpcb_t *unp = (unpcb_t *)sock->unpcb;
              if (unp->state == UNP_STATE_CLOSED || (unp->peer == NULL && unp->state == UNP_STATE_CONNECTED)) {
                mask |= (POLLIN | POLLHUP);
              }
              if (unp->peer && unp->peer->sock) {
                process_fd_socket_t *ps = (process_fd_socket_t *)unp->peer->sock;
                if (pt && pt->qproc && (fds[i].events & POLLOUT)) {
                  pt->qproc(&ps->rx_sb.waitq, pt);
                }
                extern int sockbuf_writable(sockbuf_t *sb);
                if (sockbuf_writable(&ps->rx_sb)) mask |= POLLOUT;
              } else if (unp->type == 2) {
                mask |= POLLOUT;
              }
            } else {
              if (sock->tcp_closed) mask |= (POLLIN | POLLHUP);
              if (sock->tcp_connect_error) mask |= POLLERR;
              if (sock->is_connected || sock->type == 2) mask |= POLLOUT;
            }
          }
        }
      } else if (proc->fd_kind[fd] == PROC_FD_KIND_TTY) {
        extern int tty_poll(int tty_id, struct poll_table *pt);
        mask = tty_poll(proc->tty_id, pt);
      }
    }
    
    fds[i].revents = (mask & fds[i].events) | (mask & (POLLHUP | POLLERR | POLLNVAL));
    if (fds[i].revents)
      ready++;
  }

  if (ready > 0 || timeout == 0) {
    poll_cleanup(proc);
    return (uint64_t)ready;
  }

  if (timeout > 0) {
    extern uint32_t get_ticks(void);
    uint32_t ticks = (uint32_t)timeout;
    if (ticks == 0)
      ticks = 1;
    proc->sleep_until = get_ticks() + ticks;
  }

  proc->state = PROC_STATE_BLOCKED;
  return (uint64_t)-2;
}

static uint64_t fs_cmd_ioctl(const syscall_args_t *args) {
  int fd = (int)args->arg2;
  uint64_t request = args->arg3;
  void *arg = (void *)args->arg4;

  process_t *proc = process_get_current();
  if (fd < 0 || fd >= MAX_PROCESS_FDS || !proc->fds[fd])
    return -1;

  if (proc->fd_kind[fd] == PROC_FD_KIND_FILE) {
    process_fd_file_ref_t *ref = (process_fd_file_ref_t *)proc->fds[fd];
    extern int vfs_ioctl(vfs_file_t * file, uint64_t request, void *arg);
    return (uint64_t)vfs_ioctl(ref->file, request, arg);
  } else if (proc->fd_kind[fd] == PROC_FD_KIND_TTY) {
    extern int tty_ioctl(int id, uint64_t request, void *arg);
    return (uint64_t)tty_ioctl(proc->tty_id, request, arg);
  } else if (proc->fd_kind[fd] == PROC_FD_KIND_SOCKET) {
    if (request == 0x5421 /* FIONBIO */ || request == 0x8004667E) {
      if (!arg) return (uint64_t)-1;
      int on = *(int *)arg;
      if (on) proc->fd_flags[fd] |= O_NONBLOCK;
      else proc->fd_flags[fd] &= ~O_NONBLOCK;
      return 0;
    } else if (request == 0x541B /* FIONREAD */) {
      if (!arg) return (uint64_t)-1;
      *(int *)arg = 0; // return 0 bytes pending by default
      return 0;
    }
    return 0; // Succeed basic socket ioctl queries
  }

  return -1;
}

// ---------------------------------------------------------------------------
// Flat Syscall Entry Adapters
// ---------------------------------------------------------------------------

uint64_t handle_sys_read(const syscall_args_t *args) {
  syscall_args_t shifted = *args;
  shifted.arg2 = args->arg1; // fd
  shifted.arg3 = args->arg2; // buf
  shifted.arg4 = args->arg3; // count
  return fs_cmd_read(&shifted);
}

uint64_t handle_sys_write(const syscall_args_t *args) {
  extern void cmd_write_len(const char *str, size_t len);
  process_t *proc = process_get_current();
  int fd = (int)args->arg1;
  const char *buf = (const char *)args->arg2;
  size_t len = (size_t)args->arg3;

  if (!buf || len == 0) return 0;
  if (proc && proc->is_user && !is_valid_user_ptr(buf, len)) return (uint64_t)-EFAULT;

  if (proc && fd >= 0 && fd < MAX_PROCESS_FDS && proc->fds[fd]) {
    syscall_args_t fs_args = *args;
    fs_args.arg2 = args->arg1; // fd
    fs_args.arg3 = args->arg2; // buf
    fs_args.arg4 = args->arg3; // len
    return fs_cmd_write(&fs_args);
  }

  if (!proc || !proc->is_user) {
    cmd_write_len(buf, len);
    return len;
  }
  if (proc->is_terminal_proc) {
    if (proc->tty_id >= 0) {
      tty_write_output(proc->tty_id, buf, len);
      return len;
    }
    cmd_write_len(buf, len);
    return len;
  }
  return len;
}

uint64_t handle_sys_open(const syscall_args_t *args) {
  syscall_args_t shifted = *args;
  shifted.arg2 = args->arg1; // path
  shifted.arg3 = args->arg2; // mode (string)
  return fs_cmd_open(&shifted);
}

uint64_t handle_sys_close(const syscall_args_t *args) {
  syscall_args_t shifted = *args;
  shifted.arg2 = args->arg1; // fd
  return fs_cmd_close(&shifted);
}

struct stat_k {
  uint64_t st_dev;
  uint64_t st_ino;
  uint64_t st_nlink;
  uint32_t st_mode;
  uint32_t st_uid;
  uint32_t st_gid;
  uint32_t __pad0;
  uint64_t st_rdev;
  int64_t  st_size;
  int64_t  st_blksize;
  int64_t  st_blocks;
  struct {
    int64_t tv_sec;
    int64_t tv_nsec;
  } st_atim;
  struct {
    int64_t tv_sec;
    int64_t tv_nsec;
  } st_mtim;
  struct {
    int64_t tv_sec;
    int64_t tv_nsec;
  } st_ctim;
  int64_t __unused[3];
};

#ifndef S_IFMT
#define S_IFMT   0170000
#define S_IFSOCK 0140000
#define S_IFLNK  0120000
#define S_IFREG  0100000
#define S_IFBLK  0060000
#define S_IFDIR  0040000
#define S_IFCHR  0020000
#define S_IFIFO  0010000
#endif

extern int64_t g_realtime_offset_sec;

static int64_t fat_datetime_to_unix(uint16_t date, uint16_t time) {
  if (date == 0) {
    extern uint64_t get_time_ns_highres(void);
    return (int64_t)(get_time_ns_highres() / 1000000000ULL) + g_realtime_offset_sec;
  }
  int year = ((date >> 9) & 0x7F) + 1980;
  int month = (date >> 5) & 0x0F;
  int day = date & 0x1F;
  int hour = (time >> 11) & 0x1F;
  int minute = (time >> 5) & 0x3F;
  int second = (time & 0x1F) * 2;
  static const int days_before_month[12] = {
    0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334
  };
  if (month < 1) month = 1;
  if (month > 12) month = 12;
  if (day < 1) day = 1;
  int y = year;
  int m = month - 1;
  int leap_years = (y - 1969) / 4 - (y - 1901) / 100 + (y - 1601) / 400;
  int is_leap = ((y % 4 == 0) && (y % 100 != 0)) || (y % 400 == 0);
  uint64_t days = (uint64_t)(y - 1970) * 365 + leap_years + days_before_month[m] + (day - 1);
  if (is_leap && m >= 2) days++;
  return (int64_t)(days * 86400ULL + (uint64_t)hour * 3600ULL + (uint64_t)minute * 60ULL + (uint64_t)second);
}

uint64_t handle_sys_stat(const syscall_args_t *args) {
  const char *path = (const char *)args->arg1;
  struct stat_k *st = (struct stat_k *)args->arg2;

  if (!path || !is_valid_user_string(path, 1024) || !st || !is_valid_user_ptr(st, sizeof(struct stat_k)))
    return (uint64_t)-EFAULT;

  process_t *proc = process_get_current();
  char normalized[VFS_MAX_PATH];
  vfs_normalize_path(proc ? proc->cwd : "/", path, normalized);

  memset(st, 0, sizeof(struct stat_k));
  st->st_blksize = 512;

  bool is_dir = vfs_is_directory(normalized);
  vfs_dirent_t v_info;
  memset(&v_info, 0, sizeof(v_info));
  int res = vfs_get_info(normalized, &v_info);

  if (res != 0 && !is_dir) {
    return (uint64_t)-ENOENT;
  }

  if (is_dir || v_info.is_directory) {
    st->st_mode = S_IFDIR | 0755;
    st->st_nlink = 2;
    st->st_size = 4096;
    st->st_blocks = 8;
    st->st_ino = v_info.start_cluster ? v_info.start_cluster : 1;
  } else if (strncmp(normalized, "/dev/", 5) == 0) {
    if (k_strstr(normalized, "sd") || k_strstr(normalized, "hd") || k_strstr(normalized, "nvme") || k_strstr(normalized, "ram")) {
      st->st_mode = S_IFBLK | 0660;
    } else {
      st->st_mode = S_IFCHR | 0666;
    }
    st->st_nlink = 1;
    st->st_size = v_info.size;
    st->st_blocks = (v_info.size + 511) / 512;
    st->st_rdev = 0x0103;
    st->st_ino = v_info.start_cluster ? v_info.start_cluster : 2;
  } else {
    st->st_mode = S_IFREG | 0644;
    st->st_nlink = 1;
    st->st_size = (int64_t)v_info.size;
    st->st_blocks = (int64_t)((v_info.size + 511) / 512);
    st->st_ino = v_info.start_cluster ? v_info.start_cluster : 3;
  }

  int64_t sec = fat_datetime_to_unix(v_info.write_date, v_info.write_time);
  st->st_atim.tv_sec = sec;
  st->st_mtim.tv_sec = sec;
  st->st_ctim.tv_sec = sec;
  return 0;
}

uint64_t handle_sys_fstat(const syscall_args_t *args) {
  int fd = (int)args->arg1;
  struct stat_k *st = (struct stat_k *)args->arg2;

  if (!st || !is_valid_user_ptr(st, sizeof(struct stat_k)))
    return (uint64_t)-EFAULT;

  process_t *proc = process_get_current();
  if (!proc || fd < 0 || fd >= MAX_PROCESS_FDS || !proc->fds[fd])
    return (uint64_t)-EBADF;

  memset(st, 0, sizeof(struct stat_k));
  st->st_blksize = 512;

  extern uint64_t get_time_ns_highres(void);
  int64_t now_sec = (int64_t)(get_time_ns_highres() / 1000000000ULL) + g_realtime_offset_sec;
  st->st_atim.tv_sec = now_sec;
  st->st_mtim.tv_sec = now_sec;
  st->st_ctim.tv_sec = now_sec;

  if (proc->fd_kind[fd] == PROC_FD_KIND_FILE) {
    process_fd_file_ref_t *ref = (process_fd_file_ref_t *)proc->fds[fd];
    if (ref && ref->file) {
      vfs_file_t *file = ref->file;
      if (file->is_device) {
        if (file->device_type == DEVICE_TYPE_BLOCK) {
          st->st_mode = S_IFBLK | 0660;
        } else {
          st->st_mode = S_IFCHR | 0666;
        }
        st->st_nlink = 1;
        st->st_size = 0;
        st->st_blocks = 0;
        st->st_rdev = 0x0103;
        st->st_ino = 1;
      } else {
        vfs_dirent_t v_info;
        memset(&v_info, 0, sizeof(v_info));
        if (file->path[0] && vfs_get_info(file->path, &v_info) == 0) {
          if (v_info.is_directory || vfs_is_directory(file->path)) {
            st->st_mode = S_IFDIR | 0755;
            st->st_nlink = 2;
            st->st_size = 4096;
          } else {
            st->st_mode = S_IFREG | 0644;
            st->st_nlink = 1;
            uint32_t sz = (file->mount && file->mount->ops && file->mount->ops->get_size) ?
                          file->mount->ops->get_size(file->fs_handle) : v_info.size;
            st->st_size = (int64_t)sz;
          }
          st->st_ino = v_info.start_cluster ? v_info.start_cluster : 1;
          int64_t msec = fat_datetime_to_unix(v_info.write_date, v_info.write_time);
          st->st_atim.tv_sec = msec;
          st->st_mtim.tv_sec = msec;
          st->st_ctim.tv_sec = msec;
        } else {
          st->st_mode = S_IFREG | 0644;
          st->st_nlink = 1;
          st->st_size = (file->mount && file->mount->ops && file->mount->ops->get_size) ?
                        (int64_t)file->mount->ops->get_size(file->fs_handle) : 0;
          st->st_ino = 1;
        }
        st->st_blocks = (st->st_size + 511) / 512;
      }
    }
  } else if (proc->fd_kind[fd] == PROC_FD_KIND_PIPE_READ || proc->fd_kind[fd] == PROC_FD_KIND_PIPE_WRITE) {
    process_fd_pipe_t *pipe = (process_fd_pipe_t *)proc->fds[fd];
    st->st_mode = S_IFIFO | 0600;
    st->st_nlink = 1;
    st->st_size = pipe ? (int64_t)pipe->count : 0;
    st->st_blocks = 0;
    st->st_ino = (uint64_t)(uintptr_t)pipe;
  } else if (proc->fd_kind[fd] == PROC_FD_KIND_SOCKET) {
    st->st_mode = S_IFSOCK | 0666;
    st->st_nlink = 1;
    st->st_size = 0;
    st->st_blocks = 0;
    st->st_ino = (uint64_t)(uintptr_t)proc->fds[fd];
  } else if (proc->fd_kind[fd] == PROC_FD_KIND_TTY) {
    st->st_mode = S_IFCHR | 0660;
    st->st_nlink = 1;
    st->st_size = 0;
    st->st_blocks = 0;
    st->st_rdev = 0x0500;
    st->st_ino = (uint64_t)proc->tty_id;
  }
  return 0;
}

uint64_t handle_sys_lstat(const syscall_args_t *args) {
  return handle_sys_stat(args);
}

uint64_t handle_sys_lseek(const syscall_args_t *args) {
  syscall_args_t shifted = *args;
  shifted.arg2 = args->arg1; // fd
  shifted.arg3 = args->arg2; // offset
  shifted.arg4 = args->arg3; // whence
  return fs_cmd_seek(&shifted);
}

uint64_t handle_sys_poll(const syscall_args_t *args) {
  syscall_args_t shifted = *args;
  shifted.arg2 = args->arg1; // fds
  shifted.arg3 = args->arg2; // nfds
  shifted.arg4 = args->arg3; // timeout
  int timeout = (int)args->arg3;

  uint64_t res = fs_cmd_poll(&shifted);
  while (res == (uint64_t)-2) {
    asm volatile("int $0x20");
    process_t *proc = process_get_current();
    if (proc) {
      if (proc->kill_pending) {
        poll_cleanup(proc);
        proc->sleep_until = 0;
        return (uint64_t)-EINTR;
      }
      if (timeout > 0 && proc->sleep_until > 0 && get_ticks() >= proc->sleep_until) {
        poll_cleanup(proc);
        proc->sleep_until = 0;
        return 0;
      }
    }
    shifted.arg4 = 0;
    res = fs_cmd_poll(&shifted);
    if (res == (uint64_t)-2) {
      if (proc) {
        proc->state = PROC_STATE_BLOCKED;
      }
    }
  }
  process_t *proc = process_get_current();
  if (proc) {
    proc->sleep_until = 0;
    poll_cleanup(proc);
  }
  return res;
}

uint64_t handle_sys_ioctl(const syscall_args_t *args) {
  int fd = (int)args->arg1;
  unsigned long cmd = (unsigned long)args->arg2;
  void *arg = (void *)args->arg3;

  process_t *proc = process_get_current();
  if (proc && fd >= 0 && fd < MAX_PROCESS_FDS && proc->fds[fd] &&
      proc->fd_kind[fd] == PROC_FD_KIND_SOCKET) {
    extern int network_if_ioctl(unsigned long cmd, void *arg);
    int ret = network_if_ioctl(cmd, arg);
    if (ret >= 0) return ret;
  }

  syscall_args_t shifted = *args;
  shifted.arg2 = args->arg1; // fd
  shifted.arg3 = args->arg2; // request
  shifted.arg4 = args->arg3; // arg
  return fs_cmd_ioctl(&shifted);
}

uint64_t handle_sys_fcntl(const syscall_args_t *args) {
  syscall_args_t shifted = *args;
  shifted.arg2 = args->arg1; // fd
  shifted.arg3 = args->arg2; // cmd
  shifted.arg4 = args->arg3; // val
  return fs_cmd_fcntl(&shifted);
}

uint64_t handle_sys_pipe(const syscall_args_t *args) {
  syscall_args_t shifted = *args;
  shifted.arg2 = args->arg1; // pipefd
  return fs_cmd_pipe(&shifted);
}

uint64_t handle_sys_dup(const syscall_args_t *args) {
  syscall_args_t shifted = *args;
  shifted.arg2 = args->arg1; // oldfd
  return fs_cmd_dup(&shifted);
}

uint64_t handle_sys_dup2(const syscall_args_t *args) {
  syscall_args_t shifted = *args;
  shifted.arg2 = args->arg1; // oldfd
  shifted.arg3 = args->arg2; // newfd
  return fs_cmd_dup2(&shifted);
}

uint64_t handle_sys_getcwd(const syscall_args_t *args) {
  syscall_args_t shifted = *args;
  shifted.arg2 = args->arg1; // buf
  shifted.arg3 = args->arg2; // size
  return fs_cmd_getcwd(&shifted);
}

uint64_t handle_sys_chdir(const syscall_args_t *args) {
  syscall_args_t shifted = *args;
  shifted.arg2 = args->arg1; // path
  return fs_cmd_chdir(&shifted);
}

uint64_t handle_sys_mkdir(const syscall_args_t *args) {
  syscall_args_t shifted = *args;
  shifted.arg2 = args->arg1; // path
  return fs_cmd_mkdir(&shifted);
}

uint64_t handle_sys_unlink(const syscall_args_t *args) {
  syscall_args_t shifted = *args;
  shifted.arg2 = args->arg1; // path
  return fs_cmd_delete(&shifted);
}

uint64_t handle_sys_statfs(const syscall_args_t *args) {
  process_t *proc = process_get_current();
  const char *path = (const char *)args->arg1;
  struct statfs *buf = (struct statfs *)args->arg2;

  if (!path || !is_valid_user_string(path, 1024) || !buf || !is_valid_user_ptr(buf, sizeof(struct statfs)))
    return (uint64_t)-EFAULT;

  char normalized[VFS_MAX_PATH];
  vfs_normalize_path(proc ? proc->cwd : "/", path, normalized);

  vfs_statfs_t vstat;
  memset(&vstat, 0, sizeof(vstat));
  int res = vfs_statfs(normalized, &vstat);
  if (res < 0) return (uint64_t)-ENOENT;

  memset(buf, 0, sizeof(*buf));
  buf->f_type = 0xef53;
  buf->f_bsize = vstat.block_size ? vstat.block_size : 512;
  buf->f_frsize = buf->f_bsize;
  buf->f_blocks = vstat.total_blocks;
  buf->f_bfree = vstat.free_blocks;
  buf->f_bavail = vstat.free_blocks;
  buf->f_files = 100000;
  buf->f_ffree = 100000;
  buf->f_namelen = 255;
  return 0;
}

uint64_t handle_sys_fstatfs(const syscall_args_t *args) {
  process_t *proc = process_get_current();
  int fd = (int)args->arg1;
  struct statfs *buf = (struct statfs *)args->arg2;

  if (!buf || !is_valid_user_ptr(buf, sizeof(struct statfs)))
    return (uint64_t)-EFAULT;
  if (!proc || fd < 0 || fd >= MAX_PROCESS_FDS || !proc->fds[fd])
    return (uint64_t)-EBADF;

  const char *path = "/";
  if (proc->fd_kind[fd] == PROC_FD_KIND_FILE) {
    process_fd_file_ref_t *ref = (process_fd_file_ref_t *)proc->fds[fd];
    if (ref && ref->file) {
      vfs_file_t *vf = (vfs_file_t *)ref->file;
      path = vf->path[0] ? vf->path : "/";
    }
  }

  syscall_args_t sargs = *args;
  sargs.arg1 = (uint64_t)path;
  sargs.arg2 = (uint64_t)buf;
  return handle_sys_statfs(&sargs);
}

uint64_t handle_sys_getdents64(const syscall_args_t *args) {
  process_t *proc = process_get_current();
  int fd = (int)args->arg1;
  void *dirp = (void *)args->arg2;
  size_t count = (size_t)args->arg3;

  if (!proc || !dirp || count == 0 || !is_valid_user_ptr(dirp, count))
    return (uint64_t)-EFAULT;
  if (fd < 0 || fd >= MAX_PROCESS_FDS || !proc->fds[fd])
    return (uint64_t)-EBADF;

  if (proc->fd_kind[fd] != PROC_FD_KIND_FILE)
    return (uint64_t)-ENOTDIR;

  process_fd_file_ref_t *ref = (process_fd_file_ref_t *)proc->fds[fd];
  if (!ref || !ref->file)
    return (uint64_t)-EBADF;

  vfs_file_t *vf = (vfs_file_t *)ref->file;
  if (!vfs_is_directory(vf->path))
    return (uint64_t)-ENOTDIR;

  vfs_dirent_t entries[64];
  int n = vfs_list_directory(vf->path, entries, 64, (int)vf->position);
  if (n <= 0)
    return 0; // EOF

  uint8_t *out = (uint8_t *)dirp;
  size_t bytes_written = 0;

  for (int i = 0; i < n; i++) {
    int name_len = strlen(entries[i].name);
    int reclen = (sizeof(struct linux_dirent64) + name_len + 1 + 7) & ~7;
    if (bytes_written + reclen > count) {
      if (bytes_written == 0) return (uint64_t)-EINVAL;
      break;
    }

    struct linux_dirent64 *d = (struct linux_dirent64 *)(out + bytes_written);
    d->d_ino = (uint64_t)(vf->position + 1);
    d->d_off = (int64_t)(vf->position + 1);
    d->d_reclen = (unsigned short)reclen;
    d->d_type = entries[i].is_directory ? 4 /* DT_DIR */ : 8 /* DT_REG */;
    memcpy(d->d_name, entries[i].name, name_len + 1);

    bytes_written += reclen;
    vf->position++;
  }

  return (uint64_t)bytes_written;
}

uint64_t handle_sys_faccessat(const syscall_args_t *args) {
  process_t *proc = process_get_current();
  int dirfd = (int)args->arg1;
  const char *path = (const char *)args->arg2;

  if (!path || !is_valid_user_string(path, 1024))
    return (uint64_t)-EFAULT;

  char normalized[VFS_MAX_PATH];
  const char *cwd = proc ? proc->cwd : "/";
  if (dirfd != AT_FDCWD && path[0] != '/') {
    if (proc && dirfd >= 0 && dirfd < MAX_PROCESS_FDS && proc->fds[dirfd] &&
        proc->fd_kind[dirfd] == PROC_FD_KIND_FILE) {
      process_fd_file_ref_t *ref = (process_fd_file_ref_t *)proc->fds[dirfd];
      if (ref && ref->file) {
        vfs_file_t *vf = (vfs_file_t *)ref->file;
        if (vf->path[0]) cwd = vf->path;
      }
    }
  }

  vfs_normalize_path(cwd, path, normalized);
  if (vfs_exists(normalized))
    return 0;

  return (uint64_t)-ENOENT;
}

uint64_t handle_sys_mount(const syscall_args_t *args) {
  process_t *proc = process_get_current();
  const char *source = (const char *)args->arg1;
  const char *target = (const char *)args->arg2;
  const char *fstype = (const char *)args->arg3;

  if (!target || !is_valid_user_string(target, 1024))
    return (uint64_t)-EFAULT;

  char norm_target[VFS_MAX_PATH];
  vfs_normalize_path(proc ? proc->cwd : "/", target, norm_target);

  char fs_buf[64] = {0};
  if (fstype && is_valid_user_string(fstype, sizeof(fs_buf))) {
    strncpy(fs_buf, fstype, sizeof(fs_buf) - 1);
  }

  if (fs_buf[0]) {
    if (strcmp(fs_buf, "tmpfs") == 0) {
      extern struct vfs_fs_ops *tmpfs_get_ops(void);
      return vfs_mount(norm_target, (source && is_valid_user_string(source, 64)) ? source : "tmpfs", "tmpfs", tmpfs_get_ops(), NULL) ? 0 : (uint64_t)-EINVAL;
    }
    if (strcmp(fs_buf, "procfs") == 0 || strcmp(fs_buf, "proc") == 0) {
      extern struct vfs_fs_ops *procfs_get_ops(void);
      return vfs_mount(norm_target, (source && is_valid_user_string(source, 64)) ? source : "procfs", "procfs", procfs_get_ops(), NULL) ? 0 : (uint64_t)-EINVAL;
    }
    if (strcmp(fs_buf, "sysfs") == 0 || strcmp(fs_buf, "sys") == 0) {
      extern struct vfs_fs_ops *sysfs_get_ops(void);
      return vfs_mount(norm_target, (source && is_valid_user_string(source, 64)) ? source : "sysfs", "sysfs", sysfs_get_ops(), NULL) ? 0 : (uint64_t)-EINVAL;
    }
  }

  if (source && is_valid_user_string(source, 1024) && str_starts_with(source, "/dev/")) {
    const char *dev = source + 5;
    Disk *d = disk_get_by_name(dev);
    if (!d)
      return (uint64_t)-ENOENT;

    bool want_ext4 = (strcmp(fs_buf, "ext4") == 0);
    bool want_fat = (strcmp(fs_buf, "fat32") == 0 || strcmp(fs_buf, "vfat") == 0 || strcmp(fs_buf, "fat") == 0);

    if (want_ext4) {
      void *vol = ext4fs_mount_volume(d);
      if (vol && vfs_mount(norm_target, dev, "ext4", ext4fs_get_ops(), vol)) {
        d->is_fat32 = false;
        return 0;
      }
      return (uint64_t)-EINVAL;
    }

    if (want_fat) {
      void *vol = fat32_mount_volume(d);
      if (vol && vfs_mount(norm_target, dev, "fat32", fat32_get_realfs_ops(), vol)) {
        d->is_fat32 = true;
        return 0;
      }
      return (uint64_t)-EINVAL;
    }

    uint8_t sb_buf[512] __attribute__((aligned(512)));
    bool is_ext4 = false;
    if (d->read_sector(d, 2, sb_buf) == 0) {
      uint16_t magic = *(uint16_t *)(sb_buf + 56);
      if (magic == 0xEF53) {
        is_ext4 = true;
      }
    }

    if (is_ext4) {
      void *vol = ext4fs_mount_volume(d);
      if (vol && vfs_mount(norm_target, dev, "ext4", ext4fs_get_ops(), vol)) {
        d->is_fat32 = false;
        return 0;
      }
      vol = fat32_mount_volume(d);
      if (vol && vfs_mount(norm_target, dev, "fat32", fat32_get_realfs_ops(), vol)) {
        d->is_fat32 = true;
        return 0;
      }
    } else {
      void *vol = fat32_mount_volume(d);
      if (vol && vfs_mount(norm_target, dev, "fat32", fat32_get_realfs_ops(), vol)) {
        d->is_fat32 = true;
        return 0;
      }
      vol = ext4fs_mount_volume(d);
      if (vol && vfs_mount(norm_target, dev, "ext4", ext4fs_get_ops(), vol)) {
        d->is_fat32 = false;
        return 0;
      }
    }

    return (uint64_t)-EINVAL;
  }

  return (uint64_t)-EINVAL;
}

uint64_t handle_sys_umount2(const syscall_args_t *args) {
  process_t *proc = process_get_current();
  const char *target = (const char *)args->arg1;
  if (!target || !is_valid_user_string(target, 1024))
    return (uint64_t)-EFAULT;

  char norm_target[VFS_MAX_PATH];
  vfs_normalize_path(proc ? proc->cwd : "/", target, norm_target);
  return vfs_umount(norm_target) ? 0 : (uint64_t)-EINVAL;
}

uint64_t handle_sys_sync(const syscall_args_t *args) {
  (void)args;
  vfs_sync_all();
  return 0;
}

uint64_t handle_sys_syncfs(const syscall_args_t *args) {
  process_t *proc = process_get_current();
  int fd = (int)args->arg1;
  if (fd < 0 || fd >= MAX_PROCESS_FDS || !proc || !proc->fds[fd])
    return (uint64_t)-EBADF;

  if (proc->fd_kind[fd] == PROC_FD_KIND_FILE) {
    process_fd_file_ref_t *ref = (process_fd_file_ref_t *)proc->fds[fd];
    vfs_file_t *vf = ref ? (vfs_file_t *)ref->file : NULL;
    if (vf && vf->mount && vf->mount->ops && vf->mount->ops->sync_fs) {
      vf->mount->ops->sync_fs(vf->mount->fs_private);
      return 0;
    }
  }

  vfs_sync_all();
  return 0;
}
