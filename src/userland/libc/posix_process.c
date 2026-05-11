#include <stdarg.h>
#include <stddef.h>

#include "errno.h"
#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "sys/types.h"
#include "sys/wait.h"
#include "syscall.h"
#include "unistd.h"

static int _b_is_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

static int _b_path_exists(const char *path) {
    return path && path[0] && sys_exists(path);
}

static const char *_b_resolve_exec_path(const char *file, char *out, size_t cap) {
    if (!file || !file[0]) {
        return NULL;
    }

    if (file[0] == '/') {
        return _b_path_exists(file) ? file : NULL;
    }

    snprintf(out, cap, "/bin/%s", file);
    if (_b_path_exists(out)) {
        return out;
    }

    snprintf(out, cap, "/bin/%s.elf", file);
    if (_b_path_exists(out)) {
        return out;
    }

    return NULL;
}

static int _b_join_argv(char *buf, size_t cap, char *const argv[]) {
    size_t used = 0;

    if (!argv || !argv[0]) {
        if (cap) {
            buf[0] = '\0';
        }
        return 0;
    }

    for (int i = 1; argv[i]; i++) {
        const char *a = argv[i];
        size_t len = strlen(a);
        int need_quote = 0;

        for (size_t j = 0; j < len; j++) {
            if (_b_is_space(a[j]) || a[j] == '"') {
                need_quote = 1;
                break;
            }
        }

        if (used && used + 1 < cap) {
            buf[used++] = ' ';
        }

        if (need_quote && used + 1 < cap) {
            buf[used++] = '"';
        }

        for (size_t j = 0; j < len; j++) {
            if (a[j] == '"' && used + 2 < cap) {
                buf[used++] = '\\';
            }
            if (used + 1 < cap) {
                buf[used++] = a[j];
            }
        }

        if (need_quote && used + 1 < cap) {
            buf[used++] = '"';
        }

        if (used >= cap) {
            errno = E2BIG;
            return -1;
        }
    }

    if (cap) {
        buf[used < cap ? used : cap - 1] = '\0';
    }
    return 0;
}

static int _b_exec_common(const char *path, char *const argv[]) {
    char resolved[260];
    char args[512];
    const char *exec_path = _b_resolve_exec_path(path, resolved, sizeof(resolved));

    if (!exec_path) {
        errno = ENOENT;
        return -1;
    }

    if (_b_join_argv(args, sizeof(args), argv) != 0) {
        return -1;
    }

    if (sys_exec(exec_path, args[0] ? args : NULL) < 0) {
        errno = EIO;
        return -1;
    }

    return 0;
}

__attribute__((weak)) int execv(const char *path, char *const argv[]) {
    return _b_exec_common(path, argv);
}

__attribute__((weak)) int execve(const char *path, char *const argv[], char *const envp[]) {
    (void)envp;
    return _b_exec_common(path, argv);
}

__attribute__((weak)) int execvp(const char *file, char *const argv[]) {
    return _b_exec_common(file, argv);
}

__attribute__((weak)) int execl(const char *path, const char *arg, ...) {
    va_list ap;
    char *argv[64];
    int i = 0;

    argv[i++] = (char *)arg;
    va_start(ap, arg);
    while (i < 63) {
        char *v = va_arg(ap, char *);
        argv[i++] = v;
        if (!v) {
            break;
        }
    }
    va_end(ap);

    if (argv[i - 1] != NULL) {
        argv[63] = NULL;
    }

    return execv(path, argv);
}

__attribute__((weak)) int execlp(const char *file, const char *arg, ...) {
    va_list ap;
    char *argv[64];
    int i = 0;

    argv[i++] = (char *)arg;
    va_start(ap, arg);
    while (i < 63) {
        char *v = va_arg(ap, char *);
        argv[i++] = v;
        if (!v) {
            break;
        }
    }
    va_end(ap);

    if (argv[i - 1] != NULL) {
        argv[63] = NULL;
    }

    return execvp(file, argv);
}

__attribute__((weak)) int execle(const char *path, const char *arg, ...) {
    va_list ap;
    char *argv[64];
    int i = 0;
    char *envp;

    argv[i++] = (char *)arg;
    va_start(ap, arg);
    while (i < 63) {
        char *v = va_arg(ap, char *);
        argv[i++] = v;
        if (!v) {
            break;
        }
    }
    envp = va_arg(ap, char *);
    va_end(ap);
    (void)envp;

    if (argv[i - 1] != NULL) {
        argv[63] = NULL;
    }

    return execv(path, argv);
}

__attribute__((weak)) pid_t waitpid(pid_t pid, int *status, int options) {
    int st = 0;

    for (;;) {
        int rc = sys_waitpid((int)pid, &st, options);
        if (rc > 0) {
            if (status) {
                *status = st;
            }
            return (pid_t)rc;
        }
        if (rc == 0 && (options & WNOHANG)) {
            return 0;
        }
        if (rc < 0) {
            errno = ECHILD;
            return -1;
        }
        sleep(1);
    }
}
