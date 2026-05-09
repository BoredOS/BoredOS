#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include "errno.h"
#include "sys/mman.h"

void *mmap(void *addr, unsigned long length, int prot, int flags, int fd, long offset) {
    (void)addr; (void)length; (void)prot; (void)flags; (void)fd; (void)offset;
    errno = ENOSYS;
    return MAP_FAILED;
}

int munmap(void *addr, unsigned long length) {
    (void)addr; (void)length;
    errno = ENOSYS;
    return -1;
}

int mprotect(void *addr, unsigned long length, int prot) {
    (void)addr; (void)length; (void)prot;
    errno = ENOSYS;
    return -1;
}
#include "fcntl.h"
#include "stdlib.h"
#include "string.h"
#include "sys/stat.h"
#include "syscall.h"
#include "unistd.h"

#define POSIX_MAX_FDS 256
#define PIPE_BUF_SIZE 4096

typedef enum {
    HANDLE_UNUSED = 0,
    HANDLE_KERNEL_FD,
    HANDLE_PIPE_READ,
    HANDLE_PIPE_WRITE
} handle_type_t;

typedef struct {
    unsigned char data[PIPE_BUF_SIZE];
    size_t read_pos;
    size_t write_pos;
    size_t count;
    int readers;
    int writers;
} pipe_state_t;

typedef struct {
    handle_type_t type;
    int refcount;
    int flags;
    int kernel_fd;
    pipe_state_t *pipe;
} fd_handle_t;

static fd_handle_t *g_fd_table[POSIX_MAX_FDS];
static fd_handle_t g_stdio_handles[3];
static int g_fd_initialized = 0;

static void _b_fd_init(void) {
    int i;
    if (g_fd_initialized) {
        return;
    }
    for (i = 0; i < POSIX_MAX_FDS; i++) {
        g_fd_table[i] = NULL;
    }
    for (i = 0; i < 3; i++) {
        g_stdio_handles[i].type = HANDLE_KERNEL_FD;
        g_stdio_handles[i].refcount = 1;
        g_stdio_handles[i].flags = O_RDWR;
        g_stdio_handles[i].kernel_fd = i;
        g_stdio_handles[i].pipe = NULL;
        g_fd_table[i] = &g_stdio_handles[i];
    }
    g_fd_initialized = 1;
}

static int _b_alloc_fd_from(int start) {
    int fd;
    for (fd = start; fd < POSIX_MAX_FDS; fd++) {
        if (g_fd_table[fd] == NULL) {
            return fd;
        }
    }
    return -1;
}

static fd_handle_t *_b_get_handle(int fd) {
    if (fd < 0 || fd >= POSIX_MAX_FDS) {
        return NULL;
    }
    return g_fd_table[fd];
}

static void _b_reset_stat_common(struct stat *st) {
    memset(st, 0, sizeof(*st));
    st->st_blksize = 512;
}

static int _b_fill_kernel_fstat(int kfd, struct stat *statbuf) {
    _b_reset_stat_common(statbuf);
    statbuf->st_mode = (kfd <= 2) ? (S_IFCHR | 0666) : (S_IFREG | 0644);
    statbuf->st_size = (kfd <= 2) ? 0 : (int)sys_size(kfd);
    statbuf->st_blocks = (statbuf->st_size + 511) / 512;
    statbuf->st_nlink = 1;
    return 0;
}

static const char *_b_mode_from_flags(int flags) {
    int accmode = flags & O_ACCMODE;

    if (accmode == O_RDONLY) {
        return "rb";
    }

    if (accmode == O_RDWR) {
        if (flags & O_TRUNC) {
            return "w+";
        }
        if (flags & O_APPEND) {
            return "a+";
        }
        return "r+";
    }

    if (flags & O_APPEND) {
        return "ab";
    }
    if (flags & O_TRUNC) {
        return "wb";
    }
    return "wb";
}

static int _b_pipe_read(fd_handle_t *h, void *buf, size_t count) {
    size_t n = 0;
    pipe_state_t *p = h->pipe;
    unsigned char *out = (unsigned char *)buf;

    if (!p || !buf) {
        errno = EINVAL;
        return -1;
    }

    while (n < count) {
        if (p->count == 0) {
            if (p->writers == 0) {
                break;
            }
            if (h->flags & O_NONBLOCK) {
                if (n == 0) {
                    errno = EAGAIN;
                    return -1;
                }
                break;
            }
            sys_yield();
            continue;
        }

        out[n++] = p->data[p->read_pos];
        p->read_pos = (p->read_pos + 1) % PIPE_BUF_SIZE;
        p->count--;
    }

    return (int)n;
}

