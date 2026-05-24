extern "C" {
#include "libc/stdlib.h"
#include "libc/string.h"
#include "libc/stdio.h"
#include "libc/math.h"
#include "libc/syscall.h"
#include "libc/time.h"
#include "libc/ctype.h"
#include "libc/locale.h"
#include "libc/errno.h"
#include "libc/wchar.h"
#include "libc/sys/stat.h"
#include "libc/unistd.h"
#include "libc/fcntl.h"
#include "libc/signal.h"
}

#include <stddef.h>
#include <stdint.h>

namespace std { class type_info; }

extern "C" int __gxx_personality_v0(...)
{
    return 0;
}

extern "C" void _Unwind_Resume(void *)
{
    abort();
}

extern "C" void *__dynamic_cast(const void *src_ptr, const void *, const void *, ptrdiff_t)
{
    return const_cast<void *>(src_ptr);
}

extern "C" [[noreturn]] void __cxa_call_terminate(void *)
{
    abort();
    for (;;) {}
}

extern "C" void *__cxa_begin_catch(void *exception)
{
    return exception;
}

extern "C" void __cxa_end_catch(void)
{
}

extern "C" void *__cxa_allocate_exception(size_t size)
{
    if (size == 0) size = 1;
    return malloc(size);
}

extern "C" void __cxa_free_exception(void *ptr)
{
    free(ptr);
}

extern "C" [[noreturn]] void __cxa_throw(void *exception, void *, void (*)(void *))
{
    free(exception);
    syscall2(8, 0, (uint64_t)"[cxx] unhandled C++ exception\n");
    abort();
    for (;;) {}
}

extern "C" int __popcountdi2(uint64_t value)
{
    int count = 0;
    while (value != 0) {
        count += (int)(value & 1u);
        value >>= 1;
    }
    return count;
}

extern "C" int boredos_cpp_abs(int x) asm("_Z3absi");
extern "C" int boredos_cpp_abs(int x) { return x < 0 ? -x : x; }

extern "C" [[noreturn]] void boredos_cpp_abort(void) asm("_Z5abortv");
extern "C" [[noreturn]] void boredos_cpp_abort(void) { abort(); for (;;) {} }

extern "C" float boredos_cpp_sinf(float x) asm("_Z4sinff");
extern "C" float boredos_cpp_sinf(float x) { return (float)sin((double)x); }
extern "C" float sinf(float x) { return boredos_cpp_sinf(x); }

extern "C" float boredos_cpp_cosf(float x) asm("_Z4cosff");
extern "C" float boredos_cpp_cosf(float x) { return (float)cos((double)x); }
extern "C" float cosf(float x) { return boredos_cpp_cosf(x); }

extern "C" float boredos_cpp_tanf(float x) asm("_Z4tanff");
extern "C" float boredos_cpp_tanf(float x) { return (float)tan((double)x); }
extern "C" float tanf(float x) { return boredos_cpp_tanf(x); }

extern "C" float boredos_cpp_sqrtf(float x) asm("_Z5sqrtff");
extern "C" float boredos_cpp_sqrtf(float x) { return (float)sqrt((double)x); }
extern "C" float sqrtf(float x) { return boredos_cpp_sqrtf(x); }

extern "C" float boredos_cpp_expf(float x) asm("_Z4expff");
extern "C" float boredos_cpp_expf(float x) { return (float)exp((double)x); }
extern "C" float expf(float x) { return boredos_cpp_expf(x); }

extern "C" float boredos_cpp_powf(float base, float exponent) asm("_Z4powfff");
extern "C" float boredos_cpp_powf(float base, float exponent) { return (float)pow((double)base, (double)exponent); }
extern "C" float powf(float base, float exponent) { return boredos_cpp_powf(base, exponent); }

extern "C" double boredos_cpp_pow(double base, double exponent) asm("_Z3powdd");
extern "C" double boredos_cpp_pow(double base, double exponent) { return pow(base, exponent); }

