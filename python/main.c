// MicroPython on MyOS
//   python              -> the >>> prompt
//   python game.py a b  -> run game.py from the disk, sys.argv = ["game.py", "a", "b"]
#include <string.h>

#include "py/builtin.h"
#include "py/compile.h"
#include "py/gc.h"
#include "py/mperrno.h"
#include "py/runtime.h"
#include "py/stackctrl.h"
#include "shared/runtime/gchelper.h"
#include "shared/runtime/pyexec.h"
#include "myos.h"

#if defined(__aarch64__)
// MyOS Pi: a program gets one 2 MB block for its code, data and stack, and up
// to 32 MB more at 0x02000000 (arm/user.h USER_HEAP), handed out 2 MB at a time
// when first touched. The Python heap starts as 1 MB inside the program's own
// block, so small programs (htop ...) take just that one 2 MB block; when it's
// full the GC adds bigger areas from the 0x02000000 region (split heap, see
// mpconfigport.h), up to ~33 MB in all. Empty areas go back for reuse.
static char heap[1024 * 1024];
#define PY_HEAP_START heap
#define PY_HEAP_SIZE  sizeof(heap)

#define REGION_START 0x02000000UL
#define REGION_END   0x04000000UL
#define NAREAS       32
static struct { uintptr_t start, size; int used; } areas[NAREAS];
static int nareas;                          // slots in use, in address order
static uintptr_t region_top = REGION_START; // everything above is untouched

void *myos_heap_alloc(size_t size) {
    size = (size + 4095) & ~(size_t)4095;
    for (int i = 0; i < nareas; i++) {      // an area given back earlier
        if (!areas[i].used && areas[i].size >= size) {
            areas[i].used = 1;
            return (void *)areas[i].start;
        }
    }
    if (nareas == NAREAS || size > REGION_END - region_top) return NULL;
    areas[nareas].start = region_top;
    areas[nareas].size = size;
    areas[nareas].used = 1;
    nareas++;
    region_top += size;
    return (void *)(region_top - size);
}

void myos_heap_free(void *p) {
    for (int i = 0; i < nareas; i++) {
        if (areas[i].start == (uintptr_t)p) areas[i].used = 0;
    }
    while (nareas && !areas[nareas - 1].used) {     // the top one(s): back to untouched
        nareas--;
        region_top = areas[nareas].start;
    }
}

// the biggest area the GC could add now
size_t gc_get_max_new_split(void) {
    size_t best = REGION_END - region_top;
    for (int i = 0; i < nareas; i++) {
        if (!areas[i].used && areas[i].size > best) best = areas[i].size;
    }
    return best;
}
#else
static char heap[MICROPY_HEAP_SIZE];        // MyOS PC: no such region
#define PY_HEAP_START heap
#define PY_HEAP_SIZE  sizeof(heap)
#endif

// exit() and quit() as in desktop Python; typing just "exit" says how to leave
static const char startup_code[] =
    "class _Exit:\n"
    "    def __repr__(self):\n"
    "        return 'Use exit() or quit() to leave Python'\n"
    "    def __call__(self, code=0):\n"
    "        import sys\n"
    "        sys.exit(code)\n"
    "exit = quit = _Exit()\n"
    "del _Exit\n";

static void run_startup_code(void) {
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_lexer_t *lex = mp_lexer_new_from_str_len(MP_QSTR__lt_stdin_gt_, startup_code,
                                                    sizeof startup_code - 1, 0);
        qstr source = lex->source_name;
        mp_parse_tree_t tree = mp_parse(lex, MP_PARSE_FILE_INPUT);
        mp_call_function_0(mp_compile(&tree, source, false));
        nlr_pop();
    }
}

// copy the next space-separated word of *args into word[256]; false at the end
static bool next_word(const char **args, char *word) {
    const char *p = *args;
    while (*p == ' ') {
        p++;
    }
    if (*p == 0) {
        return false;
    }
    size_t n = 0;
    while (*p && *p != ' ') {
        if (n < 255) {
            word[n++] = *p;
        }
        p++;
    }
    word[n] = 0;
    *args = p;
    return true;
}

static int run_script(const char *args) {
    char script[256], word[256];
    next_word(&args, script);
    if (sys_file_size(script) < 0) {
        sys_print("python: can't open file '");
        sys_print(script);
        sys_print("'\n");
        return 2;
    }
    #if MICROPY_PY_SYS_ARGV
    mp_obj_list_append(mp_sys_argv, mp_obj_new_str(script, strlen(script)));
    while (next_word(&args, word)) {
        mp_obj_list_append(mp_sys_argv, mp_obj_new_str(word, strlen(word)));
    }
    #endif
    int ret = pyexec_file(script);
    if (ret & PYEXEC_FORCED_EXIT) {
        return 0;                       // sys.exit()
    }
    return ret ? 0 : 1;                 // 0 = an exception was printed
}

int main(const char *args) {
    mp_stack_ctrl_init();
    mp_stack_set_limit(256 * 1024);
    gc_init(PY_HEAP_START, PY_HEAP_START + PY_HEAP_SIZE);
#if defined(__aarch64__)
    // collect after every 1 MB allocated instead of only when the 28 MB are used
    // up: freed blocks get reused from the bottom, so a program only takes (2 MB
    // blocks of) the RAM it really keeps, and several can run side by side
    MP_STATE_MEM(gc_alloc_threshold) = (1024 * 1024) / MICROPY_BYTES_PER_GC_BLOCK;
#endif
    mp_init();
    // import also looks in /lib, where 'pip install' puts libraries
    mp_obj_list_append(mp_sys_path, MP_OBJ_NEW_QSTR(qstr_from_str("/lib")));
    run_startup_code();

    int status = 0;
    if (*args) {
        status = run_script(args);
    } else {
        sys_print("Tips: end an indented block with Backspace then Enter.\n");
        sys_print("      Leave with: exit()\n");
        pyexec_friendly_repl();
    }
    mp_deinit();
    return status;
}

// garbage collector: scan CPU registers and the stack for live objects
void gc_collect(void) {
    gc_collect_start();
    gc_helper_collect_regs_and_stack();
    gc_collect_end();
}

void nlr_jump_fail(void *val) {
    (void)val;
    sys_print("MicroPython: fatal error (uncaught exception)\n");
    sys_exit(1);
}

void MP_NORETURN __fatal_error(const char *msg) {
    sys_print("MicroPython: fatal error: ");
    sys_print(msg);
    sys_print("\n");
    sys_exit(1);
}
