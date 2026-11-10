// led.c - the green ACT LED on the board, used as a "heartbeat"
// so you can see MyOS running even without a screen or serial cable.
#include <stdint.h>
#include "led.h"

#ifdef PI4
#define PERIPHERAL_BASE 0xFE000000UL
#define LED_PIN 42                      // Raspberry Pi 4
#else
#define PERIPHERAL_BASE 0x3F000000UL
#define LED_PIN 29                      // Raspberry Pi Zero 2 W
#endif

#define GPIO_BASE (PERIPHERAL_BASE + 0x200000)
#define GPFSEL(n) ((volatile uint32_t *)(GPIO_BASE + 0x00 + 4 * (n)))   // pin function
#define GPSET(n)  ((volatile uint32_t *)(GPIO_BASE + 0x1C + 4 * (n)))   // pin high
#define GPCLR(n)  ((volatile uint32_t *)(GPIO_BASE + 0x28 + 4 * (n)))   // pin low

static int led_state = -1;

void led_init(void) {
    volatile uint32_t *sel = GPFSEL(LED_PIN / 10);
    int shift = (LED_PIN % 10) * 3;
    *sel = (*sel & ~(7u << shift)) | (1u << shift);   // 001 = output
}

static void led_set(int on) {
    if (on == led_state) {
        return;
    }
    led_state = on;
    if (on) {
        *GPSET(LED_PIN / 32) = 1u << (LED_PIN % 32);
    } else {
        *GPCLR(LED_PIN / 32) = 1u << (LED_PIN % 32);
    }
}

// call often: two short blinks, then a pause, every 1.5 seconds
void led_heartbeat(uint64_t ms) {
    uint64_t phase = ms % 1500;
    led_set(phase < 100 || (phase >= 250 && phase < 350));
}

static uint64_t now_ms(void) {
    uint64_t count, freq;
    __asm__ volatile ("isb; mrs %0, cntpct_el0" : "=r"(count));
    __asm__ volatile ("mrs %0, cntfrq_el0" : "=r"(freq));
    return count / (freq / 1000);
}

// something went badly wrong: blink fast forever (5 times a second)
void led_panic(void) {
    for (;;) {
        led_set((now_ms() / 100) % 2);
    }
}
