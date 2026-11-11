/* errno.h - MyOS C library */
#ifndef MYOS_ERRNO_H
#define MYOS_ERRNO_H

extern int errno;

#define EPERM   1
#define ENOENT  2
#define EINTR   4
#define EIO     5
#define EBADF   9
#define ENOMEM  12
#define EACCES  13
#define EEXIST  17
#define EINVAL  22
#define ENFILE  23
#define EMFILE  24
#define ENOSPC  28
#define ESPIPE  29
#define EDOM    33
#define ERANGE  34
#define ENOSYS  38
#define ETIMEDOUT 110

#endif