extern "C" double boredos_cpp_sin(double x) asm("_Z3sind");
extern "C" double boredos_cpp_sin(double x) { return sin(x); }

extern "C" float boredos_cpp_atan2f(float y, float x) asm("_Z6atan2fff");
extern "C" float boredos_cpp_atan2f(float y, float x) { return (float)atan2((double)y, (double)x); }
extern "C" float atan2f(float y, float x) { return boredos_cpp_atan2f(y, x); }

extern "C" float boredos_cpp_atanf(float x) asm("_Z5atanff");
extern "C" float boredos_cpp_atanf(float x) { return (float)atan2((double)x, 1.0); }
extern "C" float atanf(float x) { return boredos_cpp_atanf(x); }

extern "C" float boredos_cpp_log10f(float x) asm("_Z6log10ff");
extern "C" float boredos_cpp_log10f(float x) { return (float)log10((double)x); }
extern "C" float log10f(float x) { return boredos_cpp_log10f(x); }

extern "C" float boredos_cpp_logf(float x) asm("_Z4logff");
extern "C" float boredos_cpp_logf(float x) { return (float)log((double)x); }
extern "C" float logf(float x) { return boredos_cpp_logf(x); }

extern "C" float boredos_cpp_acosf(float x) asm("_Z5acosff");
extern "C" float boredos_cpp_acosf(float x) { return (float)acos((double)x); }
extern "C" float acosf(float x) { return boredos_cpp_acosf(x); }

extern "C" float boredos_cpp_asinf(float x) asm("_Z5asinff");
extern "C" float boredos_cpp_asinf(float x) { return (float)asin((double)x); }
extern "C" float asinf(float x) { return boredos_cpp_asinf(x); }

extern "C" float boredos_cpp_modff(float x, float *iptr) asm("_Z5modffPf");
extern "C" float boredos_cpp_modff(float x, float *iptr)
{
    int whole = (int)x;
    if (iptr != nullptr) *iptr = (float)whole;
    return x - (float)whole;
}
extern "C" float modff(float x, float *iptr) { return boredos_cpp_modff(x, iptr); }

extern "C" double boredos_cpp_round(double x) asm("_Z5roundd");
extern "C" double boredos_cpp_round(double x) { return x >= 0.0 ? floor(x + 0.5) : ceil(x - 0.5); }

extern "C" double boredos_cpp_floor(double x) asm("_Z5floord");
extern "C" double boredos_cpp_floor(double x) { return floor(x); }

extern "C" double boredos_cpp_fmod(double x, double y) asm("_Z4fmoddd");
extern "C" double boredos_cpp_fmod(double x, double y) { return fmod(x, y); }

extern "C" void *boredos_cpp_memcpy(void *dest, const void *src, size_t n) asm("_Z6memcpyPvPKvm");
extern "C" void *boredos_cpp_memcpy(void *dest, const void *src, size_t n) { return memcpy(dest, src, n); }

extern "C" void *boredos_cpp_memmove(void *dest, const void *src, size_t n) asm("_Z7memmovePvPKvm");
extern "C" void *boredos_cpp_memmove(void *dest, const void *src, size_t n) { return memmove(dest, src, n); }

extern "C" void *boredos_cpp_memset(void *dest, int c, size_t n) asm("_Z6memsetPvim");
extern "C" void *boredos_cpp_memset(void *dest, int c, size_t n) { return memset(dest, c, n); }

extern "C" int boredos_cpp_memcmp(const void *a, const void *b, size_t n) asm("_Z6memcmpPKvS0_m");
extern "C" int boredos_cpp_memcmp(const void *a, const void *b, size_t n) { return memcmp(a, b, n); }

extern "C" size_t boredos_cpp_strlen(const char *s) asm("_Z6strlenPKc");
extern "C" size_t boredos_cpp_strlen(const char *s) { return strlen(s); }

extern "C" int boredos_cpp_toupper(int c) asm("_Z7toupperi");
extern "C" int boredos_cpp_toupper(int c) { return toupper(c); }

