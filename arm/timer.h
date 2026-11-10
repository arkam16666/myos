// timer.h - the ARM generic timer (counts up from power-on)
#ifndef TIMER_H
#define TIMER_H
#include <stdint.h>

static inline uint64_t timer_count(void) {
    uint64_t v;
    __asm__ volatile ("isb; mrs %0, cntpct_el0" : "=r"(v));
    return v;
}

static inline uint64_t timer_freq(void) {
    uint64_t v;
    __asm__ volatile ("mrs %0, cntfrq_el0" : "=r"(v));
    return v;
}

static inline uint64_t millis(void) {
    return timer_count() / (timer_freq() / 1000);
}

// 100 per second, counted by the system timer interrupt (kernel.c)
extern volatile uint64_t ticks;
#define TICKS_PER_SECOND 100

#endif
