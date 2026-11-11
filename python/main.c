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

static char heap[MICROPY_HEAP_SIZE];

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

// copy the next space-separated word of *args into word[48]; false at the end
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
        if (n < 47) {
            word[n++] = *p;
        }
        p++;
    }
    word[n] = 0;
    *args = p;
    return true;
}

static int run_script(const char *args) {
    char script[48], word[48];
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
    gc_init(heap, heap + sizeof(heap));
    mp_init();
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