extern "C" int boredos_cpp_tolower(int c) asm("_Z7toloweri");
extern "C" int boredos_cpp_tolower(int c) { return tolower(c); }

extern "C" int boredos_cpp_isalnum(int c) asm("_Z7isalnumi");
extern "C" int boredos_cpp_isalnum(int c) { return isalnum(c); }

extern "C" char *boredos_cpp_getenv(const char *) asm("_Z6getenvPKc");
extern "C" char *boredos_cpp_getenv(const char *) { return nullptr; }

extern "C" size_t boredos_cpp_fread(void *ptr, size_t size, size_t count, FILE *stream) asm("_Z5freadPvmmP13BOREDOS_FILE");
extern "C" size_t boredos_cpp_fread(void *ptr, size_t size, size_t count, FILE *stream) { return fread(ptr, size, count, stream); }
extern "C" size_t boredos_cpp_fread_12(void *ptr, size_t size, size_t count, FILE *stream) asm("_Z5freadPvmmP12BOREDOS_FILE");
extern "C" size_t boredos_cpp_fread_12(void *ptr, size_t size, size_t count, FILE *stream) { return fread(ptr, size, count, stream); }

extern "C" int boredos_cpp_fclose(FILE *stream) asm("_Z6fcloseP13BOREDOS_FILE");
extern "C" int boredos_cpp_fclose(FILE *stream) { return fclose(stream); }
extern "C" int boredos_cpp_fclose_12(FILE *stream) asm("_Z6fcloseP12BOREDOS_FILE");
extern "C" int boredos_cpp_fclose_12(FILE *stream) { return fclose(stream); }

extern "C" size_t boredos_cpp_fwrite(const void *ptr, size_t size, size_t count, FILE *stream) asm("_Z6fwritePKvmmP13BOREDOS_FILE");
extern "C" size_t boredos_cpp_fwrite(const void *ptr, size_t size, size_t count, FILE *stream) { return fwrite(ptr, size, count, stream); }
extern "C" size_t boredos_cpp_fwrite_12(const void *ptr, size_t size, size_t count, FILE *stream) asm("_Z6fwritePKvmmP12BOREDOS_FILE");
extern "C" size_t boredos_cpp_fwrite_12(const void *ptr, size_t size, size_t count, FILE *stream) { return fwrite(ptr, size, count, stream); }

extern "C" int boredos_cpp_fseek(FILE *stream, long offset, int whence) asm("_Z5fseekP13BOREDOS_FILEli");
extern "C" int boredos_cpp_fseek(FILE *stream, long offset, int whence) { return fseek(stream, offset, whence); }
extern "C" int boredos_cpp_fseek_12(FILE *stream, long offset, int whence) asm("_Z5fseekP12BOREDOS_FILEli");
extern "C" int boredos_cpp_fseek_12(FILE *stream, long offset, int whence) { return fseek(stream, offset, whence); }

extern "C" long boredos_cpp_ftell(FILE *stream) asm("_Z5ftellP13BOREDOS_FILE");
extern "C" long boredos_cpp_ftell(FILE *stream) { return ftell(stream); }
extern "C" long boredos_cpp_ftell_12(FILE *stream) asm("_Z5ftellP12BOREDOS_FILE");
extern "C" long boredos_cpp_ftell_12(FILE *stream) { return ftell(stream); }

extern "C" int boredos_cpp_fputc(int ch, FILE *stream) asm("_Z5fputciP13BOREDOS_FILE");
extern "C" int boredos_cpp_fputc(int ch, FILE *stream) { return fputc(ch, stream); }
extern "C" int boredos_cpp_fputc_12(int ch, FILE *stream) asm("_Z5fputciP12BOREDOS_FILE");
extern "C" int boredos_cpp_fputc_12(int ch, FILE *stream) { return fputc(ch, stream); }

