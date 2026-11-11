/* time.h - MyOS C library. The Pi has no clock chip: time() counts
 * seconds since MyOS started (from 1 Jan 2000 00:00, so dates look sane). */
#ifndef MYOS_TIME_H
#define MYOS_TIME_H
#include <stddef.h>

typedef long time_t;
typedef long clock_t;
#define CLOCKS_PER_SEC 1000000L

struct tm {
    int tm_sec, tm_min, tm_hour, tm_mday, tm_mon, tm_year, tm_wday, tm_yday, tm_isdst;
};

time_t time(time_t *t);
clock_t clock(void);
double difftime(time_t a, time_t b);
time_t mktime(struct tm *tm);
struct tm *gmtime(const time_t *t);
struct tm *localtime(const time_t *t);
struct tm *gmtime_r(const time_t *t, struct tm *out);
struct tm *localtime_r(const time_t *t, struct tm *out);
size_t strftime(char *s, size_t max, const char *fmt, const struct tm *tm);
char *asctime(const struct tm *tm);
char *ctime(const time_t *t);

#endif
