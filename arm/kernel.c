// kernel.c - MyOS for Raspberry Pi (ARM64)
// Boots (boot8.s drops from EL2 to EL1), turns on the MMU and caches,
// installs its exception vectors, starts the timer, USB and the SD card,
// and runs a shell over serial and USB. Programs run in EL0 (user.c).
#include <stdint.h>
#include <string.h>
#include "broadcom/caches.h"
#include "broadcom/defines.h"
#include "broadcom/interrupts.h"
#include "broadcom/mmu.h"
#include "console.h"
#include "fat.h"
#include "led.h"
#include "myfs.h"
#include "pkg.h"
#include "print.h"
#include "proc.h"
#include "sd.h"
#include "timer.h"
#include "uart.h"
#include "user.h"
#include "wifi.h"
#ifndef NO_USB
#include "net.h"
#include "remote.h"
#include "ssh.h"
#include "usb.h"
#endif

#ifdef PI4
#define PM_BASE 0xFE100000UL
#else
#define PM_BASE 0x3F100000UL
#endif

// ---------------------------------------------------------------
//  timer interrupt: 100 ticks per second from the system timer
// ---------------------------------------------------------------
volatile uint64_t ticks;
extern volatile int proc_need_resched;

static uint32_t next_tick;                  // system timer value of the next tick

void TIMER_1_IRQHandler(void) {
    SYSTMR->CS = 1u << 1;                   // acknowledge compare channel 1
    do {
        uint32_t now = SYSTMR->CLO;
        while ((int32_t)(now - next_tick) >= 0) {   // count ticks missed while we were late
            ticks++;
            next_tick += 1000000 / TICKS_PER_SECOND;
        }
        SYSTMR->C1 = next_tick;
    } while ((int32_t)(SYSTMR->CLO - next_tick) >= 0);  // already passed? (else no more interrupts)
    proc_tick();                            // CPU time for 'ps' and htop
    proc_need_resched = 1;                  // a program has had its 10 ms: next one (proc.c)
}

static void timer_init(void) {
    next_tick = SYSTMR->CLO + 1000000 / TICKS_PER_SECOND;   // the timer counts at 1 MHz
    SYSTMR->C1 = next_tick;
    SYSTMR->CS = 1u << 1;
    BP_EnableIRQ(TIMER_1_IRQn);
}

// ---------------------------------------------------------------
//  CPU information (ARM "system registers", read with mrs)
// ---------------------------------------------------------------
static unsigned current_el(void) {
    uint64_t v;
    __asm__ volatile ("mrs %0, CurrentEL" : "=r"(v));
    return (unsigned)((v >> 2) & 3);
}

static uint64_t cpu_id(void) {
    uint64_t v;
    __asm__ volatile ("mrs %0, midr_el1" : "=r"(v));
    return v;
}

// ---------------------------------------------------------------
//  small helpers
// ---------------------------------------------------------------
// does line start with word (followed by a space or the end)? -> rest of line
static const char *command(const char *line, const char *word) {
    while (*word) {
        if (*line++ != *word++) return 0;
    }
    if (*line == ' ') {
        while (*line == ' ') line++;
        return line;
    }
    return *line == 0 ? line : 0;
}

// copy the first word of s into out[MYFS_PATH_MAX]; returns the rest of s, or 0 if no word
static const char *first_word(const char *s, char *out) {
    int n = 0;
    while (*s && *s != ' ') {
        if (n < MYFS_PATH_MAX - 1) out[n++] = *s;
        s++;
    }
    out[n] = 0;
    while (*s == ' ') s++;
    return n ? s : 0;
}

// a path typed in the shell -> absolute, from this terminal's current folder
static int abs_path(const char *arg, char *out) {
    if (myfs_normalize(proc_cwd(), arg, out) == 0) return 1;
    console_puts("Bad name: ");
    console_puts(arg);
    console_puts(" (names: 1-63 letters, digits or signs, no spaces)\n");
    return 0;
}

static void fs_error(int err, const char *path) {
    console_puts(myfs_error(err));
    console_puts(": ");
    console_puts(path);
    console_putc('\n');
}

static const char banner[] =
    "  __  __        ___  ____  \n"
    " |  \\/  |_   _ / _ \\/ ___| \n"
    " | |\\/| | | | | | | \\___ \\ \n"
    " | |  | | |_| | |_| |___) |\n"
    " |_|  |_|\\__, |\\___/|____/ \n"
    "         |___/   for Raspberry Pi (ARM64) v0.6\n\n"
    "Type 'help' to see commands.\n\n";

// "myos> " in the top folder, "myos:/apps> " anywhere else
static void print_prompt(void) {
    const char *cwd = proc_cwd();
    console_puts("myos");
    if (strcmp(cwd, "/") != 0) {
        console_putc(':');
        console_puts(cwd);
    }
    console_puts("> ");
}

static void read_line(char *buf, int size) {
    int len = 0;
    for (;;) {
        int c = console_getc_nowait();
#ifndef NO_USB
        if (console_tty() == 0 && usb_take_connect_event()) {   // a terminal opened the COM port
            console_puts("\n");
            console_puts(banner);
            print_prompt();
            buf[len] = 0;
            console_puts(buf);
        }
#endif
        if (c < 0) continue;
        if (c == '\r' || c == '\n') {
            console_putc('\n');
            buf[len] = 0;
            return;
        }
        if (c == 3) {                               // Ctrl+C: forget this line
            console_puts("^C\n");
            buf[0] = 0;
            return;
        }
        if ((c == 8 || c == 127) && len > 0) {      // backspace
            len--;
            console_puts("\b \b");
        } else if (c >= ' ' && c <= '~' && len < size - 1) {
            buf[len++] = (char)c;
            console_putc((char)c);
            console_puts("");                       // send it now
        }
    }
}

// ---------------------------------------------------------------
//  storage: SD card -> FAT partition -> myfs.img -> MyFS
// ---------------------------------------------------------------
static int sd_status = 1;           // 1 = not tried, 0 = ok, <0 = init step that failed
static int fat_status = 1;
static myfs_dev_t myfs_dev;
static int mount_status = MYFS_ERR_NOFS;

static void storage_init(void) {
    sd_status = sd_init();
    if (sd_status) return;
    fat_file_t f;
    fat_status = fat_find_contiguous("MYFS    IMG", &f);
    if (fat_status) return;
    myfs_dev.start = f.start_lba;
    myfs_dev.sectors = f.sectors;
    myfs_dev.read = sd_read;
    myfs_dev.write = sd_write;
    mount_status = myfs_mount(&myfs_dev);
}