static int _b_pipe_write(fd_handle_t *h, const void *buf, size_t count) {
    size_t n = 0;
    pipe_state_t *p = h->pipe;
    const unsigned char *in = (const unsigned char *)buf;

    if (!p || !buf) {
        errno = EINVAL;
        return -1;
    }
    if (p->readers == 0) {
        errno = EPIPE;
        return -1;
    }

    while (n < count) {
        if (p->count == PIPE_BUF_SIZE) {
            if (h->flags & O_NONBLOCK) {
                if (n == 0) {
                    errno = EAGAIN;
                    return -1;
                }
                break;
            }
            sys_yield();
            continue;
        }

        p->data[p->write_pos] = in[n++];
        p->write_pos = (p->write_pos + 1) % PIPE_BUF_SIZE;
        p->count++;
    }

    return (int)n;
}

__attribute__((weak)) int open(const char *pathname, int flags, ...) {
    int fd;
    int kfd;
    int exists;
    mode_t mode = 0;
    fd_handle_t *h;

    _b_fd_init();

    if (!pathname || pathname[0] == '\0') {
        errno = EINVAL;
        return -1;
    }

    if ((flags & O_ACCMODE) > O_RDWR) {
        errno = EINVAL;
        return -1;
    }

    if ((flags & O_TRUNC) && ((flags & O_ACCMODE) == O_RDONLY)) {
        errno = EINVAL;
        return -1;
    }

    exists = sys_exists(pathname);

    if ((flags & O_CREAT) && (flags & O_EXCL) && exists) {
        errno = EEXIST;
        return -1;
    }

    if (!(flags & O_CREAT) && !exists) {
        errno = ENOENT;
        return -1;
    }

    if (flags & O_CREAT) {
        va_list ap;
        va_start(ap, flags);
        mode = (mode_t)va_arg(ap, int);
        va_end(ap);
        (void)mode;
    }

    kfd = sys_open(pathname, _b_mode_from_flags(flags));
    if (kfd < 0) {
        errno = EIO;
        return -1;
    }

    h = (fd_handle_t *)malloc(sizeof(fd_handle_t));
    if (!h) {
        sys_close(kfd);
        errno = ENOMEM;
        return -1;
    }

    fd = _b_alloc_fd_from(3);
    if (fd < 0) {
        free(h);
        sys_close(kfd);
        errno = EBUSY;
        return -1;
    }

    h->type = HANDLE_KERNEL_FD;
    h->refcount = 1;
    h->flags = flags;
    h->kernel_fd = kfd;
    h->pipe = NULL;
    g_fd_table[fd] = h;

    if (flags & O_APPEND) {
        (void)sys_seek(kfd, 0, SEEK_END);
    }

    return fd;
}

__attribute__((weak)) int close(int fd) {
    fd_handle_t *h;

    _b_fd_init();
    h = _b_get_handle(fd);
    if (!h) {
        errno = EBADF;
        return -1;
    }

    g_fd_table[fd] = NULL;
    if (--h->refcount > 0) {
        return 0;
    }

    if (h->type == HANDLE_KERNEL_FD) {
        if (h->kernel_fd >= 3) {
            sys_close(h->kernel_fd);
        }
    } else if (h->type == HANDLE_PIPE_READ || h->type == HANDLE_PIPE_WRITE) {
        pipe_state_t *p = h->pipe;
        if (p) {
            if (h->type == HANDLE_PIPE_READ) {
                p->readers--;
            } else {
                p->writers--;
            }
            if (p->readers <= 0 && p->writers <= 0) {
                free(p);
            }
        }
    }

    if (h < &g_stdio_handles[0] || h > &g_stdio_handles[2]) {
        free(h);
    }
    return 0;
}

