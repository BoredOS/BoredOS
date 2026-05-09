#include <stdint.h>

#include "time.h"
#include "stdio.h"
#include "syscall.h"
#include "sys/time.h"

static int _b_is_leap(int year) {
    return ((year % 4) == 0 && (year % 100) != 0) || ((year % 400) == 0);
}

static int _b_days_in_month(int year, int month) {
    static const int mdays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month == 1 && _b_is_leap(year)) {
        return 29;
    }
    return mdays[month];
}

static long long _b_days_before_year(int year) {
    long long y = (long long)year - 1;
    return y * 365 + y / 4 - y / 100 + y / 400;
}

static long long _b_days_since_epoch(int year, int month, int day) {
    long long days = _b_days_before_year(year) - _b_days_before_year(1970);
    int m;
    for (m = 0; m < month - 1; m++) {
        days += _b_days_in_month(year, m);
    }
    days += (day - 1);
    return days;
}

static void _b_civil_from_days(long long z, int *year, int *month, int *day) {
    z += 719468;
    long long era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned doe = (unsigned)(z - era * 146097);
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int y = (int)yoe + (int)era * 400;
    unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    unsigned mp = (5 * doy + 2) / 153;
    unsigned d = doy - (153 * mp + 2) / 5 + 1;
    unsigned m = mp + (mp < 10 ? 3 : (unsigned)-9);
    y += (m <= 2);
    *year = y;
    *month = (int)m;
    *day = (int)d;
}

static time_t _b_seconds_from_ymdhms(int year, int month, int day, int hour, int minute, int second) {
    long long days = _b_days_since_epoch(year, month, day);
    return (time_t)(days * 86400LL + hour * 3600LL + minute * 60LL + second);
}

static void _b_fill_tm_from_epoch(time_t t, struct tm *out) {
    long long sec = (long long)t;
    long long days;
    int sod;
    int year;
    int month;
    int day;

    if (sec < 0) {
        long long d = ((-sec) + 86399LL) / 86400LL;
        sec += d * 86400LL;
    }

    days = sec / 86400LL;
    sod = (int)(sec % 86400LL);
    if (sod < 0) {
        sod += 86400;
        days--;
    }

    _b_civil_from_days(days, &year, &month, &day);

    out->tm_year = year - 1900;
    out->tm_mon = month - 1;
    out->tm_mday = day;
    out->tm_hour = sod / 3600;
    out->tm_min = (sod % 3600) / 60;
    out->tm_sec = sod % 60;
    out->tm_wday = (int)((days + 4) % 7);
    if (out->tm_wday < 0) out->tm_wday += 7;

    {
        long long jan1 = _b_days_since_epoch(year, 1, 1);
        out->tm_yday = (int)(days - jan1);
    }
    out->tm_isdst = 0;
}

__attribute__((weak)) time_t time(time_t *out) {
    int dt[6] = {1970, 1, 1, 0, 0, 0};
    time_t t;
    if (sys_system(SYSTEM_CMD_RTC_GET, 0, (uint64_t)dt, 0, 0) != 0) {
        t = 0;
    } else {
        t = _b_seconds_from_ymdhms(dt[0], dt[1], dt[2], dt[3], dt[4], dt[5]);
    }
    if (out) {
        *out = t;
    }
    return t;
}

__attribute__((weak)) clock_t clock(void) {
    static uint64_t start_tsc = 0;
    unsigned int lo;
    unsigned int hi;
    uint64_t now_tsc;

    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    now_tsc = ((uint64_t)hi << 32) | (uint64_t)lo;

    if (start_tsc == 0) {
        start_tsc = now_tsc;
        __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
        now_tsc = ((uint64_t)hi << 32) | (uint64_t)lo;
    }

    return (clock_t)(now_tsc - start_tsc);
}

__attribute__((weak)) struct tm *gmtime(const time_t *timer) {
    static struct tm tmv;
    if (!timer) {
        return NULL;
    }
    _b_fill_tm_from_epoch(*timer, &tmv);
    return &tmv;
}

__attribute__((weak)) struct tm *localtime(const time_t *timer) {
    return gmtime(timer);
}

__attribute__((weak)) size_t strftime(char *s, size_t max, const char *fmt, const struct tm *tm) {
    (void)fmt;
    if (!s || max == 0 || !tm) {
        return 0;
    }
    {
        int n = snprintf(s, max, "%04d-%02d-%02d %02d:%02d:%02d",
            tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
            tm->tm_hour, tm->tm_min, tm->tm_sec);
        if (n < 0 || (size_t)n >= max) {
            if (max > 0) s[0] = '\0';
            return 0;
        }
        return (size_t)n;
    }
}

__attribute__((weak)) time_t mktime(struct tm *tm) {
    if (!tm) {
        return (time_t)-1;
    }
    return _b_seconds_from_ymdhms(
        tm->tm_year + 1900,
        tm->tm_mon + 1,
        tm->tm_mday,
        tm->tm_hour,
        tm->tm_min,
        tm->tm_sec);
}

int gettimeofday(struct timeval *tv, void *tz) {
    (void)tz;
    if (tv) {
        time_t t = time(NULL);
        tv->tv_sec = (long)t;
        tv->tv_usec = 0;
    }
    return 0;
}