static void print_check_report(const myfs_check_t *r, int full) {
    if (full) {
        print_dec(r->files);
        console_puts(" file(s), ");
        print_dec(r->dirs);
        console_puts(" folder(s), ");
        print_dec(r->used_kb);
        console_puts(" KB used, ");
        print_dec(myfs_free_kb());
        console_puts(" KB free\n");
    }
    if (r->leaked_kb) {
        console_puts("Gave back ");
        print_dec(r->leaked_kb);
        console_puts(" KB of lost space (left by an interrupted write).\n");
    }
    if (r->missing_kb) {
        console_puts("Fixed: ");
        print_dec(r->missing_kb);
        console_puts(" KB in use were marked free.\n");
    }
    if (r->damaged) {
        print_dec(r->damaged);
        console_puts(" damaged entr");
        console_puts(r->damaged == 1 ? "y" : "ies");
        console_puts(" (bad or shared sectors):\n");
        for (uint32_t i = 0; i < r->damaged && i < 4; i++) {
            console_puts("  ");
            console_puts(r->damaged_path[i]);
            console_putc('\n');
        }
    }
    if (r->too_deep) console_puts("Some folders could not be read (too deep or a disk error).\n");
    if (full && !r->leaked_kb && !r->missing_kb && !r->damaged && !r->too_deep) {
        console_puts("No problems found.\n");
    }
}

static void print_storage_status(void) {
    console_puts("Disk: ");
    if (sd_status) {
        console_puts("no SD card (error ");
        print_signed(sd_status);
        console_puts(")\n\n");
    } else if (fat_status == FAT_ERR_NOTFOUND) {
        console_puts("no myfs.img on the SD card's boot partition\n\n");
    } else if (fat_status == FAT_ERR_FRAGMENTED) {
        console_puts("myfs.img is in pieces; copy it to the card again\n\n");
    } else if (fat_status) {
        console_puts("can't read the boot partition\n\n");
    } else if (!myfs_mounted()) {
        if (mount_status == MYFS_ERR_NOFS) {
            console_puts("myfs.img has no file system. Type 'format' to create one.\n\n");
        } else {                    // there is a MyFS, it just can't be used: never suggest format
            console_puts("myfs.img has MyFS, but it can't be mounted (");
            console_puts(myfs_error(mount_status));
            console_puts(").\n      Don't 'format': copy myfs.img to the PC and run\n"
                         "      python tools/myfs.py myfs.img fsck\n\n");
        }
    } else {
        console_puts("MyFS mounted from myfs.img, ");
        print_dec((uint64_t)myfs_count());
        console_puts(" file(s)");
        if (myfs_version() == 1) {
            console_puts(". Type 'ls' to list them.\n"
                         "      (old MyFS v1: no folders, 64 files at most. Type 'fsupgrade' to\n"
                         "       make it MyFS v2 - every file is kept.)\n\n");
        } else {
            if (myfs_dir_count()) {
                console_puts(", ");
                print_dec((uint64_t)myfs_dir_count());
                console_puts(" folder(s)");
            }
            console_puts(". Type 'ls' to list them.\n");
            print_check_report(myfs_mount_check(), 0);
            console_putc('\n');
        }
    }
}

static int need_fs(void) {
    if (myfs_mounted()) return 1;
    console_puts("No file system. Type 'sd' for details.\n");
    return 0;
}

// ---------------------------------------------------------------
//  shell commands
// ---------------------------------------------------------------
static void cmd_help(void) {
    console_puts("Commands:\n"
                 "  help      show this list\n"
                 "  echo X    print X\n"
                 "  info      CPU and privilege level\n"
                 "  uptime    time since boot\n"
                 "  clear     clear the screen\n"
                 "  sd        SD card and file system details\n"
                 "  hexdump N show raw bytes of SD card sector N\n"
                 "  ls [P]    list a folder (P: a path like /apps or ..)\n"
                 "  cd P      go to folder P ('cd' alone: the top folder /);  pwd  where am I\n"
                 "  mkdir P   new folder ('mkdir -p a/b/c' makes the parents too)\n"
                 "  rmdir P   delete an empty folder\n"
                 "  tree [P]  everything inside a folder\n"
                 "  cat F     show file F\n"
                 "  write F X write text X into file F\n"
                 "  cp A B    copy a file;  mv A B  rename or move a file or folder\n"
                 "  rm F      delete file F ('rm -r P' deletes a folder and all in it)\n"
                 "  df        how full the disk (MyFS) is\n"
                 "  fsck      check the file system ('fsck fix' removes damaged entries)\n"
                 "  fsupgrade turn an old MyFS v1 into v2 (folders), keeping every file\n"
                 "  format    erase myfs.img, create an empty file system\n"
                 "  pkg       apps: pkg update | search | install N | remove N | list\n"
                 "  apt       the same as pkg (apt install -y N, apt show N ...)\n"
                 "  net       USB network status (the Pi is 192.168.7.1)\n"
                 "  passwd    set the SSH password\n"
                 "  wifi      Wi-Fi: wifi scan | connect NAME [PASSWORD] | status | disconnect\n"
                 "  ping IP   send 4 pings (e.g. ping 192.168.7.2)\n"
                 "  NAME &    run a program in the background\n"
                 "  ps        running programs;  kill PID  stops one;  fg PID  brings it back\n"
                 "  Ctrl+C    stops the program in front\n"
                 "  exit      leave an SSH or telnet session\n"
                 "  crash     test the panic screen ('crash pf' = page fault)\n"
                 "  update    receive a new MyOS from the PC (arm\\update-pi.ps1)\n"
                 "  recv F    receive file F from the PC (arm\\push-pi.ps1)\n"
                 "  reboot    restart the Pi\n"
                 "Programs run by typing their name. More in 'pkg list'.\n");
}

static void cmd_info(void) {
    uint64_t id = cpu_id();
    uint32_t part = (uint32_t)(id >> 4) & 0xFFF;
    console_puts("CPU:        ");
    if (part == 0xD03) console_puts("ARM Cortex-A53");
    else if (part == 0xD08) console_puts("ARM Cortex-A72");
    else console_puts("ARM (unknown model)");
    console_puts("  (MIDR 0x");
    print_hex(id, 16);
    console_puts(")\nMode:       64-bit (AArch64), exception level EL");
    print_dec(current_el());
    console_puts("\nTimer:      ");
    print_dec(timer_freq() / 1000);
    console_puts(" kHz\nBuilt:      " __DATE__ " " __TIME__);
    console_puts("\nConsole:    serial (GPIO 14/15)");
#ifndef NO_USB
    console_puts(" + USB serial");
#endif
    console_puts("\n");
}