extern "C" int boredos_cpp_feof(FILE *stream) asm("_Z4feofP13BOREDOS_FILE");
extern "C" int boredos_cpp_feof(FILE *stream) { return feof(stream); }
extern "C" int boredos_cpp_feof_12(FILE *stream) asm("_Z4feofP12BOREDOS_FILE");
extern "C" int boredos_cpp_feof_12(FILE *stream) { return feof(stream); }

extern "C" char *boredos_cpp_fgets_12(char *s, int size, FILE *stream) asm("_Z5fgetsPciP12BOREDOS_FILE");
extern "C" char *boredos_cpp_fgets_12(char *s, int size, FILE *stream) { return fgets(s, size, stream); }

extern "C" int boredos_cpp_rename(const char *oldpath, const char *newpath) asm("_Z6renamePKcS0_");
extern "C" int boredos_cpp_rename(const char *oldpath, const char *newpath) { return rename(oldpath, newpath); }

extern "C" int boredos_cpp_isspace(int c) asm("_Z7isspacei");
extern "C" int boredos_cpp_isspace(int c) { return isspace(c); }

extern "C" int boredos_cpp_isdigit(int c) asm("_Z7isdigiti");
extern "C" int boredos_cpp_isdigit(int c) { return isdigit(c); }

extern "C" int boredos_cpp_isxdigit(int c) asm("_Z8isxdigiti");
extern "C" int boredos_cpp_isxdigit(int c) { return isxdigit(c); }

extern "C" int boredos_cpp_isalpha(int c) asm("_Z7isalphai");
extern "C" int boredos_cpp_isalpha(int c) { return isalpha(c); }

extern "C" int boredos_cpp_iscntrl(int c) asm("_Z7iscntrli");
extern "C" int boredos_cpp_iscntrl(int c) { return iscntrl(c); }

extern "C" int boredos_cpp_isgraph(int c) asm("_Z7isgraphi");
extern "C" int boredos_cpp_isgraph(int c) { return isgraph(c); }

extern "C" struct tm *boredos_cpp_localtime(const time_t *timer) asm("_Z9localtimePKx");
extern "C" struct tm *boredos_cpp_localtime(const time_t *timer) { return localtime(timer); }

extern "C" struct tm *boredos_cpp_gmtime(const time_t *timer) asm("_Z6gmtimePKx");
extern "C" struct tm *boredos_cpp_gmtime(const time_t *timer) { return gmtime(timer); }

extern "C" [[noreturn]] void boredos_cpp__exit(int status) asm("_Z5_exiti");
extern "C" [[noreturn]] void boredos_cpp__exit(int status) { _exit(status); for (;;) {} }

extern "C" struct lconv *boredos_cpp_localeconv(void) asm("_Z10localeconvv");
extern "C" struct lconv *boredos_cpp_localeconv(void) { return localeconv(); }

extern "C" void *boredos_cpp_malloc(size_t size) asm("_Z6mallocm");
extern "C" void *boredos_cpp_malloc(size_t size) { return malloc(size); }

extern "C" void boredos_cpp_free(void *ptr) asm("_Z4freePv");
extern "C" void boredos_cpp_free(void *ptr) { free(ptr); }

extern "C" FILE *boredos_cpp_fopen(const char *path, const char *mode) asm("_Z5fopenPKcS0_");
extern "C" FILE *boredos_cpp_fopen(const char *path, const char *mode) { return fopen(path, mode); }

extern "C" int boredos_cpp_fprintf(FILE *stream, const char *fmt, ...) asm("_Z7fprintfP12BOREDOS_FILEPKcz");
extern "C" int boredos_cpp_fprintf(FILE *stream, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int result = vfprintf(stream, fmt, ap);
    va_end(ap);
    return result;
}

extern "C" int boredos_cpp_fscanf(FILE *stream, const char *fmt, ...) asm("_Z6fscanfP12BOREDOS_FILEPKcz");
extern "C" int boredos_cpp_fscanf(FILE *stream, const char *fmt, ...)
{
    (void)stream;
    (void)fmt;
    return EOF;
}

