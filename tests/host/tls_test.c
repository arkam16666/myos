/* tls_test.c - try arm/tls.c on the PC: fetch a file over HTTPS with Windows
 * sockets. Build (from the repo root):
 *   clang -O1 -Iarm -Ithird_party/monocypher/src tests/host/tls_test.c arm/tls.c
 *         arm/sha256.c third_party/monocypher/src/monocypher.c -lws2_32 -o build/tls_test.exe
 *   build\tls_test.exe raw.githubusercontent.com /arkam16666/myos/main/README.md
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include "tls.h"

static SOCKET sock;

void rng_bytes(void *out, size_t n) {          /* (arm/rng.c on the Pi) */
    unsigned char *p = out;
    for (size_t i = 0; i < n; i++) p[i] = (unsigned char)(rand() ^ (rand() >> 7));
}

static int io_read(uint8_t *buf, uint32_t n) {
    while (n) {
        int r = recv(sock, (char *)buf, (int)n, 0);
        if (r <= 0) return -1;
        buf += r;
        n -= (uint32_t)r;
    }
    return 0;
}

static int io_write(const uint8_t *buf, uint32_t n) {
    while (n) {
        int r = send(sock, (const char *)buf, (int)n, 0);
        if (r <= 0) return -1;
        buf += r;
        n -= (uint32_t)r;
    }
    return 0;
}

int main(int argc, char **argv) {
    const char *host = argc > 1 ? argv[1] : "raw.githubusercontent.com";
    const char *path = argc > 2 ? argv[2] : "/arkam16666/myos/main/README.md";
    WSADATA w;
    WSAStartup(MAKEWORD(2, 2), &w);
    srand((unsigned)GetTickCount());
    struct addrinfo hints = { 0 }, *ai;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, "443", &hints, &ai)) { printf("dns failed\n"); return 1; }
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (connect(sock, ai->ai_addr, (int)ai->ai_addrlen)) { printf("connect failed\n"); return 1; }
    tls_io_t io = { io_read, io_write };
    int r = tls_connect(&io, host);
    printf("handshake: %d %s\n", r, r ? tls_error(r) : "ok");
    if (r) return 1;
    char req[512];
    snprintf(req, sizeof req, "GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: MyOS-pkg\r\nConnection: close\r\n\r\n",
             path, host);
    tls_write((const uint8_t *)req, (uint32_t)strlen(req));
    static uint8_t buf[4 * 1024 * 1024];
    uint32_t got = 0;
    for (;;) {
        int n = tls_read(buf + got, (uint32_t)(sizeof buf - got));
        if (n < 0) { printf("read error %d %s\n", n, tls_error(n)); break; }
        if (n == 0) break;
        got += (uint32_t)n;
    }
    printf("received %u bytes\n", got);
    fwrite(buf, 1, got < 400 ? got : 400, stdout);
    printf("\n");
    return 0;
}
