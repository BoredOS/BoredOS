#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "ctype.h"
#include "errno.h"
#include "syscall.h"
#include "time.h"

static FILE boredos_stdin_obj = {0, 0, 0, 0, 0};
static FILE boredos_stdout_obj = {1, 0, 0, 0, 0};
static FILE boredos_stderr_obj = {2, 0, 0, 0, 0};
FILE *stdin = &boredos_stdin_obj;
FILE *stdout = &boredos_stdout_obj;
FILE *stderr = &boredos_stderr_obj;

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

static int _b_is_space_char(int c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

static char _b_tmpname_buf[FILENAME_MAX];
static unsigned _b_tmp_counter = 0;

static char *_b_tmpname_generate(char *out, size_t out_cap) {
    unsigned tries;
    if (out_cap == 0) {
        errno = EINVAL;
        return NULL;
    }

    (void)sys_mkdir("/tmp");
    for (tries = 0; tries < 256; tries++) {
        unsigned long long t = (unsigned long long)clock();
        snprintf(out, out_cap, "/tmp/tmp_%llu_%u.tmp", t, _b_tmp_counter++);
        if (!sys_exists(out)) {
            return out;
        }
    }

    errno = EEXIST;
    return NULL;
}

FILE *fopen(const char *path, const char *mode) {
    int fd = sys_open(path, mode);
    FILE *f;
    if (fd < 0) {
        errno = EINVAL;
        return NULL;
    }
    f = (FILE *)malloc(sizeof(FILE));
    if (!f) {
        sys_close(fd);
        errno = ENOMEM;
        return NULL;
    }
    f->fd = fd;
    f->eof = 0;
    f->err = 0;
    f->has_ungetc = 0;
    f->ungetc_char = 0;
    return f;
}

FILE *fdopen(int fd, const char *mode) {
    (void)mode;
    if (fd < 0) return NULL;
    FILE *f = (FILE *)malloc(sizeof(FILE));
    if (!f) return NULL;
    f->fd = fd;
    f->eof = 0;
    f->err = 0;
    f->has_ungetc = 0;
    f->ungetc_char = 0;
    return f;
}

FILE *freopen(const char *path, const char *mode, FILE *stream) {
    int fd;
    if (!stream) {
        return fopen(path, mode);
    }
    if (stream->fd >= 0) {
        sys_close(stream->fd);
    }
    fd = sys_open(path, mode);
    if (fd < 0) {
        stream->err = 1;
        errno = EINVAL;
        return NULL;
    }
    stream->fd = fd;
    stream->eof = 0;
    stream->err = 0;
    stream->has_ungetc = 0;
    return stream;
}

int fclose(FILE *stream) {
    if (!stream) {
        return EOF;
    }
    if (stream != stdin && stream != stdout && stream != stderr) {
        if (stream->fd >= 0) {
            sys_close(stream->fd);
        }
        free(stream);
    }
    return 0;
}

size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream) {
    size_t total;
    int n;
    if (!stream || !ptr || size == 0 || nmemb == 0) {
        return 0;
    }
    total = size * nmemb;
    n = sys_read(stream->fd, ptr, (uint32_t)total);
    if (n <= 0) {
        if (n == 0) {
            stream->eof = 1;
        } else {
            stream->err = 1;
        }
        return 0;
    }
    if ((size_t)n < total) {
        stream->eof = 1;
    }
    return (size_t)n / size;
}

size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream) {
    size_t total;
    int n;
    if (!stream || !ptr || size == 0 || nmemb == 0) {
        return 0;
    }
    total = size * nmemb;
    if (stream->fd <= 2) {
        n = sys_write(stream->fd, (const char *)ptr, (int)total);
    } else {
        n = sys_write_fs(stream->fd, ptr, (uint32_t)total);
    }
    if (n < 0) {
        stream->err = 1;
        return 0;
    }
    return (size_t)n / size;
}

int fseek(FILE *stream, long offset, int whence) {
    if (!stream) {
        return -1;
    }
    if (sys_seek(stream->fd, (int)offset, whence) < 0) {
        stream->err = 1;
        return -1;
    }
    stream->eof = 0;
    stream->has_ungetc = 0;
    return 0;
}

long ftell(FILE *stream) {
    if (!stream) {
        return -1;
    }
    return (long)sys_tell(stream->fd);
}

