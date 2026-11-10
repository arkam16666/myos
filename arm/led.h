// led.h - the board's green ACT LED
#ifndef LED_H
#define LED_H
#include <stdint.h>

void led_init(void);
void led_heartbeat(uint64_t ms);    // call often with the time in milliseconds
void led_panic(void) __attribute__((noreturn));   // fast blink forever

#endif