static void cmd_uptime(void) {
    uint64_t seconds = ticks / TICKS_PER_SECOND;
    console_puts("Uptime: ");
    print_dec(seconds / 3600);
    console_puts("h ");
    print_dec(seconds / 60 % 60);
    console_puts("m ");
    print_dec(seconds % 60);
    console_puts("s\nTimer interrupts so far: ");
    print_dec(ticks);
    console_puts(" (100 per second)\n");
}

static void cmd_sd(void) {
    if (sd_status) {
        console_puts("SD card: not ready (init failed at step ");
        print_signed(-sd_status);
        console_puts(")\n");
        return;
    }
    console_puts("SD card:  ");
    console_puts(sd_is_high_capacity() ? "SDHC/SDXC, " : "SDSC, ");
    print_dec(sd_sectors() / 2048);
    console_puts(" MB, controller clock ");
    print_dec(sd_base_clock() / 1000000);
    console_puts(" MHz, on ");
    console_puts(sd_controller());
    console_puts("\nmyfs.img: ");
    if (fat_status) {
        console_puts("not usable (");
        print_signed(fat_status);
        console_puts(")\n");
        return;
    }
    console_puts("sectors ");
    print_dec(myfs_dev.start);
    console_puts("-");
    print_dec(myfs_dev.start + myfs_dev.sectors - 1);
    console_puts(" (");
    print_dec(myfs_dev.sectors / 2048);
    console_puts(" MB)\nMyFS:     ");
    if (myfs_mounted()) {
        console_puts(myfs_version() == 2 ? "v2, " : "v1 (old: 'fsupgrade' makes it v2), ");
        print_dec((uint64_t)myfs_count());
        console_puts(" file(s), ");
        if (myfs_version() == 2) {
            print_dec((uint64_t)myfs_dir_count());
            console_puts(" folder(s), ");
        }
        print_dec(myfs_free_kb());
        console_puts(" KB free\n");
    } else {
        console_puts("not formatted\n");
    }
}

static uint8_t sector_buf[512] __attribute__((aligned(4)));
static uint8_t verify_buf[512] __attribute__((aligned(4)));

static void cmd_hexdump(const char *arg) {
    uint32_t n = 0;
    while (*arg >= '0' && *arg <= '9') n = n * 10 + (uint32_t)(*arg++ - '0');
    if (sd_read(n, sector_buf)) {
        console_puts("Can't read that sector.\n");
        return;
    }
    console_puts("Sector ");
    print_dec(n);
    console_puts(" (first 256 of 512 bytes):\n");
    for (int row = 0; row < 256; row += 16) {
        print_hex((uint64_t)row, 4);
        console_puts(": ");
        for (int i = 0; i < 16; i++) {
            print_hex(sector_buf[row + i], 2);
            console_putc(' ');
        }
        console_putc('|');
        for (int i = 0; i < 16; i++) {
            uint8_t c = sector_buf[row + i];
            console_putc(c >= ' ' && c <= '~' ? (char)c : '.');
        }
        console_puts("|\n");
    }
}

static void pad_to(int written, int width) {
    while (written++ < width) console_putc(' ');
}

static void ls_line(const char *name, int type, uint32_t size) {
    console_puts("  ");
    console_puts(name);
    int w = (int)strlen(name);
    if (type == MYFS_DIR) {
        console_putc('/');
        pad_to(w + 1, 34);
        console_puts("<folder>\n");
    } else {
        pad_to(w, 34);
        print_dec(size);
        console_puts(" bytes\n");
    }
}

// ls [path]: a folder (default: the current one), folders shown with a '/'
static void cmd_ls(const char *arg) {
    if (!need_fs()) return;
    char path[MYFS_PATH_MAX], name[MYFS_NAME_MAX + 1], word[MYFS_PATH_MAX];
    if (!abs_path(first_word(arg, word) ? word : ".", path)) return;
    uint32_t size;
    int t = myfs_stat(path, &size);
    if (t < 0) {
        fs_error(t, word[0] ? word : path);
        return;
    }
    if (t == MYFS_FILE) {
        ls_line(word, t, size);
        return;
    }
    int files = 0, dirs = 0;
    for (int i = 0; (t = myfs_readdir(path, i, name, &size)) > 0; i++) {
        ls_line(name, t, size);
        if (t == MYFS_DIR) dirs++;
        else files++;
    }
    if (t < 0) fs_error(t, path);
    if (files + dirs == 0) console_puts("  (no files)\n");
    print_dec((uint64_t)files);
    console_puts(" file(s), ");
    if (dirs) {
        print_dec((uint64_t)dirs);
        console_puts(" folder(s), ");
    }
    print_dec(myfs_free_kb());
    console_puts(" KB free\n");
}

static void cmd_cd(const char *arg) {
    if (!need_fs()) return;
    char path[MYFS_PATH_MAX], word[MYFS_PATH_MAX];
    if (!abs_path(first_word(arg, word) ? word : "/", path)) return;
    int t = myfs_stat(path, 0);
    if (t < 0) fs_error(t, word);
    else if (t != MYFS_DIR) fs_error(MYFS_ERR_NOTDIR, word);
    else strcpy(proc_current()->cwd, path);
}

static void cmd_mkdir(const char *arg) {
    if (!need_fs()) return;
    char path[MYFS_PATH_MAX], word[MYFS_PATH_MAX];
    int parents = 0;
    const char *rest = first_word(arg, word);
    if (rest && strcmp(word, "-p") == 0) {
        parents = 1;
        rest = first_word(rest, word);
    }
    if (!rest) {
        console_puts("Usage: mkdir [-p] <folder>\n");
        return;
    }
    if (!abs_path(word, path)) return;
    if (parents) {                          // each folder on the way, if it isn't there yet
        for (char *s = path + 1; *s; s++) {
            if (*s != '/') continue;
            *s = 0;
            int t = myfs_stat(path, 0);
            int r = t == MYFS_ERR_NOTFOUND ? myfs_mkdir(path) : t == MYFS_FILE ? MYFS_ERR_NOTDIR : 0;
            *s = '/';
            if (r < 0) {
                fs_error(r, word);
                return;
            }
        }
        if (myfs_stat(path, 0) == MYFS_DIR) return;
    }
    int r = myfs_mkdir(path);
    if (r) fs_error(r, word);
}