extern "C" int boredos_cpp_fileno(FILE *stream) asm("_Z6filenoP12BOREDOS_FILE");
extern "C" int boredos_cpp_fileno(FILE *stream) { return stream != nullptr ? stream->fd : -1; }

extern "C" int boredos_cpp_fflush(FILE *stream) asm("_Z6fflushP12BOREDOS_FILE");
extern "C" int boredos_cpp_fflush(FILE *stream) { return fflush(stream); }

extern "C" int boredos_cpp_ferror(FILE *stream) asm("_Z6ferrorP12BOREDOS_FILE");
extern "C" int boredos_cpp_ferror(FILE *stream) { return ferror(stream); }

extern "C" void boredos_cpp_clearerr(FILE *stream) asm("_Z8clearerrP12BOREDOS_FILE");
extern "C" void boredos_cpp_clearerr(FILE *stream) { clearerr(stream); }

extern "C" FILE *boredos_cpp_fdopen(int fd, const char *mode) asm("_Z6fdopeniPKc");
extern "C" FILE *boredos_cpp_fdopen(int fd, const char *mode) { return fdopen(fd, mode); }

extern "C" int boredos_cpp_open(const char *path, int flags, ...) asm("_Z4openPKciz");
extern "C" int boredos_cpp_open(const char *path, int flags, ...) { return open(path, flags); }

extern "C" int boredos_cpp_close(int fd) asm("_Z5closei");
extern "C" int boredos_cpp_close(int fd) { return close(fd); }

extern "C" ssize_t boredos_cpp_read(int fd, void *buf, size_t count) asm("_Z4readiPvm");
extern "C" ssize_t boredos_cpp_read(int fd, void *buf, size_t count) { return read(fd, buf, count); }

extern "C" ssize_t boredos_cpp_write(int fd, const void *buf, size_t count) asm("_Z5writeiPKvm");
extern "C" ssize_t boredos_cpp_write(int fd, const void *buf, size_t count) { return write(fd, buf, count); }

extern "C" int boredos_cpp_dup(int fd) asm("_Z3dupi");
extern "C" int boredos_cpp_dup(int fd) { return dup(fd); }

extern "C" int boredos_cpp_dup2(int oldfd, int newfd) asm("_Z4dup2ii");
extern "C" int boredos_cpp_dup2(int oldfd, int newfd) { return dup2(oldfd, newfd); }

extern "C" int boredos_cpp_pipe(int pipefd[2]) asm("_Z4pipePi");
extern "C" int boredos_cpp_pipe(int pipefd[2]) { return pipe(pipefd); }

extern "C" long boredos_cpp_sysconf(int name) asm("_Z7sysconfi");
extern "C" long boredos_cpp_sysconf(int) { return 4096; }

extern "C" int boredos_cpp_stat(const char *path, struct stat *st) asm("_Z4statPKcP4stat");
extern "C" int boredos_cpp_stat(const char *path, struct stat *st) { return stat(path, st); }

extern "C" int boredos_cpp_fstat(int fd, struct stat *st) asm("_Z5fstatiP4stat");
extern "C" int boredos_cpp_fstat(int fd, struct stat *st) { return fstat(fd, st); }

extern "C" int boredos_cpp_mkdir(const char *path, int mode) asm("_Z5mkdirPKci");
extern "C" int boredos_cpp_mkdir(const char *path, int mode) { return mkdir(path, mode); }

extern "C" char *boredos_cpp_getcwd(char *buf, int size) asm("_Z6getcwdPci");
extern "C" char *boredos_cpp_getcwd(char *buf, int size) { return getcwd(buf, size); }

extern "C" int boredos_cpp_chdir(const char *path) asm("_Z5chdirPKc");
extern "C" int boredos_cpp_chdir(const char *path) { return chdir(path); }

