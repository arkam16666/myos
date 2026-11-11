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

static const mp_rom_map_elem_t myos_module_globals_table[] = {
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
