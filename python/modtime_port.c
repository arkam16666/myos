// MyOS part of the 'time' module (included by extmod/modtime.c)
#include "py/obj.h"
#include "myos.h"

// time.time(): seconds since MyOS booted (there is no calendar clock yet)
static mp_obj_t mp_time_time_get(void) {
    return mp_obj_new_float((mp_float_t)sys_ticks() / 100);
}
