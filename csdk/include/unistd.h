/* unistd.h - MyOS C library: the folder calls only (MyFS v2, MyOS Pi) */
#ifndef MYOS_UNISTD_H
#define MYOS_UNISTD_H
#include <stddef.h>

int chdir(const char *path);
char *getcwd(char *buf, size_t size);
int rmdir(const char *path);
int unlink(const char *path);

#endif
