// modmyos.c - the 'myos' module for Python apps on MyOS Pi:
//   myos.key()        a key if one was pressed, else ''   (doesn't wait)
//   myos.getkey()     wait for a key
//   Arrow keys come back as 'up', 'down', 'left', 'right'.
//   myos.clear()      clear the terminal
//   myos.move(x, y)   put the cursor at column x, row y (0, 0 = top left)
//   myos.cursor(on)   show or hide the cursor
//   myos.ticks()      1/100 seconds since start
//   myos.procs()      running processes (list of dicts), myos.kill(pid)
//   myos.sysinfo()    memory, CPU temperature and clock, disk, network
//   myos.wifi_status(), wifi_scan(), wifi_networks(), wifi_connect(ssid, password),
//   wifi_disconnect(), wifi_forget(ssid), wifi_saved()   Wi-Fi (Pi Zero 2 W)
#include <string.h>
#include "py/runtime.h"
#include "myos.h"

#if defined(__aarch64__)

// turn ESC [ A ... into 'up' etc. 'wait' = block for the first key
static mp_obj_t read_key(int wait) {
    int c = wait ? sys_getkey() : sys_keynow();
    if (c < 0) return MP_OBJ_NEW_QSTR(MP_QSTR_);
    if (c == 0x1b) {                                    // maybe an arrow key
        unsigned long until = sys_ticks() + 5;          // the rest follows within 50 ms
        int c2;
        while ((c2 = sys_keynow()) < 0 && sys_ticks() < until) {
        }
        if (c2 == '[' || c2 == 'O') {
            int c3;
            while ((c3 = sys_keynow()) < 0 && sys_ticks() < until) {
            }
            switch (c3) {
                case 'A': return MP_OBJ_NEW_QSTR(MP_QSTR_up);
                case 'B': return MP_OBJ_NEW_QSTR(MP_QSTR_down);
                case 'C': return MP_OBJ_NEW_QSTR(MP_QSTR_right);
                case 'D': return MP_OBJ_NEW_QSTR(MP_QSTR_left);
            }
        }
        return MP_OBJ_NEW_QSTR(MP_QSTR_esc);
    }
    char s = (char)c;
    return mp_obj_new_str(&s, 1);
}

static mp_obj_t myos_key(void) {
    return read_key(0);
}
static MP_DEFINE_CONST_FUN_OBJ_0(myos_key_obj, myos_key);

static mp_obj_t myos_getkey(void) {
    return read_key(1);
}
static MP_DEFINE_CONST_FUN_OBJ_0(myos_getkey_obj, myos_getkey);

