#include "stdlib.h"
char **environ = 0;
#include "string.h"
#include "errno.h"
#include "syscall.h"
#include "stdio.h"
#include "math.h"

static int _b_is_space_char(int c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

static int _b_try_spawn_command(const char *command) {
    char cmd[128];
    char args[256];
    int i = 0;
    int j = 0;
    int pid;

    while (command[i] && _b_is_space_char((unsigned char)command[i])) i++;
    while (command[i] && !_b_is_space_char((unsigned char)command[i]) && j < (int)sizeof(cmd) - 1) {
        cmd[j++] = command[i++];
    }
    cmd[j] = '\0';

    if (cmd[0] == '\0') {
        return 0;
    }

    while (command[i] && _b_is_space_char((unsigned char)command[i])) i++;
    {
        int k = 0;
        while (command[i] && k < (int)sizeof(args) - 1) {
            args[k++] = command[i++];
        }
        args[k] = '\0';
    }

    pid = sys_spawn(cmd, args[0] ? args : NULL, SPAWN_FLAG_TERMINAL | SPAWN_FLAG_INHERIT_TTY, 0);
    if (pid >= 0) {
        return 0;
    }

    if (cmd[0] != '/') {
        char path[160];
        snprintf(path, sizeof(path), "/bin/%s", cmd);
        pid = sys_spawn(path, args[0] ? args : NULL, SPAWN_FLAG_TERMINAL | SPAWN_FLAG_INHERIT_TTY, 0);
        if (pid >= 0) {
            return 0;
        }

        snprintf(path, sizeof(path), "/bin/%s.elf", cmd);
        pid = sys_spawn(path, args[0] ? args : NULL, SPAWN_FLAG_TERMINAL | SPAWN_FLAG_INHERIT_TTY, 0);
        if (pid >= 0) {
            return 0;
        }
    }

    errno = ENOENT;
    return -1;
}

static int _b_streq(const char *a, const char *b) {
    while (*a && *b) {
        if (*a != *b) {
            return 0;
        }
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

__attribute__((weak)) int abs(int x) {
    return (x < 0) ? -x : x;
}

__attribute__((weak)) int system(const char *command) {
    if (command == NULL) {
        return 1;
    }
    return _b_try_spawn_command(command);
}

__attribute__((weak)) char *getenv(const char *name) {
    const char *cfg;
    static char cwd_buf[256];
    if (!name || name[0] == '\0') {
        return NULL;
    }

    if (_b_streq(name, "PWD")) {
        if (sys_getcwd(cwd_buf, (int)sizeof(cwd_buf)) >= 0) {
            return cwd_buf;
        }
        return NULL;
    }

    if (_b_streq(name, "PATH")) return "/bin:/";
    if (_b_streq(name, "HOME")) return "/";
    if (_b_streq(name, "SHELL")) return "/bin/bsh";
    if (_b_streq(name, "TERM")) return "boredos";

    cfg = (const char *)(uintptr_t)sys_get_shell_config(name);
    if ((uintptr_t)cfg > 0x10000ULL) {
        return (char *)cfg;
    }
    return NULL;
}

__attribute__((weak)) void abort(void) {
    sys_exit(1);
    while (1) {}
}

__attribute__((weak)) void _exit(int status) {
    sys_exit(status);
    while (1) {}
}

__attribute__((weak)) double strtod(const char *nptr, char **endptr) {
    const char *p = nptr;
    int sign = 1;
    double value = 0.0;
    double frac = 0.0;
    double scale = 1.0;
    int exp_sign = 1;
    int exp_val = 0;

    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == '\f' || *p == '\v') p++;

    if (*p == '-') {
        sign = -1;
        p++;
    } else if (*p == '+') {
        p++;
    }

    while (*p >= '0' && *p <= '9') {
        value = value * 10.0 + (double)(*p - '0');
        p++;
    }

    if (*p == '.') {
        p++;
        while (*p >= '0' && *p <= '9') {
            frac = frac * 10.0 + (double)(*p - '0');
            scale *= 10.0;
            p++;
        }
        value += frac / scale;
    }

    if (*p == 'e' || *p == 'E') {
        const char *ep = p + 1;
        if (*ep == '-') {
            exp_sign = -1;
            ep++;
        } else if (*ep == '+') {
            ep++;
        }
        if (*ep >= '0' && *ep <= '9') {
            p = ep;
            while (*p >= '0' && *p <= '9') {
                exp_val = exp_val * 10 + (*p - '0');
                p++;
            }
        }
    }

    if (endptr) {
        *endptr = (char *)p;
    }

    if (exp_val != 0) {
        value = ldexp(value, exp_sign * exp_val);
    }
    return sign * value;
}

__attribute__((weak)) float strtof(const char *nptr, char **endptr) {
    return (float)strtod(nptr, endptr);
}

__attribute__((weak)) long double strtold(const char *nptr, char **endptr) {
    return (long double)strtod(nptr, endptr);
}
