/* stdio.h - MyOS C library: files live on MyFS (read whole / written on close) */
#ifndef MYOS_STDIO_H
#define MYOS_STDIO_H
#include <stdarg.h>
#include <stddef.h>

typedef struct myos_file FILE;
typedef long fpos_t;

extern FILE *stdin, *stdout, *stderr;

#define EOF         (-1)
#define BUFSIZ      1024
#define FILENAME_MAX 256
#define FOPEN_MAX   16
#define L_tmpnam    20
#define TMP_MAX     1000
#define SEEK_SET    0
#define SEEK_CUR    1
#define SEEK_END    2
#define _IOFBF      0
#define _IOLBF      1
#define _IONBF      2

FILE *fopen(const char *name, const char *mode);
FILE *freopen(const char *name, const char *mode, FILE *f);
int fclose(FILE *f);
int fflush(FILE *f);
size_t fread(void *p, size_t size, size_t n, FILE *f);
size_t fwrite(const void *p, size_t size, size_t n, FILE *f);
int fgetc(FILE *f);
int getc(FILE *f);
int getchar(void);
int ungetc(int c, FILE *f);
char *fgets(char *s, int n, FILE *f);
int fputc(int c, FILE *f);
int putc(int c, FILE *f);
int putchar(int c);
int fputs(const char *s, FILE *f);
int puts(const char *s);
int fseek(FILE *f, long off, int whence);
long ftell(FILE *f);
void rewind(FILE *f);
int feof(FILE *f);
int ferror(FILE *f);
void clearerr(FILE *f);
int setvbuf(FILE *f, char *buf, int mode, size_t size);
FILE *tmpfile(void);
char *tmpnam(char *s);
int remove(const char *name);
int rename(const char *from, const char *to);
void perror(const char *s);

int printf(const char *fmt, ...);
int fprintf(FILE *f, const char *fmt, ...);
int sprintf(char *s, const char *fmt, ...);
int snprintf(char *s, size_t n, const char *fmt, ...);
int vprintf(const char *fmt, va_list ap);
int vfprintf(FILE *f, const char *fmt, va_list ap);
int vsprintf(char *s, const char *fmt, va_list ap);
int vsnprintf(char *s, size_t n, const char *fmt, va_list ap);
int sscanf(const char *s, const char *fmt, ...);

#endif
