// Files for MicroPython on MyOS:
//   open()                read and write files on the MyFS disk
//   import mymodule       loads mymodule.py from the disk
//   os.listdir([path]), os.remove(), and with MyFS v2 folders: os.mkdir(),
//   os.rmdir(), os.rename(), os.stat(), os.chdir(), os.getcwd()
// Names may be paths ("/apps/snake.py", "../x.txt"): the kernel makes them
// absolute from the current folder.
#include <string.h>

#include "py/builtin.h"
#include "py/lexer.h"
#include "py/mperrno.h"
#include "py/objstr.h"
#include "py/runtime.h"
#include "py/stream.h"
#include "myos.h"

#define MYOS_NAME_MAX 255             // a path (MyFS v2: names up to 63 characters)
#define MYOS_ENOTEMPTY 39             // (MicroPython has no MP_ENOTEMPTY)

static const char *check_name(mp_obj_t name_in) {
    size_t len;
    const char *name = mp_obj_str_get_data(name_in, &len);
    if (len == 0 || len > MYOS_NAME_MAX || strlen(name) != len) {
        mp_raise_OSError(MP_EINVAL);
    }
    return name;
}

// a MyFS error (negative MYOS_E* number) -> OSError
static void check(long r) {
    if (r >= 0) {
        return;
    }
    int e = MP_EIO;
    switch (r) {
        case MYOS_ENOENT: e = MP_ENOENT; break;
        case MYOS_EFULL: e = MP_ENOSPC; break;
        case MYOS_ENAME: e = MP_EINVAL; break;
        case MYOS_EEXIST: e = MP_EEXIST; break;
        case MYOS_ENOTDIR: e = MP_ENOTDIR; break;
        case MYOS_EISDIR: e = MP_EISDIR; break;
        case MYOS_ENOTEMPTY: e = MYOS_ENOTEMPTY; break;
        case MYOS_EV1: e = MP_EPERM; break;
    }
    mp_raise_OSError(e);
}

// read a whole file into a new vstr; false if the file doesn't exist
static bool read_file(const char *name, vstr_t *vstr) {
    long size = sys_file_size(name);
    if (size < 0) {
        return false;
    }
    vstr_init_len(vstr, (size_t)size);
    if (sys_read_file(name, vstr->buf, (unsigned long)size) != size) {
        vstr_clear(vstr);
        mp_raise_OSError(MP_EIO);
    }
    return true;
}

// Windows line endings (CR LF) -> LF
static void strip_cr(vstr_t *vstr) {
    size_t j = 0;
    for (size_t i = 0; i < vstr->len; i++) {
        if (vstr->buf[i] != '\r') {
            vstr->buf[j++] = vstr->buf[i];
        }
    }
    vstr->len = j;
}

// ---- import from the disk ----

mp_lexer_t *mp_lexer_new_from_file(qstr filename) {
    vstr_t vstr;
    if (!read_file(qstr_str(filename), &vstr)) {
        mp_raise_OSError(MP_ENOENT);
    }
    return mp_lexer_new_from_str_len(filename, vstr.buf, vstr.len, vstr.alloc);
}

mp_import_stat_t mp_import_stat(const char *path) {
    struct myos_stat st;
    if (strlen(path) <= MYOS_NAME_MAX && sys_stat(path, &st) == 0) {
        return st.type == 2 ? MP_IMPORT_STAT_DIR : MP_IMPORT_STAT_FILE;
    }
    if (strlen(path) <= MYOS_NAME_MAX && sys_file_size(path) >= 0) {   // (an older kernel)
        return MP_IMPORT_STAT_FILE;
    }
    return MP_IMPORT_STAT_NO_EXIST;
}

// ---- files opened for writing: collected in memory, saved on close() ----

typedef struct _myos_file_t {
    mp_obj_base_t base;
    vstr_t data;
    char name[MYOS_NAME_MAX + 1];
    bool closed;
} myos_file_t;

static mp_uint_t file_write(mp_obj_t self_in, const void *buf, mp_uint_t size, int *errcode) {
    myos_file_t *self = MP_OBJ_TO_PTR(self_in);
    if (self->closed) {
        *errcode = MP_EBADF;
        return MP_STREAM_ERROR;
    }
    vstr_add_strn(&self->data, buf, size);
    return size;
}

