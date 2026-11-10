/* libc.c - the few C library functions the compiler relies on.
 * Plain C with no system calls: used by x86 programs and by MyOS-ARM. */
#include <stddef.h>

size_t strlen(const char *s);

void *memcpy(void *dst, const void *src, size_t n) {
    unsigned char *d = dst;
    const unsigned char *s = src;
    while (n--) *d++ = *s++;
    return dst;
}

void *memmove(void *dst, const void *src, size_t n) {
    unsigned char *d = dst;
    const unsigned char *s = src;
    if (d < s) {
        while (n--) *d++ = *s++;
    } else {
        d += n;
        s += n;
        while (n--) *--d = *--s;
    }
    return dst;
}

void *memset(void *dst, int c, size_t n) {
    unsigned char *d = dst;
    while (n--) *d++ = (unsigned char)c;
    return dst;
}

int memcmp(const void *a, const void *b, size_t n) {
    const unsigned char *x = a, *y = b;
    for (; n; n--, x++, y++)
        if (*x != *y) return *x - *y;
    return 0;
}

int strcmp(const char *a, const char *b) {
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t n) {
    for (; n; n--, a++, b++) {
        if (*a != *b || *a == 0) return (unsigned char)*a - (unsigned char)*b;
    }
    return 0;
}

int atoi(const char *s) {
    int sign = 1, n = 0;
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '-' || *s == '+') sign = (*s++ == '-') ? -1 : 1;
    while (*s >= '0' && *s <= '9') n = n * 10 + (*s++ - '0');
    return sign * n;
}

char *strcpy(char *dst, const char *src) {
    char *d = dst;
    while ((*d++ = *src++)) {
    }
    return dst;
}

char *strcat(char *dst, const char *src) {
    strcpy(dst + strlen(dst), src);
    return dst;
}

size_t strlen(const char *s) {
    const char *p = s;
    while (*p) p++;
    return (size_t)(p - s);
}
