/* time.c - MyOS C library: time and dates.
 * No clock chip on the Pi: time() = 1 Jan 2000 00:00 UTC + seconds since start. */
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "../myos.h"

#define EPOCH_2000 946684800L

time_t time(time_t *t) {
    time_t now = EPOCH_2000 + (time_t)(sys_ticks() / 100);
    if (t) *t = now;
    return now;
}

clock_t clock(void) {
    return (clock_t)sys_ticks() * (CLOCKS_PER_SEC / 100);
}

double difftime(time_t a, time_t b) {
    return (double)(a - b);
}

static long days_from_civil(long y, int m, int d) {     /* days since 1970-01-01 */
    y -= m <= 2;
    long era = (y >= 0 ? y : y - 399) / 400;
    long yoe = y - era * 400;
    long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

struct tm *gmtime_r(const time_t *t, struct tm *tm) {
    long z = *t / 86400, secs = *t % 86400;
    if (secs < 0) {
        secs += 86400;
        z--;
    }
    tm->tm_hour = (int)(secs / 3600);
    tm->tm_min = (int)(secs / 60 % 60);
    tm->tm_sec = (int)(secs % 60);
    tm->tm_wday = (int)((z + 4) % 7 + 7) % 7;           /* 1970-01-01 was a Thursday */
    z += 719468;
    long era = (z >= 0 ? z : z - 146096) / 146097;
    long doe = z - era * 146097;
    long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    long y = yoe + era * 400;
    long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    long mp = (5 * doy + 2) / 153;
    int d = (int)(doy - (153 * mp + 2) / 5 + 1);
    int m = (int)(mp < 10 ? mp + 3 : mp - 9);
    y += m <= 2;
    tm->tm_year = (int)(y - 1900);
    tm->tm_mon = m - 1;
    tm->tm_mday = d;
    tm->tm_yday = (int)(days_from_civil(y, m, d) - days_from_civil(y, 1, 1));
    tm->tm_isdst = 0;
    return tm;
}

struct tm *gmtime(const time_t *t) {
    static struct tm tm;
    return gmtime_r(t, &tm);
}

struct tm *localtime_r(const time_t *t, struct tm *out) { return gmtime_r(t, out); }
struct tm *localtime(const time_t *t) { return gmtime(t); }

time_t mktime(struct tm *tm) {
    long y = tm->tm_year + 1900L, m = tm->tm_mon;
    y += m / 12;
    m %= 12;
    if (m < 0) {
        m += 12;
        y--;
    }
    time_t t = (time_t)days_from_civil(y, (int)m + 1, 1) * 86400 + (time_t)(tm->tm_mday - 1) * 86400 +
               tm->tm_hour * 3600L + tm->tm_min * 60L + tm->tm_sec;
    gmtime_r(&t, tm);                                   /* normalise the fields */
    return t;
}

static const char *const wday_names[] = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday",
                                          "Friday", "Saturday" };
static const char *const mon_names[] = { "January", "February", "March", "April", "May", "June", "July",
                                         "August", "September", "October", "November", "December" };

size_t strftime(char *s, size_t max, const char *fmt, const struct tm *tm) {
    size_t n = 0;
    char tmp[64];
    for (; *fmt; fmt++) {
        const char *add = tmp;
        tmp[0] = 0;
        if (*fmt != '%') {
            tmp[0] = *fmt;
            tmp[1] = 0;
        } else {
            fmt++;
            if (*fmt == 'E' || *fmt == 'O') fmt++;
            switch (*fmt) {
                case 'a': snprintf(tmp, sizeof tmp, "%.3s", wday_names[tm->tm_wday % 7]); break;
                case 'A': add = wday_names[tm->tm_wday % 7]; break;
                case 'b': case 'h': snprintf(tmp, sizeof tmp, "%.3s", mon_names[tm->tm_mon % 12]); break;
                case 'B': add = mon_names[tm->tm_mon % 12]; break;
                case 'c': snprintf(tmp, sizeof tmp, "%.3s %.3s %2d %02d:%02d:%02d %d", wday_names[tm->tm_wday % 7],
                                   mon_names[tm->tm_mon % 12], tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec,
                                   tm->tm_year + 1900); break;
                case 'd': snprintf(tmp, sizeof tmp, "%02d", tm->tm_mday); break;
                case 'e': snprintf(tmp, sizeof tmp, "%2d", tm->tm_mday); break;
                case 'D': case 'x': snprintf(tmp, sizeof tmp, "%02d/%02d/%02d", tm->tm_mon + 1, tm->tm_mday,
                                             tm->tm_year % 100); break;
                case 'F': snprintf(tmp, sizeof tmp, "%d-%02d-%02d", tm->tm_year + 1900, tm->tm_mon + 1,
                                   tm->tm_mday); break;
                case 'H': snprintf(tmp, sizeof tmp, "%02d", tm->tm_hour); break;
                case 'I': snprintf(tmp, sizeof tmp, "%02d", (tm->tm_hour + 11) % 12 + 1); break;
                case 'j': snprintf(tmp, sizeof tmp, "%03d", tm->tm_yday + 1); break;
                case 'm': snprintf(tmp, sizeof tmp, "%02d", tm->tm_mon + 1); break;
                case 'M': snprintf(tmp, sizeof tmp, "%02d", tm->tm_min); break;
                case 'n': add = "\n"; break;
                case 't': add = "\t"; break;
                case 'p': add = tm->tm_hour < 12 ? "AM" : "PM"; break;
                case 'S': snprintf(tmp, sizeof tmp, "%02d", tm->tm_sec); break;
                case 'T': case 'X': snprintf(tmp, sizeof tmp, "%02d:%02d:%02d", tm->tm_hour, tm->tm_min,
                                             tm->tm_sec); break;
                case 'R': snprintf(tmp, sizeof tmp, "%02d:%02d", tm->tm_hour, tm->tm_min); break;
                case 'u': snprintf(tmp, sizeof tmp, "%d", tm->tm_wday ? tm->tm_wday : 7); break;
                case 'w': snprintf(tmp, sizeof tmp, "%d", tm->tm_wday); break;
                case 'y': snprintf(tmp, sizeof tmp, "%02d", tm->tm_year % 100); break;
                case 'Y': snprintf(tmp, sizeof tmp, "%d", tm->tm_year + 1900); break;
                case 'Z': add = "UTC"; break;
                case 'z': add = "+0000"; break;
                case '%': add = "%"; break;
                default: return 0;
            }
        }
        size_t len = strlen(add);
        if (n + len >= max) return 0;
        memcpy(s + n, add, len);
        n += len;
    }
    if (n >= max) return 0;
    s[n] = 0;
    return n;
}

char *asctime(const struct tm *tm) {
    static char buf[32];
    strftime(buf, sizeof buf, "%c\n", tm);
    return buf;
}

char *ctime(const time_t *t) {
    return asctime(localtime(t));
}