static mp_uint_t file_ioctl(mp_obj_t self_in, mp_uint_t request, uintptr_t arg, int *errcode) {
    myos_file_t *self = MP_OBJ_TO_PTR(self_in);
    (void)arg;
    switch (request) {
        case MP_STREAM_FLUSH:
            return 0;
        case MP_STREAM_CLOSE:
            if (!self->closed) {
                self->closed = true;
                if (sys_write_file(self->name, self->data.buf, self->data.len) < 0) {
                    *errcode = MP_EIO;
                    return MP_STREAM_ERROR;
                }
            }
            return 0;
        default:
            *errcode = MP_EINVAL;
            return MP_STREAM_ERROR;
    }
}

static const mp_stream_p_t file_stream_p = {
    .write = file_write,
    .ioctl = file_ioctl,
    .is_text = true,
};

static const mp_rom_map_elem_t file_locals_table[] = {
    { MP_ROM_QSTR(MP_QSTR_write), MP_ROM_PTR(&mp_stream_write_obj) },
    { MP_ROM_QSTR(MP_QSTR_flush), MP_ROM_PTR(&mp_stream_flush_obj) },
    { MP_ROM_QSTR(MP_QSTR_close), MP_ROM_PTR(&mp_stream_close_obj) },
    { MP_ROM_QSTR(MP_QSTR___enter__), MP_ROM_PTR(&mp_identity_obj) },
    { MP_ROM_QSTR(MP_QSTR___exit__), MP_ROM_PTR(&mp_stream___exit___obj) },
};
static MP_DEFINE_CONST_DICT(file_locals_dict, file_locals_table);

static MP_DEFINE_CONST_OBJ_TYPE(
    myos_file_type,
    MP_QSTR_FileWriter,
    MP_TYPE_FLAG_NONE,
    protocol, &file_stream_p,
    locals_dict, &file_locals_dict
    );

// open(name, mode="r"): "r"/"rb" read, "w"/"wb" write, "a" append
static mp_obj_t myos_open(size_t n_args, const mp_obj_t *args, mp_map_t *kwargs) {
    const char *name = check_name(args[0]);
    const char *mode = "r";
    if (n_args > 1) {
        mode = mp_obj_str_get_str(args[1]);
    }
    mp_map_elem_t *kw = mp_map_lookup(kwargs, MP_OBJ_NEW_QSTR(MP_QSTR_mode), MP_MAP_LOOKUP);
    if (kw != NULL) {
        mode = mp_obj_str_get_str(kw->value);
    }
    bool binary = strchr(mode, 'b') != NULL;

    if (strchr(mode, 'w') || strchr(mode, 'a')) {
        myos_file_t *f = mp_obj_malloc(myos_file_t, &myos_file_type);
        vstr_init(&f->data, 64);
        strcpy(f->name, name);
        f->closed = false;
        vstr_t old;
        if (strchr(mode, 'a') && read_file(name, &old)) {
            vstr_add_strn(&f->data, old.buf, old.len);
            vstr_clear(&old);
        }
        return MP_OBJ_FROM_PTR(f);
    }

    vstr_t vstr;
    if (!read_file(name, &vstr)) {
        mp_raise_OSError(MP_ENOENT);
    }
    mp_obj_t content;
    if (binary) {
        content = mp_obj_new_bytes_from_vstr(&vstr);
    } else {
        strip_cr(&vstr);
        content = mp_obj_new_str_from_vstr(&vstr);
    }
    // reading gives an in-memory copy: read(), readline(), for line in f, with
    return mp_call_function_1(MP_OBJ_FROM_PTR(binary ? &mp_type_bytesio : &mp_type_stringio), content);
}
MP_DEFINE_CONST_FUN_OBJ_KW(mp_builtin_open_obj, 1, myos_open);

// ---- the os module ----

