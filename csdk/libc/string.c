/* string.c - MyOS C library: the string functions not in csdk/libc.c,
 * plus ctype (ASCII). */
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

void *memchr(const void *s, int c, size_t n) {
    const unsigned char *p = s;
    for (; n; n--, p++)
        if (*p == (unsigned char)c) return (void *)p;
    return NULL;
}

size_t strnlen(const char *s, size_t n) {
    size_t i = 0;
    while (i < n && s[i]) i++;
    return i;
}

int strcoll(const char *a, const char *b) { return strcmp(a, b); }

size_t strxfrm(char *dst, const char *src, size_t n) {
    size_t len = strlen(src);
    if (n) {
        size_t c = len < n - 1 ? len : n - 1;
        memcpy(dst, src, c);
        dst[c] = 0;
    }
    return len;
}

char *strncpy(char *dst, const char *src, size_t n) {
    size_t i = 0;
    for (; i < n && src[i]; i++) dst[i] = src[i];
    for (; i < n; i++) dst[i] = 0;
    return dst;
}

char *strncat(char *dst, const char *src, size_t n) {
    char *d = dst + strlen(dst);
    while (n-- && *src) *d++ = *src++;
    *d = 0;
    return dst;
}

char *strchr(const char *s, int c) {
    for (;; s++) {
        if (*s == (char)c) return (char *)s;
        if (!*s) return NULL;
    }
}

char *strrchr(const char *s, int c) {
    const char *last = NULL;
    for (;; s++) {
        if (*s == (char)c) last = s;
        if (!*s) return (char *)last;
    }
}

char *strstr(const char *h, const char *n) {
    size_t len = strlen(n);
    if (!len) return (char *)h;
    for (; *h; h++)
        if (*h == *n && strncmp(h, n, len) == 0) return (char *)h;
    return NULL;
}

size_t strspn(const char *s, const char *accept) {
    size_t i = 0;
    while (s[i] && strchr(accept, s[i])) i++;
    return i;
}

size_t strcspn(const char *s, const char *reject) {
    size_t i = 0;
    while (s[i] && !strchr(reject, s[i])) i++;
    return i;
}

char *strpbrk(const char *s, const char *accept) {
    s += strcspn(s, accept);
    return *s ? (char *)s : NULL;
}

char *strtok(char *s, const char *delim) {
    static char *next;
    if (s) next = s;
    if (!next) return NULL;
    next += strspn(next, delim);
    if (!*next) return next = NULL;
    char *tok = next;
    next += strcspn(next, delim);
    if (*next) *next++ = 0;
    else next = NULL;
    return tok;
}

char *strdup(const char *s) {
    size_t n = strlen(s) + 1;
    char *d = malloc(n);
    if (d) memcpy(d, s, n);
    return d;
}

/* ctype */
int isdigit(int c) { return c >= '0' && c <= '9'; }
int islower(int c) { return c >= 'a' && c <= 'z'; }
int isupper(int c) { return c >= 'A' && c <= 'Z'; }
int isalpha(int c) { return islower(c) || isupper(c); }
int isalnum(int c) { return isalpha(c) || isdigit(c); }
int isxdigit(int c) { return isdigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
int isspace(int c) { return c == ' ' || (c >= '\t' && c <= '\r'); }
int isblank(int c) { return c == ' ' || c == '\t'; }
int iscntrl(int c) { return (c >= 0 && c < 32) || c == 127; }
int isprint(int c) { return c >= 32 && c < 127; }
int isgraph(int c) { return c > 32 && c < 127; }
int ispunct(int c) { return isgraph(c) && !isalnum(c); }
int tolower(int c) { return isupper(c) ? c + 32 : c; }
int toupper(int c) { return islower(c) ? c - 32 : c; }
