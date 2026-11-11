/* sys/time.h - MyOS C library: gettimeofday (no clock chip: time since
 * MyOS started, counted from 1 Jan 2000 like time()) */
#ifndef MYOS_SYS_TIME_H
#define MYOS_SYS_TIME_H
#include <time.h>

typedef long suseconds_t;
struct timeval { time_t tv_sec; suseconds_t tv_usec; };
struct timezone { int tz_minuteswest, tz_dsttime; };

int gettimeofday(struct timeval *tv, void *tz);

#endif