static void cmd_rmdir(const char *arg) {
    if (!need_fs()) return;
    char path[MYFS_PATH_MAX], word[MYFS_PATH_MAX];
    if (!first_word(arg, word)) {
        console_puts("Usage: rmdir <folder>\n");
        return;
    }
    if (!abs_path(word, path)) return;
    if (strcmp(path, proc_cwd()) == 0 ||
        (strncmp(proc_cwd(), path, strlen(path)) == 0 && proc_cwd()[strlen(path)] == '/')) {
        console_puts("You are inside that folder: 'cd ..' first.\n");
        return;
    }
    int r = myfs_rmdir(path);
    if (r == MYFS_ERR_NOTEMPTY) console_puts("Folder is not empty (use 'rm -r' to delete it with everything in it).\n");
    else if (r) fs_error(r, word);
}

// path + "/" + name into out (path is absolute)
static int join(const char *path, const char *name, char *out) {
    size_t n = strlen(path), m = strlen(name);
    if (n + 1 + m >= MYFS_PATH_MAX) return 0;
    strcpy(out, path);
    if (n > 1) out[n++] = '/';
    strcpy(out + n, name);
    return 1;
}

// "cp a /apps" or "mv a /apps": a folder as the target means "into it"
static int target_path(const char *from, const char *to_word, char *to) {
    if (!abs_path(to_word, to)) return 0;
    if (myfs_stat(to, 0) == MYFS_DIR) {
        const char *b = from;
        for (const char *s = from; *s; s++) if (*s == '/') b = s + 1;
        char dir[MYFS_PATH_MAX];
        strcpy(dir, to);
        if (!join(dir, b, to)) {
            console_puts("Path too long.\n");
            return 0;
        }
    }
    return 1;
}

static void cmd_cp_mv(const char *arg, int move) {
    if (!need_fs()) return;
    char a[MYFS_PATH_MAX], b[MYFS_PATH_MAX], from[MYFS_PATH_MAX], to[MYFS_PATH_MAX];
    const char *rest = first_word(arg, a);
    if (!rest || !first_word(rest, b)) {
        console_puts(move ? "Usage: mv <from> <to>\n" : "Usage: cp <file> <to>\n");
        return;
    }
    if (!abs_path(a, from) || !target_path(from, b, to)) return;
    int r = move ? myfs_rename(from, to) : myfs_copy(from, to);
    if (r == MYFS_ERR_ISDIR && !move) console_puts("cp copies files; folders: use mv, or mkdir + cp.\n");
    else if (r) fs_error(r, r == MYFS_ERR_NOTFOUND && myfs_stat(from, 0) >= 0 ? b : a);
    else if (move && strncmp(proc_cwd(), from, strlen(from)) == 0 &&
             (proc_cwd()[strlen(from)] == '/' || proc_cwd()[strlen(from)] == 0)) {
        strcpy(proc_current()->cwd, "/");   // we were in the folder that moved
    }
}

static void tree(char *path, int depth, int *files, int *dirs) {
    char name[MYFS_NAME_MAX + 1], sub[MYFS_PATH_MAX];
    uint32_t size;
    int t;
    for (int i = 0; (t = myfs_readdir(path, i, name, &size)) > 0; i++) {
        for (int k = 0; k <= depth; k++) console_puts("  ");
        console_puts(name);
        if (t == MYFS_DIR) {
            console_puts("/\n");
            (*dirs)++;
            if (depth < MYFS_DEPTH_MAX && join(path, name, sub)) tree(sub, depth + 1, files, dirs);
        } else {
            console_puts("  (");
            print_dec(size);
            console_puts(" bytes)\n");
            (*files)++;
        }
    }
}

static void cmd_tree(const char *arg) {
    if (!need_fs()) return;
    char path[MYFS_PATH_MAX], word[MYFS_PATH_MAX];
    if (!abs_path(first_word(arg, word) ? word : ".", path)) return;
    int t = myfs_stat(path, 0);
    if (t != MYFS_DIR) {
        fs_error(t < 0 ? t : MYFS_ERR_NOTDIR, path);
        return;
    }
    int files = 0, dirs = 0;
    console_puts(path);
    console_putc('\n');
    tree(path, 0, &files, &dirs);
    print_dec((uint64_t)files);
    console_puts(" file(s), ");
    print_dec((uint64_t)dirs);
    console_puts(" folder(s)\n");
}

static void print_size_kb(uint32_t kb) {       // 512K, 14.8M (like df -h)
    if (kb < 1024) {
        print_dec(kb);
        console_putc('K');
        return;
    }
    uint32_t tenths = (uint32_t)(((uint64_t)kb * 10 + 512) / 1024);
    print_dec(tenths / 10);
    console_putc('.');
    print_dec(tenths % 10);
    console_putc('M');
}

static int size_width(uint32_t kb) {            // characters print_size_kb will use
    int digits = 1;
    uint32_t v = kb < 1024 ? kb : (uint32_t)(((uint64_t)kb * 10 + 512) / 1024 / 10);
    while (v >= 10) { v /= 10; digits++; }
    return kb < 1024 ? digits + 1 : digits + 3;
}

static void df_column(uint32_t kb) {
    pad_to(size_width(kb), 7);
    print_size_kb(kb);
}

static int dec_width(uint64_t v) {
    int n = 1;
    while (v >= 10) { v /= 10; n++; }
    return n;
}

// df [-h]: how full MyFS is (sizes are always "human readable")
static void cmd_df(void) {
    if (!need_fs()) return;
    uint32_t size = myfs_total_kb(), avail = myfs_free_kb();
    uint32_t used = size > avail ? size - avail : 0;
    console_puts("Filesystem   Size   Used  Avail  Use%  Files  Mounted on\n");
    console_puts("myfs.img  ");
    df_column(size);
    df_column(used);
    df_column(avail);
    uint32_t pct = size ? (uint32_t)(((uint64_t)used * 100 + size - 1) / size) : 0;
    pad_to(pct >= 100 ? 3 : pct >= 10 ? 2 : 1, 5);
    print_dec(pct);
    console_putc('%');
    int files = myfs_count();
    if (myfs_version() == 1) {                  // v1: a directory of 64 places
        pad_to(dec_width((uint64_t)files) + 3, 7);
        print_dec((uint64_t)files);
        console_puts("/");
        print_dec(MYFS_MAX_FILES);
    } else {
        pad_to(dec_width((uint64_t)files), 7);
        print_dec((uint64_t)files);
    }
    console_puts("  /\n");
}

