#ifndef BOREDOS_LIBC_STDIO_H
#define BOREDOS_LIBC_STDIO_H

#include <stdarg.h>
#include <stddef.h>

typedef struct BOREDOS_FILE {
    int fd;
    int eof;
    int err;
    int has_ungetc;
    int ungetc_char;
} FILE;

typedef long fpos_t;

extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

#define EOF (-1)
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#define BUFSIZ 1024
#define FILENAME_MAX 260
#define TMP_MAX 32

FILE *fopen(const char *path, const char *mode);
FILE *fdopen(int fd, const char *mode);
FILE *freopen(const char *path, const char *mode, FILE *stream);
int fclose(FILE *stream);
int fgetc(FILE *stream);
int getchar(void);
size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream);
size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream);
int fseek(FILE *stream, long offset, int whence);
long ftell(FILE *stream);
int getc(FILE *stream);
int putc(int c, FILE *stream);
int ungetc(int c, FILE *stream);
char *fgets(char *s, int n, FILE *stream);
int fputs(const char *s, FILE *stream);
int feof(FILE *stream);
int ferror(FILE *stream);
void clearerr(FILE *stream);
int fflush(FILE *stream);
int fputc(int c, FILE *stream);
int putchar(int c);
int fprintf(FILE *stream, const char *fmt, ...);
int vfprintf(FILE *stream, const char *fmt, va_list ap);
int fscanf(FILE *stream, const char *fmt, ...);
int scanf(const char *fmt, ...);
int vfscanf(FILE *stream, const char *fmt, va_list ap);
int vscanf(const char *fmt, va_list ap);
int vsscanf(const char *str, const char *fmt, va_list ap);
int vprintf(const char *fmt, va_list ap);
int vsprintf(char *str, const char *fmt, va_list ap);
void setbuf(FILE *stream, char *buf);
int setvbuf(FILE *stream, char *buf, int mode, size_t size);
int fgetpos(FILE *stream, fpos_t *pos);
int fsetpos(FILE *stream, const fpos_t *pos);
void rewind(FILE *stream);
void perror(const char *s);
int fileno(FILE *stream);
long filelength(FILE *f);
int vsnprintf(char *str, size_t size, const char *fmt, va_list ap);
int snprintf(char *str, size_t size, const char *fmt, ...);
int sprintf(char *str, const char *fmt, ...);
int sscanf(const char *str, const char *fmt, ...);
int remove(const char *path);
int rename(const char *oldpath, const char *newpath);
FILE *tmpfile(void);
char *tmpnam(char *s);

void puts(const char *s);
void printf(const char *fmt, ...);

#endif