__attribute__((weak)) ssize_t read(int fd, void *buf, size_t count) {
    fd_handle_t *h;
    int n;

    _b_fd_init();

    if (!buf && count != 0) {
        errno = EINVAL;
        return -1;
    }

    h = _b_get_handle(fd);
    if (!h) {
        errno = EBADF;
        return -1;
    }

    if (h->type == HANDLE_PIPE_WRITE) {
        errno = EBADF;
        return -1;
    }

    if (h->type == HANDLE_PIPE_READ) {
        n = _b_pipe_read(h, buf, count);
        return (ssize_t)n;
    }

    n = sys_read(h->kernel_fd, buf, (uint32_t)count);
    if (n < 0) {
        errno = EIO;
        return -1;
    }
    return (ssize_t)n;
}

__attribute__((weak)) ssize_t write(int fd, const void *buf, size_t count) {
    fd_handle_t *h;
    int n;

    _b_fd_init();
    if (!buf && count != 0) {
        errno = EINVAL;
        return -1;
    }

    h = _b_get_handle(fd);
    if (!h) {
        errno = EBADF;
        return -1;
    }

    if (h->type == HANDLE_PIPE_READ) {
        errno = EBADF;
        return -1;
    }

    if (h->type == HANDLE_PIPE_WRITE) {
        n = _b_pipe_write(h, buf, count);
        return (ssize_t)n;
    }

    if (h->kernel_fd <= 2) {
        n = sys_write(h->kernel_fd, (const char *)buf, (int)count);
    } else {
        n = sys_write_fs(h->kernel_fd, buf, (uint32_t)count);
    }

    if (n < 0) {
        errno = EIO;
        return -1;
    }
    return (ssize_t)n;
}

__attribute__((weak)) off_t lseek(int fd, off_t offset, int whence) {
    fd_handle_t *h;

    _b_fd_init();
    h = _b_get_handle(fd);
    if (!h) {
        errno = EBADF;
        return -1;
    }
    if (h->type != HANDLE_KERNEL_FD) {
        errno = ESPIPE;
        return -1;
    }
    if (whence != SEEK_SET && whence != SEEK_CUR && whence != SEEK_END) {
        errno = EINVAL;
        return -1;
    }

    if (sys_seek(h->kernel_fd, (int)offset, whence) < 0) {
        errno = EIO;
        return -1;
    }
    return (off_t)sys_tell(h->kernel_fd);
}

__attribute__((weak)) int unlink(const char *pathname) {
    if (!pathname || pathname[0] == '\0') {
        errno = EINVAL;
        return -1;
    }
    if (sys_delete(pathname) != 0) {
        errno = ENOENT;
        return -1;
    }
    return 0;
}

__attribute__((weak)) int isatty(int fd) {
    fd_handle_t *h;
    _b_fd_init();
    h = _b_get_handle(fd);
    if (!h) {
        errno = EBADF;
        return 0;
    }
    return (h->type == HANDLE_KERNEL_FD && h->kernel_fd <= 2) ? 1 : 0;
}

__attribute__((weak)) int fstat(int fd, struct stat *statbuf) {
    fd_handle_t *h;
    _b_fd_init();
    if (!statbuf) {
        errno = EINVAL;
        return -1;
    }

    h = _b_get_handle(fd);
    if (!h) {
        errno = EBADF;
        return -1;
    }

    if (h->type == HANDLE_PIPE_READ || h->type == HANDLE_PIPE_WRITE) {
        _b_reset_stat_common(statbuf);
        statbuf->st_mode = S_IFIFO | 0666;
        statbuf->st_size = (int)(h->pipe ? h->pipe->count : 0);
        return 0;
    }

    return _b_fill_kernel_fstat(h->kernel_fd, statbuf);
}

__attribute__((weak)) int dup(int oldfd) {
    fd_handle_t *h;
    int newfd;
    int newkfd;
    _b_fd_init();

    h = _b_get_handle(oldfd);
    if (!h) {
        errno = EBADF;
        return -1;
    }

    if (h->type != HANDLE_KERNEL_FD) {
        errno = ENOTSUP;
        return -1;
    }

    newkfd = sys_dup(h->kernel_fd);
    if (newkfd < 0) {
        errno = EBADF;
        return -1;
    }

    newfd = _b_alloc_fd_from(0);
    if (newfd < 0) {
        sys_close(newkfd);
        errno = EBUSY;
        return -1;
    }

    h = (fd_handle_t *)malloc(sizeof(fd_handle_t));
    if (!h) {
        sys_close(newkfd);
        errno = ENOMEM;
        return -1;
    }

    h->type = HANDLE_KERNEL_FD;
    h->refcount = 1;
    h->flags = O_RDWR;
    h->kernel_fd = newkfd;
    h->pipe = NULL;
    g_fd_table[newfd] = h;
    return newfd;
}

