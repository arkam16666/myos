/* locale.h - MyOS C library: only the "C" locale */
#ifndef MYOS_LOCALE_H
#define MYOS_LOCALE_H

#define LC_ALL      0
#define LC_COLLATE  1
#define LC_CTYPE    2
#define LC_MONETARY 3
#define LC_NUMERIC  4
#define LC_TIME     5

struct lconv {
    char *decimal_point, *thousands_sep, *grouping;
};

char *setlocale(int category, const char *locale);
struct lconv *localeconv(void);

#endif