int getc(FILE *stream) {
    unsigned char ch;
    int n;
    if (!stream) {
        return EOF;
    }
    if (stream->has_ungetc) {
        stream->has_ungetc = 0;
        return stream->ungetc_char;
    }
    n = sys_read(stream->fd, &ch, 1);
    if (n <= 0) {
        if (n == 0) {
            stream->eof = 1;
        } else {
            stream->err = 1;
        }
        return EOF;
    }
    return (int)ch;
}

int fgetc(FILE *stream) {
    return getc(stream);
}

int ungetc(int c, FILE *stream) {
    if (!stream || c == EOF) {
        return EOF;
    }
    stream->has_ungetc = 1;
    stream->ungetc_char = (unsigned char)c;
    stream->eof = 0;
    return c;
}

char *fgets(char *s, int n, FILE *stream) {
    int i;
    if (!s || n <= 0 || !stream) {
        return NULL;
    }
    for (i = 0; i < n - 1; i++) {
        int c = getc(stream);
        if (c == EOF) {
            break;
        }
        s[i] = (char)c;
        if (c == '\n') {
            i++;
            break;
        }
    }
    if (i == 0) {
        return NULL;
    }
    s[i] = '\0';
    return s;
}

int fputs(const char *s, FILE *stream) {
    size_t len;
    size_t written;
    if (!s || !stream) {
        return EOF;
    }
    len = strlen(s);
    written = fwrite(s, 1, len, stream);
    return (written == len) ? (int)len : EOF;
}

int feof(FILE *stream) {
    return stream ? stream->eof : 1;
}

int ferror(FILE *stream) {
    return stream ? stream->err : 1;
}

void clearerr(FILE *stream) {
    if (stream) {
        stream->eof = 0;
        stream->err = 0;
    }
}

int fflush(FILE *stream) {
    (void)stream;
    return 0;
}

int remove(const char *path) {
    return sys_delete(path);
}

int rename(const char *oldpath, const char *newpath) {
    FILE *src;
    FILE *dst;
    char buf[1024];
    size_t nread;

    if (!oldpath || !newpath || oldpath[0] == '\0' || newpath[0] == '\0') {
        errno = EINVAL;
        return -1;
    }
    if (_b_streq(oldpath, newpath)) {
        return 0;
    }

    src = fopen(oldpath, "rb");
    if (!src) {
        errno = ENOENT;
        return -1;
    }
    dst = fopen(newpath, "wb");
    if (!dst) {
        fclose(src);
        return -1;
    }

    for (;;) {
        nread = fread(buf, 1, sizeof(buf), src);
        if (nread == 0) {
            break;
        }
        if (fwrite(buf, 1, nread, dst) != nread) {
            fclose(src);
            fclose(dst);
            sys_delete(newpath);
            errno = EIO;
            return -1;
        }
    }

    if (ferror(src) || ferror(dst)) {
        fclose(src);
        fclose(dst);
        sys_delete(newpath);
        errno = EIO;
        return -1;
    }

    fclose(src);
    fclose(dst);
    if (sys_delete(oldpath) != 0) {
        errno = EIO;
        return -1;
    }
    return 0;
}

FILE *tmpfile(void) {
    char path[FILENAME_MAX];
    if (!_b_tmpname_generate(path, sizeof(path))) {
        return NULL;
    }
    return fopen(path, "w+");
}

char *tmpnam(char *s) {
    char *dst = s ? s : _b_tmpname_buf;
    return _b_tmpname_generate(dst, FILENAME_MAX);
}

static int _b_hex_digit(unsigned value, int upper) {
    if (value < 10U) {
        return (int)('0' + value);
    }
    return (int)((upper ? 'A' : 'a') + (value - 10U));
}

static void _b_append_char(char *out, size_t cap, size_t *idx, int c) {
    if (*idx + 1 < cap) {
        out[*idx] = (char)c;
    }
    (*idx)++;
}

static void _b_append_strn(char *out, size_t cap, size_t *idx, const char *s, size_t n) {
    size_t i;
    for (i = 0; i < n; i++) {
        _b_append_char(out, cap, idx, s[i]);
    }
}

static void _b_append_repeat(char *out, size_t cap, size_t *idx, char ch, int count) {
    int i;
    for (i = 0; i < count; i++) {
        _b_append_char(out, cap, idx, ch);
    }
}

static void _b_utoa(unsigned long long v, unsigned base, int upper, char *buf, size_t *len) {
    char tmp[64];
    size_t i = 0;
    if (v == 0) {
        tmp[i++] = '0';
    } else {
        while (v && i < sizeof(tmp)) {
            tmp[i++] = (char)_b_hex_digit((unsigned)(v % base), upper);
            v /= base;
        }
    }
    *len = i;
    while (i > 0) {
        *buf++ = tmp[--i];
    }
}