extern "C" int boredos_cpp_sys_exists(const char *path) asm("_Z10sys_existsPKc");
extern "C" int boredos_cpp_sys_exists(const char *path) { return sys_exists(path); }

extern "C" int boredos_cpp_sys_delete(const char *path) asm("_Z10sys_deletePKc");
extern "C" int boredos_cpp_sys_delete(const char *path) { return sys_delete(path); }

extern "C" double boredos_cpp_strtod(const char *nptr, char **endptr) asm("_Z6strtodPKcPPc");
extern "C" double boredos_cpp_strtod(const char *nptr, char **endptr) { return strtod(nptr, endptr); }

extern "C" long long boredos_cpp_strtoll(const char *nptr, char **endptr, int base) asm("_Z7strtollPKcPPci");
extern "C" long long boredos_cpp_strtoll(const char *nptr, char **endptr, int base)
{
    int neg = 0;
    while (*nptr == ' ' || *nptr == '\t' || *nptr == '\n') nptr++;
    if (*nptr == '-') {
        neg = 1;
        nptr++;
    } else if (*nptr == '+') {
        nptr++;
    }
    unsigned long long value = strtoull(nptr, endptr, base);
    return neg ? -(long long)value : (long long)value;
}

extern "C" long long strtoll(const char *nptr, char **endptr, int base)
{
    return boredos_cpp_strtoll(nptr, endptr, base);
}

extern "C" unsigned long long boredos_cpp_strtoull(const char *nptr, char **endptr, int base) asm("_Z8strtoullPKcPPci");
extern "C" unsigned long long boredos_cpp_strtoull(const char *nptr, char **endptr, int base) { return strtoull(nptr, endptr, base); }

extern "C" int boredos_cpp_snprintf(char *str, size_t size, const char *fmt, ...) asm("_Z8snprintfPcmPKcz");
extern "C" int boredos_cpp_snprintf(char *str, size_t size, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int result = vsnprintf(str, size, fmt, ap);
    va_end(ap);
    return result;
}

extern "C" char *boredos_cpp_strerror(int errnum) asm("_Z8strerrori");
extern "C" char *boredos_cpp_strerror(int)
{
    static char msg[] = "BoredOS error";
    return msg;
}

extern "C" void (*boredos_cpp_signal(int signum, void (*handler)(int)))(int) asm("_Z6signaliPFviE");
extern "C" void (*boredos_cpp_signal(int signum, void (*handler)(int)))(int) { return signal(signum, handler); }

extern "C" size_t wcslen(const wchar_t *s)
{
    size_t len = 0;
    if (s != nullptr) while (s[len] != 0) len++;
    return len;
}

extern "C" wchar_t *wmemchr(const wchar_t *s, wchar_t c, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        if (s[i] == c) return const_cast<wchar_t *>(&s[i]);
    }
    return nullptr;
}

extern "C" int wmemcmp(const wchar_t *a, const wchar_t *b, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
    }
    return 0;
}

extern "C" long wcstol(const wchar_t *, wchar_t **endptr, int)
{
    if (endptr != nullptr) *endptr = nullptr;
    return 0;
}

extern "C" long long wcstoll(const wchar_t *, wchar_t **endptr, int)
{
    if (endptr != nullptr) *endptr = nullptr;
    return 0;
}

extern "C" unsigned long wcstoul(const wchar_t *, wchar_t **endptr, int)
{
    if (endptr != nullptr) *endptr = nullptr;
    return 0;
}

extern "C" unsigned long long wcstoull(const wchar_t *, wchar_t **endptr, int)
{
    if (endptr != nullptr) *endptr = nullptr;
    return 0;
}

extern "C" float wcstof(const wchar_t *, wchar_t **endptr)
{
    if (endptr != nullptr) *endptr = nullptr;
    return 0.0f;
}

extern "C" double wcstod(const wchar_t *, wchar_t **endptr)
{
    if (endptr != nullptr) *endptr = nullptr;
    return 0.0;
}