static void cmd_cat(const char *arg) {
    char word[MYFS_PATH_MAX], name[MYFS_PATH_MAX];
    if (!need_fs()) return;
    if (!first_word(arg, word)) {
        console_puts("Usage: cat <file>\n");
        return;
    }
    if (!abs_path(word, name)) return;
    int32_t size = myfs_size(name);
    if (size < 0) {
        if (size == MYFS_ERR_ISDIR) console_puts("That's a folder (try 'ls'): ");
        else console_puts("No such file: ");
        console_puts(word);
        console_putc('\n');
        return;
    }
    char chunk[128];
    for (uint32_t off = 0; off < (uint32_t)size; off += sizeof chunk) {
        int n = myfs_read(name, off, chunk, sizeof chunk);
        if (n <= 0) break;
        for (int i = 0; i < n; i++) {
            char c = chunk[i];
            if (c == '\r') continue;                // Windows line endings
            if (c == '\t') c = ' ';
            if (c != '\n' && (c < ' ' || c > '~')) c = '.';
            console_putc(c);
        }
    }
    if (!console_at_line_start()) console_putc('\n');
    console_puts("");
}

static void cmd_write(const char *arg) {
    char word[MYFS_PATH_MAX], name[MYFS_PATH_MAX];
    if (!need_fs()) return;
    const char *text = first_word(arg, word);
    if (!text) {
        console_puts("Usage: write <file> <text>\n");
        return;
    }
    if (!abs_path(word, name)) return;
    char data[96];
    size_t n = strlen(text);
    if (n > sizeof data - 1) n = sizeof data - 1;
    memcpy(data, text, n);
    data[n++] = '\n';
    int r = myfs_write(name, data, (uint32_t)n);
    if (r == MYFS_ERR_FULL) {
        console_puts("Disk or directory is full.\n");
    } else if (r) {
        fs_error(r, word);
    } else {
        console_puts("Wrote ");
        print_dec(n);
        console_puts(" bytes to ");
        console_puts(word);
        console_putc('\n');
    }
}

// rm -r: a folder with everything inside it
static int remove_tree(const char *path) {
    int t = myfs_stat(path, 0);
    if (t == MYFS_FILE) return myfs_remove(path);
    if (t < 0) return t;
    char name[MYFS_NAME_MAX + 1], sub[MYFS_PATH_MAX];
    for (;;) {
        int r = myfs_readdir(path, 0, name, 0);
        if (r < 0) return r;
        if (r == 0) break;
        if (!join(path, name, sub)) return MYFS_ERR_NAME;
        if ((r = remove_tree(sub)) < 0) return r;
    }
    return myfs_rmdir(path);
}

static void cmd_rm(const char *arg) {
    char word[MYFS_PATH_MAX], name[MYFS_PATH_MAX];
    if (!need_fs()) return;
    const char *rest = first_word(arg, word);
    int recursive = rest && strcmp(word, "-r") == 0;
    if (recursive) rest = first_word(rest, word);
    if (!rest) {
        console_puts("Usage: rm <file>   or   rm -r <folder>\n");
        return;
    }
    if (!abs_path(word, name)) return;
    if (recursive) {
        size_t n = strlen(name);
        if (strcmp(name, "/") == 0 || (strncmp(proc_cwd(), name, n) == 0 &&
                                       (proc_cwd()[n] == '/' || proc_cwd()[n] == 0))) {
            console_puts(strcmp(name, "/") == 0 ? "Not the top folder (use 'format yes' to erase all).\n"
                                                : "You are inside that folder: 'cd ..' first.\n");
            return;
        }
        int r = remove_tree(name);
        if (r) fs_error(r, word);
        else {
            console_puts("Removed ");
            console_puts(word);
            console_putc('\n');
        }
        return;
    }
    int r = myfs_remove(name);
    if (r == MYFS_ERR_ISDIR) console_puts("That's a folder: use rmdir, or rm -r to delete everything in it.\n");
    else if (r && r != MYFS_ERR_NOTFOUND) fs_error(r, word);
    else {
        console_puts(r == 0 ? "Removed " : "No such file: ");
        console_puts(word);
        console_putc('\n');
    }
}

static void cmd_fsck(const char *arg) {
    if (!need_fs()) return;
    if (myfs_version() == 1) {
        console_puts("This is the old MyFS v1 (nothing to check: no bitmap, no folders).\n"
                     "Type 'fsupgrade' to make it MyFS v2.\n");
        return;
    }
    myfs_check_t r;
    console_puts("Checking every folder and file...\n");
    int e = myfs_check(strcmp(arg, "fix") == 0, &r);
    if (e) {
        console_puts("Check failed: ");
        console_puts(myfs_error(e));
        console_putc('\n');
        return;
    }
    print_check_report(&r, 1);
    if (r.damaged && strcmp(arg, "fix") != 0) {
        console_puts("Type 'fsck fix' to remove the damaged entries (data another file uses stays).\n");
    } else if (r.damaged) {
        console_puts("Removed them.\n");
    }
}

static void cmd_fsupgrade(void) {
    if (!need_fs()) return;
    if (myfs_version() == 2) {
        console_puts("Already MyFS v2.\n");
        return;
    }
    console_puts("Upgrading MyFS v1 -> v2. File data stays where it is; a new folder table\n"
                 "and free-space bitmap are written into free space, then the first sector.\n");
    int n = myfs_upgrade();
    if (n < 0) {
        console_puts("Upgrade failed: ");
        console_puts(myfs_error(n));
        console_puts(myfs_version() == 1 ? ". Nothing was changed.\n"
                                         : ". Type 'fsck' to look at the result.\n");
        return;
    }
    console_puts("Done: ");
    print_dec((uint64_t)n);
    console_puts(" file(s) kept, ");
    print_dec(myfs_free_kb());
    console_puts(" KB free. Folders work now: try 'mkdir'.\n");
}

static void cmd_format(const char *arg) {
    if (sd_status || fat_status) {
        console_puts("No usable myfs.img. Type 'sd' for details.\n");
        return;
    }
    if (strcmp(arg, "yes") != 0) {
        console_puts("This ERASES everything in myfs.img. Type 'format yes' to continue.\n");
        return;
    }
    if (myfs_format(&myfs_dev)) {
        console_puts("Disk error!\n");
        return;
    }
    for (int i = 0; i < NPROC; i++) strcpy(proc_slot(i)->cwd, "/");     // old folders are gone
    console_puts("myfs.img formatted with MyFS v2. ");
    print_dec(myfs_free_kb());
    console_puts(" KB free\n");
}

