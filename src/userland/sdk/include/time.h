#ifndef BOREDOS_LIBC_TIME_H
#define BOREDOS_LIBC_TIME_H

#include <stddef.h>

typedef long long time_t;
typedef unsigned long long clock_t;

#define CLOCKS_PER_SEC 3000000000ULL

struct tm {
    int tm_sec;
    int tm_min;
    int tm_hour;
    int tm_mday;
    int tm_mon;
    int tm_year;
    int tm_wday;
    int tm_yday;
    int tm_isdst;
};

time_t time(time_t *out);
clock_t clock(void);
struct tm *localtime(const time_t *timer);
struct tm *gmtime(const time_t *timer);
size_t strftime(char *s, size_t max, const char *fmt, const struct tm *tm);
time_t mktime(struct tm *tm);

#endif