extern "C" long double wcstold(const wchar_t *, wchar_t **endptr)
{
    if (endptr != nullptr) *endptr = nullptr;
    return 0.0;
}

extern "C" int swprintf(wchar_t *s, size_t n, const wchar_t *, ...)
{
    if (s != nullptr && n != 0) s[0] = 0;
    return 0;
}

extern "C" int boredos_cpp_sys_list(const char *path, FAT32_FileInfo *entries, int max_entries) asm("_Z8sys_listPKcP14FAT32_FileInfoi");
extern "C" int boredos_cpp_sys_list(const char *path, FAT32_FileInfo *entries, int max_entries) { return sys_list(path, entries, max_entries); }

extern "C" time_t boredos_cpp_time(time_t *out) asm("_Z4timePx");
extern "C" time_t boredos_cpp_time(time_t *out) { return time(out); }

extern "C" int64_t boredos_cpp_steady_clock_now(void) asm("_ZNSt3__16chrono12steady_clock3nowEv");
extern "C" int64_t boredos_cpp_steady_clock_now(void)
{
    uint64_t ticks = sys_system(SYSTEM_CMD_GET_TICKS, 0, 0, 0, 0);
    return (int64_t)((ticks * 50000000ull) / 3ull);
}

extern "C" int pthread_mutex_lock(void *) { return 0; }
extern "C" int pthread_mutex_unlock(void *) { return 0; }
extern "C" int pthread_rwlock_rdlock(void *) { return 0; }
extern "C" int pthread_rwlock_wrlock(void *) { return 0; }
extern "C" int pthread_rwlock_unlock(void *) { return 0; }
extern "C" int __cxa_thread_atexit_impl(void (*)(void *), void *, void *) { return 0; }
extern "C" void *aligned_alloc(size_t, size_t size) { return malloc(size); }
extern "C" int bcmp(const void *a, const void *b, size_t n) { return memcmp(a, b, n); }
extern "C" int *__errno_location(void) { return &errno; }
extern "C" int strerror_r(int, char *buf, size_t buflen)
{
    if (buflen != 0) buf[0] = '\0';
    return 0;
}
extern "C" void *__dso_handle;
extern "C" void *__cxa_get_globals(void) { return nullptr; }
extern "C" void *__cxa_get_globals_fast(void) { return nullptr; }
extern "C" void __assert_fail(const char *, const char *, unsigned int, const char *) { abort(); }
extern "C" int dladdr(const void *, void *) { return 0; }
extern "C" void *dlsym(void *, const char *) { return nullptr; }
extern "C" int dl_iterate_phdr(int (*)(void *, size_t, void *), void *) { return 0; }

namespace __cxxabiv1 {

class __class_type_info {
public:
    virtual ~__class_type_info();
};

class __si_class_type_info : public __class_type_info {
public:
    virtual ~__si_class_type_info();
};

class __vmi_class_type_info : public __class_type_info {
public:
    virtual ~__vmi_class_type_info();
};

class __function_type_info : public __class_type_info {
public:
    virtual ~__function_type_info();
};

class __pointer_type_info : public __class_type_info {
public:
    virtual ~__pointer_type_info();
};

__class_type_info::~__class_type_info() = default;
__si_class_type_info::~__si_class_type_info() = default;
__vmi_class_type_info::~__vmi_class_type_info() = default;
__function_type_info::~__function_type_info() = default;
__pointer_type_info::~__pointer_type_info() = default;

}