static void _b_itoa(long long v, char *buf, size_t *len) {
    unsigned long long uv;
    size_t n = 0;
    if (v < 0) {
        *buf++ = '-';
        n++;
        uv = (unsigned long long)(-(v + 1)) + 1ULL;
    } else {
        uv = (unsigned long long)v;
    }
    _b_utoa(uv, 10U, 0, buf, len);
    *len += n;
}

static void _b_ftoa(double d, int precision, char *buf, size_t *len) {
    long long ip;
    double frac;
    size_t n = 0;
    if (precision < 0) {
        precision = 6;
    }
    if (d < 0.0) {
        buf[n++] = '-';
        d = -d;
    }
    ip = (long long)d;
    frac = d - (double)ip;
    {
        char ibuf[64];
        size_t ilen = 0;
        _b_utoa((unsigned long long)ip, 10U, 0, ibuf, &ilen);
        memcpy(buf + n, ibuf, ilen);
        n += ilen;
    }
    if (precision > 0) {
        int i;
        buf[n++] = '.';
        for (i = 0; i < precision; i++) {
            int digit;
            frac *= 10.0;
            digit = (int)frac;
            if (digit < 0) digit = 0;
            if (digit > 9) digit = 9;
            buf[n++] = (char)('0' + digit);
            frac -= (double)digit;
        }
    }
    *len = n;
}

