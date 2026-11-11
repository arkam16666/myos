// http.h - download a file from the PC over the USB network (plain HTTP)
#ifndef HTTP_H
#define HTTP_H
#include <stdint.h>

#define REPO_PORT 8080

// GET http://<the PC>:8080/<path> into buf. Returns the HTTP status
// (200 = OK) with the body in buf[0..*len), or a negative error:
enum { HTTP_ERR_NONET = -1, HTTP_ERR_CONNECT = -2, HTTP_ERR_TIMEOUT = -3,
       HTTP_ERR_TOOBIG = -4, HTTP_ERR_BAD = -5 };
int http_get(const char *path, uint8_t *buf, uint32_t max, uint32_t *len);
const char *http_error(int code);      // a sentence for a negative code
const char *http_host(void);           // "192.168.7.2:8080"

#endif
