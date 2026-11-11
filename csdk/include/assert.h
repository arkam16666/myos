/* assert.h - MyOS C library */
#ifndef MYOS_ASSERT_H
#define MYOS_ASSERT_H

void __myos_assert_fail(const char *expr, const char *file, int line) __attribute__((noreturn));

#ifdef NDEBUG
#define assert(x) ((void)0)
#else
#define assert(x) ((x) ? (void)0 : __myos_assert_fail(#x, __FILE__, __LINE__))
#endif

#endif