static void cmd_net(void) {
#ifdef NO_USB
    console_puts("No network in this build (QEMU can't be a USB device).\n");
#else
    char pc[16];
    console_puts("Network:  USB network adapter (CDC-NCM)\n"
                 "This Pi:  192.168.7.1  (name: myos.usb)\n"
                 "Link:     ");
    console_puts(net_link_up() ? "up\n" : "down (USB not connected)\n");
    console_puts("PC:       ");
    console_puts(net_pc_address(pc) ? pc : "no address handed out yet");
    console_puts("\nPackets:  ");
    print_dec(net_packets_in());
    console_puts(" in, ");
    print_dec(net_packets_out());
    console_puts(" out\nShell:    telnet 192.168.7.1 (port 23) - ");
    if (remote_client(pc)) {
        console_puts("connected from ");
        console_puts(pc);
    } else {
        console_puts("nobody connected");
    }
    console_puts("\nSSH:      ssh myos@192.168.7.1 - ");
    char who[64];
    int n = 0;
    for (int i = 0; i < ssh_sessions(); i++) {
        if (!ssh_client(i, who)) continue;
        console_puts(n++ ? ", " : "logged in: ");
        console_puts(who);
        if (console_tty() == TTY_SSH + i) console_puts(" (you)");
    }
    if (!n) console_puts(ssh_password_set() ? "nobody logged in" : "no password yet: type 'passwd'");
    console_puts("\n          (up to ");
    print_dec((uint64_t)ssh_sessions());
    console_puts(" sessions at once, each with its own shell)");
    char fp[52];
    if (ssh_fingerprint(fp)) {
        console_puts("\nHost key: ED25519 ");
        console_puts(fp);
        if (!ssh_host_key_saved()) console_puts(" (not saved: no MyFS)");
    }
    console_puts("\nTry on the PC:  ping 192.168.7.1\n");
#endif
}

// read a line without showing it (for passwords)
static void read_secret(char *buf, int size) {
    int len = 0;
    for (;;) {
        int c = console_getc_nowait();
        if (c < 0) continue;
        if (c == '\r' || c == '\n') break;
        if ((c == 8 || c == 127) && len > 0) len--;
        else if (c >= ' ' && c <= '~' && len < size - 1) buf[len++] = (char)c;
    }
    buf[len] = 0;
    console_putc('\n');
}

static void cmd_passwd(void) {
#ifdef NO_USB
    console_puts("No SSH in this build.\n");
#else
    if (!need_fs()) return;
    char a[64], b[64];
    console_puts("New SSH password (not shown): ");
    read_secret(a, sizeof a);
    if (strlen(a) < 6) {
        console_puts("Too short: use at least 6 characters.\n");
        memset(a, 0, sizeof a);
        return;
    }
    console_puts("Type it again: ");
    read_secret(b, sizeof b);
    if (strcmp(a, b) != 0) console_puts("They don't match. Nothing changed.\n");
    else if (ssh_set_password(a) != 0) console_puts("Couldn't save it to MyFS.\n");
    else console_puts("Saved. Log in from the PC with:  ssh myos@192.168.7.1\n");
    memset(a, 0, sizeof a);
    memset(b, 0, sizeof b);
#endif
}

static void cmd_exit(void) {
#ifndef NO_USB
    int t = console_tty();
    if (t >= TTY_SSH) { ssh_logout(t - TTY_SSH); return; }
    if (t == TTY_TELNET) { remote_logout(); return; }
#endif
    console_puts("'exit' ends an SSH or telnet session; there's nothing to leave here.\n");
}

// ---------------------------------------------------------------
//  processes: ps, kill, fg
// ---------------------------------------------------------------
static const char *tty_name(int t) {
    static const char *const names[NTTY] = { "serial", "telnet", "ssh1", "ssh2", "ssh3" };
    return t >= 0 && t < NTTY ? names[t] : "?";
}

static void cmd_ps(void) {
    console_puts("  PID  TTY     STATE       MEMORY  TIME  NAME\n");
    for (int i = 0; i < NPROC; i++) {
        proc_t *p = proc_slot(i);
        if (p->state == P_FREE) continue;
        console_puts(p->tty == console_tty() ? "* " : "  ");     // * = this terminal
        if (p->pid < 10) console_putc(' ');
        print_dec((uint64_t)p->pid);
        console_puts("  ");
        const char *tn = tty_name(p->tty);
        console_puts(tn);
        for (size_t k = strlen(tn); k < 8; k++) console_putc(' ');
        const char *st = p->kernel ? "shell" : p->state == P_ZOMBIE ? "finished"
                       : p->background ? "background" : "foreground";
        console_puts(st);
        for (size_t k = strlen(st); k < 12; k++) console_putc(' ');
        uint32_t mb = proc_used_blocks(p) * 2;
        if (mb < 10) console_putc(' ');
        if (mb < 100) console_putc(' ');
        print_dec(mb);
        console_puts(" MB  ");
        uint64_t secs = i == 0 ? ticks / TICKS_PER_SECOND : (ticks - p->started) / TICKS_PER_SECOND;
        if (secs < 1000) console_putc(' ');
        if (secs < 100) console_putc(' ');
        if (secs < 10) console_putc(' ');
        print_dec(secs);
        console_puts("s  ");
        console_puts(p->name);
        console_putc('\n');
    }
    console_puts("* = on this terminal.  Free memory for programs: ");
    print_dec((uint64_t)proc_free_blocks() * 2);
    console_puts(" MB\n");
}

static int parse_pid(const char *arg) {
    int n = 0;
    if (!arg || *arg < '0' || *arg > '9') return -1;
    while (*arg >= '0' && *arg <= '9') n = n * 10 + (*arg++ - '0');
    return n;
}

static void cmd_kill(const char *arg) {
    int pid = parse_pid(arg);
    if (pid <= 0 || proc_kill(pid) != 0) {
        console_puts("Usage: kill PID   (see 'ps'; the shell itself can't be killed)\n");
        return;
    }
    console_puts("Stopping ");
    print_dec((uint64_t)pid);
    console_puts(" ...\n");
    for (int i = 0; i < 50; i++) console_poll();    // let it stop now; the notice follows
}

static void report_job_end(proc_t *p) {
    console_puts("[");
    print_dec((uint64_t)p->pid);
    console_puts("] ");
    console_puts(p->name);
    if (p->exit_code == EXIT_KILLED) console_puts(" was stopped\n");
    else if (p->exit_code == EXIT_CRASHED) console_puts(" crashed\n");
    else {
        console_puts(" finished");
        if (p->exit_code) {
            console_puts(" (exit code ");
            print_dec((uint64_t)p->exit_code);
            console_putc(')');
        }
        console_putc('\n');
    }
}

static void report_finished_jobs(void) {
    for (int i = 1; i < NPROC; i++) {
        proc_t *p = proc_slot(i);
        if (p->state == P_ZOMBIE && p->tty == console_tty()) {     // only this terminal's jobs
            if (!console_at_line_start()) console_putc('\n');
            report_job_end(p);
            proc_reap(p);
        }
    }
}

