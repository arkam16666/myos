// @version 1.1
// @description Shows a random saying
#include "myos.h"

static const char *const sayings[] = {
    "A journey of a thousand miles begins with a single step.",
    "There are 10 kinds of people: those who know binary and those who don't.",
    "Every expert was once a beginner.",
    "First make it work. Then make it nice.",
    "The best time to plant a tree was 20 years ago. The next best is now.",
    "It works on my machine.",
    "Small steps every day add up to big things.",
    "You wrote an operating system. What can't you do?",
};

int main(const char *args) {
    (void)args;
    unsigned long count = sizeof sayings / sizeof sayings[0];
    sys_print("  ");
    sys_print(sayings[sys_ticks() % count]);
    sys_putc('\n');
    return 0;
}
