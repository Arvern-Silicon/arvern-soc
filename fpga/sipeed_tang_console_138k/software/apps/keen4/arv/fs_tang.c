/*----------------------------------------------------------------------------
 *          _    _           Family:    aRVern System IPs
 *         / \__/ \          File:      fs_tang.c (keen4)
 *        /   /\   \         --------------------------------------------
 *    ===/   /=========      Copyright: (c) 2026, aRVern-dev
 *      /   / RV \   \       Contact:   arvernsilicon@gmail.com
 *     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * Full license text is available in the LICENSE file at the repository root.
 *----------------------------------------------------------------------------
 * Omnispeak's file layer (ID_FS) over FatFs: every file of the game, its
 * scripts, its configuration and its saved games in the program's folder on
 * the card, the current directory (8.3 names, which FatFs matches whatever
 * their case). A handle is one of a few FatFs file objects.
 *----------------------------------------------------------------------------*/
#include <stdarg.h>
#include <string.h>
#include "ff.h"
#include "id_fs.h"
#include "id_us.h"
#include "ck_ep.h"
#include "tang.h"


#define NFILES 4

struct arv_file {
    FIL  f;
    bool used;
};

static struct arv_file files[NFILES];

static FS_File open_mode(const char *name, BYTE mode)
{
    for (int i = 0; i < NFILES; i++)
        if (!files[i].used) {
            const FRESULT r = f_open(&files[i].f, name, mode);
            if (r != FR_OK) {
                if (r != FR_NO_FILE)
                    arv_printf("fs: open %s: error %d\n", name, (int) r);
                return NULL;
            }
            files[i].used = true;
            return &files[i];
        }
    Quit("FS: too many open files");
    return NULL;
}

bool    FS_IsFileValid(FS_File file)          { return file != NULL; }
size_t  FS_GetFileSize(FS_File file)          { return (size_t) f_size(&file->f); }
FS_File FS_OpenKeenFile(const char *fileName) { return open_mode(fileName, FA_READ); }
FS_File FS_OpenOmniFile(const char *fileName) { return open_mode(fileName, FA_READ); }
FS_File FS_OpenUserFile(const char *fileName) { return open_mode(fileName, FA_READ); }
FS_File FS_CreateUserFile(const char *fileName) { return open_mode(fileName, FA_WRITE | FA_CREATE_ALWAYS); }

size_t FS_Read(void *ptr, size_t size, size_t nmemb, FS_File file)
{
    UINT n;
    FRESULT r;
    if (!size)
        return 0;
    r = f_read(&file->f, ptr, (UINT) (size * nmemb), &n);
    if (r != FR_OK) {
        arv_printf("fs: read %u bytes at %u: error %d\n", (unsigned) (size * nmemb),
                   (unsigned) f_tell(&file->f), (int) r);
        return 0;
    }
    return n / size;
}

size_t FS_Write(const void *ptr, size_t size, size_t nmemb, FS_File file)
{
    UINT n;
    if (!size || f_write(&file->f, ptr, (UINT) (size * nmemb), &n) != FR_OK)
        return 0;
    return n / size;
}

size_t FS_SeekTo(FS_File file, size_t offset)
{
    return (f_lseek(&file->f, (FSIZE_t) offset) == FR_OK) ? offset : 0;
}

void FS_CloseFile(FS_File file)
{
    if (file) {
        f_close(&file->f);
        file->used = false;
    }
}

static bool present(const char *name)
{
    FILINFO fi;
    return f_stat(name, &fi) == FR_OK;
}

bool FS_IsKeenFilePresent(const char *filename) { return present(filename); }
bool FS_IsOmniFilePresent(const char *filename) { return present(filename); }
bool FS_IsUserFilePresent(const char *filename) { return present(filename); }

/* The name with the current episode's extension; valid until the next call */
char *FS_AdjustExtension(const char *filename)
{
    static char name[16];
    size_t n = strlen(filename);
    if (n >= sizeof(name))
        n = sizeof(name) - 1u;
    memcpy(name, filename, n);
    name[n] = '\0';
    if (n >= 3u && ck_currentEpisode)
        memcpy(name + n - 3u, ck_currentEpisode->ext, 3u);
    return name;
}

void FS_Startup(void) {}

size_t FS_ReadInt8LE(void *ptr, size_t count, FS_File stream)   { return FS_Read(ptr, 1, count, stream); }
size_t FS_ReadInt16LE(void *ptr, size_t count, FS_File stream)  { return FS_Read(ptr, 2, count, stream); }
size_t FS_ReadInt32LE(void *ptr, size_t count, FS_File stream)  { return FS_Read(ptr, 4, count, stream); }
size_t FS_WriteInt8LE(const void *ptr, size_t count, FS_File stream)  { return FS_Write(ptr, 1, count, stream); }
size_t FS_WriteInt16LE(const void *ptr, size_t count, FS_File stream) { return FS_Write(ptr, 2, count, stream); }
size_t FS_WriteInt32LE(const void *ptr, size_t count, FS_File stream) { return FS_Write(ptr, 4, count, stream); }

size_t FS_ReadBoolFrom16LE(void *ptr, size_t count, FS_File stream)
{
    bool *b = (bool *) ptr;
    size_t n = 0;
    uint8_t v[2];
    for (size_t i = 0; i < count; i++)
        if (FS_Read(v, 2, 1, stream)) {
            b[i] = (v[0] | v[1]) != 0;
            n++;
        }
    return n;
}

size_t FS_WriteBoolTo16LE(const void *ptr, size_t count, FS_File stream)
{
    const bool *b = (const bool *) ptr;
    size_t n = 0;
    for (size_t i = 0; i < count; i++) {
        const uint8_t v[2] = { b[i] ? 1u : 0u, 0u };
        n += FS_Write(v, 2, 1, stream);
    }
    return n;
}

int arv_vsnprintf(char *buf, size_t size, const char *fmt, va_list ap);

int FS_PrintF(FS_File stream, const char *fmt, ...)
{
    char buf[128];
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = arv_vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n > (int) sizeof(buf) - 1)
        n = (int) sizeof(buf) - 1;
    return (int) FS_Write(buf, 1, (size_t) n, stream);
}

bool FS_LoadUserFile(const char *filename, mm_ptr_t *ptr, int *memsize)
{
    FS_File f = FS_OpenUserFile(filename);
    int length, got;
    if (!f) {
        *ptr = 0;
        if (memsize)
            *memsize = 0;
        return false;
    }
    length = (int) FS_GetFileSize(f);
    MM_GetPtr(ptr, (unsigned long) length);
    if (memsize)
        *memsize = length;
    got = (int) FS_Read(*ptr, 1, (size_t) length, f);
    FS_CloseFile(f);
    return got == length;
}
