// Files for MicroPython on MyOS:
//   open()                read and write files on the MyFS disk
//   import mymodule       loads mymodule.py from the disk
//   os.listdir(), os.remove()
#include <string.h>

#include "py/builtin.h"
#include "py/lexer.h"
#include "py/mperrno.h"
#include "py/objstr.h"
#include "py/runtime.h"
#include "py/stream.h"
#include "myos.h"

#define MYOS_NAME_MAX 47

static const char *check_name(mp_obj_t name_in) {
    size_t len;
    const char *name = mp_obj_str_get_data(name_in, &len);
    if (len == 0 || len > MYOS_NAME_MAX || strlen(name) != len) {
        mp_raise_OSError(MP_EINVAL);
    }
    return name;
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
    if (strlen(path) <= MYOS_NAME_MAX && sys_file_size(path) >= 0) {
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

static mp_obj_t os_listdir(void) {
    mp_obj_t list = mp_obj_new_list(0, NULL);
    char name[MYOS_NAME_MAX + 1];
    for (unsigned long i = 0; sys_list_files(i, name) >= 0; i++) {
        mp_obj_list_append(list, mp_obj_new_str(name, strlen(name)));
    }
    return list;
}
static MP_DEFINE_CONST_FUN_OBJ_0(os_listdir_obj, os_listdir);

static mp_obj_t os_remove(mp_obj_t name_in) {
    if (sys_delete_file(check_name(name_in)) < 0) {
        mp_raise_OSError(MP_ENOENT);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(os_remove_obj, os_remove);

static const mp_rom_map_elem_t os_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_os) },
    { MP_ROM_QSTR(MP_QSTR_listdir), MP_ROM_PTR(&os_listdir_obj) },
    { MP_ROM_QSTR(MP_QSTR_remove), MP_ROM_PTR(&os_remove_obj) },
};
static MP_DEFINE_CONST_DICT(os_globals, os_globals_table);

const mp_obj_module_t myos_os_module = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&os_globals,
};
MP_REGISTER_MODULE(MP_QSTR_os, myos_os_module);