static void cmd_fg(const char *arg) {
    int pid = parse_pid(arg);
    proc_t *p = pid > 0 ? proc_by_pid(pid) : 0;
    if (!p || p->state != P_READY || p->kernel || p->tty != console_tty()) {
        console_puts("Usage: fg PID   (a background program of this terminal, see 'ps')\n");
        return;
    }
    console_puts("Bringing back ");
    console_puts(p->name);
    console_puts(" (Ctrl+C stops it)\n");
    p->background = 0;
    proc_set_foreground(pid);
    int64_t code;
    if (proc_wait(pid, &code)) {
        if (!console_at_line_start()) console_putc('\n');
        if (code == EXIT_KILLED) console_puts("^C - stopped.\n");
    }
}

static void cmd_crash(const char *arg) {
    if (arg[0] == 'p') {
        *(volatile uint64_t *)0x100000000000ULL = 1;     // not mapped: data abort
    }
    __asm__ volatile ("udf #0");                        // undefined instruction
}

// ---------------------------------------------------------------
//  receiving data from the PC: 'update' (new MyOS) and 'recv' (a file)
//  MyOS: "<WORD> READY <capacity>"   PC: "MYUP", u32 size, u32 crc32, data
//  MyOS: "<WORD> OK ..." or "<WORD> FAILED <reason>"
// ---------------------------------------------------------------
#ifdef PI4
#define PM_RSTC (*(volatile uint32_t *)(0xFE100000UL + 0x1C))
#define PM_WDOG (*(volatile uint32_t *)(0xFE100000UL + 0x24))
#else
#define PM_RSTC (*(volatile uint32_t *)(0x3F100000UL + 0x1C))
#define PM_WDOG (*(volatile uint32_t *)(0x3F100000UL + 0x24))
#endif
#define PM_PASSWORD 0x5A000000u
#define RECEIVE_BUF ((uint8_t *)0x01000000)     // 16 MB: free RAM to receive into

static void cmd_reboot(void) {
    console_puts("Rebooting...\n");
    uint64_t end = millis() + 200;          // give USB time to send that
    while (millis() < end) console_poll();
    PM_WDOG = PM_PASSWORD | 10;             // watchdog fires in ~150 microseconds
    PM_RSTC = PM_PASSWORD | (PM_RSTC & ~0x30u) | 0x20;   // ...and does a full reset
    for (;;) {
    }
}

static uint32_t crc32(const uint8_t *p, uint32_t n) {
    uint32_t crc = 0xFFFFFFFF;
    while (n--) {
        crc ^= *p++;
        for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & -(crc & 1));
    }
    return ~crc;
}

static int read_byte(uint64_t timeout_ms) {
    uint64_t end = millis() + timeout_ms;
    while (millis() < end) {
        int c = console_getc_nowait();
        if (c >= 0) return c;
    }
    return -1;
}

static void receive_failed(const char *word, const char *why) {
    console_puts(word);
    console_puts(" FAILED ");
    console_puts(why);
    console_puts("\n");
}

// receive into RECEIVE_BUF; returns the size, or -1 (failure already reported)
static int64_t receive_blob(const char *word, uint32_t capacity) {
    console_puts(word);
    console_puts(" READY ");
    print_dec(capacity);
    console_puts("\n");
    uint8_t hdr[12];
    for (int i = 0; i < 12; i++) {
        int c = read_byte(15000);
        if (c < 0) { receive_failed(word, "timeout"); return -1; }
        hdr[i] = (uint8_t)c;
    }
    if (memcmp(hdr, "MYUP", 4) != 0) { receive_failed(word, "bad header"); return -1; }
    uint32_t size = hdr[4] | hdr[5] << 8 | hdr[6] << 16 | (uint32_t)hdr[7] << 24;
    uint32_t crc = hdr[8] | hdr[9] << 8 | hdr[10] << 16 | (uint32_t)hdr[11] << 24;
    if (size > capacity) { receive_failed(word, "too big"); return -1; }
    for (uint32_t i = 0; i < size; i++) {
        int c = read_byte(5000);
        if (c < 0) { receive_failed(word, "timeout while receiving"); return -1; }
        RECEIVE_BUF[i] = (uint8_t)c;
    }
    if (crc32(RECEIVE_BUF, size) != crc) { receive_failed(word, "checksum mismatch"); return -1; }
    return size;
}

static void cmd_update(void) {
    fat_file_t k;
    if (sd_status || fat_find_contiguous("MYOS8   IMG", &k)) {
        receive_failed("UPDATE", "no usable myos8.img on the SD card");
        return;
    }
    int64_t size = receive_blob("UPDATE", k.sectors * 512);
    if (size < 0) return;
    if (size == 0) { receive_failed("UPDATE", "empty image"); return; }
    // write the new kernel (rest of the file: zeros), checking every sector
    for (uint32_t s = 0; s < k.sectors; s++) {
        uint32_t off = s * 512;
        memset(sector_buf, 0, 512);
        if (off < size) memcpy(sector_buf, RECEIVE_BUF + off, size - off < 512 ? size - off : 512);
        if (sd_write(k.start_lba + s, sector_buf)) { receive_failed("UPDATE", "SD write error"); return; }
        if (sd_read(k.start_lba + s, verify_buf) || memcmp(sector_buf, verify_buf, 512) != 0) {
            receive_failed("UPDATE", "SD verify error");
            return;
        }
        if ((s & 63) == 0) console_poll();  // keep USB alive
    }
    console_puts("UPDATE OK ");
    print_dec((uint64_t)size);
    console_puts(" bytes written. Type 'reboot' to start it.\n");
}

static void cmd_recv(const char *arg) {
    char word[MYFS_PATH_MAX], name[MYFS_PATH_MAX];
    if (!myfs_mounted() || !first_word(arg, word) ||
        myfs_normalize(proc_cwd(), word, name) || !myfs_valid_name(name)) {
        receive_failed("RECV", !myfs_mounted() ? "no file system" : "usage: recv <file>");
        return;
    }
    int64_t size = receive_blob("RECV", 8 * 1024 * 1024);
    if (size < 0) return;
    int r = myfs_write(name, RECEIVE_BUF, (uint32_t)size);
    if (r) {
        receive_failed("RECV", r == MYFS_ERR_FULL ? "disk full" : r == MYFS_ERR_IO ? "disk error"
                                                                        : myfs_error(r));
        return;
    }
    console_puts("RECV OK ");
    print_dec((uint64_t)size);
    console_puts(" bytes saved as ");
    console_puts(name);
    console_puts("\n");
}