__attribute__((weak)) int dup2(int oldfd, int newfd) {
    fd_handle_t *h;
    fd_handle_t *nh;
    int newkfd;
    _b_fd_init();

    h = _b_get_handle(oldfd);
    if (!h || newfd < 0 || newfd >= POSIX_MAX_FDS) {
        errno = EBADF;
        return -1;
    }

    if (oldfd == newfd) {
        return newfd;
    }

    if (h->type != HANDLE_KERNEL_FD) {
        errno = ENOTSUP;
        return -1;
    }

    if (g_fd_table[newfd]) {
        if (close(newfd) != 0) {
            return -1;
        }
    }

    newkfd = sys_dup(h->kernel_fd);
    if (newkfd < 0) {
        errno = EBADF;
        return -1;
    }

    nh = (fd_handle_t *)malloc(sizeof(fd_handle_t));
    if (!nh) {
        errno = ENOMEM;
        return -1;
    }

    nh->type = HANDLE_KERNEL_FD;
    nh->refcount = 1;
    nh->flags = h->flags;
    nh->kernel_fd = newkfd;
    nh->pipe = NULL;

    g_fd_table[newfd] = nh;
    return newfd;
}

__attribute__((weak)) int pipe(int pipefd[2]) {
    fd_handle_t *rh;
    fd_handle_t *wh;
    int rfd;
    int wfd;
    int kpipe[2];

    _b_fd_init();
    if (!pipefd) {
        errno = EINVAL;
        return -1;
    }

    if (sys_pipe(kpipe) < 0) {
        errno = EIO;
        return -1;
    }

    rfd = _b_alloc_fd_from(3);
    if (rfd < 0) {
        sys_close(kpipe[0]);
        sys_close(kpipe[1]);
        errno = EBUSY;
        return -1;
    }
    wfd = _b_alloc_fd_from(rfd + 1);
    if (wfd < 0) {
        sys_close(kpipe[0]);
        sys_close(kpipe[1]);
        errno = EBUSY;
        return -1;
    }

    rh = (fd_handle_t *)malloc(sizeof(fd_handle_t));
    wh = (fd_handle_t *)malloc(sizeof(fd_handle_t));
    if (!rh || !wh) {
        free(rh);
        free(wh);
        sys_close(kpipe[0]);
        sys_close(kpipe[1]);
        errno = ENOMEM;
        return -1;
    }

    rh->type = HANDLE_KERNEL_FD;
    rh->refcount = 1;
    rh->flags = O_RDONLY;
    rh->kernel_fd = kpipe[0];
    rh->pipe = NULL;

    wh->type = HANDLE_KERNEL_FD;
    wh->refcount = 1;
    wh->flags = O_WRONLY;
    wh->kernel_fd = kpipe[1];
    wh->pipe = NULL;

    g_fd_table[rfd] = rh;
    g_fd_table[wfd] = wh;
    pipefd[0] = rfd;
    pipefd[1] = wfd;
    return 0;
}

__attribute__((weak)) int fcntl(int fd, int cmd, ...) {
    fd_handle_t *h;
    va_list ap;
    int val;

    _b_fd_init();
    h = _b_get_handle(fd);
    if (!h) {
        errno = EBADF;
        return -1;
    }

    switch (cmd) {
        case F_GETFL:
            if (h->type == HANDLE_KERNEL_FD) {
                int k = sys_fcntl(h->kernel_fd, cmd, 0);
                if (k < 0) {
                    errno = ENOSYS;
                    return -1;
                }
                h->flags = k;
            }
            return h->flags;
        case F_SETFL:
            va_start(ap, cmd);
            val = va_arg(ap, int);
            va_end(ap);
            if (h->type == HANDLE_KERNEL_FD) {
                if (sys_fcntl(h->kernel_fd, cmd, val) < 0) {
                    errno = ENOSYS;
                    return -1;
                }
            }
            h->flags = (h->flags & ~(O_APPEND | O_NONBLOCK)) | (val & (O_APPEND | O_NONBLOCK));
            return 0;
        default:
            errno = ENOSYS;
            return -1;
    }
}