namespace std {

class exception {
public:
    virtual ~exception() noexcept;
    virtual const char *what() const noexcept;
};

class logic_error : public exception {
public:
    explicit logic_error(const char *what_arg);
    virtual ~logic_error() noexcept;
    virtual const char *what() const noexcept override;

protected:
    const char *_what;
};

class length_error : public logic_error {
public:
    explicit length_error(const char *what_arg);
    virtual ~length_error() noexcept;
};

class runtime_error : public exception {
public:
    explicit runtime_error(const char *what_arg);
    runtime_error(const runtime_error &other);
    virtual ~runtime_error() noexcept;
    virtual const char *what() const noexcept override;

protected:
    const char *_what;
};

class out_of_range : public logic_error {
public:
    explicit out_of_range(const char *what_arg);
    virtual ~out_of_range() noexcept;
};

class bad_variant_access : public exception {
public:
    bad_variant_access() noexcept;
    virtual ~bad_variant_access() noexcept;
    virtual const char *what() const noexcept override;
};

class bad_array_new_length : public exception {
public:
    bad_array_new_length() noexcept;
    virtual ~bad_array_new_length() noexcept;
    virtual const char *what() const noexcept override;
};

exception::~exception() noexcept = default;

const char *exception::what() const noexcept
{
    return "exception";
}

logic_error::logic_error(const char *what_arg) : _what(what_arg != nullptr ? what_arg : "logic error")
{
}

logic_error::~logic_error() noexcept = default;

const char *logic_error::what() const noexcept
{
    return this->_what;
}

length_error::length_error(const char *what_arg) : logic_error(what_arg)
{
}

length_error::~length_error() noexcept = default;

runtime_error::runtime_error(const char *what_arg) : _what(what_arg != nullptr ? what_arg : "runtime error")
{
}

runtime_error::runtime_error(const runtime_error &other) : _what(other._what)
{
}

runtime_error::~runtime_error() noexcept = default;

const char *runtime_error::what() const noexcept
{
    return this->_what;
}

out_of_range::out_of_range(const char *what_arg) : logic_error(what_arg)
{
}

out_of_range::~out_of_range() noexcept = default;

bad_variant_access::bad_variant_access() noexcept = default;
bad_variant_access::~bad_variant_access() noexcept = default;

const char *bad_variant_access::what() const noexcept
{
    return "bad variant access";
}

bad_array_new_length::bad_array_new_length() noexcept = default;
bad_array_new_length::~bad_array_new_length() noexcept = default;

const char *bad_array_new_length::what() const noexcept
{
    return "bad array new length";
}

namespace __fs::filesystem {
class path {
public:
    path __filename() const;
};

path path::__filename() const
{
    return path();
}
}

}

namespace std::__1::__fs::filesystem {
class path {
public:
    path __filename() const;
    path __extension() const;
    path __parent_path() const;
};

path path::__filename() const
{
    return path();
}

path path::__extension() const
{
    return path();
}

path path::__parent_path() const
{
    return path();
}
}

namespace std {
struct nothrow_t {
    explicit nothrow_t() = default;
};

extern const nothrow_t nothrow;
const nothrow_t nothrow;
}

namespace std::__1 {

class __shared_count {
protected:
    long __shared_owners_;

public:
    virtual ~__shared_count();
};

class __shared_weak_count : public __shared_count {
protected:
    long __shared_weak_owners_;

public:
    virtual ~__shared_weak_count();
    const void *__get_deleter(const std::type_info &) const noexcept;
    void __release_weak() noexcept;
};

__shared_count::~__shared_count() = default;
__shared_weak_count::~__shared_weak_count() = default;

void __shared_weak_count::__release_weak() noexcept
{
}

const void *__shared_weak_count::__get_deleter(const std::type_info &) const noexcept
{
    return nullptr;
}

class bad_function_call : public std::exception {
public:
    virtual ~bad_function_call() noexcept;
    virtual const char *what() const noexcept override;
};

bad_function_call::~bad_function_call() noexcept = default;

const char *bad_function_call::what() const noexcept
{
    return "bad function call";
}

size_t __hash_memory(const void *data, size_t size)
{
    const unsigned char *bytes = static_cast<const unsigned char *>(data);
    size_t hash = 1469598103934665603ull;
    for (size_t i = 0; i < size; i++) {
        hash ^= bytes[i];
        hash *= 1099511628211ull;
    }
    return hash;
}

size_t __next_prime(size_t value)
{
    if (value < 2) return 2;
    return value | 1u;
}

}
