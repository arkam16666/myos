// MicroPython hardware layer for MyOS: everything goes through syscalls
#include "myos.h"

static inline mp_uint_t mp_hal_ticks_ms(void) {
    return (mp_uint_t)sys_ticks() * 10;     // timer runs at 100 Hz
}

static inline void mp_hal_set_interrupt_char(char c) {
    (void)c;
}
