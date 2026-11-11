/* myos_info.h - what the system-information calls hand to programs
 * (shared by the kernel, arm/user.c, and programs through myos.h). */
#ifndef MYOS_INFO_H
#define MYOS_INFO_H
#include <stdint.h>

/* SYS_PROCINFO: one process (index 0.. until the call returns -1) */
struct myos_procinfo {
    int32_t pid;
    int32_t tty;            /* 0 serial, 1 telnet, 2.. ssh sessions */
    int32_t state;          /* 1 running, 2 finished (not collected yet), 0 = empty slot */
    int32_t kind;           /* 0 program, 1 shell (kernel thread) */
    int32_t foreground;     /* 1 if it gets its terminal's keys */
    uint32_t mem_kb;        /* RAM it has (2 MB blocks) */
    uint64_t cpu_ticks;     /* 1/100 s it has run */
    uint64_t start_ticks;   /* when it started (1/100 s since boot) */
    char name[16];
};

/* SYS_SYSINFO */
struct myos_sysinfo {
    uint64_t ticks;         /* 1/100 s since boot */
    uint64_t idle_ticks;    /* ticks when only waiting shells ran */
    uint32_t mem_total_kb;  /* the RAM pool for programs */
    uint32_t mem_free_kb;
    int32_t temp_millic;    /* CPU temperature in 1/1000 C (0 = unknown) */
    uint32_t arm_hz;        /* CPU clock (0 = unknown) */
    uint32_t net_in, net_out;   /* USB network packets */
    uint32_t files;         /* files on MyFS */
    uint32_t disk_free_kb;
    uint32_t proc_slots;    /* most processes at once */
    uint32_t terminals_open;
};

/* SYS_WIFI (MyOS Pi): sys_wifi(op, a, b) with op = one of these */
enum {
    MYOS_WIFI_STATUS,       /* (struct myos_wifi_status *) -> 0 */
    MYOS_WIFI_SCAN,         /* start a scan (results come in while it runs) -> 0, or -1 */
    MYOS_WIFI_RESULT,       /* (index, struct myos_wifi_net *) -> 0, or -1 past the end */
    MYOS_WIFI_CONNECT,      /* (struct myos_wifi_connect *) start joining -> 0, or -1 */
    MYOS_WIFI_DISCONNECT,
    MYOS_WIFI_FORGET,       /* (ssid) drop it from the saved networks -> 0, or -1 */
    MYOS_WIFI_SAVED         /* (index, char ssid[33]) a saved network -> 0, or -1 past the end */
};

/* state: -1 no Wi-Fi chip, 0 off, 1 starting, 2 on (not connected), 3 joining,
 * 4 joined (asking for an address), 5 online (has an IP address) */
struct myos_wifi_status {
    int32_t state;
    int32_t scanning;       /* 1 while a scan runs */
    int32_t rssi;           /* dBm, while connected */
    int32_t channel;
    char ssid[33];
    char ip[16];
    char gateway[16];
    char mac[18];
    char message[64];       /* the last thing that happened, for people */
};

/* security: 0 open, 1 WEP, 2 WPA, 3 WPA2, 4 WPA3 only (not supported) */
struct myos_wifi_net {
    int32_t rssi;
    int32_t channel;
    int32_t security;
    int32_t saved;          /* 1 if it is a saved network */
    char ssid[33];
    uint8_t bssid[6];
};

struct myos_wifi_connect {
    char ssid[33];
    char password[64];      /* "" for an open network; 8-63 characters for WPA/WPA2 */
};

/* SYS_FETCH (MyOS Pi): download a URL (http:// to the PC, https:// over Wi-Fi).
 * The server's certificate is NOT checked (see arm/tls.c): check what you get
 * (hashes, signatures) before trusting it. */
struct myos_fetch {
    uint64_t url;           /* in: const char *, up to 299 characters */
    uint64_t buf;           /* in: where the body goes (unused with save_as) */
    uint32_t max;           /* in: size of buf */
    uint32_t len;           /* out: bytes of body */
    int32_t status;         /* out: HTTP status (200 = OK) */
    int32_t error;          /* out: 0, or a MYOS_FETCH_E* below */
    uint64_t save_as;       /* in: const char * path, or 0: write the body to this file (max 8 MB) */
};
enum {
    MYOS_FETCH_ENONET = -1, MYOS_FETCH_ECONNECT = -2, MYOS_FETCH_ETIMEOUT = -3,
    MYOS_FETCH_ETOOBIG = -4, MYOS_FETCH_EBAD = -5, MYOS_FETCH_EDNS = -6, MYOS_FETCH_ETLS = -7,
    MYOS_FETCH_EARGS = -8, MYOS_FETCH_EDISK = -9
};

/* SYS_STAT (MyFS v2 folders) */
struct myos_stat {
    uint32_t type;          /* 1 file, 2 folder */
    uint32_t size;          /* bytes */
};

/* SYS_READDIR: one thing in a folder */
struct myos_dirent {
    char name[64];          /* up to 63 characters */
    uint32_t type;          /* 1 file, 2 folder */
    uint32_t size;
};

#endif
