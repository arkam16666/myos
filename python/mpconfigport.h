// MicroPython settings for MyOS
#include <stdint.h>

#define MICROPY_CONFIG_ROM_LEVEL          (MICROPY_CONFIG_ROM_LEVEL_EXTRA_FEATURES)

// extra modules (sources in extmod/)
#define MICROPY_PY_RANDOM                 (1)
#define MICROPY_PY_RANDOM_EXTRA_FUNCS     (1)   // randint, choice, ...
#define MICROPY_PY_RANDOM_SEED_INIT_FUNC  ({ uint32_t lo, hi; __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi)); lo ^ hi; })
#define MICROPY_PY_JSON                   (1)
#define MICROPY_PY_TIME                   (1)
#define MICROPY_PY_TIME_GMTIME_LOCALTIME_MKTIME (0)
#define MICROPY_PY_TIME_TIME_TIME_NS      (1)
#define MICROPY_PY_TIME_INCLUDEFILE       "modtime_port.c"
#define MICROPY_PY_SYS_STDFILES           (0)   // no sys.stdin/stdout streams yet
#define MICROPY_PY_UCTYPES                (0)   // raw memory access: not for ring 3
#define MICROPY_PY_OS                     (0)   // MyOS has its own small os module (file.c)
#define MICROPY_PY_SYS_ARGV               (1)   // python game.py a b -> sys.argv

#define MICROPY_ENABLE_COMPILER           (1)
#define MICROPY_ENABLE_GC                 (1)
#define MICROPY_HELPER_REPL               (1)
#define MICROPY_REPL_AUTO_INDENT          (1)
#define MICROPY_STACK_CHECK               (1)
#define MICROPY_ENABLE_EXTERNAL_IMPORT    (1)
#define MICROPY_ALLOC_PATH_MAX            (256)
#define MICROPY_USE_INTERNAL_ERRNO        (1)   // no libc errno.h values

#define MICROPY_LONGINT_IMPL              (MICROPY_LONGINT_IMPL_MPZ)   // 2**100 works
#define MICROPY_FLOAT_IMPL                (MICROPY_FLOAT_IMPL_DOUBLE)  // 7 / 2 = 3.5
#define MICROPY_PY_MATH                   (1)                          // import math
#define MICROPY_FLOAT_USE_NATIVE_FLT16    (0)   // no compiler runtime for half floats

#define MICROPY_PY_SYS_EXIT               (1)                          // import sys; sys.exit()

#define MICROPY_HEAP_SIZE                 (512 * 1024)

#define MICROPY_HW_BOARD_NAME             "MyOS"
#define MICROPY_HW_MCU_NAME               "x86-64"

typedef long mp_off_t;

#include <limits.h>
#ifndef SSIZE_MAX
#define SSIZE_MAX LONG_MAX                      // POSIX value clang's limits.h lacks
#endif

#include <alloca.h>

#define MP_STATE_PORT MP_STATE_VM
