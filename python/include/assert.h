// minimal assert.h for MyOS: assertions are compiled out (NDEBUG build)
#ifndef MYOS_ASSERT_H
#define MYOS_ASSERT_H
#define assert(x) ((void)0)
#endif
