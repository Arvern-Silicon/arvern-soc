/*----------------------------------------------------------------------------
 *          _    _           Family:    aRVern System IPs
 *         / \__/ \          File:      zork_main.c (zork)
 *        /   /\   \         --------------------------------------------
 *    ===/   /=========      Copyright: (c) 2026, aRVern-dev
 *      /   / RV \   \       Contact:   arvernsilicon@gmail.com
 *     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * Full license text is available in the LICENSE file at the repository root.
 *----------------------------------------------------------------------------
 * Zork I, II and III on the Tang: Ryan C. Gordon's MojoZork Z-machine
 * (mojozork/mojozork.c, version 3 stories, unchanged) with the console of
 * console.c. The story files (ZORKn.Z3) and the saved games (ZORKn.SAV) are
 * in the program's folder on the card; the menu at start-up picks the story.
 * MojoZork's standard input, output and files are mapped here: its fgets is
 * the console's line editor, its text goes to the console, its files to
 * FatFs (stdio_tang.c), "save.dat" being the current story's saved game.
 *----------------------------------------------------------------------------*/
#include <ctype.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "tang.h"

int   arv_vsnprintf(char *buf, size_t size, const char *fmt, va_list ap);
void *arv_fopen(const char *name, const char *mode);

static char save_name[16] = "ZORK1.SAV";
static char status_text[96];

/* MojoZork's library calls, renamed to these by the defines below */
static char *zk_fgets(char *s, int size, void *stream)
{
    (void) stream;
    if (status_text[0])                         /* the status MojoZork has just computed */
        con_status(status_text);
    const int n = con_read_line(s, size - 1);
    s[n] = '\n';
    s[n + 1] = '\0';
    return s;
}

static void *zk_fopen(const char *name, const char *mode)
{
    return arv_fopen(strcmp(name, "save.dat") == 0 ? save_name : name, mode);
}

#define fgets(s, n, f)  zk_fgets(s, n, f)
#define fopen(n, m)     zk_fopen(n, m)
#define printf          arv_printf
#define snprintf        arv_snprintf
#define fread           arv_fread
#define fwrite          arv_fwrite
#define fclose          arv_fclose
#define fseek           arv_fseek
#define ftell           arv_ftell
#define rewind          arv_rewind
#define time(t)         ((long) arv_mtime())
int arv_snprintf(char *buf, size_t size, const char *fmt, ...);
unsigned arv_fread(void *buf, unsigned size, unsigned n, void *f);
unsigned arv_fwrite(const void *buf, unsigned size, unsigned n, void *f);
int arv_fclose(void *f);
int arv_fseek(void *f, long off, int whence);
long arv_ftell(void *f);
void arv_rewind(void *f);

#define MOJOZORK_LIBRETRO 1                     /* MojoZork's own main left out */
#include "mojozork.c"
#undef fgets
#undef fopen
#undef printf
#undef snprintf

/*------------------------------- the platform -------------------------------*/

static void zk_die(const char *fmt, ...) __attribute__((noreturn));
static void zk_die(const char *fmt, ...)
{
    char buf[160];
    va_list ap;
    va_start(ap, fmt);
    arv_vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    arv_printf("zork: error: %s (pc=%x)\n", buf, (unsigned) GState->logical_pc);
    con_write("\n*** ", 5);
    con_write(buf, strlen(buf));
    con_write("\n", 1);
    arv_halt();
}

static void zk_writestr(const char *str, const uintptr slen)
{
    con_write(str, (size_t) slen);
}

int zork_main(void)
{
    static ZMachineState zmachine_state;
    static const char *const games[] = {
        "Zork I: The Great Underground Empire",
        "Zork II: The Wizard of Frobozz",
        "Zork III: The Dungeon Master",
    };
    static const char *const files[] = { "ZORK1.Z3", "ZORK2.Z3", "ZORK3.Z3" };
    con_init();
    static const char intro[] = "ZORK - Infocom, 1980-1982. MojoZork by Ryan C. Gordon.\n\nWhich game?\n\n";
    con_write(intro, sizeof(intro) - 1);
    const int g = con_menu(games, 3);
    save_name[4] = (char) ('1' + g);
    con_clear();

    GState = &zmachine_state;
    GState->startup_script = NULL;
    GState->die = zk_die;
    GState->writestr = zk_writestr;
    GState->status_bar = status_text;
    GState->status_bar_len = (arv_frame_w / 8u) + 1u;
    GState->status_bar_enabled = 1;
    random_seed = (int) arv_mtime();
    loadStory(files[g]);
    arv_printf("zork: %s loaded\n", files[g]);
    while (!GState->quit) {
        runInstruction();
        if (GState->step_completed) {
            GState->step_completed = 0;
        }
    }
    con_write("\n[The End]\n", sizeof("\n[The End]\n") - 1);
    arv_puts("zork: quit\n");
    for (;;)
        arv_board_poll();
}