static mp_obj_t myos_clear(void) {
    sys_print("\x1b[2J\x1b[H");
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(myos_clear_obj, myos_clear);

static void print_num(int n) {
    char buf[12];
    int i = sizeof buf - 1;
    buf[i] = 0;
    if (n < 0) n = 0;
    do {
        buf[--i] = (char)('0' + n % 10);
        n /= 10;
    } while (n);
    sys_print(&buf[i]);
}

static mp_obj_t myos_move(mp_obj_t x, mp_obj_t y) {
    sys_print("\x1b[");
    print_num(mp_obj_get_int(y) + 1);
    sys_print(";");
    print_num(mp_obj_get_int(x) + 1);
    sys_print("H");
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(myos_move_obj, myos_move);

static mp_obj_t myos_cursor(mp_obj_t on) {
    sys_print(mp_obj_is_true(on) ? "\x1b[?25h" : "\x1b[?25l");
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(myos_cursor_obj, myos_cursor);

static mp_obj_t myos_ticks(void) {
    return mp_obj_new_int_from_uint(sys_ticks());
}
static MP_DEFINE_CONST_FUN_OBJ_0(myos_ticks_obj, myos_ticks);

// myos.procs(): a list of dicts, one per process
static void put(mp_obj_t d, qstr key, mp_obj_t value) {
    mp_obj_dict_store(d, MP_OBJ_NEW_QSTR(key), value);
}

static mp_obj_t myos_procs(void) {
    mp_obj_t list = mp_obj_new_list(0, NULL);
    struct myos_procinfo p;
    for (long i = 0; sys_procinfo(i, &p) == 0; i++) {
        if (p.state == 0) continue;
        mp_obj_t d = mp_obj_new_dict(9);
        size_t n = 0;
        while (n < sizeof p.name && p.name[n]) n++;
        put(d, MP_QSTR_pid, mp_obj_new_int(p.pid));
        put(d, MP_QSTR_tty, mp_obj_new_int(p.tty));
        put(d, MP_QSTR_running, mp_obj_new_bool(p.state == 1));
        put(d, MP_QSTR_shell, mp_obj_new_bool(p.kind == 1));
        put(d, MP_QSTR_foreground, mp_obj_new_bool(p.foreground));
        put(d, MP_QSTR_mem_kb, mp_obj_new_int_from_uint(p.mem_kb));
        put(d, MP_QSTR_cpu_ticks, mp_obj_new_int_from_ull(p.cpu_ticks));
        put(d, MP_QSTR_start_ticks, mp_obj_new_int_from_ull(p.start_ticks));
        put(d, MP_QSTR_name, mp_obj_new_str(p.name, n));
        mp_obj_list_append(list, d);
    }
    return list;
}
static MP_DEFINE_CONST_FUN_OBJ_0(myos_procs_obj, myos_procs);

// myos.sysinfo(): a dict about the whole system
static mp_obj_t myos_sysinfo(void) {
    struct myos_sysinfo s;
    sys_sysinfo(&s);
    mp_obj_t d = mp_obj_new_dict(12);
    put(d, MP_QSTR_ticks, mp_obj_new_int_from_ull(s.ticks));
    put(d, MP_QSTR_idle_ticks, mp_obj_new_int_from_ull(s.idle_ticks));
    put(d, MP_QSTR_mem_total_kb, mp_obj_new_int_from_uint(s.mem_total_kb));
    put(d, MP_QSTR_mem_free_kb, mp_obj_new_int_from_uint(s.mem_free_kb));
    put(d, MP_QSTR_temp, mp_obj_new_float((mp_float_t)s.temp_millic / 1000));
    put(d, MP_QSTR_mhz, mp_obj_new_int_from_uint(s.arm_hz / 1000000));
    put(d, MP_QSTR_net_in, mp_obj_new_int_from_uint(s.net_in));
    put(d, MP_QSTR_net_out, mp_obj_new_int_from_uint(s.net_out));
    put(d, MP_QSTR_files, mp_obj_new_int_from_uint(s.files));
    put(d, MP_QSTR_disk_free_kb, mp_obj_new_int_from_uint(s.disk_free_kb));
    put(d, MP_QSTR_proc_slots, mp_obj_new_int_from_uint(s.proc_slots));
    put(d, MP_QSTR_terminals, mp_obj_new_int_from_uint(s.terminals_open));
    return d;
}
static MP_DEFINE_CONST_FUN_OBJ_0(myos_sysinfo_obj, myos_sysinfo);

// myos.kill(pid): stop a program; True if it was stopped
static mp_obj_t myos_kill(mp_obj_t pid) {
    return mp_obj_new_bool(sys_kill(mp_obj_get_int(pid)) == 0);
}
static MP_DEFINE_CONST_FUN_OBJ_1(myos_kill_obj, myos_kill);

// Wi-Fi (MyOS Pi on a Pi Zero 2 W)
static mp_obj_t str0(const char *s, size_t max) {      // a C string from a fixed-size field
    size_t n = 0;
    while (n < max && s[n]) n++;
    return mp_obj_new_str(s, n);
}

// myos.wifi_status(): a dict; 'state' -1 no chip, 0 off, 1 starting, 2 on, 3 joining,
// 4 joined (getting an address), 5 online
static mp_obj_t myos_wifi_status(void) {
    struct myos_wifi_status s;
    if (sys_wifi(MYOS_WIFI_STATUS, (long)&s, 0) != 0) s.state = -1;
    mp_obj_t d = mp_obj_new_dict(9);
    put(d, MP_QSTR_state, mp_obj_new_int(s.state));
    if (s.state >= 0) {
        put(d, MP_QSTR_scanning, mp_obj_new_bool(s.scanning));
        put(d, MP_QSTR_rssi, mp_obj_new_int(s.rssi));
        put(d, MP_QSTR_ssid, str0(s.ssid, sizeof s.ssid));
        put(d, MP_QSTR_ip, str0(s.ip, sizeof s.ip));
        put(d, MP_QSTR_gateway, str0(s.gateway, sizeof s.gateway));
        put(d, MP_QSTR_mac, str0(s.mac, sizeof s.mac));
        put(d, MP_QSTR_message, str0(s.message, sizeof s.message));
    }
    return d;
}
static MP_DEFINE_CONST_FUN_OBJ_0(myos_wifi_status_obj, myos_wifi_status);

// myos.wifi_scan(): start a scan; False if there is no Wi-Fi
static mp_obj_t myos_wifi_scan(void) {
    return mp_obj_new_bool(sys_wifi(MYOS_WIFI_SCAN, 0, 0) == 0);
}
static MP_DEFINE_CONST_FUN_OBJ_0(myos_wifi_scan_obj, myos_wifi_scan);

// myos.wifi_networks(): what the last scan found (list of dicts)
static mp_obj_t myos_wifi_networks(void) {
    mp_obj_t list = mp_obj_new_list(0, NULL);
    struct myos_wifi_net n;
    for (long i = 0; sys_wifi(MYOS_WIFI_RESULT, i, (long)&n) == 0; i++) {
        mp_obj_t d = mp_obj_new_dict(5);
        put(d, MP_QSTR_ssid, str0(n.ssid, sizeof n.ssid));
        put(d, MP_QSTR_rssi, mp_obj_new_int(n.rssi));
        put(d, MP_QSTR_channel, mp_obj_new_int(n.channel));
        put(d, MP_QSTR_security, mp_obj_new_int(n.security));
        put(d, MP_QSTR_saved, mp_obj_new_bool(n.saved));
        mp_obj_list_append(list, d);
    }
    return list;
}
static MP_DEFINE_CONST_FUN_OBJ_0(myos_wifi_networks_obj, myos_wifi_networks);

// myos.wifi_connect(ssid, password=''): start joining (watch wifi_status())
static mp_obj_t myos_wifi_connect(size_t n_args, const mp_obj_t *args) {
    struct myos_wifi_connect c;
    memset(&c, 0, sizeof c);
    size_t len;
    const char *s = mp_obj_str_get_data(args[0], &len);
    if (len == 0 || len > 32) return mp_const_false;
    memcpy(c.ssid, s, len);
    if (n_args > 1) {
        s = mp_obj_str_get_data(args[1], &len);
        if (len > 63) return mp_const_false;
        memcpy(c.password, s, len);
    }
    return mp_obj_new_bool(sys_wifi(MYOS_WIFI_CONNECT, (long)&c, 0) == 0);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(myos_wifi_connect_obj, 1, 2, myos_wifi_connect);

static mp_obj_t myos_wifi_disconnect(void) {
    sys_wifi(MYOS_WIFI_DISCONNECT, 0, 0);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(myos_wifi_disconnect_obj, myos_wifi_disconnect);

// myos.wifi_forget(ssid): drop a saved network
static mp_obj_t myos_wifi_forget(mp_obj_t ssid) {
    char buf[33];
    size_t len;
    const char *s = mp_obj_str_get_data(ssid, &len);
    if (len == 0 || len > 32) return mp_const_false;
    memcpy(buf, s, len);
    buf[len] = 0;
    return mp_obj_new_bool(sys_wifi(MYOS_WIFI_FORGET, (long)buf, 0) == 0);
}
static MP_DEFINE_CONST_FUN_OBJ_1(myos_wifi_forget_obj, myos_wifi_forget);

// myos.wifi_saved(): names of the saved networks
static mp_obj_t myos_wifi_saved(void) {
    mp_obj_t list = mp_obj_new_list(0, NULL);
    char name[33];
    for (long i = 0; sys_wifi(MYOS_WIFI_SAVED, i, (long)name) == 0; i++)
        mp_obj_list_append(list, str0(name, sizeof name));
    return list;
}
static MP_DEFINE_CONST_FUN_OBJ_0(myos_wifi_saved_obj, myos_wifi_saved);

// ---- downloads (SYS_FETCH): the certificate isn't checked - check hashes yourself
static void fetch_raise(int err) {
    static const char *const text[] = {
        "", "no network", "can't connect to the server", "the server stopped answering",
        "too big", "the server sent something odd", "can't find the server's address (DNS)",
        "the secure (HTTPS) connection failed", "bad arguments", "disk full or disk error" };
    int i = -err;
    mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT(i > 0 && i < (int)(sizeof text / sizeof text[0])
                                                 ? text[i] : "download failed"));
}

// myos.fetch(url, max=262144) -> (status, bytes)
static mp_obj_t myos_fetch(size_t n_args, const mp_obj_t *args) {
    const char *url = mp_obj_str_get_str(args[0]);
    mp_int_t max = n_args > 1 ? mp_obj_get_int(args[1]) : 256 * 1024;
    if (max < 1024) max = 1024;
    vstr_t v;
    vstr_init_len(&v, (size_t)max);
    struct myos_fetch f;
    memset(&f, 0, sizeof f);
    f.url = (uint64_t)(uintptr_t)url;
    f.buf = (uint64_t)(uintptr_t)v.buf;
    f.max = (uint32_t)max;
    long r = sys_fetch(&f);
    if (r < 0 || f.error) {
        vstr_clear(&v);
        fetch_raise(f.error ? f.error : (int)r);
    }
    v.len = f.len;
    mp_obj_t t[2] = { mp_obj_new_int(f.status), mp_obj_new_bytes_from_vstr(&v) };
    return mp_obj_new_tuple(2, t);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(myos_fetch_obj, 1, 2, myos_fetch);

// myos.download(url, path) -> (status, size); the file is written only when status is 200
static mp_obj_t myos_download(mp_obj_t url_in, mp_obj_t path_in) {
    struct myos_fetch f;
    memset(&f, 0, sizeof f);
    f.url = (uint64_t)(uintptr_t)mp_obj_str_get_str(url_in);
    f.save_as = (uint64_t)(uintptr_t)mp_obj_str_get_str(path_in);
    long r = sys_fetch(&f);
    if (r < 0 || f.error) fetch_raise(f.error ? f.error : (int)r);
    mp_obj_t t[2] = { mp_obj_new_int(f.status), mp_obj_new_int_from_uint(f.len) };
    return mp_obj_new_tuple(2, t);
}
static MP_DEFINE_CONST_FUN_OBJ_2(myos_download_obj, myos_download);

static const mp_rom_map_elem_t myos_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR_fetch), MP_ROM_PTR(&myos_fetch_obj) },
    { MP_ROM_QSTR(MP_QSTR_download), MP_ROM_PTR(&myos_download_obj) },
    { MP_ROM_QSTR(MP_QSTR_wifi_status), MP_ROM_PTR(&myos_wifi_status_obj) },
    { MP_ROM_QSTR(MP_QSTR_wifi_scan), MP_ROM_PTR(&myos_wifi_scan_obj) },
    { MP_ROM_QSTR(MP_QSTR_wifi_networks), MP_ROM_PTR(&myos_wifi_networks_obj) },
    { MP_ROM_QSTR(MP_QSTR_wifi_connect), MP_ROM_PTR(&myos_wifi_connect_obj) },
    { MP_ROM_QSTR(MP_QSTR_wifi_disconnect), MP_ROM_PTR(&myos_wifi_disconnect_obj) },
    { MP_ROM_QSTR(MP_QSTR_wifi_forget), MP_ROM_PTR(&myos_wifi_forget_obj) },
    { MP_ROM_QSTR(MP_QSTR_wifi_saved), MP_ROM_PTR(&myos_wifi_saved_obj) },
    { MP_ROM_QSTR(MP_QSTR_procs), MP_ROM_PTR(&myos_procs_obj) },
    { MP_ROM_QSTR(MP_QSTR_sysinfo), MP_ROM_PTR(&myos_sysinfo_obj) },
    { MP_ROM_QSTR(MP_QSTR_kill), MP_ROM_PTR(&myos_kill_obj) },
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_myos) },
    { MP_ROM_QSTR(MP_QSTR_key), MP_ROM_PTR(&myos_key_obj) },
    { MP_ROM_QSTR(MP_QSTR_getkey), MP_ROM_PTR(&myos_getkey_obj) },
    { MP_ROM_QSTR(MP_QSTR_clear), MP_ROM_PTR(&myos_clear_obj) },
    { MP_ROM_QSTR(MP_QSTR_move), MP_ROM_PTR(&myos_move_obj) },
    { MP_ROM_QSTR(MP_QSTR_cursor), MP_ROM_PTR(&myos_cursor_obj) },
    { MP_ROM_QSTR(MP_QSTR_ticks), MP_ROM_PTR(&myos_ticks_obj) },
};
static MP_DEFINE_CONST_DICT(myos_module_globals, myos_module_globals_table);

const mp_obj_module_t myos_user_module = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&myos_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_myos, myos_user_module);

#endif
