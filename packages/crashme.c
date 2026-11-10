// @version 1.1
// @description Breaks the rules to show the OS survives
//
// Tries to write into memory that belongs to the kernel. The CPU stops
// it, the kernel ends just this program, and the shell keeps running.
#include "myos.h"

int main(const char *args) {
    (void)args;
    sys_print("crashme: writing into kernel memory at 0x1000...\n"
              "Only the kernel may touch that, so the CPU should stop me.\n");
    sys_sleep(100);                     // 1 second, for suspense
    *(volatile unsigned long *)0x1000 = 0xDEAD;
    sys_print("Uh oh, nothing stopped me!\n");
    return 1;
}