__attribute__((weak)) int vsnprintf(char *str, size_t size, const char *fmt, va_list ap) {
    size_t out_i = 0;

    while (*fmt) {
        if (*fmt != '%') {
            _b_append_char(str, size, &out_i, *fmt++);
            continue;
        }

        fmt++;
        if (*fmt == '%') {
            _b_append_char(str, size, &out_i, '%');
            fmt++;
            continue;
        }

        {
            int left = 0;
            int plus = 0;
            int space = 0;
            int alt = 0;
            int zero = 0;
            int width = -1;
            int precision = -1;
            int lcount = 0;
            char spec;

            while (*fmt == '-' || *fmt == '+' || *fmt == ' ' || *fmt == '#' || *fmt == '0') {
                if (*fmt == '-') left = 1;
                else if (*fmt == '+') plus = 1;
                else if (*fmt == ' ') space = 1;
                else if (*fmt == '#') alt = 1;
                else if (*fmt == '0') zero = 1;
                fmt++;
            }

            if (*fmt == '*') {
                width = va_arg(ap, int);
                if (width < 0) {
                    left = 1;
                    width = -width;
                }
                fmt++;
            } else if (isdigit((unsigned char)*fmt)) {
                width = 0;
                while (isdigit((unsigned char)*fmt)) {
                    width = width * 10 + (*fmt - '0');
                    fmt++;
                }
            }

            if (*fmt == '.') {
                fmt++;
                precision = 0;
                if (*fmt == '*') {
                    precision = va_arg(ap, int);
                    if (precision < 0) {
                        precision = -1;
                    }
                    fmt++;
                } else {
                    while (isdigit((unsigned char)*fmt)) {
                        precision = precision * 10 + (*fmt - '0');
                        fmt++;
                    }
                }
            }

            while (*fmt == 'l') {
                lcount++;
                fmt++;
            }

            spec = *fmt;
            if (!spec) {
                break;
            }

            switch (spec) {
                case 'd':
                case 'i': {
                    long long sv;
                    unsigned long long uv;
                    char digits[64];
                    size_t dlen = 0;
                    int neg = 0;
                    char signch = '\0';
                    int zeros = 0;
                    int spaces = 0;
                    int total;

                    if (lcount >= 2) sv = va_arg(ap, long long);
                    else if (lcount == 1) sv = va_arg(ap, long);
                    else sv = va_arg(ap, int);

                    if (sv < 0) {
                        neg = 1;
                        uv = (unsigned long long)(-(sv + 1)) + 1ULL;
                    } else {
                        uv = (unsigned long long)sv;
                    }

                    if (neg) signch = '-';
                    else if (plus) signch = '+';
                    else if (space) signch = ' ';

                    if (!(precision == 0 && uv == 0ULL)) {
                        _b_utoa(uv, 10U, 0, digits, &dlen);
                    }

                    if (precision > 0 && (size_t)precision > dlen) {
                        zeros = precision - (int)dlen;
                    }

                    if (precision < 0 && zero && !left && width > 0) {
                        int signw = (signch != '\0') ? 1 : 0;
                        int need = width - (signw + (int)dlen);
                        if (need > zeros) {
                            zeros = need;
                        }
                    }

                    total = ((signch != '\0') ? 1 : 0) + zeros + (int)dlen;
                    if (width > total) {
                        spaces = width - total;
                    }

                    if (!left) {
                        _b_append_repeat(str, size, &out_i, ' ', spaces);
                    }
                    if (signch != '\0') {
                        _b_append_char(str, size, &out_i, signch);
                    }
                    _b_append_repeat(str, size, &out_i, '0', zeros);
                    _b_append_strn(str, size, &out_i, digits, dlen);
                    if (left) {
                        _b_append_repeat(str, size, &out_i, ' ', spaces);
                    }
                    break;
                }

                case 'u':
                case 'x':
                case 'X':
                case 'o': {
                    unsigned long long uv;
                    unsigned base = (spec == 'o') ? 8U : ((spec == 'u') ? 10U : 16U);
                    int upper = (spec == 'X');
                    char digits[64];
                    size_t dlen = 0;
                    int zeros = 0;
                    int spaces = 0;
                    int prefix_len = 0;
                    char p1 = '\0';
                    char p2 = '\0';
                    int total;

                    if (lcount >= 2) uv = va_arg(ap, unsigned long long);
                    else if (lcount == 1) uv = va_arg(ap, unsigned long);
                    else uv = va_arg(ap, unsigned int);

                    if (!(precision == 0 && uv == 0ULL)) {
                        _b_utoa(uv, base, upper, digits, &dlen);
                    }

                    if (alt && uv != 0ULL) {
                        if (spec == 'x' || spec == 'X') {
                            p1 = '0';
                            p2 = upper ? 'X' : 'x';
                            prefix_len = 2;
                        } else if (spec == 'o') {
                            p1 = '0';
                            prefix_len = 1;
                        }
                    }

                    if (precision > 0 && (size_t)precision > dlen) {
                        zeros = precision - (int)dlen;
                    }

                    if (precision < 0 && zero && !left && width > 0) {
                        int need = width - (prefix_len + (int)dlen);
                        if (need > zeros) {
                            zeros = need;
                        }
                    }

                    total = prefix_len + zeros + (int)dlen;
                    if (width > total) {
                        spaces = width - total;
                    }

                    if (!left) {
                        _b_append_repeat(str, size, &out_i, ' ', spaces);
                    }
                    if (prefix_len >= 1) {
                        _b_append_char(str, size, &out_i, p1);
                    }
                    if (prefix_len >= 2) {
                        _b_append_char(str, size, &out_i, p2);
                    }
                    _b_append_repeat(str, size, &out_i, '0', zeros);
                    _b_append_strn(str, size, &out_i, digits, dlen);
                    if (left) {
                        _b_append_repeat(str, size, &out_i, ' ', spaces);
                    }
                    break;
                }

                case 'c': {
                    int c = va_arg(ap, int);
                    int spaces = (width > 1) ? (width - 1) : 0;
                    if (!left) {
                        _b_append_repeat(str, size, &out_i, ' ', spaces);
                    }
                    _b_append_char(str, size, &out_i, c);
                    if (left) {
                        _b_append_repeat(str, size, &out_i, ' ', spaces);
                    }
                    break;
                }

                case 's': {
                    const char *s = va_arg(ap, const char *);
                    size_t slen;
                    int spaces;
                    if (!s) s = "(null)";
                    slen = strlen(s);
                    if (precision >= 0 && (size_t)precision < slen) {
                        slen = (size_t)precision;
                    }
                    spaces = (width > (int)slen) ? (width - (int)slen) : 0;
                    if (!left) {
                        _b_append_repeat(str, size, &out_i, ' ', spaces);
                    }
                    _b_append_strn(str, size, &out_i, s, slen);
                    if (left) {
                        _b_append_repeat(str, size, &out_i, ' ', spaces);
                    }
                    break;
                }

                case 'p': {
                    uintptr_t v = (uintptr_t)va_arg(ap, void *);
                    char nbuf[32];
                    size_t nlen = 0;
                    _b_append_strn(str, size, &out_i, "0x", 2);
                    _b_utoa((unsigned long long)v, 16U, 0, nbuf, &nlen);
                    _b_append_strn(str, size, &out_i, nbuf, nlen);
                    break;
                }

                case 'f':
                case 'g':
                case 'e': {
                    double v = va_arg(ap, double);
                    char nbuf[96];
                    size_t nlen = 0;
                    int spaces;
                    _b_ftoa(v, precision, nbuf, &nlen);
                    spaces = (width > (int)nlen) ? (width - (int)nlen) : 0;
                    if (!left) {
                        _b_append_repeat(str, size, &out_i, ' ', spaces);
                    }
                    _b_append_strn(str, size, &out_i, nbuf, nlen);
                    if (left) {
                        _b_append_repeat(str, size, &out_i, ' ', spaces);
                    }
                    break;
                }

                default:
                    _b_append_char(str, size, &out_i, '%');
                    _b_append_char(str, size, &out_i, spec);
                    break;
            }
        }

        if (*fmt) {
            fmt++;
        }
    }

    if (size > 0) {
        size_t term = (out_i < size - 1) ? out_i : (size - 1);
        str[term] = '\0';
    }

    return (int)out_i;
}

