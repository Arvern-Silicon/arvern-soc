/* newlib system calls: files on the microSD card (FatFs), heap in SRAM */
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include "ff.h"

#define NFILES 7                      /* the game keeps 4 data files open, the music and the sound bank 2 more */

static FATFS fs;
static int   mounted;
static FIL   files[NFILES];
static char  used[NFILES];
/* Fast seek (FF_USE_FASTSEEK): a read-only file gets a cluster link map, so a
 * seek (a page of VSWAP, a sound of the bank) does not walk the cluster chain
 * from the start; 12 words map a file in up to 5 fragments, a more fragmented
 * one seeks the plain way */
#define CLMT_N 12
static DWORD clmt[NFILES][CLMT_N];

extern char _heap_start[], _heap_end[];
static char *brk = _heap_start;

static int mount_card(void)
{
    if (!mounted && (f_mount(&fs, "", 1) == FR_OK)) mounted = 1;
    return mounted;
}

/* Current directory of the relative paths (the program's folder on the card) */
int arv_chdir(const char *path)
{
    return (mount_card() && (f_chdir(path) == FR_OK)) ? 0 : -1;
}

/* Heap reached by _sbrk (the high-water mark of the newlib allocator) */
char *arv_heap_brk(void) { return brk; }

void *_sbrk(ptrdiff_t incr)
{
    char *old = brk;
    if (brk + incr > _heap_end) { errno = ENOMEM; return (void *) -1; }
    brk += incr;
    return old;
}

int _open(const char *name, int flags, int mode)
{
    BYTE m = FA_READ;
    int  i;
    (void) mode;
    if (!mount_card()) { errno = EIO; return -1; }
    if ((flags & O_ACCMODE) != O_RDONLY) m = FA_WRITE | ((flags & O_ACCMODE) == O_RDWR ? FA_READ : 0);
    if (flags & O_CREAT) m |= (flags & O_TRUNC) ? FA_CREATE_ALWAYS : FA_OPEN_ALWAYS;
    for (i = 0; i < NFILES && used[i]; i++) ;
    if (i == NFILES) { errno = EMFILE; return -1; }
    if (f_open(&files[i], name, m) != FR_OK) { errno = ENOENT; return -1; }
    if (m == FA_READ) {
        clmt[i][0] = CLMT_N;
        files[i].cltbl = clmt[i];
        if (f_lseek(&files[i], CREATE_LINKMAP) != FR_OK)
            files[i].cltbl = NULL;
    }
    used[i] = 1;
    return i + 3;
}

int _close(int fd)
{
    int i = fd - 3;
    if (i < 0 || i >= NFILES || !used[i]) { errno = EBADF; return -1; }
    used[i] = 0;
    return (f_close(&files[i]) == FR_OK) ? 0 : -1;
}

/* File read statistics for a debugger: calls, bytes, machine-timer ticks */
uint32_t arv_read_calls, arv_read_bytes, arv_read_ticks;
uint32_t arv_mtime(void);

int _read(int fd, void *buf, size_t len)
{
    UINT n; int i = fd - 3;
    uint32_t t0;
    FRESULT r;
    if (i < 0 || i >= NFILES || !used[i]) { errno = EBADF; return -1; }
    t0 = arv_mtime();
    r = f_read(&files[i], buf, len, &n);
    arv_read_ticks += arv_mtime() - t0;
    arv_read_calls++;
    arv_read_bytes += n;
    return (r == FR_OK) ? (int) n : -1;
}

void arv_tty_write(const void *buf, size_t len);

int _write(int fd, const void *buf, size_t len)
{
    UINT n; int i = fd - 3;
    if (fd == 1 || fd == 2) {                                 /* stdout / stderr: the TTY */
        arv_tty_write(buf, len);
        return (int) len;
    }
    if (i < 0 || i >= NFILES || !used[i]) { errno = EBADF; return -1; }
    return (f_write(&files[i], buf, len, &n) == FR_OK) ? (int) n : -1;
}

off_t _lseek(int fd, off_t off, int whence)
{
    int i = fd - 3; FSIZE_t pos;
    if (i < 0 || i >= NFILES || !used[i]) { errno = EBADF; return -1; }
    pos = (whence == SEEK_SET) ? 0 : (whence == SEEK_CUR) ? f_tell(&files[i]) : f_size(&files[i]);
    pos += off;
    return (f_lseek(&files[i], pos) == FR_OK) ? (off_t) pos : -1;
}

int _fstat(int fd, struct stat *st) { memset(st, 0, sizeof(*st)); st->st_mode = (fd < 3) ? S_IFCHR : S_IFREG; return 0; }
int _isatty(int fd) { return fd < 3; }
int _getpid(void) { return 1; }
int _kill(int pid, int sig) { (void) pid; (void) sig; errno = EINVAL; return -1; }
void _exit(int code) { (void) code; for (;;) ; }
int _unlink(const char *name) { return (f_unlink(name) == FR_OK) ? 0 : -1; }
int _gettimeofday(void *tv, void *tz) { (void) tv; (void) tz; return -1; }

/* No real-time clock: files get a fixed time stamp (2026-01-01 00:00) */
DWORD get_fattime(void) { return ((DWORD) (2026 - 1980) << 25) | (1u << 21) | (1u << 16); }

/* stat over FatFs: the root directory always exists */
int _stat(const char *path, struct stat *st)
{
    FILINFO fi;
    char    p[64];
    size_t  n = strlen(path);
    memset(st, 0, sizeof(*st));
    if (n >= sizeof(p)) { errno = ENAMETOOLONG; return -1; }
    memcpy(p, path, n + 1);
    while ((n > 1) && (p[n - 1] == '/')) p[--n] = '\0';      /* "dir/" is "dir" */
    if ((strcmp(p, "/") == 0) || (strcmp(p, ".") == 0)) { st->st_mode = S_IFDIR; return 0; }
    if (!mount_card() || f_stat(p, &fi) != FR_OK) { errno = ENOENT; return -1; }
    st->st_mode = (fi.fattrib & AM_DIR) ? S_IFDIR : S_IFREG;
    st->st_size = (off_t) fi.fsize;
    return 0;
}
