// Keyboard in / screen out for MicroPython, using MyOS syscalls
#include "py/mpconfig.h"
#include "py/mphal.h"
#include "myos.h"

// time: the MyOS timer ticks 100 times a second
void mp_hal_delay_ms(mp_uint_t ms) {
    sys_sleep((ms + 9) / 10);
}

void mp_hal_delay_us(mp_uint_t us) {
    sys_sleep((us + 9999) / 10000);
}

mp_uint_t mp_hal_ticks_us(void) {
    return (mp_uint_t)sys_ticks() * 10000;
}

mp_uint_t mp_hal_ticks_cpu(void) {
    uint32_t lo, hi;
    __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    return ((mp_uint_t)hi << 32) | lo;
}

uint64_t mp_hal_time_ns(void) {
    return (uint64_t)sys_ticks() * 10000000ULL;   // since boot (no calendar yet)
}

int mp_hal_stdin_rx_chr(void) {
    int c = sys_getkey();
    if (c == '\n') {
        c = '\r';                   // the REPL expects Enter as CR
    }
    return c;
}

// The REPL sends VT100 terminal codes (ESC [ ... letter) and CR LF line
// ends. The MyOS screen understands neither, so filter them out here.
static int esc_state;               // 0 = normal, 1 = after ESC, 2 = inside ESC [

mp_uint_t mp_hal_stdout_tx_strn(const char *str, size_t len) {
    char buf[128];
    size_t n = 0;
    for (size_t i = 0; i < len; i++) {
        char c = str[i];
        if (esc_state == 1) {
            esc_state = (c == '[') ? 2 : 0;
            continue;
        }
        if (esc_state == 2) {
            if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) {
                esc_state = 0;
            }
            continue;
        }
        if (c == 0x1b) {
            esc_state = 1;
            continue;
        }
        if (c == '\r' || c == 0) {
            continue;
        }
        buf[n++] = c;
        if (n == sizeof(buf) - 1) {
            buf[n] = 0;
            sys_print(buf);
            n = 0;
        }
    }
    if (n) {
        buf[n] = 0;
        sys_print(buf);
    }
    return len;
}