__attribute__((weak)) int snprintf(char *str, size_t size, const char *fmt, ...) {
    int n;
    va_list ap;
    va_start(ap, fmt);
    n = vsnprintf(str, size, fmt, ap);
    va_end(ap);
    return n;
}

__attribute__((weak)) int sprintf(char *str, const char *fmt, ...) {
    int n;
    va_list ap;
    va_start(ap, fmt);
    n = vsnprintf(str, (size_t)-1, fmt, ap);
    va_end(ap);
    return n;
}

int fprintf(FILE *stream, const char *fmt, ...) {
    char buf[1024];
    int len;
    va_list ap;
    va_start(ap, fmt);
    len = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (len <= 0) {
        return len;
    }
    if ((size_t)len > sizeof(buf)) {
        len = (int)sizeof(buf);
    }
    if (fwrite(buf, 1, (size_t)len, stream) == 0) {
        return -1;
    }
    return len;
}

__attribute__((weak)) int vfprintf(FILE *stream, const char *fmt, va_list ap) {
    if (!stream) return 0;
    char buf[1024];
    int len = vsnprintf(buf, sizeof(buf), fmt, ap);
    if (len <= 0) {
        return len;
    }
    if ((size_t)len > sizeof(buf)) {
        len = (int)sizeof(buf);
    }
    if (fwrite(buf, 1, (size_t)len, stream) == 0) {
        return -1;
    }
    return len;
}

int fputc(int c, FILE *stream) {
    unsigned char ch = (unsigned char)c;
    if (!stream) {
        return EOF;
    }
    if (fwrite(&ch, 1, 1, stream) != 1) {
        return EOF;
    }
    return c;
}

__attribute__((weak)) int putchar(int c) {
    return fputc(c, stdout);
}

long filelength(FILE *f) {
    if (!f) {
        return -1;
    }
    return (long)sys_size(f->fd);
}

static const char *_b_skip_spaces(const char *p) {
    while (*p && _b_is_space_char((unsigned char)*p)) {
        p++;
    }
    return p;
}