// os.listdir(path="."): names in a folder
static mp_obj_t os_listdir(size_t n_args, const mp_obj_t *args) {
    const char *path = n_args ? check_name(args[0]) : ".";
    mp_obj_t list = mp_obj_new_list(0, NULL);
    struct myos_dirent d;
    long r = sys_readdir(path, 0, &d);
    if (r == -1 && n_args == 0) {                   // an older kernel without folders
        char name[48];
        for (unsigned long i = 0; sys_list_files(i, name) >= 0; i++) {
            mp_obj_list_append(list, mp_obj_new_str(name, strlen(name)));
        }
        return list;
    }
    for (unsigned long i = 1; r > 0; i++) {
        d.name[63] = 0;
        mp_obj_list_append(list, mp_obj_new_str(d.name, strlen(d.name)));
        r = sys_readdir(path, i, &d);
    }
    check(r);
    return list;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(os_listdir_obj, 0, 1, os_listdir);

static mp_obj_t os_remove(mp_obj_t name_in) {
    const char *name = check_name(name_in);
    struct myos_stat st;
    if (sys_stat(name, &st) == 0 && st.type == 2) {
        mp_raise_OSError(MP_EISDIR);
    }
    if (sys_delete_file(name) < 0) {
        mp_raise_OSError(MP_ENOENT);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(os_remove_obj, os_remove);

static mp_obj_t os_mkdir(mp_obj_t name_in) {
    check(sys_mkdir(check_name(name_in)));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(os_mkdir_obj, os_mkdir);

static mp_obj_t os_rmdir(mp_obj_t name_in) {
    check(sys_rmdir(check_name(name_in)));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(os_rmdir_obj, os_rmdir);

static mp_obj_t os_rename(mp_obj_t from_in, mp_obj_t to_in) {
    check(sys_rename(check_name(from_in), check_name(to_in)));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(os_rename_obj, os_rename);

// os.stat(path) -> (mode, 0, 0, 0, 0, 0, size, 0, 0, 0) like CPython's tuple
static mp_obj_t os_stat(mp_obj_t name_in) {
    struct myos_stat st;
    check(sys_stat(check_name(name_in), &st));
    mp_obj_t t[10];
    for (int i = 0; i < 10; i++) {
        t[i] = MP_OBJ_NEW_SMALL_INT(0);
    }
    t[0] = MP_OBJ_NEW_SMALL_INT(st.type == 2 ? 0x4000 | 0755 : 0x8000 | 0644);
    t[6] = mp_obj_new_int_from_uint(st.size);
    return mp_obj_new_tuple(10, t);
}
static MP_DEFINE_CONST_FUN_OBJ_1(os_stat_obj, os_stat);

static mp_obj_t os_chdir(mp_obj_t name_in) {
    check(sys_chdir(check_name(name_in)));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(os_chdir_obj, os_chdir);

static mp_obj_t os_getcwd(void) {
    char buf[MYOS_NAME_MAX + 1];
    long n = sys_getcwd(buf, sizeof buf);
    if (n < 0) {
        return MP_OBJ_NEW_QSTR(MP_QSTR__slash_);
    }
    return mp_obj_new_str(buf, (size_t)n);
}
static MP_DEFINE_CONST_FUN_OBJ_0(os_getcwd_obj, os_getcwd);

static const mp_rom_map_elem_t os_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_os) },
    { MP_ROM_QSTR(MP_QSTR_listdir), MP_ROM_PTR(&os_listdir_obj) },
    { MP_ROM_QSTR(MP_QSTR_remove), MP_ROM_PTR(&os_remove_obj) },
    { MP_ROM_QSTR(MP_QSTR_mkdir), MP_ROM_PTR(&os_mkdir_obj) },
    { MP_ROM_QSTR(MP_QSTR_rmdir), MP_ROM_PTR(&os_rmdir_obj) },
    { MP_ROM_QSTR(MP_QSTR_rename), MP_ROM_PTR(&os_rename_obj) },
    { MP_ROM_QSTR(MP_QSTR_stat), MP_ROM_PTR(&os_stat_obj) },
    { MP_ROM_QSTR(MP_QSTR_chdir), MP_ROM_PTR(&os_chdir_obj) },
    { MP_ROM_QSTR(MP_QSTR_getcwd), MP_ROM_PTR(&os_getcwd_obj) },
    { MP_ROM_QSTR(MP_QSTR_sep), MP_ROM_QSTR(MP_QSTR__slash_) },
};
static MP_DEFINE_CONST_DICT(os_globals, os_globals_table);

const mp_obj_module_t myos_os_module = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&os_globals,
};
MP_REGISTER_MODULE(MP_QSTR_os, myos_os_module);
