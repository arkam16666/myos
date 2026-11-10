// arch/cc.h - compiler and platform glue for lwIP on MyOS-ARM
#ifndef MYOS_LWIP_CC_H
#define MYOS_LWIP_CC_H

#include <stdint.h>

typedef int sys_prot_t;

#define PACK_STRUCT_BEGIN
#define PACK_STRUCT_STRUCT __attribute__((__packed__))
#define PACK_STRUCT_END
#define PACK_STRUCT_FIELD(x) x

#define LWIP_NO_CTYPE_H         1           // lwIP brings its own isdigit() etc.
#define LWIP_PLATFORM_DIAG(x)   do { } while (0)
void myos_lwip_assert(void) __attribute__((noreturn));
#define LWIP_PLATFORM_ASSERT(x) myos_lwip_assert()

#endif
