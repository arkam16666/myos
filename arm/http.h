// http.h - download files: from the PC over the USB cable (plain HTTP) or
// from the internet over Wi-Fi (HTTPS through tls.c)
#ifndef HTTP_H
#define HTTP_H
#include <stdint.h>

#define REPO_PORT 8080

// Both return the HTTP status (200 = OK) with the body in buf[0..*len),
// or a negative error:
enum { HTTP_ERR_NONET = -1, HTTP_ERR_CONNECT = -2, HTTP_ERR_TIMEOUT = -3,
       HTTP_ERR_TOOBIG = -4, HTTP_ERR_BAD = -5, HTTP_ERR_DNS = -6, HTTP_ERR_TLS = -7 };
int http_get(const char *path, uint8_t *buf, uint32_t max, uint32_t *len);     // http://<the PC>:8080/<path>
int http_get_url(const char *url, uint8_t *buf, uint32_t max, uint32_t *len);  // http:// or https:// URL
const char *http_error(int code);      // a sentence for a negative code
const char *http_host(void);           // "192.168.7.2:8080"
int http_tls_error(void);              // the TLS error behind HTTP_ERR_TLS (tls.h)

#endif
