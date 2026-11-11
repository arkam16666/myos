/* config.h - how SQLite is built for MyOS (forced into sqlite3.c with -include
 * by arm/build.ps1). MyOS has no file locking, threads, mmap or shared
 * memory, so those parts are left out; ports/sqlite3/sqlite3.c supplies the
 * "operating system" layer (a VFS on top of MyFS). */
#define SQLITE_OS_OTHER 1               /* our own VFS: sqlite3_os_init() in ports/sqlite3/sqlite3.c */
#define SQLITE_THREADSAFE 0
#define SQLITE_TEMP_STORE 3             /* temporary tables always in memory */
#define SQLITE_MAX_MMAP_SIZE 0
#define SQLITE_OMIT_WAL 1               /* needs shared memory */
#define SQLITE_OMIT_LOAD_EXTENSION 1
#define SQLITE_OMIT_SHARED_CACHE 1
#define SQLITE_OMIT_DEPRECATED 1
#define SQLITE_OMIT_UTF16 1
#define SQLITE_OMIT_TCL_VARIABLE 1
#define SQLITE_OMIT_PROGRESS_CALLBACK 1
#define SQLITE_DEFAULT_MEMSTATUS 0
#define SQLITE_DQS 0                    /* "text" is a name, 'text' a string */
#define SQLITE_ENABLE_MATH_FUNCTIONS 1  /* sqrt(), pi(), ... */
#define SQLITE_DEFAULT_LOCKING_MODE 1   /* nobody else uses the file */
#define HAVE_LOCALTIME_R 1
#define SQLITE_HAVE_ISNAN 1
