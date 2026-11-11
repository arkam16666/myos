// tls.h - a small TLS 1.3 client (HTTPS downloads)
#ifndef TLS_H
#define TLS_H
#include <stdint.h>

// how TLS reaches the network (http.c gives it a TCP connection)
typedef struct {
    int (*read)(uint8_t *buf, uint32_t n);          // exactly n bytes: 0, or <0 (closed/timeout)
    int (*write)(const uint8_t *buf, uint32_t n);   // 0, or <0
} tls_io_t;

enum { TLS_ERR_IO = -1, TLS_ERR_PROTOCOL = -2, TLS_ERR_ALERT = -3, TLS_ERR_CIPHER = -4 };

int tls_connect(const tls_io_t *io, const char *host);  // the handshake: 0 = ready
int tls_write(const uint8_t *data, uint32_t n);          // 0, or <0
int tls_read(uint8_t *out, uint32_t max);                // bytes; 0 = the server closed; <0 error
const char *tls_error(int code);

#endif
