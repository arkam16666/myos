// MyOS part of the 'time' module (included by extmod/modtime.c)
#include "py/obj.h"
#include "myos.h"

// time.time(): seconds since MyOS booted (there is no calendar clock yet)
static mp_obj_t mp_time_time_get(void) {
    return mp_obj_new_float((mp_float_t)sys_ticks() / 100);
}

#include "shared/timeutils/timeutils.h"

// time.localtime() / gmtime(): no clock yet either, so 1 Jan 1970 + the time since boot
static void mp_time_localtime_get(timeutils_struct_time_t *tm) {
    timeutils_seconds_since_epoch_to_struct_time(sys_ticks() / 100, tm);
}