// a command's program: "prog" is looked for in the top folder (where pkg
// installs), then in the current folder; "dir/prog" is a path
static int find_program(const char *name, char *out) {
    if (myfs_normalize("/", name, out)) return 0;
    for (const char *s = name; *s; s++) {
        if (*s == '/') return myfs_normalize(proc_cwd(), name, out) == 0 && myfs_stat(out, 0) == MYFS_FILE;
    }
    if (myfs_stat(out, 0) == MYFS_FILE) return 1;
    return myfs_normalize(proc_cwd(), name, out) == 0 && myfs_stat(out, 0) == MYFS_FILE;
}

// ---------------------------------------------------------------
//  the shell: one per terminal (the serial one runs on the boot stack,
//  telnet and SSH ones are kernel threads started when they connect)
// ---------------------------------------------------------------
static void shell_loop(void) __attribute__((noreturn));

static void shell_thread(void) {
    console_puts("\n");
    console_puts(banner);
    shell_loop();
}

void terminal_opened(int t) {           // console.c: telnet or an SSH session came up
    (void)t;
    if (proc_spawn_kernel("shell", shell_thread, t) < 0) {
        // no free process slot: the session gets no shell (it sees nothing)
    }
}

int main(void) {
    uart_init();
    led_init();
    setup_mmu_flat_map();           // normal memory + device memory, caches on
    init_caches();
    extern char myos_vectors[];
    __asm__ volatile ("msr vbar_el1, %0; isb" :: "r"(myos_vectors));
    console_init();
    user_init();                    // floating point for programs, the process table
    timer_init();
#ifndef NO_USB
    usb_init();
    console_enable_usb();
    net_init();
    remote_init();
#endif
    BP_EnableIRQs();
    storage_init();
#ifndef NO_USB
    ssh_init();                     // the host key is kept in MyFS
#endif
    wifi_boot();                    // a saved Wi-Fi network: connect in the background
    console_puts("\n");
    console_puts(banner);
    print_storage_status();
    shell_loop();
}

static void shell_loop(void) {
    char line[250];                 // programs get at most 250 bytes of arguments
    for (;;) {
        report_finished_jobs();
        print_prompt();
        read_line(line, sizeof line);
        // "program args &" runs it in the background
        int background = 0;
        size_t len = strlen(line);
        while (len && line[len - 1] == ' ') line[--len] = 0;
        if (len && line[len - 1] == '&') {
            background = 1;
            line[--len] = 0;
            while (len && line[len - 1] == ' ') line[--len] = 0;
        }
        const char *arg;
        if (line[0] == 0) {
            continue;
        } else if (command(line, "ps")) {
            cmd_ps();
        } else if ((arg = command(line, "kill"))) {
            cmd_kill(arg);
        } else if ((arg = command(line, "fg"))) {
            cmd_fg(arg);
        } else if (command(line, "help")) {
            cmd_help();
        } else if ((arg = command(line, "echo"))) {
            console_puts(arg);
            console_putc('\n');
        } else if (command(line, "info")) {
            cmd_info();
        } else if (command(line, "uptime")) {
            cmd_uptime();
        } else if (command(line, "clear")) {
            console_puts("\x1b[2J\x1b[H");  // terminal codes: clear + home
        } else if (command(line, "sd")) {
            cmd_sd();
        } else if ((arg = command(line, "hexdump"))) {
            cmd_hexdump(arg);
        } else if ((arg = command(line, "ls"))) {
            cmd_ls(arg);
        } else if ((arg = command(line, "cd"))) {
            cmd_cd(arg);
        } else if (command(line, "pwd")) {
            console_puts(proc_cwd());
            console_putc('\n');
        } else if ((arg = command(line, "mkdir"))) {
            cmd_mkdir(arg);
        } else if ((arg = command(line, "rmdir"))) {
            cmd_rmdir(arg);
        } else if ((arg = command(line, "cp"))) {
            cmd_cp_mv(arg, 0);
        } else if ((arg = command(line, "mv"))) {
            cmd_cp_mv(arg, 1);
        } else if ((arg = command(line, "tree"))) {
            cmd_tree(arg);
        } else if ((arg = command(line, "fsck"))) {
            cmd_fsck(arg);
        } else if (command(line, "fsupgrade")) {
            cmd_fsupgrade();
        } else if (command(line, "df") || command(line, "df -h")) {
            cmd_df();
        } else if ((arg = command(line, "cat"))) {
            cmd_cat(arg);
        } else if ((arg = command(line, "write"))) {
            cmd_write(arg);
        } else if ((arg = command(line, "rm"))) {
            cmd_rm(arg);
        } else if ((arg = command(line, "format"))) {
            cmd_format(arg);
        } else if ((arg = command(line, "pkg"))) {
            pkg_command(arg);
        } else if ((arg = command(line, "apt")) || (arg = command(line, "apt-get"))) {
            apt_command(arg);
        } else if (command(line, "net")) {
            cmd_net();
        } else if ((arg = command(line, "wifi"))) {
            wifi_command(arg);
        } else if ((arg = command(line, "ping"))) {
            ping_command(arg);
        } else if (command(line, "passwd")) {
            cmd_passwd();
        } else if (command(line, "exit") || command(line, "logout")) {
            cmd_exit();
        } else if ((arg = command(line, "crash"))) {
            cmd_crash(arg);
        } else if (command(line, "update")) {
            cmd_update();
        } else if ((arg = command(line, "recv"))) {
            cmd_recv(arg);
        } else if (command(line, "reboot")) {
            cmd_reboot();
        } else {
            // not built in: a program on the disk?
            char name[MYFS_PATH_MAX], prog[MYFS_PATH_MAX];
            const char *args = first_word(line, name);
            if (!myfs_mounted() || !find_program(name, prog) ||
                run_program(prog, args ? args : "", background) < 0) {
                if (myfs_mounted() && pkg_python_app(name)) {   // a Python app from pkg
                    char pyargs[128];
                    // (in another folder, python needs the app's full path)
                    strcpy(pyargs, strcmp(proc_cwd(), "/") == 0 ? "" : "/");
                    strcat(pyargs, name);
                    strcat(pyargs, ".py");
                    if (args && strlen(args) < sizeof pyargs - 24) {
                        strcat(pyargs, " ");
                        strcat(pyargs, args);
                    }
                    if (run_program("/python", pyargs, background) < 0) {
                        console_puts("This is a Python app: install python first (pkg install python).\n");
                    }
                } else {
                    console_puts("Unknown command. Type 'help'.\n");
                }
            }
        }
    }
}