static int _b_digit_val(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

static int _b_parse_u64(const char **sp, int base, int width, unsigned long long *out) {
    const char *p = *sp;
    unsigned long long v = 0;
    int any = 0;

    if (width != 0 && base == 16 && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        p += 2;
        if (width > 0) {
            width -= 2;
        }
    }

    while (*p && (width < 0 || width > 0)) {
        int d = _b_digit_val((unsigned char)*p);
        if (d < 0 || d >= base) {
            break;
        }
        v = v * (unsigned long long)base + (unsigned long long)d;
        any = 1;
        p++;
        if (width > 0) {
            width--;
        }
    }

    if (!any) {
        return 0;
    }
    *sp = p;
    *out = v;
    return 1;
}

static int _b_parse_auto_int(const char **sp, int width, long long *out) {
    const char *p = *sp;
    int neg = 0;
    int base = 10;
    unsigned long long uv;

    if (*p == '+' || *p == '-') {
        neg = (*p == '-');
        p++;
        if (width > 0) {
            width--;
        }
    }

    if ((width < 0 || width >= 2) && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        base = 16;
    } else if ((width < 0 || width >= 1) && p[0] == '0') {
        base = 8;
    }

    if (!_b_parse_u64(&p, base, width, &uv)) {
        return 0;
    }
    *sp = p;
    *out = neg ? -(long long)uv : (long long)uv;
    return 1;
}

static int _b_parse_s64(const char **sp, int base, int width, long long *out) {
    const char *p = *sp;
    int neg = 0;
    unsigned long long uv;

    if (*p == '+' || *p == '-') {
        neg = (*p == '-');
        p++;
        if (width > 0) {
            width--;
        }
    }
    if (!_b_parse_u64(&p, base, width, &uv)) {
        return 0;
    }
    *sp = p;
    *out = neg ? -(long long)uv : (long long)uv;
    return 1;
}

static int _b_vsscanf_core(const char *str, const char *fmt, va_list ap) {
    const char *s = str;
    const char *f = fmt;
    int assigned = 0;

    while (*f) {
        if (_b_is_space_char((unsigned char)*f)) {
            while (_b_is_space_char((unsigned char)*f)) f++;
            s = _b_skip_spaces(s);
            continue;
        }

        if (*f != '%') {
            if (*s != *f) {
                break;
            }
            s++;
            f++;
            continue;
        }

        f++;
        if (*f == '%') {
            if (*s != '%') {
                break;
            }
            s++;
            f++;
            continue;
        }

        {
            int suppress = 0;
            int width = -1;
            int lcount = 0;

            if (*f == '*') {
                suppress = 1;
                f++;
            }

            if (isdigit((unsigned char)*f)) {
                width = 0;
                while (isdigit((unsigned char)*f)) {
                    width = width * 10 + (*f - '0');
                    f++;
                }
            }

            while (*f == 'l') {
                lcount++;
                f++;
            }

            if (*f != 'c' && *f != 'n') {
                s = _b_skip_spaces(s);
            }

            switch (*f) {
                case 'd': {
                    long long v;
                    if (!_b_parse_s64(&s, 10, width, &v)) return assigned;
                    if (!suppress) {
                        if (lcount >= 1) *va_arg(ap, long *) = (long)v;
                        else *va_arg(ap, int *) = (int)v;
                        assigned++;
                    }
                    break;
                }
                case 'i': {
                    long long v;
                    if (!_b_parse_auto_int(&s, width, &v)) return assigned;
                    if (!suppress) {
                        if (lcount >= 1) *va_arg(ap, long *) = (long)v;
                        else *va_arg(ap, int *) = (int)v;
                        assigned++;
                    }
                    break;
                }
                case 'u':
                case 'x':
                case 'X':
                case 'o': {
                    int base = (*f == 'o') ? 8 : ((*f == 'u') ? 10 : 16);
                    unsigned long long v;
                    if (!_b_parse_u64(&s, base, width, &v)) return assigned;
                    if (!suppress) {
                        if (lcount >= 1) *va_arg(ap, unsigned long *) = (unsigned long)v;
                        else *va_arg(ap, unsigned *) = (unsigned)v;
                        assigned++;
                    }
                    break;
                }
                case 'c': {
                    int count = (width > 0) ? width : 1;
                    if (!*s) return assigned;
                    if (!suppress) {
                        char *dst = va_arg(ap, char *);
                        while (count-- > 0 && *s) {
                            *dst++ = *s++;
                        }
                        assigned++;
                    } else {
                        while (count-- > 0 && *s) s++;
                    }
                    break;
                }
                case 's': {
                    int wrote = 0;
                    if (!*s) return assigned;
                    if (!suppress) {
                        char *dst = va_arg(ap, char *);
                        while (*s && !_b_is_space_char((unsigned char)*s) && (width < 0 || width-- > 0)) {
                            *dst++ = *s++;
                            wrote = 1;
                        }
                        if (!wrote) return assigned;
                        *dst = '\0';
                        assigned++;
                    } else {
                        while (*s && !_b_is_space_char((unsigned char)*s) && (width < 0 || width-- > 0)) {
                            s++;
                            wrote = 1;
                        }
                        if (!wrote) return assigned;
                    }
                    break;
                }
                case 'n': {
                    if (!suppress) {
                        *va_arg(ap, int *) = (int)(s - str);
                    }
                    break;
                }
                default:
                    return assigned;
            }
        }

        if (*f) {
            f++;
        }
    }

    return assigned;
}

int sscanf(const char *str, const char *fmt, ...) {
    int n;
    va_list ap;
    if (!str || !fmt) {
        errno = EINVAL;
        return 0;
    }
    va_start(ap, fmt);
    n = _b_vsscanf_core(str, fmt, ap);
    va_end(ap);
    return n;
}

__attribute__((weak)) void puts(const char *s) {
    if (!s) return;
    fputs(s, stdout);
    fputc('\n', stdout);
}

__attribute__((weak)) void printf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stdout, fmt, ap);
    va_end(ap);
}
