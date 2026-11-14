/*
SDLPoP, a port/conversion of the DOS game Prince of Persia.
Copyright (C) 2013-2025  Dávid Nagy

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <https://www.gnu.org/licenses/>.

The authors of this program may be contacted at https://forum.princed.org
*/

#include "common.h"
#include "digi_resample.h"
#include <time.h>
#include <errno.h>
#include <strings.h>

#ifdef _WIN32
#include <windows.h>
#include <wchar.h>
#else
// #include "dirent.h"
#endif

#ifdef __amigaos4__
#include <workbench/startup.h>
#include <proto/dos.h>
#endif

// Most functions in this file are different from those in the original game.

void sdlperror(const char* header) {
	const char* error = SDL_GetError();
	printf("%s: %s\n",header,error);
	//quit(1);
}

#if 0
char exe_dir[POP_MAX_PATH] = ".";
bool found_exe_dir = false;
#if ! (defined WIN32 || _WIN32 || WIN64 || _WIN64)
char home_dir[POP_MAX_PATH];
bool found_home_dir = false;
char share_dir[POP_MAX_PATH];
bool found_share_dir = false;
#endif

void find_exe_dir(void) {
	if (found_exe_dir) return;
#ifdef __amigaos4__
	if(g_argc == 0) { // from Workbench
		struct WBStartup *WBenchMsg = (struct WBStartup *)g_argv;
		NameFromLock( WBenchMsg->sm_ArgList->wa_Lock, exe_dir, sizeof(exe_dir) );
	}
	else { // from Shell/CLI
		NameFromLock( GetProgramDir(), exe_dir, sizeof(exe_dir) );
	}
#else
	snprintf_check(exe_dir, sizeof(exe_dir), "%s", g_argv[0]);
	char* last_slash = NULL;
	char* pos = exe_dir;
	for (char c = *pos; c != '\0'; ++pos, c = *pos) {
		if (c == '/' || c == '\\') {
			last_slash = pos;
		}
	}
	if (last_slash != NULL) {
		*last_slash = '\0';
	}
#endif
	found_exe_dir = true;
}

#if ! (defined WIN32 || _WIN32 || WIN64 || _WIN64)
void find_home_dir(void) {
	if (found_home_dir) return;
	const char* home_path = getenv("HOME");
	snprintf_check(home_dir, POP_MAX_PATH - 1, "%s/.%s", home_path, POP_DIR_NAME);
	if(file_exists(home_dir))
		found_home_dir = true;
}

void find_share_dir(void) {
	if (found_share_dir) return;
	snprintf_check(share_dir, POP_MAX_PATH - 1, "%s/%s", SHARE_PATH, POP_DIR_NAME);
	if(file_exists(share_dir))
		found_share_dir = true;
}
#endif

bool file_exists(const char* filename) {
	return (vfs_access(filename, F_OK) != -1);
}

const char* find_first_file_match(char* dst, int size, char* format, const char* filename) {
	find_exe_dir();
#if defined WIN32 || _WIN32 || WIN64 || _WIN64
	snprintf_check(dst, size, format, exe_dir, filename);
#else
	find_home_dir();
	find_share_dir();
	char* dirs[3] = {home_dir, share_dir, exe_dir};
	for (int i = 0; i < 3; i++) {
		snprintf_check(dst, size, format, dirs[i], filename);
		if(file_exists(dst))
			break;
	}
#endif
	return (const char*) dst;
}

const char* locate_save_file_(const char* filename, char* dst, int size) {
	find_exe_dir();
#if defined WIN32 || _WIN32 || WIN64 || _WIN64
	snprintf_check(dst, size, "%s/%s", exe_dir, filename);
#else
	find_home_dir();
	find_share_dir();
	char* dirs[3] = {home_dir, share_dir, exe_dir};
	for (int i = 0; i < 3; i++) {
		struct stat path_stat;
		int result = vfs_stat(dirs[i], &path_stat);
		if (result == 0 && S_ISDIR(path_stat.st_mode) && vfs_access(dirs[i], W_OK) == 0) {
			snprintf_check(dst, size, "%s/%s", dirs[i], filename);
			break;
		}
	}
#endif
	return (const char*) dst;
}
const char* locate_file_(const char* filename, char* path_buffer, int buffer_size) {
	if(file_exists(filename)) {
		return filename;
	} else {
		// If failed, it may be that SDLPoP is being run from the wrong different working directory.
		// We can try to rescue the situation by loading from the directory of the executable.
		return find_first_file_match(path_buffer, buffer_size, "%s/%s", filename);
	}
}

#ifdef _WIN32
// These macros are from the SDL2 source. (src/core/windows/SDL_windows.h)
// The pointers returned by these macros must be freed with SDL_free().
#define WIN_StringToUTF8(S) SDL_iconv_string("UTF-8", "UTF-16LE", (char *)(S), (SDL_wcslen(S)+1)*sizeof(WCHAR))
#define WIN_UTF8ToString(S) (WCHAR *)SDL_iconv_string("UTF-16LE", "UTF-8", (char *)(S), SDL_strlen(S)+1)

// This hack is needed because SDL uses UTF-8 everywhere (even in argv!), but fopen on Windows uses whatever code page is currently set.
FILE* fopen_UTF8(const char* filename_UTF8, const char* mode_UTF8) {
	WCHAR* filename_UTF16 = WIN_UTF8ToString(filename_UTF8);
	WCHAR* mode_UTF16 = WIN_UTF8ToString(mode_UTF8);
	FILE* result = _wfopen(filename_UTF16, mode_UTF16);
	SDL_free(mode_UTF16);
	SDL_free(filename_UTF16);
	return result;
}

int chdir_UTF8(const char* path_UTF8) {
	WCHAR* path_UTF16 = WIN_UTF8ToString(path_UTF8);
	int result = _wchdir(path_UTF16);
	SDL_free(path_UTF16);
	return result;
}

int mkdir_UTF8(const char* path_UTF8) {
	WCHAR* path_UTF16 = WIN_UTF8ToString(path_UTF8);
	int result = _wmkdir(path_UTF16);
	SDL_free(path_UTF16);
	return result;
}

int access_UTF8(const char* filename_UTF8, int mode) {
	WCHAR* filename_UTF16 = WIN_UTF8ToString(filename_UTF8);
	int result = _waccess(filename_UTF16, mode);
	SDL_free(filename_UTF16);
	return result;
}

int stat_UTF8(const char *filename_UTF8, struct stat *_Stat) {
	WCHAR* filename_UTF16 = WIN_UTF8ToString(filename_UTF8);
#ifdef _MSC_VER
	int result = _wstat(filename_UTF16, _Stat);
#else
	// There is a _wstat() function on Dev-C++ as well, but it expects the second argument to be a different type than stat().
	int result = wstat(filename_UTF16, _Stat);
#endif
	SDL_free(filename_UTF16);
	return result;
}

#endif //_WIN32

#ifdef USE_OPTIONS
// OS abstraction for listing directory contents
// Directory listing using dirent.h is available using MinGW on Windows, but not using MSVC (need to use Win32 API).
// - Under GNU/Linux, etc (or if compiling with MinGW on Windows), we can use dirent.h
// - Under Windows, we'd like to directly call the Win32 API. (Note: MSVC does not include dirent.h)
// NOTE: If we are using MinGW, we'll opt to use the Win32 API as well: dirent.h would just wrap Win32 anyway!

#ifdef _WIN32
struct directory_listing_type {
	WIN32_FIND_DATAW find_data;
	HANDLE search_handle;
	char* current_filename_UTF8;
};

directory_listing_type* create_directory_listing_and_find_first_file(const char* directory, const char* extension) {
	directory_listing_type* directory_listing = calloc(1, sizeof(directory_listing_type));
	char search_pattern[POP_MAX_PATH];
	snprintf_check(search_pattern, POP_MAX_PATH, "%s/*.%s", directory, extension);
	WCHAR* search_pattern_UTF16 = WIN_UTF8ToString(search_pattern);
	directory_listing->search_handle = FindFirstFileW( search_pattern_UTF16, &directory_listing->find_data );
	SDL_free(search_pattern_UTF16);
	if (directory_listing->search_handle != INVALID_HANDLE_VALUE) {
		return directory_listing;
	} else {
		free(directory_listing);
		return NULL;
	}
}

char* get_current_filename_from_directory_listing(directory_listing_type* data) {
	SDL_free(data->current_filename_UTF8);
	data->current_filename_UTF8 = NULL;
	data->current_filename_UTF8 = WIN_StringToUTF8(data->find_data.cFileName);
	return data->current_filename_UTF8;
}

bool find_next_file(directory_listing_type* data) {
	return (bool) FindNextFileW( data->search_handle, &data->find_data );
}

void close_directory_listing(directory_listing_type* data) {
	FindClose(data->search_handle);
	SDL_free(data->current_filename_UTF8);
	data->current_filename_UTF8 = NULL;
	free(data);
}

#else // use dirent.h API for listing files

struct directory_listing_type {
	DIR* dp;
	char* found_filename;
	const char* extension;
};

directory_listing_type* create_directory_listing_and_find_first_file(const char* directory, const char* extension) {
	directory_listing_type* data = calloc(1, sizeof(directory_listing_type));
	bool ok = false;
	data->dp = vfs_opendir(directory);
	if (data->dp != NULL) {
		struct dirent* ep;
		while ((ep = vfs_readdir(data->dp))) {
			char *ext = strrchr(ep->d_name, '.');
			if (ext != NULL && strcasecmp(ext+1, extension) == 0) {
				data->found_filename = ep->d_name;
				data->extension = extension;
				ok = true;
				break;
			}
		}
	}
	if (ok) {
		return data;
	} else {
		free(data);
		return NULL;
	}
}

char* get_current_filename_from_directory_listing(directory_listing_type* data) {
	return data->found_filename;
}

bool find_next_file(directory_listing_type* data) {
	bool ok = false;
	struct dirent* ep;
	while ((ep = vfs_readdir(data->dp))) {
		char *ext = strrchr(ep->d_name, '.');
		if (ext != NULL && strcasecmp(ext+1, data->extension) == 0) {
			data->found_filename = ep->d_name;
			ok = true;
			break;
		}
	}
	return ok;
}

void close_directory_listing(directory_listing_type *data) {
	vfs_closedir(data->dp);
	free(data);
}

#endif //_WIN32
#endif //USE_OPTIONS

#endif

dat_type* dat_chain_ptr = NULL;

char last_text_input;

#ifdef PICOPOP
/* Non-zero while do_paused() is blocked waiting for a key. Lets the controller
 * handler resume rather than re-pause; see the note at its use. */
int picopop_pause_wait;
#endif

// seg009:000D
int read_key() {
	// stub
	int key = last_key_scancode;
	last_key_scancode = 0;
	return key;
}

// seg009:019A
void clear_kbd_buf() {
	// stub
	last_key_scancode = 0;
	last_text_input = 0;
}

// seg009:040A
word prandom(word max) {
	if (!seed_was_init) {
		// init from current time
		random_seed = (dword)time(NULL);
		seed_was_init = 1;
	}
	random_seed = random_seed * 214013 + 2531011;
	return (random_seed >> 16) % (max + 1);
}

// seg009:0467
int round_xpos_to_byte(int xpos,int round_direction) {
	// stub
	return xpos;
}

// seg009:0C7A
void quit(int exit_code) {
	restore_stuff();
	exit(exit_code);
}

// seg009:0C90
void restore_stuff() {
	SDL_Quit();
}

// seg009:0E33
int key_test_quit() {
	word key = read_key();
	if (key == (SDL_SCANCODE_Q | WITH_CTRL)) { // Ctrl+Q

		#ifdef USE_REPLAY
		if (recording) save_recorded_replay_dialog();
		#endif
		#ifdef USE_MENU
		if (is_menu_shown) menu_was_closed();
		#endif

		quit(0);
	}
	return key;
}

// seg009:0E54
const char* check_param(const char* param) {
	// stub
	for (short arg_index = 1; arg_index < g_argc; ++arg_index) {

		char* curr_arg = g_argv[arg_index];

		// Filenames (e.g. replays) should never be a valid 'normal' param so we should skip these to prevent conflicts.
		// We can lazily distinguish filenames from non-filenames by checking whether they have a dot in them.
		// (Assumption: all relevant files, e.g. replay files, have some file extension anyway)
		if (strchr(curr_arg, '.') != NULL) {
			continue;
		}

		// List of params that expect a specifier ('sub-') arg directly after it (e.g. the mod's name, after "mod" arg)
		// Such sub-args may conflict with the normal params (so, we should 'skip over' them)
		static const char params_with_one_subparam[][16] = { "mod", "validate", /*...*/ };

		bool curr_arg_has_one_subparam = false;
		for (int i = 0; i < COUNT(params_with_one_subparam); ++i) {
			if (strncasecmp(curr_arg, params_with_one_subparam[i], strlen(params_with_one_subparam[i])) == 0) {
				curr_arg_has_one_subparam = true;
				break;
			}
		}

		if (curr_arg_has_one_subparam) {
			// Found an arg that has one sub-param, so we want to:
			// 1: skip over the next arg                (if we are NOT checking for this specific param)
			// 2: return a pointer below to the SUB-arg (if we ARE checking for this specific param)
			++arg_index;
			if (!(arg_index < g_argc)) return NULL; // not enough arguments
		}

		if (/*strnicmp*/strncasecmp(curr_arg, param, strlen(param)) == 0) {
			return g_argv[arg_index];
		}
	}
	return NULL;
}

// seg009:0EDF
int pop_wait(int timer_index,int time) {
	start_timer(timer_index, time);
	return do_wait(timer_index);
}

int showmessage(char* text,int arg_4,void* arg_0);

// seg009:0F58
/*
 * The open-DAT chain.
 *
 * A static pool, not the heap. The chain is a stack - open_dat pushes,
 * close_dat pops - and it never gets deep: the game holds PRINCE.DAT open
 * while loading a level, which opens the tile set and then a guard DAT on top
 * of that. Eight is several times what has ever been observed; running out is
 * a bug in the caller, not a condition to recover from, so it says so.
 */
#define POP_MAX_OPEN_DATS 8
static dat_type s_dat_pool[POP_MAX_OPEN_DATS];
static byte     s_dat_used[POP_MAX_OPEN_DATS];

static dat_type* alloc_dat(void) {
	for (int i = 0; i < POP_MAX_OPEN_DATS; ++i) {
		if (!s_dat_used[i]) {
			s_dat_used[i] = 1;
			memset(&s_dat_pool[i], 0, sizeof(s_dat_pool[i]));
			return &s_dat_pool[i];
		}
	}
	printf("open_dat: more than %d DAT files open at once\n", POP_MAX_OPEN_DATS);
	quit(1);
	return NULL;
}

static void release_dat(dat_type* d) {
	for (int i = 0; i < POP_MAX_OPEN_DATS; ++i)
		if (&s_dat_pool[i] == d) { s_dat_used[i] = 0; return; }
}

dat_type* open_dat(const char* filename, int optional __attribute__((unused))) {
#ifdef ARV_TANG
	/* The pack of that DAT on the card (arv/pak.c) */
	struct datfile_s *df = pak_open(filename);
	if (df == NULL) {
		return NULL;
	}
	dat_type* pointer = alloc_dat();
	pointer->df = df;
	pointer->next_dat = dat_chain_ptr;
	dat_chain_ptr = pointer;
	return pointer;
#else
	for (unsigned i = 0; i < datfiles_count; i++) {
		const struct datfile_s *df = &datfiles[i];
		if (0 == strcmp(filename, df->filename)) {
			/* found */
			dat_type* pointer = alloc_dat();
			pointer->df = df;
			pointer->next_dat = dat_chain_ptr;
			dat_chain_ptr = pointer;
			return pointer;
		}
	}
	return NULL;
#endif
}

// seg009:9CAC
void set_loaded_palette(dat_pal_type* palette_ptr) {
	int dest_row, dest_index, source_row;
	for (dest_row = dest_index = source_row = 0; dest_row < 16; ++dest_row, dest_index += 0x10) {
		if (palette_ptr->row_bits & (1 << dest_row)) {
			set_pal_arr(dest_index, 16, palette_ptr->vga + source_row*0x10);
			++source_row;
		}
	}
}

/*
 * The game's shadow copy of the palette, in the DAT's 6-bit form.
 *
 * The CLUT itself lives in picosdl and is the display hardware's; this array
 * exists because read_palette_256(), set_bg_attr() and the fade buffers all
 * read palette entries back. Declared here rather than beside set_pal() so it
 * is in scope for the sprite loader above.
 */
rgb_type palette[256];

/*
 * Widening the DAT's 6-bit colour components to the 8-bit ones SDL_Color and
 * the display CLUT use.
 *
 * `v << 2` is the obvious conversion and is what this used to do, but it is
 * wrong at the top of the range: it maps 63 to 252 rather than 255, and the
 * panel driver then quantises to RGB565 as `v * 31 / 255`, so full-scale white
 * arrives as 30 of 31 and 62 of 63. Every colour is slightly dark and pure
 * white is never pure.
 *
 * Replicating the high bits into the low ones is the standard fix and is
 * exact at both ends: 0 stays 0 and 63 becomes 255.
 */
static inline Uint8 pal6_to_8(int v)      { return (Uint8)((v << 2) | (v >> 4)); }

/* The generated sprite palettes in resources.c are already stored multiplied
 * by four, so they need the same replication applied to the widened value. */
static inline Uint8 pal8x4_to_8(int v)    { return (Uint8)(v | (v >> 6)); }

// data:3356
word chtab_palette_bits = 1;

/* Glyphs in the built-in font: hc_font_data[] covers 0x20..0x83. */
#define POP_MAX_FONT_CHARS 128

/*
 * The chtab pool.
 *
 * There are ten chtab slots in the game (chtab_addrs[]) plus the font, and a
 * chtab is now a fixed-size header, so they come from a static array rather
 * than the heap. the no-libm / no-heap rules in TODO.md: the firmware links with no allocator at
 * all, and this was one of the sites keeping one alive.
 */
#define POP_MAX_CHTABS 12
static chtab_type s_chtab_pool[POP_MAX_CHTABS];
static byte       s_chtab_used[POP_MAX_CHTABS];

static chtab_type* alloc_chtab(void) {
	for (int i = 0; i < POP_MAX_CHTABS; ++i) {
		if (!s_chtab_used[i]) {
			s_chtab_used[i] = 1;
			memset(&s_chtab_pool[i], 0, sizeof(s_chtab_pool[i]));
			return &s_chtab_pool[i];
		}
	}
	printf("out of chtab slots (max %d)\n", POP_MAX_CHTABS);
	quit(1);
	return NULL;
}

static void release_chtab(chtab_type* chtab) {
	for (int i = 0; i < POP_MAX_CHTABS; ++i) {
		if (&s_chtab_pool[i] == chtab) {
			s_chtab_used[i] = 0;
			return;
		}
	}
}

/*
 * Give a chtab a writable copy of its image table.
 *
 * Only the environment chtab needs this, and only because load_more_opt_graf()
 * overlays extra tiles onto it at level load. Everything else points straight
 * at flash and stays there.
 */
#define POP_PATCHABLE_IMAGES 192
static const SDL_Surface *s_patchable_images[POP_PATCHABLE_IMAGES];

static void chtab_make_patchable(chtab_type* chtab) {
	if (chtab == NULL) return;

	/*
	 * Is this chtab already using the writable copy?
	 *
	 * Ask the chtab, not a remembered pointer. This used to keep a
	 * `s_patchable_owner` chtab_type* and return early when it matched, which is
	 * wrong because chtab headers come from a recycled pool: release_chtab()
	 * clears s_chtab_used[i] and the next alloc_chtab() hands out the same
	 * address. Restarting the attract demo frees every chtab and reloads them, so
	 * the new environment chtab landed on the old one's slot, compared equal to
	 * the stale owner, skipped the copy - and kept pointing at the image table in
	 * flash. The next patch wrote there: a segfault on the host, and on the board
	 * a write to XIP.
	 *
	 * `chtab->images` is the state that actually answers the question, and
	 * alloc_chtab() memsets it, so a recycled slot cannot inherit a stale answer.
	 */
	if (chtab->images == s_patchable_images) return;

	if (chtab->n_images > POP_PATCHABLE_IMAGES) {
		printf("chtab has %d images, only %d are patchable\n",
		       chtab->n_images, POP_PATCHABLE_IMAGES);
		quit(1);
	}

	/*
	 * There is one copy, so only one chtab can hold it. Only the environment
	 * chtab is ever patched (load_more_opt_graf), so this should not fire - but
	 * silently handing the array to a second chtab would leave the first pointing
	 * at someone else's tiles, which is a far worse bug to chase than a message.
	 */
	for (int i = 0; i < POP_MAX_CHTABS; ++i) {
		if (s_chtab_used[i] && &s_chtab_pool[i] != chtab &&
		    s_chtab_pool[i].images == s_patchable_images) {
			printf("two live chtabs need patchable images; only one copy exists\n");
			quit(1);
		}
	}

	for (int i = 0; i < chtab->n_images; ++i)
		s_patchable_images[i] = chtab->images[i];
	chtab->images = s_patchable_images;
}

void chtab_patch_image(chtab_type* chtab, int index, const SDL_Surface* image) {
	if (chtab == NULL || image == NULL) return;
	if (index < 0 || index >= chtab->n_images) return;
	chtab_make_patchable(chtab);
#ifdef ARV_TANG
	pak_free_image(chtab->images[index]);
#endif
	chtab->images[index] = image;
}

#ifdef ARV_TANG
/*
 * A resource's entry in the open packs, searched most-recent-first as the
 * DAT chain is; *df_out says in which pack.
 */
static const struct pak_entry* find_entry(int resource_id, const struct datfile_s **df_out) {
	for (dat_type* pointer = dat_chain_ptr; pointer != NULL; pointer = pointer->next_dat) {
		const struct pak_entry *e = pak_find(pointer->df, resource_id);
		if (e != NULL) {
			if (df_out) *df_out = pointer->df;
			return e;
		}
	}
	return NULL;
}

/* An image of the open packs as a lazy 4-bit surface, its own row */
const SDL_Surface* find_resource_surface(int resource_id) {
	const struct datfile_s *df;
	const struct pak_entry *e = find_entry(resource_id, &df);
	if (e == NULL || e->kind != 1) return NULL;
	return pak_image(df, e, e->row);
}

/* Write a sprite set's sixteen colours (6-bit, from its shpl) into its row */
static void load_set_palette_6(int row, const rgb_type *pal6) {
	SDL_Color colors[16];
	for (int i = 0; i < 16; ++i) {
		colors[i].r = pal6_to_8(pal6[i].r);
		colors[i].g = pal6_to_8(pal6[i].g);
		colors[i].b = pal6_to_8(pal6[i].b);
		colors[i].a = SDL_ALPHA_OPAQUE;
		palette[row * 16 + i] = pal6[i];
	}
	colors[0].r = colors[0].g = colors[0].b = 0;
	colors[0].a = SDL_ALPHA_TRANSPARENT;
	SDL_SetPaletteColors(PSDL_GlobalPalette(), colors, row * 16, 16);
}

/*
 * Load a sprite set: its shpl (the image count and the sixteen colours) and
 * a lazy surface for each image that follows it in the open packs; the
 * pixels are read from the card when first drawn (arv/pak.c).
 */
chtab_type* load_sprites_from_file(int resource, int palette_bits, int quit_on_error) {
	dat_shpl_type shpl;
	const struct datfile_s *df;
	const struct pak_entry *e = find_entry(resource, &df);
	if (e == NULL || pak_read(df, e, &shpl, sizeof(shpl)) < 3) {
		printf("Can't load sprites from resource %d.\n", resource);
		if (quit_on_error) quit(1);
		return NULL;
	}
	const int row = palette_bits ? find_first_pal_row(palette_bits) : 0;
	const int n = shpl.n_images;
	chtab_type* chtab = alloc_chtab();
	chtab->n_images = (word) n;
	chtab->images = calloc((size_t) n + 1, sizeof(SDL_Surface *));
	chtab->palette_row = (unsigned char) row;
	chtab->chtab_palette_bits = (word) palette_bits;
	chtab->has_palette_bits = (palette_bits != 0);
	for (int i = 0; i < n; ++i) {
		const struct datfile_s *idf;
		const struct pak_entry *ie = find_entry(resource + 1 + i, &idf);
		chtab->images[i] = (ie != NULL && ie->kind == 1) ? pak_image(idf, ie, row) : NULL;
	}
	if (graphics_mode == gmMcgaVga && palette_bits != 0) {
		chtab_palette_bits |= palette_bits;
		load_set_palette_6(row, shpl.palette.vga);
	}
	return chtab;
}

// seg009:11A8
void free_chtab(chtab_type *chtab_ptr) {
	if (chtab_ptr == NULL) return;
	if (graphics_mode == gmMcgaVga && chtab_ptr->has_palette_bits) {
		chtab_palette_bits &= ~ chtab_ptr->chtab_palette_bits;
	}
	if (chtab_ptr->images != NULL) {
		for (int i = 0; i < chtab_ptr->n_images; ++i)
			pak_free_image(chtab_ptr->images[i]);
		if (chtab_ptr->images != s_patchable_images)
			free(chtab_ptr->images);
	}
	release_chtab(chtab_ptr);
}
#else
/*
 * Find the generated sprite set for a shpl resource, searching the open DAT
 * chain most-recent-first, exactly as load_from_opendats_const() would.
 *
 * Walking the chain rather than using just its head matters for the guards:
 * seg000.c opens GUARD1.DAT or GUARD2.DAT and *then* asks for resource 750
 * from GUARD.DAT, so the head is GUARD.DAT - which has no set of its own,
 * because the images are shared and only the palette differs. The chain then
 * yields the GUARD1/GUARD2 entry, which is the one carrying that palette.
 */
static const struct sprite_set_s* find_sprite_set(int shpl_id) {
	for (dat_type* dat = dat_chain_ptr; dat != NULL; dat = dat->next_dat) {
		if (dat->df == NULL) continue;
		const struct sprite_set_s* set =
			resources_find_sprite_set(dat->df->filename, shpl_id);
		if (set != NULL) return set;
	}
	return NULL;
}

/*
 * The converted surface for a resource id, or NULL if that resource was not
 * converted to an image. Searches the open DAT chain, as
 * load_from_opendats_metadata() does.
 */
const struct resource_s* find_resource(int resource_id) {
	for (dat_type* pointer = dat_chain_ptr; pointer != NULL; pointer = pointer->next_dat) {
		const struct datfile_s *df = pointer->df;
		if (df == NULL) continue;
		for (unsigned i = 0; i < df->resources_count; i++) {
			const struct resource_s *r = df->resources[i];
			if (resource_id == r->resource_id)
				return r;
		}
	}
	return NULL;
}

const SDL_Surface* find_resource_surface(int resource_id) {
	const struct resource_s* r = find_resource(resource_id);
	return r ? r->surface : NULL;
}

/* Write a sprite set's sixteen colours into its row of the global CLUT. */
static void load_set_palette(const struct sprite_set_s* set) {
	SDL_Color colors[16];
	for (int i = 0; i < 16; ++i) {
		colors[i].r = pal8x4_to_8(set->palette[i][0]);
		colors[i].g = pal8x4_to_8(set->palette[i][1]);
		colors[i].b = pal8x4_to_8(set->palette[i][2]);
		colors[i].a = SDL_ALPHA_OPAQUE;
	}
	/* Index row*16 is this set's transparent colour, and is drawn as the
	 * background rather than as a colour of its own - the same rule the
	 * original applied to colour 0 of every shpl. */
	colors[0].r = colors[0].g = colors[0].b = 0;
	colors[0].a = SDL_ALPHA_TRANSPARENT;

	if (SDL_SetPaletteColors(PSDL_GlobalPalette(), colors,
	                         set->palette_row * 16, 16) != 0) {
		sdlperror("load_set_palette: SDL_SetPaletteColors");
		quit(1);
	}

	/* Keep the game's shadow copy in step. It is 6-bit, as the DAT was. */
	for (int i = 0; i < 16; ++i) {
		palette[set->palette_row * 16 + i].r = set->palette[i][0] >> 2;
		palette[set->palette_row * 16 + i].g = set->palette[i][1] >> 2;
		palette[set->palette_row * 16 + i].b = set->palette[i][2] >> 2;
	}
}

// seg009:104E
/*
 * Load a sprite set.
 *
 * No decoding, no allocation, no copying: the images were converted at build
 * time into 8bpp indexed surfaces with this set's palette row already added to
 * every pixel, and they live in flash. All that remains is to point a chtab at
 * them and write the set's sixteen colours into the CLUT.
 */
chtab_type* load_sprites_from_file(int resource, int palette_bits, int quit_on_error) {
	const struct sprite_set_s* set = find_sprite_set(resource);
	if (set == NULL) {
		printf("Can't load sprites from resource %d.\n", resource);
		if (quit_on_error) {
			char error_message[256];
			snprintf_check(error_message, sizeof(error_message),
				"Can't load sprites from resource %d.\nThe last opened data file is: %s\nPress any key to quit.",
				resource,
				dat_chain_ptr && dat_chain_ptr->df ? dat_chain_ptr->df->filename : "(none)");
			showmessage(error_message, 1, &key_test_quit);
			quit(1);
		}
		return NULL;
	}

	/*
	 * The converter derived palette_row from this very call's palette_bits,
	 * so the two must agree. If they ever do not, the sprites would be drawn
	 * against someone else's sixteen colours - which is exactly the kind of
	 * bug that looks like a corrupt sprite rather than a palette mistake.
	 */
	if (graphics_mode == gmMcgaVga && palette_bits != 0 &&
	    palette_bits != (1 << set->palette_row)) {
		printf("sprite set %s:%d has palette row %d but was asked for bits %#x\n",
		       set->datfile, resource, set->palette_row, palette_bits);
		quit(1);
	}

	chtab_type* chtab = alloc_chtab();
	chtab->n_images          = (word) set->n_images;
	/* Points into flash; only chtab_make_patchable() ever makes it writable. */
	chtab->images            = (const SDL_Surface **) set->images;
	chtab->palette_row       = set->palette_row;
	chtab->chtab_palette_bits = (word) palette_bits;
	chtab->has_palette_bits  = (palette_bits != 0);

	if (graphics_mode == gmMcgaVga && palette_bits != 0) {
		chtab_palette_bits |= palette_bits;
	}
	load_set_palette(set);
	return chtab;
}

// seg009:11A8
void free_chtab(chtab_type *chtab_ptr) {
	if (chtab_ptr == NULL) return;
	if (graphics_mode == gmMcgaVga && chtab_ptr->has_palette_bits) {
		chtab_palette_bits &= ~ chtab_ptr->chtab_palette_bits;
	}
	/* The images are in flash and are not owned; there is nothing to free
	 * but the header itself. */
	release_chtab(chtab_ptr);
}

#endif

// seg009:8CE6
void decompress_rle_lr(byte* destination,const byte* source,int dest_length) {
	const byte* src_pos = source;
	byte* dest_pos = destination;
	short rem_length = dest_length;
	while (rem_length) {
		sbyte count = *src_pos;
		src_pos++;
		if (count >= 0) { // copy
			++count;
			do {
				*dest_pos = *src_pos;
				dest_pos++;
				src_pos++;
				--rem_length;
				--count;
			} while (count && rem_length);
		} else { // repeat
			byte al = *src_pos;
			src_pos++;
			count = -count;
			do {
				*dest_pos = al;
				dest_pos++;
				--rem_length;
				--count;
			} while (count && rem_length);
		}
	}
}

// seg009:8D1C
void decompress_rle_ud(byte* destination,const byte* source,int dest_length,int width,int height) {
	short rem_height = height;
	const byte* src_pos = source;
	byte* dest_pos = destination;
	short rem_length = dest_length;
	--dest_length;
	--width;
	while (rem_length) {
		sbyte count = *src_pos;
		src_pos++;
		if (count >= 0) { // copy
			++count;
			do {
				*dest_pos = *src_pos;
				dest_pos++;
				src_pos++;
				dest_pos += width;
				--rem_height;
				if (rem_height == 0) {
					dest_pos -= dest_length;
					rem_height = height;
				}
				--rem_length;
				--count;
			} while (count && rem_length);
		} else { // repeat
			byte al = *src_pos;
			src_pos++;
			count = -count;
			do {
				*dest_pos = al;
				dest_pos++;
				dest_pos += width;
				--rem_height;
				if (rem_height == 0) {
					dest_pos -= dest_length;
					rem_height = height;
				}
				--rem_length;
				--count;
			} while (count && rem_length);
		}
	}
}

// seg009:90FA
byte* decompress_lzg_lr(byte* dest,const byte* source,int dest_length) {
	/* The LZG sliding window. Static: this is the last runtime image decode
	 * left (the built-in font, via decompr_img), it runs on core 0 at start-up,
	 * and the two decompressors are never active at once. */
	static byte window[0x400];
	memset(window, 0, 0x400);
	byte* window_pos = window + 0x400 - 0x42; // bx
	short remaining = dest_length; // cx
	byte* window_end = window + 0x400; // dx
	const byte* source_pos = source;
	byte* dest_pos = dest;
	word mask = 0;
	do {
		mask >>= 1;
		if ((mask & 0xFF00) == 0) {
			mask = *source_pos | 0xFF00;
			source_pos++;
		}
		if (mask & 1) {
			*window_pos = *dest_pos = *source_pos;
			window_pos++;
			dest_pos++;
			source_pos++;
			if (window_pos >= window_end) window_pos = window;
			--remaining;
		} else {
			word copy_info = *source_pos;
			source_pos++;
			copy_info = (copy_info << 8) | *source_pos;
			source_pos++;
			byte* copy_source = window + (copy_info & 0x3FF);
			byte copy_length = (copy_info >> 10) + 3;
			do {
				*window_pos = *dest_pos = *copy_source;
				window_pos++;
				dest_pos++;
				copy_source++;
				if (copy_source >= window_end) copy_source = window;
				if (window_pos >= window_end) window_pos = window;
				--remaining;
				--copy_length;
			} while (remaining && copy_length);
		}
	} while (remaining);
//	end:
	return dest;
}

// seg009:91AD
byte* decompress_lzg_ud(byte* dest,const byte* source,int dest_length,int stride,int height) {
	/* The LZG sliding window. Static: this is the last runtime image decode
	 * left (the built-in font, via decompr_img), it runs on core 0 at start-up,
	 * and the two decompressors are never active at once. */
	static byte window[0x400];
	memset(window, 0, 0x400);
	byte* window_pos = window + 0x400 - 0x42; // bx
	short remaining = height; // cx
	byte* window_end = window + 0x400; // dx
	const byte* source_pos = source;
	byte* dest_pos = dest;
	word mask = 0;
	short dest_end = dest_length - 1;
	do {
		mask >>= 1;
		if ((mask & 0xFF00) == 0) {
			mask = *source_pos | 0xFF00;
			source_pos++;
		}
		if (mask & 1) {
			*window_pos = *dest_pos = *source_pos;
			window_pos++;
			source_pos++;
			dest_pos += stride;
			--remaining;
			if (remaining == 0) {
				dest_pos -= dest_end;
				remaining = height;
			}
			if (window_pos >= window_end) window_pos = window;
			--dest_length;
		} else {
			word copy_info = *source_pos;
			source_pos++;
			copy_info = (copy_info << 8) | *source_pos;
			source_pos++;
			byte* copy_source = window + (copy_info & 0x3FF);
			byte copy_length = (copy_info >> 10) + 3;
			do {
				*window_pos = *dest_pos = *copy_source;
				window_pos++;
				copy_source++;
				dest_pos += stride;
				--remaining;
				if (remaining == 0) {
					dest_pos -= dest_end;
					remaining = height;
				}
				if (copy_source >= window_end) copy_source = window;
				if (window_pos >= window_end) window_pos = window;
				--dest_length;
				--copy_length;
			} while (dest_length && copy_length);
		}
	} while (dest_length);
//	end:
	return dest;
}

// seg009:938E
void decompr_img(byte* dest,const image_data_type* source,int decomp_size,int cmeth, int stride) {
	switch (cmeth) {
		case 0: // RAW left-to-right
			memcpy(dest, &source->data, decomp_size);
		break;
		case 1: // RLE left-to-right
			decompress_rle_lr(dest, source->data, decomp_size);
		break;
		case 2: // RLE up-to-down
			decompress_rle_ud(dest, source->data, decomp_size, stride, SDL_SwapLE16(source->height));
		break;
		case 3: // LZG left-to-right
			decompress_lzg_lr(dest, source->data, decomp_size);
		break;
		case 4: // LZG up-to-down
			decompress_lzg_ud(dest, source->data, decomp_size, stride, SDL_SwapLE16(source->height));
		break;
	}
}

int calc_stride(const image_data_type* image_data) {
	int width = SDL_SwapLE16(image_data->width);
	int flags = SDL_SwapLE16(image_data->flags);
	int depth = ((flags >> 12) & 7) + 1;
	return (depth * width + 7) / 8;
}

byte* conv_to_8bpp(byte* in_data, int width, int height, int stride, int depth) {
	byte* out_data = (byte*) malloc(width * height);
	int pixels_per_byte = 8 / depth;
	int mask = (1 << depth) - 1;
	for (int y = 0; y < height; ++y) {
		byte* in_pos = in_data + y*stride;
		byte* out_pos = out_data + y*width;
		for (int x_pixel = 0, x_byte = 0; x_byte < stride; ++x_byte) {
			byte v = *in_pos;
			int shift = 8;
			for (int pixel_in_byte = 0; pixel_in_byte < pixels_per_byte && x_pixel < width; ++pixel_in_byte, ++x_pixel) {
				shift -= depth;
				*out_pos = (v >> shift) & mask;
				++out_pos;
			}
			++in_pos;
		}
	}
	return out_data;
}

/*
 * Decode one font glyph into static storage.
 *
 * The built-in font is the last thing in the game that is still decoded at
 * runtime - hc_font_data[] is compiled into this file rather than being a DAT
 * resource, so convert.py never sees it (TODO.md).
 *
 * decode_image() below cannot be used for it on this target: it mallocs twice
 * per image and takes its surface from picosdl's LIFO arena. A hundred glyphs
 * would need more arena entries than the arena has, and they are permanent, so
 * parking them at the bottom of a LIFO would pin it for the whole run. Instead
 * the pixels come from a static pool and the surface wraps them with
 * SDL_CreateRGBSurfaceFrom, which allocates nothing.
 *
 * The glyphs are drawn mono - method_3_blit_mono substitutes a colour per call
 * - so they need no palette row and no colour key beyond index 0.
 */
#ifdef ARV_TANG
#define POP_FONT_PIXEL_POOL VRAM_FONT_SIZE
#define s_font_pixels (arv_vram + VRAM_FONT)            /* in the VRAM */
#else
#define POP_FONT_PIXEL_POOL 12288
static byte s_font_pixels[POP_FONT_PIXEL_POOL];
#endif
static int  s_font_pixels_used;

static image_type* decode_font_glyph(const image_data_type* image_data) {
	int height = SDL_SwapLE16(image_data->height);
	if (height == 0) height = 1;
	int width  = SDL_SwapLE16(image_data->width);
	int flags  = SDL_SwapLE16(image_data->flags);
	int depth  = ((flags >> 12) & 7) + 1;
	int cmeth  = (flags >> 8) & 0x0F;
	int stride = calc_stride(image_data);

	if (width <= 0 || height <= 0) return NULL;

	/* Scratch for the packed form, reused for every glyph. */
	static byte packed[512];
	int dest_size = stride * height;
	if (dest_size > (int)sizeof(packed)) {
		printf("font glyph needs %d bytes of scratch, have %d\n",
		       dest_size, (int)sizeof(packed));
		quit(1);
	}
	memset(packed, 0, (size_t)dest_size);
	decompr_img(packed, image_data, dest_size, cmeth, stride);

	if (s_font_pixels_used + width * height > POP_FONT_PIXEL_POOL) {
		printf("font pixel pool exhausted (%d used, %d more wanted, %d total)\n",
		       s_font_pixels_used, width * height, POP_FONT_PIXEL_POOL);
		quit(1);
	}
	byte* out = s_font_pixels + s_font_pixels_used;
	s_font_pixels_used += width * height;

	/* The same unpacking conv_to_8bpp() does, straight into the pool. */
	int pixels_per_byte = 8 / depth;
	int mask = (1 << depth) - 1;
	for (int y = 0; y < height; ++y) {
		byte* in_pos  = packed + y * stride;
		byte* out_pos = out + y * width;
		for (int x_pixel = 0, x_byte = 0; x_byte < stride; ++x_byte) {
			byte v = *in_pos;
			int shift = 8;
			for (int p = 0; p < pixels_per_byte && x_pixel < width; ++p, ++x_pixel) {
				shift -= depth;
				*out_pos++ = (v >> shift) & mask;
			}
			++in_pos;
		}
	}

	image_type* image = SDL_CreateRGBSurfaceFrom(out, width, height, 8, width,
	                                             0, 0, 0, 0);
	if (image == NULL) {
		sdlperror("decode_font_glyph: SDL_CreateRGBSurfaceFrom");
		quit(1);
	}
	return image;
}

int font_pixels_used(void) { return s_font_pixels_used; }

image_type* decode_image(const image_data_type* image_data, dat_pal_type* palette) {
	int height = SDL_SwapLE16(image_data->height);
	if (height == 0) {
		height = 1;
	}
	int width = SDL_SwapLE16(image_data->width);
	int flags = SDL_SwapLE16(image_data->flags);
	int depth = ((flags >> 12) & 7) + 1;
	int cmeth = (flags >> 8) & 0x0F;
	int stride = calc_stride(image_data);
	int dest_size = stride * height;
	byte* dest = (byte*) malloc(dest_size);
	memset(dest, 0, dest_size);
	decompr_img(dest, image_data, dest_size, cmeth, stride);
	byte* image_8bpp = conv_to_8bpp(dest, width, height, stride, depth);
	free(dest); dest = NULL;
	image_type* image = SDL_CreateRGBSurface(0, width, height, 8, 0, 0, 0, 0);
	if (image == NULL) {
		sdlperror("decode_image: SDL_CreateRGBSurface");
		quit(1);
	}
	if (SDL_LockSurface(image) != 0) {
		sdlperror("decode_image: SDL_LockSurface");
	}
	for (int y = 0; y < height; ++y) {
		// fill image with data
		memcpy((byte*)image->pixels + y*image->pitch, image_8bpp + y*width, width);
	}
	SDL_UnlockSurface(image);

	free(image_8bpp); image_8bpp = NULL;
	SDL_Color colors[16];
	for (int i = 0; i < 16; ++i) {
		colors[i].r = palette->vga[i].r << 2;
		colors[i].g = palette->vga[i].g << 2;
		colors[i].b = palette->vga[i].b << 2;
		colors[i].a = SDL_ALPHA_OPAQUE;   // SDL2's SDL_Color has a fourth alpha component
	}
	// Force 0th color to be black for non-transparent blitters. (hitpoints, shadow)
	// This is needed to remove the colored rectangles around hitpoints and the shadow, when using Brain's SNES graphics for example.
	colors[0].r = 0;
	colors[0].g = 0;
	colors[0].b = 0;
	colors[0].a = SDL_ALPHA_TRANSPARENT;
	SDL_SetPaletteColors(image->format->palette, colors, 0, 16); // SDL_SetColors = deprecated
	return image;
}

// seg009:121A
/*
 * One image by resource id.
 *
 * No decoding: the converter turned every image resource into an 8bpp surface
 * in flash with its palette row already applied, so this is a table lookup.
 * The `palette` argument is what the runtime decoder used to need and is now
 * unused - the row is baked into the pixels.
 */
image_type* load_image(int resource_id, dat_pal_type* palette) {
	(void) palette;
	return (image_type*) find_resource_surface(resource_id);
}

#if 0   /* the old runtime decode path; see TODO.md */
image_type* load_image_decoded(int resource_id, dat_pal_type* palette) {
	int size;
	const void* image_data = load_from_opendats_const(resource_id, &size);
	if (!image_data) {
		return NULL;
	}
	image_type* image = decode_image((const image_data_type*) image_data, palette);

	if (image != NULL) {
		// should immediately start using the onscreen pixel format, so conversion will not be needed

		if (SDL_SetColorKey(image, SDL_TRUE, 0) != 0) { //sdl 1.2: SDL_SRCCOLORKEY
			sdlperror("load_image: SDL_SetColorKey");
			quit(1);
		}
//		printf("bpp = %d\n", image->format->BitsPerPixel);
/*
		if (SDL_SetSurfaceAlphaMod(image, 0) != 0) { //sdl 1.2: SDL_SetAlpha removed
			sdlperror("load_image: SDL_SetAlpha");
			quit(1);
		}
*/
//		image_type* colored_image = SDL_ConvertSurfaceFormat(image, SDL_PIXELFORMAT_ARGB8888, 0);
//		if (!colored_image) {
//			sdlperror("load_image: SDL_ConvertSurfaceFormat");
//			quit(1);
//		}
//		SDL_FreeSurface(image);
//		image = colored_image;
	}
	return image;
}
#endif

// seg009:13C4
void draw_image_transp(image_type* image,image_type* mask,int xpos,int ypos) {
	if (graphics_mode == gmMcgaVga) {
		draw_image_transp_vga(image, xpos, ypos);
	} else {
		// ...
	}
}

// seg009:157E
int set_joy_mode() {
	// stub
	if (SDL_NumJoysticks() < 1) {
		is_joyst_mode = 0;
	} else {
		if (gamecontrollerdb_file[0] != '\0') {
			SDL_GameControllerAddMappingsFromFile(gamecontrollerdb_file);
		}

		if (SDL_IsGameController(0)) {
			sdl_controller_ = SDL_GameControllerOpen(0);
			if (sdl_controller_ == NULL) {
				is_joyst_mode = 0;
			} else {
				is_joyst_mode = 1;
			}
		}
		// We have a joystick connected, but it's NOT compatible with the SDL_GameController
		// interface, so we resort to the classic SDL_Joystick interface instead
		else {
			sdl_joystick_ = SDL_JoystickOpen(0);
			is_joyst_mode = 1;
			using_sdl_joystick_interface = 1;
		}
	}
	if (enable_controller_rumble && is_joyst_mode) {
		sdl_haptic = SDL_HapticOpen(0);
		SDL_HapticRumbleInit(sdl_haptic); // initialize the device for simple rumble
	} else {
		sdl_haptic = NULL;
	}

	is_keyboard_mode = !is_joyst_mode;
	return is_joyst_mode;
}

// seg009:178B
/*
 * The offscreen compositing buffer.
 *
 * Always allocated full-screen, whatever rectangle is asked for. The game asks
 * for 320x192 during gameplay (rect_top) and 320x200 for titles and cutscenes,
 * but picosdl serves screen-sized surfaces from a dedicated pool - anything
 * else comes out of the 28 KB LIFO arena, which a 60 KB buffer does not fit in.
 * Rounding the gameplay case up to the pool costs 2560 bytes and keeps both
 * cases on the same path. picosdl/README.md has why there are two of these at
 * all, and why the attempt to get to one was measured and abandoned.
 */
/*
 * Do these two surfaces address the same pixels?
 *
 * With one screen buffer the offscreen surface is a view of the window surface -
 * a separate header over shared pixels - so a pointer comparison misses it. Every
 * copy between the two becomes a copy onto itself, which is a no-op worth
 * skipping rather than performing.
 */
/*
 * While this is set, update_screen() sends only this rectangle instead of the
 * whole canvas.
 *
 * The wipe transition needs it. With one screen buffer the new screen is already
 * in the buffer the panel is sent, so the wipe is done by pushing the strip that
 * has just been uncovered and letting the panel hold the rest - but the transition
 * loop calls idle(), and idle() presents. Without this the full present lands
 * immediately after the strip and shows the whole new screen at the first step,
 * so there is no wipe at all and every frame costs two pushes instead of one.
 */
/*
 * The next present sends only this rectangle, once.
 *
 * Single-shot and held by value, both deliberately. It was a pointer to the
 * caller's local kept across presents, and that is two bugs waiting: the pointer
 * outlives the scope that owns it, and any path that leaves without clearing it
 * leaves every later present sending a 2-pixel strip. That is what it did - the
 * cutscene looked frozen and gameplay stayed black, because after a fade-out the
 * only thing reaching the panel was one column.
 *
 * Consumed on use, so a missed clear costs one frame instead of the session. The
 * wipe sets it again each step, which is what it wants anyway.
 */
static SDL_Rect s_screen_push_rect;
static int      s_screen_push_rect_set;

/*
 * Whether what is in the buffer may be shown yet.
 *
 * With two buffers the game composes a screen into `offscreen_surface`, which
 * nobody can see, and then reveals it - by copying it across, by fading it in, or
 * by wiping it in. With one buffer that memory *is* what the panel gets, so a
 * present between composing and revealing shows the new screen at once and the
 * reveal has nothing left to do. That is one bug with two faces: the princess
 * scene appeared fully painted before its fade-in, and two of the three wipes per
 * demo cycle did nothing at all, because idle() had already presented the screen
 * they were supposed to wipe in.
 *
 * So presents are held while the game is drawing into what it believes is
 * offscreen, and released by whichever reveal the game chose. Drawing aimed
 * straight at the screen is unaffected - that is meant to be visible immediately,
 * and update_screen() only holds when the target is the offscreen surface.
 *
 * Missing a compose site fails safe: the present happens, which is just the old
 * behaviour for that one screen.
 */
static int s_screen_revealed = 1;
static int s_push_pending;

static void push_screen_now(void);

/*
 * The game is about to compose again. Anything revealed since the last push has
 * to reach the panel *now*, because the drawing that follows will overwrite the
 * very pixels that were revealed.
 *
 * That lag is what two buffers provided for free. The title sequence depends on
 * it: each step copies rect_titles across to show a banner, then immediately
 * redraws the background - which with one buffer erases the banner it just
 * showed. Holding presents does not help, because the pixels are already gone by
 * the time a present would have happened. Pushing here, at the last moment
 * before they are overwritten, reproduces the two-buffer behaviour exactly.
 *
 * Pushing inside the reveal instead would be simpler and wrong: gameplay reveals
 * once per frame through copy_screen_rect(), and update_screen() pushes straight
 * after, so the panel would get two full frames per frame and the rate would
 * halve.
 */
void picopop_compose_begin(void)
{
	if (s_push_pending) {
		s_push_pending = 0;
		push_screen_now();
	}
	s_screen_revealed = 0;
}

void picopop_compose_reveal(void)
{
	s_screen_revealed = 1;
	s_push_pending    = 1;
}

static void push_screen_now(void)
{
	if (window_surface_ == NULL)
		return;
	if (s_screen_push_rect_set) {
		s_screen_push_rect_set = 0;      /* single-shot */
		SDL_UpdateWindowSurfaceRects(window_, &s_screen_push_rect, 1);
	} else {
		SDL_UpdateWindowSurface(window_);
	}

	/*
	 * Wait out the transfer before returning.
	 *
	 * A present is asynchronous: it returns once the DMA has been armed, so the
	 * panel is still reading our pixels. With two buffers that is the whole
	 * point - you draw into the other one. With one there is no other one, and
	 * the next draw would land in memory being read, which shows up as tearing
	 * and torn sprites rather than as anything that fails.
	 *
	 * picosdl cannot do this for us: it does not know how many buffers the
	 * client allocated or which one is about to be touched. Only we do.
	 *
	 * The cost is the overlap between drawing and pushing - about 17.4 ms of
	 * panel time per full frame at 125 MHz, which is the trade one buffer makes.
	 * Waiting later, at picopop_compose_begin(), would win some of it back.
	 */
	PSDL_PresentSync();
}

void picopop_set_screen_push_rect(const SDL_Rect* r) {
	if (r == NULL) {
		s_screen_push_rect_set = 0;
		return;
	}
	s_screen_push_rect     = *r;
	s_screen_push_rect_set = 1;
}

int picopop_same_pixels(const surface_type* a, const surface_type* b) {
	return a != NULL && b != NULL && a->pixels == b->pixels;
}

surface_type* make_offscreen_buffer(const rect_type* rect) {
#ifdef PICOPOP_ONE_SCREEN_BUFFER
	/*
	 * There is no second buffer: the offscreen surface is a *view* of the one the
	 * panel is sent. Same pixels, its own header - 68 bytes instead of 62.5 KB -
	 * so the game draws straight into the buffer that gets pushed, and the frame
	 * the player is looking at lives in the panel's own memory rather than in
	 * ours.
	 *
	 * Safe because picosdl's present does not return until the push has finished,
	 * so drawing happens strictly between pushes and a half-drawn frame is never
	 * on the glass. What it costs is the overlap: drawing used to run while the
	 * previous frame went out, and now it waits its turn. Worth it - the screen
	 * pool was 52% of this firmware's RAM and the only remaining item of that
	 * size.
	 *
	 * A view rather than the window surface itself, because the size the caller
	 * asks for is load-bearing: see the note below. The view carries the
	 * requested w/h while the window surface stays 320x200 for the panel, and
	 * because the size lives in the header, reset_clip_rect() still resolves to
	 * the play area. Setting a clip rect on the shared surface instead does not
	 * work - reset_clip_rect() resets it to w/h, which would be the full 200 rows
	 * and lets sprites spill over the status bar.
	 */
	surface_type* screen = SDL_GetWindowSurface(window_);
	if (screen == NULL) {
		printf("make_offscreen_buffer: no window surface yet\n");
		quit(1);
	}
	int vw = rect->right - rect->left;
	int vh = rect->bottom - rect->top;
	if (vw <= 0 || vw > 320) vw = 320;
	if (vh <= 0 || vh > 200) vh = 200;
	surface_type* view = SDL_CreateRGBSurfaceFrom(screen->pixels, vw, vh, 8,
	                                              screen->pitch, 0, 0, 0, 0);
	if (view == NULL) {
		printf("make_offscreen_buffer: %s\n", SDL_GetError());
		PSDL_ReportMemory();
		quit(1);
	}
	return view;
#else
	/*
	 * Storage always comes from the screen pool, which only serves requests for
	 * exactly the screen size - anything else falls into the 28 KB LIFO arena,
	 * which a 60 KB buffer does not fit in.
	 */
	surface_type* surface = SDL_CreateRGBSurface(0, 320, 200, 8, 0, 0, 0, 0);
	if (surface == NULL) {
		printf("make_offscreen_buffer: %s\n", SDL_GetError());
		PSDL_ReportMemory();
		quit(1);
	}

	/*
	 * ...but present it as the rectangle that was asked for.
	 *
	 * This matters, and getting it wrong is subtle. The game asks for
	 * rect_top - 320x192 - during gameplay, and relies on the surface being
	 * only 192 rows tall to clip for it: nothing clamps a sprite to the play
	 * area on the way in (draw_image just blits at ypos), and add_drect()
	 * happily records a dirty rectangle that extends past row 192. Upstream
	 * both were bounded by the surface itself.
	 *
	 * Handing back the full 200-row buffer therefore let sprites near the
	 * bottom of the play area spill into rows 192..199 and get copied out over
	 * the status line and the life triangles.
	 *
	 * Narrowing w/h is enough to restore that: picosdl's blitter clips the
	 * source by src->h and the destination by dst->clip_rect intersected with
	 * dst->w/h, so drawing *into* this surface and copying *out of* it are both
	 * bounded again. SDL_SetClipRect(NULL) resets to w/h, so reset_clip_rect()
	 * stays correct too. The pool slot is released by pool_slot, not by size.
	 */
	int w = rect->right - rect->left;
	int h = rect->bottom - rect->top;
	if (w <= 0 || w > 320) w = 320;
	if (h <= 0 || h > 200) h = 200;
	surface->w = w;
	surface->h = h;
	surface->clip_rect.x = 0;
	surface->clip_rect.y = 0;
	surface->clip_rect.w = w;
	surface->clip_rect.h = h;
	return surface;
#endif
}

// seg009:17BD
void free_surface(surface_type* surface) {
#ifdef PICOPOP_ONE_SCREEN_BUFFER
	/* The offscreen buffer *is* the window surface, and freeing that would take
	 * the screen with it. */
	if (surface == SDL_GetWindowSurface(window_))
		return;
#endif
	SDL_FreeSurface(surface);
}

// seg009:17EA
/*
 * Peels.
 *
 * peels_table is fifty entries and add_peel() refuses to exceed it
 * (seg008.c:891), so fifty is the real bound and a static pool is exact rather
 * than a guess. The pixels still come from picosdl's LIFO arena; this is only
 * the little descriptor that points at them.
 */
#define POP_MAX_PEELS 50
static peel_type s_peel_pool[POP_MAX_PEELS];
static byte      s_peel_used[POP_MAX_PEELS];

static peel_type* alloc_peel(void) {
	for (int i = 0; i < POP_MAX_PEELS; ++i) {
		if (!s_peel_used[i]) {
			s_peel_used[i] = 1;
			memset(&s_peel_pool[i], 0, sizeof(s_peel_pool[i]));
			return &s_peel_pool[i];
		}
	}
	printf("read_peel_from_screen: more than %d peels live\n", POP_MAX_PEELS);
	quit(1);
	return NULL;
}

void free_peel(peel_type* peel_ptr) {
	if (peel_ptr == NULL) return;
	SDL_FreeSurface(peel_ptr->peel);
	for (int i = 0; i < POP_MAX_PEELS; ++i)
		if (&s_peel_pool[i] == peel_ptr) { s_peel_used[i] = 0; return; }
}

// seg009:182F
void set_hc_pal() {
	// stub
	if (graphics_mode == gmMcgaVga) {
		set_pal_arr(0, 16, custom->vga_palette);
	} else {
		// ...
	}
}

// seg009:2446
void flip_not_ega(byte* memory,int height,int stride) {
	/* One row of scratch. The widest surface here is the screen, and this is
	 * called from flip_screen() on core 0 only, so one static row is enough. */
	static byte row_buffer[320];
	if (stride > (int)sizeof(row_buffer)) {
		printf("flip_not_ega: stride %d exceeds the %d-byte row buffer\n",
		       stride, (int)sizeof(row_buffer));
		quit(1);
	}
	byte* top_ptr;
	byte* bottom_ptr;
	bottom_ptr = top_ptr = memory;
	bottom_ptr += (height - 1) * stride;
	short rem_rows = height >> 1;
	do {
		memcpy(row_buffer, top_ptr, stride);
		memcpy(top_ptr, bottom_ptr, stride);
		memcpy(bottom_ptr, row_buffer, stride);
		top_ptr += stride;
		bottom_ptr -= stride;
		--rem_rows;
	} while (rem_rows);
}

// seg009:19B1
void flip_screen(surface_type* surface) {
	// stub
	if (graphics_mode != gmEga) {
		if (SDL_LockSurface(surface) != 0) {
			sdlperror("flip_screen: SDL_LockSurface");
			quit(1);
		}
		flip_not_ega((byte*) surface->pixels, surface->h, surface->pitch);
		SDL_UnlockSurface(surface);
	} else {
		// ...
	}
}

#ifndef USE_FADE
// seg009:19EF
void fade_in_2(surface_type* source_surface,int which_rows) {
	// stub
	method_1_blit_rect(onscreen_surface_, source_surface, &screen_rect, &screen_rect, 0);
}

// seg009:1CC9
void fade_out_2(int rows) {
	// stub
}
#endif // USE_FADE

// seg009:2288
void draw_image_transp_vga(image_type* image,int xpos,int ypos) {
	// stub
	method_6_blit_img_to_scr(image, xpos, ypos, blitters_10h_transp);
}

#ifdef USE_TEXT


/*const*/ byte hc_font_data[] = {
0x20,0x83,0x07,0x00,0x02,0x00,0x01,0x00,0x01,0x00,0xD2,0x00,0xD8,0x00,0xE5,0x00,
0xEE,0x00,0xFA,0x00,0x07,0x01,0x14,0x01,0x21,0x01,0x2A,0x01,0x37,0x01,0x44,0x01,
0x50,0x01,0x5C,0x01,0x6A,0x01,0x74,0x01,0x81,0x01,0x8E,0x01,0x9B,0x01,0xA8,0x01,
0xB5,0x01,0xC2,0x01,0xCF,0x01,0xDC,0x01,0xE9,0x01,0xF6,0x01,0x03,0x02,0x10,0x02,
0x1C,0x02,0x2A,0x02,0x37,0x02,0x42,0x02,0x4F,0x02,0x5C,0x02,0x69,0x02,0x76,0x02,
0x83,0x02,0x90,0x02,0x9D,0x02,0xAA,0x02,0xB7,0x02,0xC4,0x02,0xD1,0x02,0xDE,0x02,
0xEB,0x02,0xF8,0x02,0x05,0x03,0x12,0x03,0x1F,0x03,0x2C,0x03,0x39,0x03,0x46,0x03,
0x53,0x03,0x60,0x03,0x6D,0x03,0x7A,0x03,0x87,0x03,0x94,0x03,0xA1,0x03,0xAE,0x03,
0xBB,0x03,0xC8,0x03,0xD5,0x03,0xE2,0x03,0xEB,0x03,0xF9,0x03,0x02,0x04,0x0F,0x04,
0x1C,0x04,0x29,0x04,0x36,0x04,0x43,0x04,0x50,0x04,0x5F,0x04,0x6C,0x04,0x79,0x04,
0x88,0x04,0x95,0x04,0xA2,0x04,0xAF,0x04,0xBC,0x04,0xC9,0x04,0xD8,0x04,0xE7,0x04,
0xF4,0x04,0x01,0x05,0x0E,0x05,0x1B,0x05,0x28,0x05,0x35,0x05,0x42,0x05,0x51,0x05,
0x5E,0x05,0x6B,0x05,0x78,0x05,0x85,0x05,0x8D,0x05,0x9A,0x05,0xA7,0x05,0xBB,0x05,
0xD9,0x05,0x00,0x00,0x03,0x00,0x00,0x00,0x07,0x00,0x02,0x00,0x01,0x00,0xC0,0xC0,
0xC0,0xC0,0xC0,0x00,0xC0,0x03,0x00,0x05,0x00,0x01,0x00,0xD8,0xD8,0xD8,0x06,0x00,
0x07,0x00,0x01,0x00,0x00,0x6C,0xFE,0x6C,0xFE,0x6C,0x07,0x00,0x07,0x00,0x01,0x00,
0x10,0x7C,0xD0,0x7C,0x16,0x7C,0x10,0x07,0x00,0x08,0x00,0x01,0x00,0xC3,0xC6,0x0C,
0x18,0x30,0x63,0xC3,0x07,0x00,0x08,0x00,0x01,0x00,0x38,0x6C,0x38,0x7A,0xCC,0xCE,
0x7B,0x03,0x00,0x03,0x00,0x01,0x00,0x60,0x60,0xC0,0x07,0x00,0x04,0x00,0x01,0x00,
0x30,0x60,0xC0,0xC0,0xC0,0x60,0x30,0x07,0x00,0x04,0x00,0x01,0x00,0xC0,0x60,0x30,
0x30,0x30,0x60,0xC0,0x06,0x00,0x07,0x00,0x01,0x00,0x00,0x6C,0x38,0xFE,0x38,0x6C,
0x06,0x00,0x06,0x00,0x01,0x00,0x00,0x30,0x30,0xFC,0x30,0x30,0x08,0x00,0x03,0x00,
0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x60,0x60,0xC0,0x04,0x00,0x04,0x00,0x01,0x00,
0x00,0x00,0x00,0xF0,0x07,0x00,0x02,0x00,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0xC0,
0xC0,0x07,0x00,0x08,0x00,0x01,0x00,0x03,0x06,0x0C,0x18,0x30,0x60,0xC0,0x07,0x00,
0x06,0x00,0x01,0x00,0x78,0xCC,0xCC,0xCC,0xCC,0xCC,0x78,0x07,0x00,0x06,0x00,0x01,
0x00,0x30,0x70,0xF0,0x30,0x30,0x30,0xFC,0x07,0x00,0x06,0x00,0x01,0x00,0x78,0xCC,
0x0C,0x18,0x30,0x60,0xFC,0x07,0x00,0x06,0x00,0x01,0x00,0x78,0xCC,0x0C,0x18,0x0C,
0xCC,0x78,0x07,0x00,0x07,0x00,0x01,0x00,0x1C,0x3C,0x6C,0xCC,0xFE,0x0C,0x0C,0x07,
0x00,0x06,0x00,0x01,0x00,0xF8,0xC0,0xC0,0xF8,0x0C,0x0C,0xF8,0x07,0x00,0x06,0x00,
0x01,0x00,0x78,0xC0,0xC0,0xF8,0xCC,0xCC,0x78,0x07,0x00,0x06,0x00,0x01,0x00,0xFC,
0x0C,0x18,0x30,0x30,0x30,0x30,0x07,0x00,0x06,0x00,0x01,0x00,0x78,0xCC,0xCC,0x78,
0xCC,0xCC,0x78,0x07,0x00,0x06,0x00,0x01,0x00,0x78,0xCC,0xCC,0x7C,0x0C,0xCC,0x78,
0x06,0x00,0x02,0x00,0x01,0x00,0x00,0xC0,0xC0,0x00,0xC0,0xC0,0x08,0x00,0x03,0x00,
0x01,0x00,0x00,0x60,0x60,0x00,0x00,0x60,0x60,0xC0,0x07,0x00,0x05,0x00,0x01,0x00,
0x18,0x30,0x60,0xC0,0x60,0x30,0x18,0x05,0x00,0x04,0x00,0x01,0x00,0x00,0x00,0xF0,
0x00,0xF0,0x07,0x00,0x05,0x00,0x01,0x00,0xC0,0x60,0x30,0x18,0x30,0x60,0xC0,0x07,
0x00,0x06,0x00,0x01,0x00,0x78,0xCC,0x0C,0x18,0x30,0x00,0x30,0x07,0x00,0x06,0x00,
0x01,0x00,0x78,0xCC,0xDC,0xDC,0xD8,0xC0,0x78,0x07,0x00,0x06,0x00,0x01,0x00,0x78,
0xCC,0xCC,0xFC,0xCC,0xCC,0xCC,0x07,0x00,0x06,0x00,0x01,0x00,0xF8,0xCC,0xCC,0xF8,
0xCC,0xCC,0xF8,0x07,0x00,0x06,0x00,0x01,0x00,0x78,0xCC,0xC0,0xC0,0xC0,0xCC,0x78,
0x07,0x00,0x06,0x00,0x01,0x00,0xF8,0xCC,0xCC,0xCC,0xCC,0xCC,0xF8,0x07,0x00,0x05,
0x00,0x01,0x00,0xF8,0xC0,0xC0,0xF0,0xC0,0xC0,0xF8,0x07,0x00,0x05,0x00,0x01,0x00,
0xF8,0xC0,0xC0,0xF0,0xC0,0xC0,0xC0,0x07,0x00,0x06,0x00,0x01,0x00,0x78,0xCC,0xC0,
0xDC,0xCC,0xCC,0x78,0x07,0x00,0x06,0x00,0x01,0x00,0xCC,0xCC,0xCC,0xFC,0xCC,0xCC,
0xCC,0x07,0x00,0x04,0x00,0x01,0x00,0xF0,0x60,0x60,0x60,0x60,0x60,0xF0,0x07,0x00,
0x06,0x00,0x01,0x00,0x0C,0x0C,0x0C,0x0C,0x0C,0xCC,0x78,0x07,0x00,0x07,0x00,0x01,
0x00,0xC6,0xCC,0xD8,0xF0,0xD8,0xCC,0xC6,0x07,0x00,0x05,0x00,0x01,0x00,0xC0,0xC0,
0xC0,0xC0,0xC0,0xC0,0xF8,0x07,0x00,0x08,0x00,0x01,0x00,0xC3,0xE7,0xFF,0xDB,0xC3,
0xC3,0xC3,0x07,0x00,0x06,0x00,0x01,0x00,0xCC,0xCC,0xEC,0xFC,0xDC,0xCC,0xCC,0x07,
0x00,0x06,0x00,0x01,0x00,0x78,0xCC,0xCC,0xCC,0xCC,0xCC,0x78,0x07,0x00,0x06,0x00,
0x01,0x00,0xF8,0xCC,0xCC,0xF8,0xC0,0xC0,0xC0,0x07,0x00,0x06,0x00,0x01,0x00,0x78,
0xCC,0xCC,0xCC,0xCC,0xD8,0x6C,0x07,0x00,0x06,0x00,0x01,0x00,0xF8,0xCC,0xCC,0xF8,
0xD8,0xCC,0xCC,0x07,0x00,0x06,0x00,0x01,0x00,0x78,0xCC,0xC0,0x78,0x0C,0xCC,0x78,
0x07,0x00,0x06,0x00,0x01,0x00,0xFC,0x30,0x30,0x30,0x30,0x30,0x30,0x07,0x00,0x06,
0x00,0x01,0x00,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0x7C,0x07,0x00,0x06,0x00,0x01,0x00,
0xCC,0xCC,0xCC,0xCC,0xCC,0x78,0x30,0x07,0x00,0x08,0x00,0x01,0x00,0xC3,0xC3,0xC3,
0xDB,0xFF,0xE7,0xC3,0x07,0x00,0x06,0x00,0x01,0x00,0xCC,0xCC,0x78,0x30,0x78,0xCC,
0xCC,0x07,0x00,0x06,0x00,0x01,0x00,0xCC,0xCC,0xCC,0x78,0x30,0x30,0x30,0x07,0x00,
0x08,0x00,0x01,0x00,0xFF,0x06,0x0C,0x18,0x30,0x60,0xFF,0x07,0x00,0x04,0x00,0x01,
0x00,0xF0,0xC0,0xC0,0xC0,0xC0,0xC0,0xF0,0x07,0x00,0x08,0x00,0x01,0x00,0xC0,0x60,
0x30,0x18,0x0C,0x06,0x03,0x07,0x00,0x04,0x00,0x01,0x00,0xF0,0x30,0x30,0x30,0x30,
0x30,0xF0,0x03,0x00,0x06,0x00,0x01,0x00,0x30,0x78,0xCC,0x08,0x00,0x06,0x00,0x01,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFC,0x03,0x00,0x04,0x00,0x01,0x00,0xC0,
0x60,0x30,0x07,0x00,0x06,0x00,0x01,0x00,0x00,0x00,0x78,0x0C,0x7C,0xCC,0x7C,0x07,
0x00,0x06,0x00,0x01,0x00,0xC0,0xC0,0xF8,0xCC,0xCC,0xCC,0xF8,0x07,0x00,0x06,0x00,
0x01,0x00,0x00,0x00,0x78,0xCC,0xC0,0xCC,0x78,0x07,0x00,0x06,0x00,0x01,0x00,0x0C,
0x0C,0x7C,0xCC,0xCC,0xCC,0x7C,0x07,0x00,0x06,0x00,0x01,0x00,0x00,0x00,0x78,0xCC,
0xFC,0xC0,0x7C,0x07,0x00,0x05,0x00,0x01,0x00,0x38,0x60,0xF8,0x60,0x60,0x60,0x60,
0x09,0x00,0x06,0x00,0x01,0x00,0x00,0x00,0x78,0xCC,0xCC,0xCC,0x7C,0x0C,0x78,0x07,
0x00,0x06,0x00,0x01,0x00,0xC0,0xC0,0xF8,0xCC,0xCC,0xCC,0xCC,0x07,0x00,0x02,0x00,
0x01,0x00,0xC0,0x00,0xC0,0xC0,0xC0,0xC0,0xC0,0x09,0x00,0x04,0x00,0x01,0x00,0x30,
0x00,0x30,0x30,0x30,0x30,0x30,0x30,0xE0,0x07,0x00,0x06,0x00,0x01,0x00,0xC0,0xC0,
0xCC,0xD8,0xF0,0xD8,0xCC,0x07,0x00,0x02,0x00,0x01,0x00,0xC0,0xC0,0xC0,0xC0,0xC0,
0xC0,0xC0,0x07,0x00,0x08,0x00,0x01,0x00,0x00,0x00,0xFE,0xDB,0xDB,0xDB,0xDB,0x07,
0x00,0x06,0x00,0x01,0x00,0x00,0x00,0xF8,0xCC,0xCC,0xCC,0xCC,0x07,0x00,0x06,0x00,
0x01,0x00,0x00,0x00,0x78,0xCC,0xCC,0xCC,0x78,0x09,0x00,0x06,0x00,0x01,0x00,0x00,
0x00,0xF8,0xCC,0xCC,0xCC,0xF8,0xC0,0xC0,0x09,0x00,0x06,0x00,0x01,0x00,0x00,0x00,
0x78,0xCC,0xCC,0xCC,0x7C,0x0C,0x0C,0x07,0x00,0x06,0x00,0x01,0x00,0x00,0x00,0x78,
0xCC,0xC0,0xC0,0xC0,0x07,0x00,0x06,0x00,0x01,0x00,0x00,0x00,0x78,0xC0,0x78,0x0C,
0xF8,0x07,0x00,0x05,0x00,0x01,0x00,0x60,0x60,0xF8,0x60,0x60,0x60,0x38,0x07,0x00,
0x06,0x00,0x01,0x00,0x00,0x00,0xCC,0xCC,0xCC,0xCC,0x7C,0x07,0x00,0x06,0x00,0x01,
0x00,0x00,0x00,0xCC,0xCC,0xCC,0x78,0x30,0x07,0x00,0x08,0x00,0x01,0x00,0x00,0x00,
0xC3,0xC3,0xDB,0xFF,0x66,0x07,0x00,0x06,0x00,0x01,0x00,0x00,0x00,0xCC,0x78,0x30,
0x78,0xCC,0x09,0x00,0x06,0x00,0x01,0x00,0x00,0x00,0xCC,0xCC,0xCC,0xCC,0x7C,0x0C,
0x78,0x07,0x00,0x06,0x00,0x01,0x00,0x00,0x00,0xFC,0x18,0x30,0x60,0xFC,0x07,0x00,
0x04,0x00,0x01,0x00,0x30,0x60,0x60,0xC0,0x60,0x60,0x30,0x07,0x00,0x02,0x00,0x01,
0x00,0xC0,0xC0,0xC0,0x00,0xC0,0xC0,0xC0,0x07,0x00,0x04,0x00,0x01,0x00,0xC0,0x60,
0x60,0x30,0x60,0x60,0xC0,0x02,0x00,0x07,0x00,0x01,0x00,0x76,0xDC,0x07,0x00,0x07,
0x00,0x01,0x00,0x00,0x00,0x70,0xC4,0xCC,0x8C,0x38,0x07,0x00,0x07,0x00,0x01,0x00,
0x00,0x06,0x0C,0xD8,0xF0,0xE0,0xC0,0x08,0x00,0x10,0x00,0x02,0x00,0x7F,0xFE,0xCD,
0xC7,0xB5,0xEF,0xB5,0xEF,0x85,0xEF,0xB5,0xEF,0xB4,0x6F,0x08,0x00,0x13,0x00,0x03,
0x00,0x7F,0xFF,0xC0,0xCC,0x46,0xE0,0xB6,0xDA,0xE0,0xBE,0xDA,0xE0,0xBE,0xC6,0xE0,
0xB6,0xDA,0xE0,0xCE,0xDA,0x20,0x7F,0xFF,0xC0,0x08,0x00,0x11,0x00,0x03,0x00,0x7F,
0xFF,0x00,0xC6,0x73,0x80,0xDD,0xAD,0x80,0xCE,0xEF,0x80,0xDF,0x6F,0x80,0xDD,0xAD,
0x80,0xC6,0x73,0x80,0x7F,0xFF,0x00
};

static void load_font_character_offsets(rawfont_type* data) {
	int n_chars = data->last_char - data->first_char + 1;
	byte* pos = (byte*) &data->offsets[n_chars];
	for (int index = 0; index < n_chars; ++index) {
		data->offsets[index] = SDL_SwapLE16(pos - (byte*) data);
		const image_data_type* image_data = (image_data_type*) pos;
		int image_bytes = SDL_SwapLE16(image_data->height) * calc_stride(image_data);
		pos = (byte*) &image_data->data + image_bytes;
	}
}

font_type load_font_from_data(/*const*/ rawfont_type* data) {
	font_type font;
	font.first_char = data->first_char;
	font.last_char = data->last_char;
	font.height_above_baseline = SDL_SwapLE16(data->height_above_baseline);
	font.height_below_baseline = SDL_SwapLE16(data->height_below_baseline);
	font.space_between_lines = SDL_SwapLE16(data->space_between_lines);
	font.space_between_chars = SDL_SwapLE16(data->space_between_chars);
	int n_chars = font.last_char - font.first_char + 1;
	// Allow loading a font even if the offsets for each character image were not supplied in the raw data.
	if (SDL_SwapLE16(data->offsets[0]) == 0) {
		load_font_character_offsets(data);
	}
	/*
	 * INTERIM - TODO.md.
	 *
	 * The built-in font is not a DAT resource: hc_font_data[] is compiled in
	 * and decoded here into ~100 small surfaces. That is the last caller of
	 * decode_image() and the last runtime image decode in the game, and the
	 * plan is to have convert.py emit the glyphs as const surfaces like every
	 * other sprite. Until that lands, the decode stays but the *storage* does
	 * not come from the heap: the chtab and its image table are static, and
	 * the glyph pixels come from picosdl's arena.
	 */
	static chtab_type         s_font_chtab;
	static const SDL_Surface *s_font_images[POP_MAX_FONT_CHARS];
	if (n_chars > POP_MAX_FONT_CHARS) {
		printf("font has %d characters, only %d fit\n", n_chars, POP_MAX_FONT_CHARS);
		quit(1);
	}
	chtab_type* chtab = &s_font_chtab;
	chtab->n_images = (word) n_chars;
	chtab->images   = s_font_images;
	chtab->palette_row = 0;

	// Make a dummy palette for decode_image().
	dat_pal_type dat_pal;
	memset(&dat_pal, 0, sizeof(dat_pal));
	dat_pal.vga[1].r = dat_pal.vga[1].g = dat_pal.vga[1].b = 0x3F; // white
	for (int index = 0, chr = data->first_char; chr <= data->last_char; ++index, ++chr) {
		const image_data_type* image_data = (/*const*/ image_data_type*)((/*const*/ byte*)data + SDL_SwapLE16(data->offsets[index]));
		//image_data->flags=0;
		// if (image_data->height == SDL_SwapLE16(0)) image_data->height = SDL_SwapLE16(1); // HACK: decode_image() returns NULL if height==0.
		image_type* image = decode_font_glyph(image_data);
		s_font_images[index] = image;
		if (image != NULL && SDL_SetColorKey(image, SDL_TRUE, 0) != 0) {
			sdlperror("load_font_from_data: SDL_SetColorKey");
			quit(1);
		}
	}
	font.chtab = chtab;
	return font;
}

// Small font data (hardcoded), defined in menu.c
extern byte hc_small_font_data[];

void load_font(void) {
#if USE_SDL_IMAGE
	// Try to load font from a file.
	dat_type* dathandle = open_dat("font", 1);
	hc_font.chtab = load_sprites_from_file(1000, 1<<1, 0);
	close_dat(dathandle);
	if (hc_font.chtab == NULL) {
#endif
		// Use built-in font.
		hc_font = load_font_from_data((/*const*/ rawfont_type*)hc_font_data);
#if USE_SDL_IMAGE
	}
#endif

#ifdef USE_MENU
	hc_small_font = load_font_from_data((rawfont_type*)hc_small_font_data);
#endif

}

// seg009:35C5
int get_char_width(byte character) {
	font_type* font = textstate.ptr_font;
	int width = 0;
	if (character <= font->last_char && character >= font->first_char) {
		image_type* image = (image_type*) font->chtab->images[character - font->first_char];
		if (image != NULL) {
			width += image->w; //char_ptrs[character - font->first_char]->width;
			if (width) width += font->space_between_chars;
		}
	}
	return width;
}

// seg009:3E99
int find_linebreak(const char* text,int length,int break_width,int x_align) {
	int curr_char_pos = 0;
	short last_break_pos = 0; // in characters
	short curr_line_width = 0; // in pixels
	const char* text_pos = text;
	while (curr_char_pos < length) {
		curr_line_width += get_char_width(*text_pos);
		if (curr_line_width <= break_width) {
			++curr_char_pos;
			char curr_char = *text_pos;
			text_pos++;
			if (curr_char == '\n') {
				return curr_char_pos;
			}
			if (curr_char == '-' ||
				(x_align <= 0 && (curr_char == ' ' || *text_pos == ' ')) ||
				(*text_pos == ' ' && curr_char == ' ')
			) {
				// May break here.
				last_break_pos = curr_char_pos;
			}
		} else {
			if (last_break_pos == 0) {
				// If the first word is wider than break_width then break it.
				return curr_char_pos;
			} else {
				// Otherwise break at the last space.
				return last_break_pos;
			}
		}
	}
	return curr_char_pos;
}

// seg009:403F
int get_line_width(const char* text,int length) {
	int width = 0;
	const char* text_pos = text;
	while (--length >= 0) {
		width += get_char_width(*text_pos);
		text_pos++;
	}
	return width;
}

// seg009:3706
int draw_text_character(byte character) {
	//printf("going to do draw_text_character...\n");
	font_type* font = textstate.ptr_font;
	int width = 0;
	if (character <= font->last_char && character >= font->first_char) {
		image_type* image = (image_type*) font->chtab->images[character - font->first_char]; //char_ptrs[character - font->first_char];
		if (image != NULL) {
			method_3_blit_mono(image, textstate.current_x, textstate.current_y - font->height_above_baseline, textstate.textblit, textstate.textcolor);
			width = font->space_between_chars + image->w;
		}
	}
	textstate.current_x += width;
	return width;
}

// seg009:377F
int draw_text_line(const char* text,int length) {
	//hide_cursor();
	int width = 0;
	const char* text_pos = text;
	while (--length >= 0) {
		width += draw_text_character(*text_pos);
		text_pos++;
	}
	//show_cursor();
	return width;
}

// seg009:3755
int draw_cstring(const char* string) {
	//hide_cursor();
	int width = 0;
	const char* text_pos = string;
	while ('\0' != *text_pos) {
		width += draw_text_character(*text_pos);
		text_pos++;
	}
	//show_cursor();
	return width;
}

// seg009:3F01
const rect_type* draw_text(const rect_type* rect_ptr,int x_align,int y_align,const char* text,int length) {
	//printf("going to do draw_text()...\n");
	short rect_top;
	short rect_height;
	short rect_width;
	//textinfo_type var_C;
	short num_lines;
	short font_line_distance;
	//hide_cursor();
	//get_textinfo(&var_C);
	set_clip_rect(rect_ptr);
	rect_width = rect_ptr->right - rect_ptr->left;
	rect_top = rect_ptr->top;
	rect_height = rect_ptr->bottom - rect_ptr->top;
	num_lines = 0;
	int rem_length = length;
	const char* line_start = text;
	#define MAX_LINES 100
	const char* line_starts[MAX_LINES];
	int line_lengths[MAX_LINES];
	do {
		int line_length = find_linebreak(line_start, rem_length, rect_width, x_align);
		if (line_length == 0) break;
		if (num_lines >= MAX_LINES) {
			//... ERROR!
			printf("draw_text(): Too many lines!\n");
			quit(1);
		}
		line_starts[num_lines] = line_start;
		line_lengths[num_lines] = line_length;
		++num_lines;
		line_start += line_length;
		rem_length -= line_length;
	} while(rem_length);
	font_type* font = textstate.ptr_font;
	font_line_distance = font->height_above_baseline + font->height_below_baseline + font->space_between_lines;
	int text_height = font_line_distance * num_lines - font->space_between_lines;
	int text_top = rect_top;
	if (y_align >= 0) {
		if (y_align <= 0) {
			// middle
			// The +1 is for simulating SHR + ADC/SBB.
			text_top += (rect_height+1)/2 - (text_height+1)/2;
		} else {
			// bottom
			text_top += rect_height - text_height;
		}
	}
	textstate.current_y = text_top + font->height_above_baseline;
	for (int i = 0; i < num_lines; ++i) {
		const char* line_pos = line_starts[i];
		int line_length = line_lengths[i];
		if (x_align < 0 &&
			*line_pos == ' ' &&
			i != 0 &&
			*(line_pos-1) != '\n'
		) {
			// Skip over space if it's not at the beginning of a line.
			++line_pos;
			--line_length;
			if (line_length != 0 &&
				*line_pos == ' ' &&
				*(line_pos-2) == '.'
			) {
				// Skip over second space after point.
				++line_pos;
				--line_length;
			}
		}
		int line_width = get_line_width(line_pos,line_length);
		int text_left = rect_ptr->left;
		if (x_align >= 0) {
			if (x_align <= 0) {
				// center
				text_left += rect_width/2 - line_width/2;
			} else {
				// right
				text_left += rect_width - line_width;
			}
		}
		textstate.current_x = text_left;
		//printf("going to draw text line...\n");
		draw_text_line(line_pos,line_length);
		textstate.current_y += font_line_distance;
	}
	reset_clip_rect();
	//set_textinfo(...);
	//show_cursor();
	return rect_ptr;
}

// seg009:3E4F
void show_text(const rect_type* rect_ptr,int x_align,int y_align,const char* text) {
	// stub
	//printf("show_text: %s\n",text);
	draw_text(rect_ptr, x_align, y_align, text, (int)strlen(text));
}

// seg009:04FF
void show_text_with_color(const rect_type* rect_ptr,int x_align,int y_align, const char* text,int color) {
	short saved_textcolor;
	saved_textcolor = textstate.textcolor;
	textstate.textcolor = color;
	show_text(rect_ptr, x_align, y_align, text);
	textstate.textcolor = saved_textcolor;
}

// seg009:3A91
void set_curr_pos(int xpos,int ypos) {
	textstate.current_x = xpos;
	textstate.current_y = ypos;
}

// seg009:145A
void init_copyprot_dialog() {
	copyprot_dialog = make_dialog_info(&dialog_settings, &dialog_rect_1, &dialog_rect_1, NULL);
	/*
	 * Upstream also grabbed the peel here:
	 *
	 *     copyprot_dialog->peel = read_peel_from_screen(&copyprot_dialog->peel_rect);
	 *
	 * Not here, because nothing ever reads it. Every use of `copyprot_dialog` in
	 * the tree is `->peel_rect` or `->text_rect` - the dialogs that cover the
	 * screen set `need_full_redraw = 1` and let the next frame repaint instead of
	 * restoring a peel, and the one call that would have restored it
	 * (`restore_dialog_peel_2`, in showmessage) is commented out upstream.
	 *
	 * On a desktop a write-only 220x75 surface is invisible. Here it was 16,500
	 * bytes pinned at the bottom of a 28 KB LIFO arena for the life of the
	 * process - 58% of it - because it is allocated at start-up and never freed,
	 * so nothing above it can ever be reclaimed past it. That left ~12 KB for the
	 * 50 peels `add_peel()` may hold at once plus the temporary `hflip()` mirrors
	 * interleaved between them, and a busy enough room ran out:
	 *
	 *     read_peel_from_screen: SDL_CreateRGBSurface: picosdl: arena exhausted
	 *     (27956 used, 1560 wanted, 28672 total)
	 *
	 * It is also captured before anything has been drawn, so even the pixels were
	 * meaningless. `read_dialog_peel()` still allocates one lazily if a dialog
	 * ever genuinely needs it.
	 */
}

// seg009:0838
int showmessage(char* text,int arg_4,void *arg_0) {
	word key;
	rect_type rect;
	//font_type* saved_font_ptr;
	//surface_type* old_target;
	//old_target = current_target_surface;
	//current_target_surface = onscreen_surface_;
	// In the disassembly there is some messing with the current_target_surface and font (?)
	// However, this does not seem to be strictly necessary
	if (NULL == offscreen_surface) offscreen_surface = make_offscreen_buffer(&screen_rect); // In case we get an error before there is an offsceen buffer
	method_1_blit_rect(offscreen_surface, onscreen_surface_, &copyprot_dialog->peel_rect, &copyprot_dialog->peel_rect, 0);
	draw_dialog_frame(copyprot_dialog);
	//saved_font_ptr = textstate.ptr_font;
	//saved_font_ptr = current_target_surface->ptr_font;
	//current_target_surface->ptr_font = ptr_font;
	shrink2_rect(&rect, &copyprot_dialog->text_rect, 2, 1);
	show_text_with_color(&rect, halign_center, valign_middle, text, color_15_brightwhite);
	//textstate.ptr_font = saved_font_ptr;
	//current_target_surface->ptr_font = saved_font_ptr;
	clear_kbd_buf();
	do {
		idle();
		key = key_test_quit(); // Press any key to continue...
	} while(key == 0);
	//restore_dialog_peel_2(copyprot_dialog->peel);
	//current_target_surface = old_target;
	need_full_redraw = 1; // lazy: instead of neatly restoring only the relevant part, just redraw the whole screen
	return key;
}

// seg009:08FB
dialog_type* make_dialog_info(dialog_settings_type* settings, rect_type* dialog_rect,
                                            rect_type* text_rect, peel_type* dialog_peel) {
	/* One dialog at a time - the game is modal while one is up. */
	static dialog_type s_dialog_info;
	dialog_type* dialog_info = &s_dialog_info;
	memset(dialog_info, 0, sizeof(*dialog_info));
	dialog_info->settings = settings;
	dialog_info->has_peel = 0;
	dialog_info->peel = dialog_peel;
	if (text_rect != NULL)
		dialog_info->text_rect = *text_rect;
	calc_dialog_peel_rect(dialog_info);
	if (text_rect != NULL) {        // does not seem to be quite right; see seg009:0948 (?)
		read_dialog_peel(dialog_info);
	}
	return dialog_info;
}

// seg009:0BE7
void calc_dialog_peel_rect(dialog_type* dialog) {
	dialog_settings_type* settings;
	settings = dialog->settings;
	dialog->peel_rect.left = dialog->text_rect.left - settings->left_border;
	dialog->peel_rect.top = dialog->text_rect.top - settings->top_border;
	dialog->peel_rect.right = dialog->text_rect.right + settings->right_border + settings->shadow_right;
	dialog->peel_rect.bottom = dialog->text_rect.bottom + settings->bottom_border + settings->shadow_bottom;
}

// seg009:0BB0
void read_dialog_peel(dialog_type* dialog) {
	if (dialog->has_peel) {
		if (dialog->peel == NULL) {
			dialog->peel = read_peel_from_screen(&dialog->peel_rect);
		}
		dialog->has_peel = 1;
		draw_dialog_frame(dialog);
	}
}

// seg009:09DE
void draw_dialog_frame(dialog_type* dialog) {
	dialog->settings->method_2_frame(dialog);
}

// A pointer to this function is the first field of dialog_settings (data:2944)
// Perhaps used when replacing a dialog's text with another text (?)
// seg009:096F
void add_dialog_rect(dialog_type* dialog) {
	draw_rect(&dialog->text_rect, color_0_black);
}

// seg009:09F0
void dialog_method_2_frame(dialog_type* dialog) {
	rect_type rect;
	short shadow_right = dialog->settings->shadow_right;
	short shadow_bottom = dialog->settings->shadow_bottom;
	short bottom_border = dialog->settings->bottom_border;
	short outer_border = dialog->settings->outer_border;
	short peel_top = dialog->peel_rect.top;
	short peel_left = dialog->peel_rect.left;
	short peel_bottom = dialog->peel_rect.bottom;
	short peel_right = dialog->peel_rect.right;
	short text_top = dialog->text_rect.top;
	short text_left = dialog->text_rect.left;
	short text_bottom = dialog->text_rect.bottom;
	short text_right = dialog->text_rect.right;
	// Draw outer border
	rect = (rect_type) { peel_top, peel_left, peel_bottom - shadow_bottom, peel_right - shadow_right };
	draw_rect(&rect, color_0_black);
	// Draw shadow (right)
	rect = (rect_type) { text_top, peel_right - shadow_right, peel_bottom, peel_right };
	draw_rect(&rect, get_text_color(0, color_8_darkgray /*dialog's shadow*/, 0));
	// Draw shadow (bottom)
	rect = (rect_type) { peel_bottom - shadow_bottom, text_left, peel_bottom, peel_right };
	draw_rect(&rect, get_text_color(0, color_8_darkgray /*dialog's shadow*/, 0));
	// Draw inner border (left)
	rect = (rect_type) { peel_top + outer_border, peel_left + outer_border, text_bottom, text_left };
	draw_rect(&rect, color_15_brightwhite);
	// Draw inner border (top)
	rect = (rect_type) { peel_top + outer_border, text_left, text_top, text_right + dialog->settings->right_border - outer_border };
	draw_rect(&rect, color_15_brightwhite);
	// Draw inner border (right)
	rect.top = text_top;
	rect.left =  text_right;
	rect.bottom = text_bottom + bottom_border - outer_border;           // (rect.right stays the same)
	draw_rect(&rect, color_15_brightwhite);
	// Draw inner border (bottom)
	rect = (rect_type) { text_bottom, peel_left + outer_border, text_bottom + bottom_border - outer_border, text_right };
	draw_rect(&rect, color_15_brightwhite);
}

// seg009:0C44
void show_dialog(const char* text) {
	char string[256];
	snprintf(string, sizeof(string), "%s\n\nPress any key to continue.", text);
	showmessage(string, 1, &key_test_quit);
}

// seg009:0791
int get_text_center_y(const rect_type* rect) {
	const font_type* font;
	short empty_height; // height of empty space above+below the line of text
	font = &hc_font;//current_target_surface->ptr_font;
	empty_height = rect->bottom - font->height_above_baseline - font->height_below_baseline - rect->top;
	return ((empty_height - empty_height % 2) >> 1) + font->height_above_baseline + empty_height % 2 + rect->top;
}

// seg009:3E77
int get_cstring_width(const char* text) {
	int width = 0;
	const char* text_pos = text;
	char curr_char;
	while ('\0' != (curr_char = *text_pos)) {
		text_pos++;
		width += get_char_width(curr_char);
	}
	return width;
}

// seg009:0767
void draw_text_cursor(int xpos,int ypos,int color) {
	set_curr_pos(xpos, ypos);
	/*current_target_surface->*/textstate.textcolor = color;
	draw_text_character('_');
	//restore_curr_color();
	textstate.textcolor = 15;
}

// seg009:053C
int input_str(const rect_type* rect,char* buffer,int max_length,const char *initial,int has_initial,int arg_4,int color,int bgcolor) {
	// Display the screen keyboard if supported.
	//SDL_SetHint(SDL_HINT_IME_SHOW_UI, "1");
	SDL_Rect sdlrect;
	rect_to_sdlrect(rect, &sdlrect);
	SDL_SetTextInputRect(&sdlrect);
	SDL_StartTextInput();

	word key;
	short current_xpos;
	short length = 0;
	short cursor_visible = 0;
	draw_rect(rect, bgcolor);
	short init_length = strlen(initial);
	if (has_initial) {
		strcpy(buffer, initial);
		length = init_length;
	}
	current_xpos = rect->left + arg_4;
	short ypos = get_text_center_y(rect);
	set_curr_pos(current_xpos, ypos);
	/*current_target_surface->*/textstate.textcolor = color;
	draw_cstring(initial);
	//restore_curr_pos?();
	current_xpos += get_cstring_width(initial) + (init_length != 0) * arg_4;
	do {
		key = 0;
		do {
			if (cursor_visible) {
				draw_text_cursor(current_xpos, ypos, color);
			} else {
				draw_text_cursor(current_xpos, ypos, bgcolor);
			}
			cursor_visible = !cursor_visible;
			start_timer(timer_0, 6);
			if (key) {
				if (cursor_visible) {
					draw_text_cursor(current_xpos, ypos, color);
					cursor_visible = !cursor_visible;
				}
				if (key == SDL_SCANCODE_RETURN) { // Enter
					buffer[length] = 0;
					SDL_StopTextInput();
					return length;
				} else break;
			}
			while (!has_timer_stopped(timer_0) && (key = key_test_quit()) == 0) idle();
		} while (1);
		// Only use the printable ASCII chars (UTF-8 encoding)
		char entered_char = last_text_input <= 0x7E ? last_text_input : 0;
		clear_kbd_buf();

		if (key == SDL_SCANCODE_ESCAPE) { // Esc
			draw_rect(rect, bgcolor);
			buffer[0] = 0;
			SDL_StopTextInput();
			return -1;
		}
		if (length != 0 && (key == SDL_SCANCODE_BACKSPACE ||
				key == SDL_SCANCODE_DELETE)) { // Backspace, Delete
			--length;
			draw_text_cursor(current_xpos, ypos, bgcolor);
			current_xpos -= get_char_width(buffer[length]);
			set_curr_pos(current_xpos, ypos);
			/*current_target_surface->*/textstate.textcolor = bgcolor;
			draw_text_character(buffer[length]);
			//restore_curr_pos?();
			draw_text_cursor(current_xpos, ypos, color);
		}
		else if (entered_char >= 0x20 && entered_char <= 0x7E && length < max_length) {
			// Would the new character make the cursor go past the right side of the rect?
			if (get_char_width('_') + get_char_width(entered_char) + current_xpos < rect->right) {
				draw_text_cursor(current_xpos, ypos, bgcolor);
				set_curr_pos(current_xpos, ypos);
				/*current_target_surface->*/textstate.textcolor = color;
				buffer[length] = entered_char;
				length++;
				current_xpos += draw_text_character(entered_char);
			}
		}
	} while(1);
}

#else // USE_TEXT

// seg009:3706
int draw_text_character(byte character) {
	// stub
	printf("draw_text_character: %c\n",character);
	return 0;
}

// seg009:3E4F
void show_text(const rect_type* rect_ptr,int x_align,int y_align,const char *text) {
	// stub
	printf("show_text: %s\n",text);
}

// seg009:04FF
void show_text_with_color(const rect_type* rect_ptr,int x_align,int y_align,const char* text,int color) {
	//short saved_textcolor;
	//saved_textcolor = textstate.textcolor;
	//textstate.textcolor = color;
	show_text(rect_ptr, x_align, y_align, text);
	//textstate.textcolor = saved_textcolor;
}

// seg009:3A91
void set_curr_pos(int xpos,int ypos) {
	// stub
}

// seg009:0C44
void show_dialog(const char *text) {
	// stub
	puts(text);
}

// seg009:053C
int input_str(const rect_type* rect,char* buffer,int max_length,const char* initial,int has_initial,int arg_4,int color,int bgcolor) {
	// stub
	strncpy(buffer, "dummy input text", max_length);
	return strlen(buffer);
}

int showmessage(char* text,int arg_4,void *arg_0) {
	// stub
	puts(text);
	return 0;
}

void init_copyprot_dialog() {
	// stub
}

void draw_dialog_frame(dialog_type *dialog) {
	// stub
}

void add_dialog_rect(dialog_type *dialog) {
	// stub
}

void dialog_method_2_frame(dialog_type *dialog) {
	// stub
}

#endif // USE_TEXT

// seg009:37E8
void draw_rect(const rect_type* rect,int color) {
	method_5_rect(rect, blitters_0_no_transp, color);
}

// seg009:3985
surface_type *rect_sthg(surface_type* surface,const rect_type* rect) {
	// stub
	return surface;
}

// seg009:39CE
rect_type *shrink2_rect(rect_type* target_rect,const rect_type* source_rect,int delta_x,int delta_y) {
	target_rect->top    = source_rect->top    + delta_y;
	target_rect->left   = source_rect->left   + delta_x;
	target_rect->bottom = source_rect->bottom - delta_y;
	target_rect->right  = source_rect->right  - delta_x;
	return target_rect;
}

// seg009:3BBA
void restore_peel(peel_type* peel_ptr) {
	//printf("restoring peel at (x=%d, y=%d)\n", peel_ptr.rect.left, peel_ptr.rect.top); // debug
	method_6_blit_img_to_scr(peel_ptr->peel, peel_ptr->rect.left, peel_ptr->rect.top, /*0x10*/0);
	free_peel(peel_ptr);
	//SDL_FreeSurface(peel_ptr.peel);
}

// seg009:3BE9
peel_type* read_peel_from_screen(const rect_type* rect) {
	peel_type* result = alloc_peel();
	//memset(&result, 0, sizeof(result));
	result->rect = *rect;
#ifndef USE_ALPHA
	SDL_Surface* peel_surface = SDL_CreateRGBSurface(0, rect->right - rect->left, rect->bottom - rect->top,
	                                                 24, Rmsk, Gmsk, Bmsk, 0);
#else
	SDL_Surface* peel_surface = SDL_CreateRGBSurface(0, rect->right - rect->left, rect->bottom - rect->top, 32, Rmsk, Gmsk, Bmsk, Amsk);
#endif
	if (peel_surface == NULL) {
		sdlperror("read_peel_from_screen: SDL_CreateRGBSurface");
		quit(1);
	}
	result->peel = peel_surface;
	rect_type target_rect = {0, 0, rect->right - rect->left, rect->bottom - rect->top};
	method_1_blit_rect(result->peel, current_target_surface, &target_rect, rect, 0);
	return result;
}

// seg009:3D95
int intersect_rect(rect_type* output,const rect_type* input1,const rect_type* input2) {
	short left = MAX(input1->left, input2->left);
	short right = MIN(input1->right, input2->right);
	if (left < right) {
		output->left = left;
		output->right = right;
		short top = MAX(input1->top, input2->top);
		short bottom = MIN(input1->bottom, input2->bottom);
		if (top < bottom) {
			output->top = top;
			output->bottom = bottom;
			return 1;
		}
	}
	memset(output, 0, sizeof(rect_type));
	return 0;
}

// seg009:4063
rect_type* union_rect(rect_type* output,const rect_type* input1,const rect_type* input2) {
	short top = MIN(input1->top, input2->top);
	short left = MIN(input1->left, input2->left);
	short bottom = MAX(input1->bottom, input2->bottom);
	short right = MAX(input1->right, input2->right);
	output->top = top;
	output->left = left;
	output->bottom = bottom;
	output->right = right;
	return output;
}

enum userevents {
	userevent_SOUND,
	userevent_TIMER,
};

short speaker_playing = 0;
short digi_playing = 0;
short midi_playing = 0;
#if USE_OGG
short ogg_playing = 0;
#endif

#if USE_SPEAKER
// The currently playing sound buffer for the PC speaker.
const speaker_type* current_speaker_sound;
// Index for which note is currently playing.
int speaker_note_index;
// Tracks how long the last (partially played) speaker note has been playing (for the audio callback).
int current_speaker_note_samples_already_emitted;

void speaker_sound_stop(void) {
	if (!speaker_playing) return;
	SDL_LockAudio();
	speaker_playing = 0;
	current_speaker_sound = NULL;
	speaker_note_index = 0;
	current_speaker_note_samples_already_emitted = 0;
	SDL_UnlockAudio();
}
#endif

/*
 * The digi sound currently playing, resampled on the fly.
 *
 * The source is the DAT's own 8-bit unsigned mono data, read straight out of
 * flash. Nothing is expanded and nothing is allocated: the mixer steps a
 * 16.16 fixed-point cursor through the source at source_rate/output_rate and
 * interpolates, instead of the sound being pre-expanded to 16-bit stereo at
 * 44.1 kHz. That expansion was about 16x, and caching all 53 sounds cost on
 * the order of 1.5 MB - more RAM than the board has. See TODO.md.
 *
 * The step is deliberately integer rather than float: this runs in the audio
 * interrupt on core 1, and it is the shape the RP2350 interpolator wants if
 * that optimisation is taken later (one POP per output sample gives the
 * source index).
 */
const byte* digi_source = NULL;      // 8-bit unsigned mono, in flash
int         digi_source_count = 0;   // frames in the source
Uint64      digi_pos = 0;            // 32.32 cursor into the source
Uint64      digi_step = 0;           // 32.32 source frames per output frame

// The properties of the audio device.
SDL_AudioSpec* digi_audiospec = NULL;
// The desired samplerate. Everything will be resampled to this.
/*
 * The output sample rate, and the mixer's CPU budget.
 *
 * 22050, not the 44100 SDLPoP uses on a PC. This is a real-time constraint,
 * not a quality preference:
 *
 *   - the mixer runs in the I2S DMA interrupt on core 1 and has one audio
 *     block to fill before the DMA needs it;
 *   - the block is a fixed 256 frames, so at 44100 it must be filled in
 *     5.80 ms where at 22050 it has 11.61 ms;
 *   - DBOPL generates at the output rate, so doubling the rate also doubles
 *     the synthesis work per second.
 *
 * The mixer was measured on hardware at 38-41% average and 43-64% peak of an
 * audio block's budget - at 22050, which is what picosdl's demo opens. Running
 * the same mixer at 44100 scales that to roughly 86-128% at the peak, i.e.
 * over budget, and a block that misses its deadline is played as whatever the
 * DMA finds: a burst of noise. See TODO.md.
 *
 * Nothing is lost that this game has: every digi sound is 8200, 11000 or
 * 14000 Hz, all below the 11025 Hz Nyquist... the two highest are above it,
 * so they are the only content 22050 actually band-limits, and they are 8-bit
 * effects. Only the OPL's top octave is affected otherwise.
 *
 * Override at build time if the M33's real mixer load turns out to leave room
 * (TODO.md lists this as a number still to retake on the board).
 */
#ifndef PICOPOP_AUDIO_RATE
#define PICOPOP_AUDIO_RATE 22050
#endif
const int digi_samplerate = PICOPOP_AUDIO_RATE;

void stop_digi(void) {
//	SDL_PauseAudio(1);
	if (!digi_playing) return;
	SDL_LockAudio();
	digi_playing = 0;
	/*
//	if (SDL_GetAudioStatus() == SDL_AUDIO_PLAYING) {
		SDL_PauseAudio(1);
		SDL_CloseAudio();
//	}
	if (digi_audiospec != NULL) {
		free(digi_audiospec);
		digi_audiospec = NULL;
	}
	*/
	digi_source = NULL;
	digi_source_count = 0;
	digi_pos = 0;
	digi_step = 0;
	SDL_UnlockAudio();
}

#if USE_OGG
// Decoder for the currently playing OGG sound. (This also holds the playback position.)
stb_vorbis* ogg_decoder;

void stop_ogg(void) {
	SDL_PauseAudio(1);
	if (!ogg_playing) return;
	ogg_playing = 0;
	SDL_LockAudio();
	ogg_decoder = NULL;
	SDL_UnlockAudio();
}
#endif

// seg009:7214
void stop_sounds() {
	// stub
	stop_digi();
	stop_midi();
#if USE_SPEAKER
	speaker_sound_stop();
#endif
#if USE_OGG
	stop_ogg();
#endif
}

#if USE_SPEAKER
short square_wave_state = 4000; // If the amplitude is too high, the speaker sounds will be really loud!
float square_wave_samples_since_last_flip;

void generate_square_wave(byte* stream, float note_freq, int samples) {
	int channels = digi_audiospec->channels;
	float half_period_in_samples = (digi_audiospec->freq / note_freq) * 0.5f;

	int samples_left = samples;
	while (samples_left > 0) {
		if (square_wave_samples_since_last_flip > half_period_in_samples) {
			// Produce a square wave by flipping the signal.
			square_wave_state = ~square_wave_state;
			// Note(Falcury): not completely sure that this is the right way to prevent glitches in the sound...
			// Because I can still hear some hiccups, e.g. in the music. Especially when switching between notes.
			square_wave_samples_since_last_flip -= half_period_in_samples;
		} else {
			int samples_until_next_flip = (int)(half_period_in_samples - square_wave_samples_since_last_flip);
			++samples_until_next_flip; // round up.

			int samples_to_emit = MIN(samples_until_next_flip, samples_left);
			for (int i = 0; i < samples_to_emit * channels; ++i) {
				*(short*)stream = square_wave_state;
				stream += sizeof(short);
			}
			samples_left -= samples_to_emit;
			square_wave_samples_since_last_flip += samples_to_emit;
		}
	}
}

void speaker_callback(void *userdata, Uint8 *stream, int len) {
	int output_channels = digi_audiospec->channels;
	int bytes_per_sample = sizeof(short) * output_channels;
	int samples_requested = len / bytes_per_sample;

	if (current_speaker_sound == NULL) return;
	word tempo = SDL_SwapLE16(current_speaker_sound->tempo);

	int total_samples_left = samples_requested;
	while (total_samples_left > 0) {
		const note_type* note = current_speaker_sound->notes + speaker_note_index;
		if (SDL_SwapLE16(note->frequency) == 0x12 /*end*/) {
			speaker_playing = 0;
			current_speaker_sound = NULL;
			speaker_note_index = 0;
			SDL_Event event;
			memset(&event, 0, sizeof(event));
			event.type = SDL_USEREVENT;
			event.user.code = userevent_SOUND;
			SDL_PushEvent(&event);
			return;
		}

		int note_length_in_samples = (note->length * digi_audiospec->freq) / tempo;
		int note_samples_to_emit = MIN(note_length_in_samples - current_speaker_note_samples_already_emitted, total_samples_left);
		total_samples_left -= note_samples_to_emit;
		size_t copy_len = (size_t)note_samples_to_emit * bytes_per_sample;
		if (SDL_SwapLE16(note->frequency) <= 0x01 /*rest*/) {
			memset(stream, digi_audiospec->silence, copy_len);
		} else {
			generate_square_wave(stream, (float)SDL_SwapLE16(note->frequency), note_samples_to_emit);
		}
		stream += copy_len;

		int note_samples_emitted = current_speaker_note_samples_already_emitted + note_samples_to_emit;
		if (note_samples_emitted < note_length_in_samples) {
			current_speaker_note_samples_already_emitted += note_samples_to_emit;
		} else {
			++speaker_note_index;
			current_speaker_note_samples_already_emitted = 0;
		}
	}
}

// seg009:7640
void play_speaker_sound(const sound_buffer_type* buffer) {
	speaker_sound_stop();
	stop_sounds();
	current_speaker_sound = &buffer->speaker;
	speaker_note_index = 0;
	speaker_playing = 1;
	SDL_PauseAudio(0);
}
#endif

/*
 * Fill one block from the digi sound, resampling as we go.
 *
 * Output is 16-bit signed stereo at digi_audiospec->freq. The source byte is
 * widened the same way the old pre-conversion did - `(v | v<<8) - 32768` -
 * so the result is sample-for-sample what the expanded buffer used to hold,
 * with the same linear interpolation between neighbours.
 */
void digi_callback(void *userdata, Uint8 *stream, int len) {
	short* out = (short*) stream;
	int    frames = len / (2 * (int)sizeof(short));

	for (int i = 0; i < frames; ++i) {
		int sample = digi_resample_sample(digi_source, digi_source_count, digi_pos);

		// Sound off still advances the cursor, so a muted sound still ends.
		short v = is_sound_on ? (short) sample : 0;
		out[i*2] = out[i*2 + 1] = v;

		digi_pos += digi_step;
	}

	// If the sound ended, push an event.
	if (digi_playing && (Sint64)(digi_pos >> 32) >= digi_source_count) {
		SDL_Event event;
		memset(&event, 0, sizeof(event));
		event.type = SDL_USEREVENT;
		event.user.code = userevent_SOUND;
		digi_playing = 0;
		SDL_PushEvent(&event);
	}
}

#if USE_OGG
void ogg_callback(void *userdata, Uint8 *stream, int len) {
	int output_channels = digi_audiospec->channels;
	int bytes_per_sample = sizeof(short) * output_channels;
	int samples_requested = len / bytes_per_sample;

	int samples_filled;
	if (is_sound_on) {
		samples_filled = stb_vorbis_get_samples_short_interleaved(ogg_decoder, output_channels,
                                                                      (short*) stream, len / sizeof(short));
		if (samples_filled < samples_requested) {
			// In case the sound does not fill the buffer: fill the rest of the buffer with silence.
			int bytes_filled = samples_filled * bytes_per_sample;
			int remaining_bytes = (samples_requested - samples_filled) * bytes_per_sample;
			memset(stream + bytes_filled, digi_audiospec->silence, remaining_bytes);
		}
	} else {
		// If sound is off: Mute the sound, but keep track of where we are.
		memset(stream, digi_audiospec->silence, len);
		// Let the decoder run normally (to advance the position), but discard the result.
		byte* discarded_samples = alloca(len);
		samples_filled = stb_vorbis_get_samples_short_interleaved(ogg_decoder, output_channels,
																  (short*) discarded_samples, len / sizeof(short));
	}
	// Push an event if the sound has ended.
	if (samples_filled == 0) {
		//printf("ogg_callback(): sound ended\n");
		SDL_Event event;
		memset(&event, 0, sizeof(event));
		event.type = SDL_USEREVENT;
		event.user.code = userevent_SOUND;
		ogg_playing = 0;
		SDL_PushEvent(&event);
	}
}
#endif // USE_OGG

#ifdef USE_FAST_FORWARD
int audio_speed = 1; // =1 normally, >1 during fast forwarding
#endif

/*
 * Mixer load, as a fraction of each audio block's deadline.
 *
 * The mixer runs in the I2S DMA interrupt on core 1 and has exactly one block
 * of wall clock to produce one block of samples. Over 100% means a block was
 * late and the DMA played whatever was in the buffer - which is heard as a
 * burst of noise, not as a dropout.
 *
 * This is the number TODO.md still owes for the RP2350, and the
 * one that decides whether the output rate can go back up to 44100.
 */
volatile Uint32 pop_mix_us_total, pop_mix_blocks, pop_mix_us_peak;

void pop_audio_load(unsigned* avg_pct, unsigned* peak_pct)
{
	Uint32 blocks = pop_mix_blocks, total = pop_mix_us_total, peak = pop_mix_us_peak;
	pop_mix_blocks = pop_mix_us_total = pop_mix_us_peak = 0;

	unsigned budget = 1;
	if (digi_audiospec != NULL && digi_audiospec->freq > 0)
		budget = 1000000u * (unsigned)digi_audiospec->samples /
		         (unsigned)digi_audiospec->freq;
	if (budget == 0) budget = 1;

	*avg_pct  = blocks ? (unsigned)(total / blocks) * 100u / budget : 0;
	*peak_pct = peak * 100u / budget;
}

void audio_callback(void* userdata, Uint8* stream_orig, int len_orig) {
	Uint64 mix_started = SDL_GetPerformanceCounter();

	Uint8* stream;
	int len;
#ifdef USE_FAST_FORWARD
	if (audio_speed > 1) {
		len = len_orig * audio_speed;
		stream = malloc(len);
	} else
#endif
	{
		len = len_orig;
		stream = stream_orig;
	}

	memset(stream, digi_audiospec->silence, len);
	if (digi_playing) {
		digi_callback(userdata, stream, len);
#if USE_SPEAKER
	} else if (speaker_playing) {
		speaker_callback(userdata, stream, len);
#endif
	}
	// Note: music sounds and digi sounds are allowed to play simultaneously (will be blended together)
	// I.e., digi sounds and music will not cut each other short.
	if (midi_playing) {
		midi_callback(userdata, stream, len);
#if USE_OGG
	} else if (ogg_playing) {
		ogg_callback(userdata, stream, len);
#endif
	}

	{
		Uint32 elapsed = (Uint32)(SDL_GetPerformanceCounter() - mix_started);
		pop_mix_us_total += elapsed;
		pop_mix_blocks++;
		if (elapsed > pop_mix_us_peak)
			pop_mix_us_peak = elapsed;
	}

#ifdef USE_FAST_FORWARD
	if (audio_speed > 1) {

#ifdef FAST_FORWARD_MUTE
		memset(stream_orig, digi_audiospec->silence, len_orig);
#else
#ifdef FAST_FORWARD_RESAMPLE_SOUND
		static SDL_AudioCVT cvt;
		static bool cvt_initialized = false;
		if (!cvt_initialized) {
			SDL_BuildAudioCVT(&cvt,
				digi_audiospec->format, digi_audiospec->channels, digi_audiospec->freq * audio_speed,
				digi_audiospec->format, digi_audiospec->channels, digi_audiospec->freq);
			cvt_initialized = true;
		}
		//realloc(stream, len * cvt.len_mult);
		//cvt.buf = stream;
		cvt.len = len;
		cvt.buf = malloc(cvt.len * cvt.len_mult);
		memcpy(cvt.buf, stream, cvt.len);
		//printf("cvt.needed = %d\n", cvt.needed);
		//printf("cvt.len_mult = %d\n", cvt.len_mult);
		//printf("cvt.len_ratio = %lf\n", cvt.len_ratio);
		SDL_ConvertAudio(&cvt);

		memcpy(stream_orig, cvt.buf, len_orig);
		free(cvt.buf);
		cvt.buf = NULL;
#else
		// Hack: use the beginning of the buffer instead of resampling.
		memcpy(stream_orig, stream, len_orig);
#endif
#endif

		free(stream);
	}
#endif

}

int digi_unavailable = 0;
void init_digi() {
	if (digi_unavailable) return;
	if (digi_audiospec != NULL) return;
	// Open the audio device. Called once.
	//printf("init_digi(): called\n");

	SDL_AudioFormat desired_audioformat;
	SDL_version version;
	SDL_GetVersion(&version);
	//printf("SDL Version = %d.%d.%d\n", version.major, version.minor, version.patch);
	if (version.major <= 2 && version.minor <= 0 && version.patch <= 3) {
		// In versions before 2.0.4, 16-bit audio samples don't work properly (the sound becomes garbled).
		// See: https://bugzilla.libsdl.org/show_bug.cgi?id=2389
		// Workaround: set the audio format to 8-bit, if we are linking against an older SDL2 version.
		desired_audioformat = AUDIO_U8;
		printf("Your SDL.dll is older than 2.0.4. Using 8-bit audio format to work around resampling bug.");
	} else {
		desired_audioformat = AUDIO_S16SYS;
	}

	/* Static, not malloc'd: TODO.md lists this as one of the
	 * remaining heap sites, and there is only ever one audio device. */
	static SDL_AudioSpec spec;
	SDL_AudioSpec *desired = &spec;
	memset(desired, 0, sizeof(*desired));
	desired->freq = digi_samplerate; //buffer->digi.sample_rate;
	desired->format = desired_audioformat;
	desired->channels = 2;
	desired->samples = 1024;
	desired->callback = audio_callback;
	desired->userdata = NULL;
	/*
	 * Take the obtained spec, not the desired one.
	 *
	 * picosdl fixes the block size (PSDL_AUDIO_BLOCK_FRAMES) regardless of
	 * what is asked for, and passing NULL here meant the game went on
	 * believing it had the 1024-frame blocks it requested when the mixer was
	 * really being called for 256. Anything deriving a deadline from
	 * digi_audiospec->samples - such as the mixer load report below - was
	 * wrong by 4x.
	 */
	if (SDL_OpenAudio(desired, desired) != 0) {
		sdlperror("init_digi: SDL_OpenAudio");
		//quit(1);
		digi_unavailable = 1;
		return;
	}
	//SDL_PauseAudio(0);
	digi_audiospec = desired;
}

const int sound_channel = 0;
const int max_sound_id = 58;

#if USE_OGG
void load_sound_names() {
	const char* names_path = locate_file("data/music/names.txt");
	if (sound_names != NULL) return;
	FILE* fp = fopen(names_path,"rt");
	if (fp==NULL) return;
	sound_names = (char**) calloc(sizeof(char*) * max_sound_id, 1);
	while (!feof(fp)) {
		int index;
		char name[POP_MAX_PATH];
		if (fscanf(fp, "%d=%255s\n", &index, /*sizeof(name)-1,*/ name) != 2) {
			perror(names_path);
			continue;
		}
		//if (feof(fp)) break;
		//printf("sound_names[%d] = %s\n",index,name);
		if (index >= 0 && index < max_sound_id) {
			sound_names[index] = strdup(name);
		}
	}
	fclose(fp);
}

char* sound_name(int index) {
	if (sound_names != NULL && index >= 0 && index < max_sound_id) {
		return sound_names[index];
	} else {
		return NULL;
	}
}
#endif

sound_buffer_type* convert_digi_sound(const sound_buffer_type* digi_buffer);

/*
 * Is this one of the sounds that exists only in the PC speaker set?
 *
 * Sounds 31, 34, 38 and 42 of the 0..43 that load_sounds() asks for are held only
 * by IBM_SND1.DAT. No DAT has a digitised or MIDI form of them, so with a sound
 * card rather than the speaker the original game had nothing to play here either.
 *
 * With USE_SPEAKER off there is therefore nothing to find, and looking is what
 * produced four "not available" lines at every start-up. Skipping them keeps that
 * message meaning what it says: a sound that should have been there and was not.
 *
 * Do not resolve this by adding the speaker DATs to the build on their own. Every
 * resource in them is type 0, sound_speaker, and with the speaker cases compiled
 * out one reaching play_sound_from_buffer() falls through to its default, which is
 * quit(1) - turning a silent sound into an exit. Define USE_SPEAKER first; see the
 * note in config.h, which records what that does and does not require.
 */
static int sound_is_speaker_only(int index) {
#if USE_SPEAKER
	(void)index;
	return 0;   /* with speaker support they load like anything else */
#else
	return index == 31 || index == 34 || index == 38 || index == 42;
#endif
}

#ifdef ARV_TANG
/*
 * A sound is kept as its header and where it is on the card; play_digi_sound
 * reads the samples into the VRAM when it plays it (one sound plays at a time).
 */
struct snd_handle {
	uint8_t  file;
	uint32_t off, len;
	int      rid;
	byte     hdr[32];
};
#define SOUND_HANDLE(b) ((struct snd_handle *) ((char *) (b) - offsetof(struct snd_handle, hdr)))

/* The game's sound number of a sound (music.c names its file after it) */
int arv_sound_number(const sound_buffer_type *buffer) {
	return buffer ? SOUND_HANDLE(buffer)->rid - 10000 : -1;
}

static const sound_buffer_type* load_sound_handle(int rid) {
	const struct datfile_s *df;
	const struct pak_entry *e = find_entry(rid, &df);
	if (e == NULL) return NULL;
	struct snd_handle *h = calloc(1, sizeof(*h));
	if (h == NULL) return NULL;
	h->file = df->file;
	h->off = e->off;
	h->len = e->len;
	h->rid = rid;
	pak_read(df, e, h->hdr, (int) sizeof(h->hdr));
	return (const sound_buffer_type*) h->hdr;
}
#endif

const sound_buffer_type* load_sound(int index) {
	const sound_buffer_type* result = NULL;
	//printf("load_sound(%d)\n", index);
	init_digi();
#if USE_OGG
	if (enable_music && !digi_unavailable && result == NULL && index >= 0 && index < max_sound_id) {
		//printf("Trying to load from music folder\n");

		//load_sound_names();  // Moved to load_sounds()
		if (sound_names != NULL && sound_name(index) != NULL) {
			//printf("Loading from music folder\n");
			do {
				FILE* fp = NULL;
				char filename[POP_MAX_PATH];
				if (!skip_mod_data_files) {
					// before checking the root directory, first try mods/MODNAME/
					snprintf_check(filename, sizeof(filename), "%s/music/%s.ogg", mod_data_path, sound_name(index));
					fp = fopen(filename, "rb");
				}
				if (fp == NULL && !skip_normal_data_files) {
					snprintf_check(filename, sizeof(filename), "data/music/%s.ogg", sound_name(index));
					fp = fopen(locate_file(filename), "rb");
				}
				if (fp == NULL) {
					break;
				}
				// Read the entire file (undecoded) into memory.
				struct stat info;
				if (fstat(fileno(fp), &info))
					break;
				size_t file_size = (size_t) MAX(0, info.st_size);
				byte* file_contents = malloc(file_size);
				if (fread(file_contents, 1, file_size, fp) != file_size) {
					free(file_contents);
					fclose(fp);
					break;
				}
				fclose(fp);

				// Decoding the entire file immediately would make the loading time much longer.
				// However, we can also create the decoder now, and only use it when we are actually playing the file.
				// (In the audio callback, we'll decode chunks of samples to the output stream, as needed).
				int error = 0;
				stb_vorbis* decoder = stb_vorbis_open_memory(file_contents, (int)file_size, &error, NULL);
				if (decoder == NULL) {
					printf("Error %d when creating decoder from file \"%s\"!\n", error, filename);
					free(file_contents);
					break;
				}
				result = malloc(sizeof(sound_buffer_type));
				result->type = sound_ogg;
				result->ogg.total_length = stb_vorbis_stream_length_in_samples(decoder) * sizeof(short);
				result->ogg.file_contents = file_contents; // Remember in case we want to free the sound later.
				result->ogg.decoder = decoder;
			} while(0); // do once (breakable block)
		} else {
			//printf("sound_names = %p\n", sound_names);
			//printf("sound_names[%d] = %p\n", index, sound_name(index));
		}
	}
#endif
#ifdef ARV_TANG
	result = load_sound_handle(index + 10000);
#else
	result = (const sound_buffer_type*) load_from_opendats_const(index + 10000, NULL);
#endif
	if (result != NULL && (result->type & 7) == sound_digi) {
		result = convert_digi_sound(result);
	}
	/*
	 * The message means "a sound that should have been there and was not", so it
	 * has to stay quiet in the two cases where absence is expected.
	 *
	 * digi_unavailable is the second of them and covers most of the set:
	 * convert_digi_sound() returns NULL without an output format to convert into,
	 * so with picosdl built without audio (PICOSDL_AUDIO=OFF) every digitised
	 * sound fails and start-up printed 38 lines saying so. Nothing is wrong there -
	 * there is no audio device - and 38 lines of it buries anything that is.
	 */
	if (result == NULL && !skip_normal_data_files &&
	    !sound_is_speaker_only(index) && !digi_unavailable) {
		printf("sound %d not available\n", index);
	}
	return result;
}

#if USE_OGG
void play_ogg_sound(sound_buffer_type *buffer) {
	init_digi();
	if (digi_unavailable) return;
	stop_sounds();

	// Need to rewind the music, or else the decoder might continue where it left off, the last time this sound played.
	stb_vorbis_seek_start(buffer->ogg.decoder);

	SDL_LockAudio();
	ogg_decoder = buffer->ogg.decoder;
	SDL_UnlockAudio();
	SDL_PauseAudio(0);

	ogg_playing = 1;
}
#endif

int wave_version = -1;

typedef struct waveinfo_type {
	int sample_rate, sample_size, sample_count;
	const byte* samples;
} waveinfo_type;

bool determine_wave_version(const sound_buffer_type *buffer, waveinfo_type* waveinfo) {
	int version = wave_version;
	if (version == -1) {
		// Determine the version of the wave data.
		version = 0;
		if (buffer->digi.sample_size == 8) version += 1;
		if (buffer->digi_new.sample_size == 8) version += 2;
		if (version == 1 || version == 2) wave_version = version;
	}

	switch (version) {
		case 1: // 1.0 and 1.1
			waveinfo->sample_rate = SDL_SwapLE16(buffer->digi.sample_rate);
			waveinfo->sample_size = buffer->digi.sample_size;
			waveinfo->sample_count = SDL_SwapLE16(buffer->digi.sample_count);
			waveinfo->samples = buffer->digi.samples;
			return true;
		case 2: // 1.3 and 1.4 (and PoP2)
			waveinfo->sample_rate = SDL_SwapLE16(buffer->digi_new.sample_rate);
			waveinfo->sample_size = buffer->digi_new.sample_size;
			waveinfo->sample_count = SDL_SwapLE16(buffer->digi_new.sample_count);
			waveinfo->samples = buffer->digi_new.samples;
			return true;
		case 3: // ambiguous
			printf("Warning: Ambiguous wave version.\n");
			return false;
		default: // case 0, unknown
			printf("Warning: Can't determine wave version.\n");
			return false;
	}
}

/*
 * Formerly the 16x expansion to 16-bit stereo at 44.1 kHz; now a no-op.
 *
 * The mixer resamples from the source instead (digi_callback above), so the
 * sound is played from the bytes the DAT already holds in flash. Kept as a
 * function because load_sound() and the sound table still call it, and
 * returning the buffer unchanged is what "already usable" means here.
 */
sound_buffer_type* convert_digi_sound(const sound_buffer_type* digi_buffer) {
	init_digi();
	if (digi_unavailable) return NULL;
	waveinfo_type waveinfo;
	if (false == determine_wave_version(digi_buffer, &waveinfo)) return NULL;
	return (sound_buffer_type*) digi_buffer;
}

// seg009:74F0
void play_digi_sound(const sound_buffer_type* buffer) {
	//if (!is_sound_on) return;
	init_digi();
	if (digi_unavailable) return;
	stop_digi();
//	stop_sounds();
	//printf("play_digi_sound(): called\n");
	waveinfo_type waveinfo;
	if ((buffer->type & 7) != sound_digi) {
		printf("Tried to play a non-digi sound (type %d).\n", buffer->type & 7);
		return;
	}
	if (false == determine_wave_version(buffer, &waveinfo)) return;
	if (waveinfo.sample_count == 0 || waveinfo.sample_rate == 0) return;
#ifdef ARV_TANG
	{
		const struct snd_handle *h = SOUND_HANDLE(buffer);
		const uint32_t skip = (uint32_t) (waveinfo.samples - (const byte *) buffer);
		uint32_t n = h->len < VRAM_SOUND_SIZE ? h->len : VRAM_SOUND_SIZE;
		SDL_LockAudio();
		digi_playing = 0;
		SDL_UnlockAudio();
		if (pak_read_at(h->file, h->off, arv_vram + VRAM_SOUND, n) != (int) n) return;
		waveinfo.samples = arv_vram + VRAM_SOUND + skip;
		if (skip + waveinfo.sample_count > n) waveinfo.sample_count = (n > skip) ? n - skip : 0;
	}
#endif

	SDL_LockAudio();
	digi_source       = waveinfo.samples;
	digi_source_count = waveinfo.sample_count;
	digi_pos          = 0;
	digi_step = digi_resample_step(waveinfo.sample_rate,
	                               (unsigned)digi_audiospec->freq);
	digi_playing = 1;
	SDL_UnlockAudio();
	SDL_PauseAudio(0);
}

void free_sound(sound_buffer_type* buffer) {
	if (buffer == NULL) return;
#if USE_OGG
	if (buffer->type == sound_ogg) {
		stb_vorbis_close(buffer->ogg.decoder);
		free(buffer->ogg.file_contents);
	}
#endif
#ifdef ARV_TANG
	free(SOUND_HANDLE(buffer));
#else
	free(buffer);
#endif
}

// seg009:7220
void play_sound_from_buffer(const sound_buffer_type* buffer) {

#ifdef USE_REPLAY
	if (replaying && skipping_replay) return;
#endif

	// stub
	if (buffer == NULL) {
		/*
		 * Once, not every time. This fires from inside the frame loop - the
		 * "Press Button to Continue" blink asks for sound_38_blink twelve times
		 * a second (seg000.c) - and every one of those is a printf to a 115200
		 * UART and to USB from the game loop. load_sound() has already named
		 * which sounds are missing and why, at start-up, so the repeats carry no
		 * information at all.
		 */
		static int warned = 0;
		if (!warned) {
			warned = 1;
			printf("Tried to play a sound that failed to load; see the start-up\n"
			       "  \"sound N not available\" lines. Not repeating this.\n");
		}
		return;
	}
	switch (buffer->type & 7) {
#if USE_SPEAKER
		case sound_speaker:
			play_speaker_sound(buffer);
		break;
#endif
		case sound_digi_converted:
		case sound_digi:
			play_digi_sound(buffer);
		break;
		case sound_midi:
			play_midi_sound(buffer);
		break;
#if USE_OGG
		case sound_ogg:
			play_ogg_sound(buffer);
		break;
#endif
		default:
			printf("Tried to play unimplemented sound type %d.\n", buffer->type);
			quit(1);
		break;
	}
}

void turn_music_on_off(byte new_state) {
	enable_music = new_state;
	turn_sound_on_off(is_sound_on);
}

// seg009:7273
void turn_sound_on_off(byte new_state) {
	// stub
	is_sound_on = new_state;
	//if (!is_sound_on) stop_sounds();
}

// seg009:7299
int check_sound_playing() {
	return speaker_playing || digi_playing || midi_playing
#if USE_OGG
	 || ogg_playing
#endif
	;
}

#if defined(USE_RENDERER) && defined(USE_SCALING)
void apply_aspect_ratio() {
	// Allow us to use a consistent set of screen co-ordinates, even if the screen size changes
	if (use_correct_aspect_ratio) {
		SDL_RenderSetLogicalSize(renderer_, 320 * 5, 200 * 6); // 4:3
	} else {
		SDL_RenderSetLogicalSize(renderer_, 320, 200); // 16:10
	}
	window_resized();
}
#endif

#if defined(USE_RENDERER) && defined(USE_SCALING)
void window_resized() {
#if SDL_VERSION_ATLEAST(2,0,5) // SDL_RenderSetIntegerScale
	if (use_integer_scaling) {
		int window_width, window_height;
		// On high-DPI screens, this is what we need instead of SDL_GetWindowSize().
		//SDL_GL_GetDrawableSize(window_, &window_width, &window_height);
		SDL_GetRendererOutputSize(renderer_, &window_width, &window_height);
		int render_width, render_height;
		SDL_RenderGetLogicalSize(renderer_, &render_width, &render_height);
		// Disable integer scaling if it would result in downscaling.
		// Because then the only suitable integer scaling factor is zero, i.e. the picture disappears.
		SDL_bool makes_sense = (window_width >= render_width && window_height >= render_height);
		SDL_RenderSetIntegerScale(renderer_, makes_sense);
	}
#endif
}
#endif

/*
 * The overlay pair, allocated on demand.
 *
 * `overlay_surface` is what the pause menu and the debug timer box draw into,
 * and `merged_surface` is where it is composited over the game before being
 * shown. Two more full screens - 125 KB - which upstream allocates
 * unconditionally at start-up because on a PC it does not matter.
 *
 * Here it does: they would take two more slots from picosdl's screen pool, and
 * the pool has two. Making this lazy means the base game runs in the two the
 * footprint table in picosdl/README.md accounts for, and only opening the menu
 * costs more.
 */
void init_overlay(void) {
	if (overlay_surface == NULL) {
		overlay_surface = SDL_CreateRGBSurface(0, 320, 200, 8, 0, 0, 0, 0);
		merged_surface  = SDL_CreateRGBSurface(0, 320, 200, 8, 0, 0, 0, 0);
		if (overlay_surface == NULL || merged_surface == NULL) {
			printf("init_overlay: %s\n", SDL_GetError());
			printf("  the overlay wants two more 320x200 surfaces, and picosdl\n"
			       "  serves SDL_CreateRGBSurface() from a 16 KB arena - so the\n"
			       "  menu needs two client-owned buffers, the way the screen has\n"
			       "  one, not a bigger arena.\n");
			overlay_surface = merged_surface = NULL;
		}
	}
}

#ifdef USE_RENDERER
#ifdef USE_SCALING
SDL_Surface* onscreen_surface_2x;

void init_scaling(void) {
	// Don't crash in validate mode.
	if (renderer_ == NULL) return;

	if (texture_sharp == NULL) {
		texture_sharp = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, 320, 200);
	}
	if (scaling_type == 1) {
		if (!is_renderer_targettexture_supported && onscreen_surface_2x == NULL) {
#ifdef __amigaos4__
			overlay_surface = SDL_CreateRGBSurface(0, 320*2, 200*2, 24, Rmsk, Gmsk, Bmsk, 0);
#else
			onscreen_surface_2x = SDL_CreateRGBSurface(0, 320*2, 200*2, 24, Rmsk, Gmsk, Bmsk, 0);
#endif
		}
		if (texture_fuzzy == NULL) {
			SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
			int access = is_renderer_targettexture_supported ? SDL_TEXTUREACCESS_TARGET : SDL_TEXTUREACCESS_STREAMING;
			texture_fuzzy = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGB24, access, 320*2, 200*2);
			SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
		}
		target_texture = texture_fuzzy;
	} else if (scaling_type == 2) {
		if (texture_blurry == NULL) {
			SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
			texture_blurry = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, 320, 200);
			SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
		}
		target_texture = texture_blurry;
	} else {
		target_texture = texture_sharp;
	}
	if (target_texture == NULL) {
		sdlperror("init_scaling: SDL_CreateTexture");
		quit(1);
	}
}
#else
// Simple init for renderer mode without scaling
void init_scaling(void) {
	// Don't crash in validate mode.
	if (renderer_ == NULL) return;

	if (texture_sharp == NULL) {
		texture_sharp = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, 320, 200);
		if (texture_sharp == NULL) {
			sdlperror("init_scaling: SDL_CreateTexture");
			quit(1);
		}
	}
	target_texture = texture_sharp;
}
#endif
#endif

// seg009:38ED
/*
 * The screen framebuffer.
 *
 * picosdl allocates no pixels anywhere: every buffer it touches belongs to the
 * client, which is what lets the library carry no full-screen memory of its own.
 * So this is ours, it has to outlive the window, and a static in .bss is the
 * simplest thing that is both. 62.5 KB.
 *
 * One buffer, not two: see PICOPOP_ONE_SCREEN_BUFFER. A second would go here
 * beside it, and nothing in picosdl would need rebuilding to use it.
 *
 * Pitch is in bytes and rows must stay 4-byte aligned. 320 already is, so the
 * pitch is the width; anything else would need (w + 3) & ~3.
 */
#define PICOPOP_FB_W 320
#define PICOPOP_FB_H 200
#ifdef ARV_TANG
/* The game's frame in the VRAM, copied to the displayed one at each present */
#define picopop_screen_fb ((Uint8 (*)[PICOPOP_FB_W]) (arv_vram + VRAM_BACK))
#else
static Uint8 picopop_screen_fb[PICOPOP_FB_H][PICOPOP_FB_W];
#endif

void set_gr_mode(byte grmode) {
#ifdef SDL_HINT_WINDOWS_DISABLE_THREAD_NAMING
	SDL_SetHint(SDL_HINT_WINDOWS_DISABLE_THREAD_NAMING, "1");
#endif
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_NOPARACHUTE | SDL_INIT_GAMECONTROLLER) != 0) {
		sdlperror("set_gr_mode: SDL_Init");
		quit(1);
	}
	if (enable_controller_rumble) {
		if (SDL_InitSubSystem(SDL_INIT_HAPTIC) != 0) {
			printf("Warning: Haptic subsystem unavailable, ignoring enable_controller_rumble = true\n");
		}
	}

	//SDL_EnableUNICODE(1); //deprecated
	Uint32 flags = 0;
	if (!start_fullscreen) start_fullscreen = check_param("full") != NULL;
	if (start_fullscreen) flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
#ifdef USE_SCALING
	flags |= SDL_WINDOW_RESIZABLE;
#endif
	flags |= SDL_WINDOW_ALLOW_HIGHDPI; // for Retina displays

	// Should use different default window dimensions when using 4:3 aspect ratio
#ifdef USE_SCALING
	if (use_correct_aspect_ratio && pop_window_width == 640 && pop_window_height == 400) {
		pop_window_height = 480;
	}
#else
	// When scaling is disabled, use native 320x200 window size
	pop_window_width = 320;
	pop_window_height = 200;
#endif

#if _WIN32
	// Tell Windows that the application is DPI aware, to prevent unwanted bitmap stretching.
	// SetProcessDPIAware() is only available on Windows Vista and later, so we need to load it dynamically.
	typedef BOOL (WINAPI *dpiaware)(void);
	HMODULE user32dll = LoadLibraryA("User32.dll");
	if (user32dll) {
		dpiaware SetProcessDPIAware = (dpiaware)GetProcAddress(user32dll, "SetProcessDPIAware");
		if (SetProcessDPIAware) {
			SetProcessDPIAware();
		}
		FreeLibrary(user32dll);
	}
#endif

#ifdef USE_REPLAY
	if (!is_validate_mode) // run without a window if validating a replay
#endif
	/*
	 * PSDL_CreateWindow() takes the buffer instead of a size to allocate.
	 * Title, position and flags are gone from the call because none of them
	 * ever meant anything on a soldered-on panel; `flags` is still computed
	 * above for the side effect on start_fullscreen.
	 */
	(void)flags;
	window_ = PSDL_CreateWindow(picopop_screen_fb,
	                            PICOPOP_FB_W, PICOPOP_FB_H, PICOPOP_FB_W);
#ifdef USE_RENDERER
	// Make absolutely sure that VSync will be off, to prevent timer issues.
	SDL_SetHint(SDL_HINT_RENDER_VSYNC, "0");
	flags = 0;
	switch (use_hardware_acceleration) {
		case 0:  flags |= SDL_RENDERER_SOFTWARE;    break;
		case 1:  flags |= SDL_RENDERER_ACCELERATED; break;
		case 2:  // let SDL decide
		         // fallthrough!
		default: break;
	}
	renderer_ = SDL_CreateRenderer(window_, -1 , flags | SDL_RENDERER_TARGETTEXTURE);
	SDL_RendererInfo renderer_info;
	if (SDL_GetRendererInfo(renderer_, &renderer_info) == 0) {
		if (renderer_info.flags & SDL_RENDERER_TARGETTEXTURE) {
			is_renderer_targettexture_supported = true;
		}
	}
#ifdef USE_SCALING
	if (use_integer_scaling) {
#if SDL_VERSION_ATLEAST(2,0,5) // SDL_RenderSetIntegerScale
		SDL_RenderSetIntegerScale(renderer_, SDL_TRUE);
#else
		printf("Warning: You need to compile with SDL 2.0.5 or newer for the use_integer_scaling option.\n");
#endif
	}
#endif
#else
	// Software rendering mode - get the window surface
	window_surface_ = SDL_GetWindowSurface(window_);
	if (window_surface_ == NULL) {
		sdlperror("set_gr_mode: SDL_GetWindowSurface");
		quit(1);
	}
#endif

#if USE_SDL_IMAGE
	SDL_Surface* icon = IMG_Load(locate_file("data/icon.png"));
	if (icon == NULL) {
		sdlperror("set_gr_mode: Could not load icon");
	} else {
		SDL_SetWindowIcon(window_, icon);
	}
#endif

#if defined(USE_RENDERER) && defined(USE_SCALING)
	apply_aspect_ratio();
	window_resized();
#endif

	/* Migration to SDL2: everything is still blitted to onscreen_surface_, however:
	 * SDL2 renders textures to the screen instead of surfaces; so, every screen
	 * update causes the onscreen_surface_ to be copied into the target_texture, which is
	 * subsequently displayed.
	 * The function handling the screen updates is update_screen()
	 * */
	/*
	 * The onscreen surface *is* the window surface.
	 *
	 * Upstream this was a standalone 24bpp surface that update_screen() copied
	 * into a texture for the renderer to present. There is no renderer here and
	 * no second format: picosdl's window surface is the 8bpp framebuffer the
	 * panel's DMA reads, so drawing to it directly is what presenting means.
	 * Allocating a separate one would also cost a third screen-pool slot, and
	 * there are two (picosdl/README.md).
	 */
	onscreen_surface_ = SDL_GetWindowSurface(window_);
	if (onscreen_surface_ == NULL) {
		sdlperror("set_gr_mode: SDL_GetWindowSurface");
		quit(1);
	}
	/* init_overlay() is deferred until something actually needs the overlay. */
#ifdef USE_RENDERER
	init_scaling();
#endif
	if (start_fullscreen) {
		SDL_ShowCursor(SDL_DISABLE);
	}


	//SDL_WM_SetCaption(WINDOW_TITLE, NULL);
//	if (SDL_EnableKeyRepeat(SDL_DEFAULT_REPEAT_DELAY, SDL_DEFAULT_REPEAT_INTERVAL) != 0) {  //deprecated
//		sdlperror("set_gr_mode: SDL_EnableKeyRepeat");
//		quit(1);
//	}
	graphics_mode = gmMcgaVga;
#ifdef USE_TEXT
	load_font();
#endif
}

SDL_Surface* get_final_surface() {
	if (!is_overlay_displayed) {
		return onscreen_surface_;
	} else {
		return merged_surface;
	}
}

void draw_overlay(void) {
	int overlay = 0;
	is_overlay_displayed = false;
#ifdef USE_DEBUG_CHEATS
	if (is_timer_displayed && start_level > 0) overlay = 1; // Timer overlay
	else if (fixes->fix_quicksave_during_feather &&
				is_feather_timer_displayed &&
				start_level > 0 &&
				is_feather_fall > 0) {
		overlay = 3; // Feather timer overlay
	}
#endif
#ifdef USE_MENU
	// Menu overlay - not drawn here directly, only copied from the overlay surface.
	if (is_paused && is_menu_shown) overlay = 2;
#endif
	if (overlay != 0) {
		is_overlay_displayed = true;
		surface_type* saved_target_surface = current_target_surface;
		current_target_surface = overlay_surface;
		rect_type drawn_rect;
		if (overlay == 1) {
#ifdef USE_DEBUG_CHEATS
			char timer_text[32];
			if (rem_min < 0) {
				snprintf(timer_text, sizeof(timer_text), "%02d:%02d:%02d",
				         -(rem_min + 1), (719 - rem_tick) / 12, (719 - rem_tick) % 12);
			} else {
				snprintf(timer_text, sizeof(timer_text), "%02d:%02d:%02d",
				         rem_min - 1, rem_tick / 12, rem_tick % 12);
			}
			int expected_numeric_chars = 6;
			int extra_numeric_chars = MAX(0, (int)strnlen(timer_text, sizeof(timer_text)) - 8);
			int line_width = 5 + (expected_numeric_chars + extra_numeric_chars) * 9;

			rect_type timer_box_rect = {0, 0, 11, 2 + line_width};
			rect_type timer_text_rect = {2, 2, 10, 100};
			draw_rect_with_alpha(&timer_box_rect, color_0_black, 128);
			show_text(&timer_text_rect, halign_left, valign_top, timer_text);

#ifdef USE_REPLAY
			// During playback, display the number of ticks since start, if the timer is shown (debug cheats: T).
			if (replaying) {
				char ticks_text[12];
				snprintf(ticks_text, sizeof(ticks_text), "T: %d", curr_tick);
				rect_type ticks_box_rect = timer_box_rect;
				ticks_box_rect.top += 12;
				ticks_box_rect.bottom += 12;
				rect_type ticks_text_rect = timer_text_rect;
				ticks_text_rect.top += 12;
				ticks_text_rect.bottom += 12;

				draw_rect_with_alpha(&ticks_box_rect, color_0_black, 128);
				show_text(&ticks_text_rect, halign_left, valign_top, ticks_text);

				timer_box_rect.bottom += 12;
			}
#endif

			drawn_rect = timer_box_rect; // Only need to blit this bit to the merged_surface.
#endif
		} else if (overlay == 3) { // Feather timer
#ifdef USE_DEBUG_CHEATS
			char timer_text[32];
			int ticks_per_sec = get_ticks_per_sec(timer_1);
			snprintf(timer_text, sizeof(timer_text), "%02d:%02d", is_feather_fall / ticks_per_sec, is_feather_fall % ticks_per_sec);
			int expected_numeric_chars = 6;
			int extra_numeric_chars = MAX(0, (int)strnlen(timer_text, sizeof(timer_text)) - 8);
			int line_width = 5 + (expected_numeric_chars + extra_numeric_chars) * 9;

			rect_type timer_box_rect = {0, 0, 11, 2 + line_width};
			rect_type timer_text_rect = {2, 2, 10, 100};
			draw_rect_with_alpha(&timer_box_rect, color_0_black, 128);
			show_text_with_color(&timer_text_rect, halign_left, valign_top, timer_text, color_10_brightgreen);

			drawn_rect = timer_box_rect; // Only need to blit this bit to the merged_surface.
#endif
		} else {
			drawn_rect = screen_rect; // We'll blit the whole contents of overlay_surface to the merged_surface.
		}
		SDL_Rect sdl_rect;
		rect_to_sdlrect(&drawn_rect, &sdl_rect);
		SDL_BlitSurface(onscreen_surface_, NULL, merged_surface, NULL);
		SDL_BlitSurface(overlay_surface, &sdl_rect, merged_surface, &sdl_rect);
		current_target_surface = saved_target_surface;
	}
}

void update_screen() {
	draw_overlay();
	SDL_Surface* surface = get_final_surface();
#ifdef USE_RENDERER
	init_scaling();
#ifdef USE_SCALING
	if (scaling_type == 1) {
		// Make "fuzzy pixels" like DOSBox does:
		// First scale to double size with nearest-neighbor scaling, then scale to full screen with smooth scaling.
		// The result is not as blurry as if we did only a smooth scaling, but not as sharp as if we did only nearest-neighbor scaling.
		if (is_renderer_targettexture_supported) {
			SDL_UpdateTexture(texture_sharp, NULL, surface->pixels, surface->pitch);
			SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
			SDL_SetRenderTarget(renderer_, target_texture);
			SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
			SDL_RenderClear(renderer_);
			SDL_RenderCopy(renderer_, texture_sharp, NULL, NULL);
			SDL_SetRenderTarget(renderer_, NULL);
		} else {
			SDL_BlitScaled(surface, NULL, onscreen_surface_2x, NULL);
			surface = onscreen_surface_2x;
			SDL_UpdateTexture(target_texture, NULL, surface->pixels, surface->pitch);
		}
	} else {
		SDL_UpdateTexture(target_texture, NULL, surface->pixels, surface->pitch);
	}
#else
	// Simple renderer mode without scaling - just update the texture directly
	SDL_UpdateTexture(target_texture, NULL, surface->pixels, surface->pitch);
#endif
	SDL_RenderClear(renderer_);
	SDL_RenderCopy(renderer_, target_texture, NULL, NULL);
	SDL_RenderPresent(renderer_);
#else
	/*
	 * Software rendering mode - blit surface directly to window surface.
	 *
	 * Except that here there is usually nothing to blit. `window_surface_` and
	 * `onscreen_surface_` are both SDL_GetWindowSurface(window_), and picosdl
	 * has exactly one window surface, so they are the same pointer.
	 * get_final_surface() returns onscreen_surface_ whenever no overlay is up -
	 * so this was a 62.5 KB blit of the framebuffer onto itself, once per
	 * present, for nothing.
	 *
	 * It is a real copy only when get_final_surface() returns `merged_surface`,
	 * which needs the pause menu or the debug timer. Both USE_MENU and
	 * USE_DEBUG_CHEATS are off in config.h, and init_overlay() would in any case
	 * fail for want of a third and fourth screen-pool slot, so as built the
	 * comparison is always true and the blit never runs. It is written as a
	 * pointer comparison rather than deleted because it then stays correct if
	 * the overlay is ever enabled; it is exact, not a heuristic.
	 *
	 * Upstream cannot hit this: there the window surface is the OS's and
	 * onscreen_surface_ is a separate 320x200 buffer, so the blit always moves
	 * pixels. See picosdl/README.md.
	 */
	if (surface != NULL && window_surface_ != NULL && surface != window_surface_) {
#ifdef USE_SCALING
		// Scale the 320x200 game surface to fit the window surface
		if (SDL_BlitScaled(surface, NULL, window_surface_, NULL) != 0) {
			sdlperror("update_screen: SDL_BlitScaled");
		}
#else
		// Direct 1:1 blitting without scaling
		if (SDL_BlitSurface(surface, NULL, window_surface_, NULL) != 0) {
			sdlperror("update_screen: SDL_BlitSurface");
		}
#endif
	}
	/* Outside the blit: the frame still has to be pushed to the panel even when
	 * there was no compositing to do. */
	if (window_surface_ != NULL) {
#ifdef PICOPOP_ONE_SCREEN_BUFFER
		/* Composing, not yet revealed: leave the panel showing the last frame. */
		if (!s_screen_revealed)
			return;
		s_push_pending = 0;
#endif
		push_screen_now();
	}
#endif
}

// seg009:9289
/*
 * Write a run of palette entries.
 *
 * Batched into a single SDL_SetPaletteColors rather than looping over set_pal,
 * because on hardware each call reaches the CLUT the panel's DMA chain is
 * reading and has to sync against a transfer in flight. The fades call this
 * sixteen entries at a time (fade_in_frame, fade_out_frame), so batching turns
 * a fade step from sixteen syncs into one.
 */
void set_pal_arr(int start,int count,const rgb_type* array) {
	if (count <= 0) return;
	if (start < 0 || start + count > 256) return;

	SDL_Color colors[256];
	for (int i = 0; i < count; ++i) {
		rgb_type c = array ? array[i] : (rgb_type){0, 0, 0};
		palette[start + i] = c;              // the game's 6-bit shadow copy
		colors[i].r = pal6_to_8(c.r);        // the CLUT wants 8-bit
		colors[i].g = pal6_to_8(c.g);
		colors[i].b = pal6_to_8(c.b);
		colors[i].a = SDL_ALPHA_OPAQUE;
	}
	SDL_SetPaletteColors(PSDL_GlobalPalette(), colors, start, count);
}


// seg009:92DF
/*
 * One palette entry. The arguments are 6-bit, as the DAT stores them; the CLUT
 * is 8-bit, hence the shift. `palette[]` is kept as the game's own shadow copy
 * because read_palette_256(), set_bg_attr() and the fade buffers all read it.
 */
void set_pal(int index,int red,int green,int blue) {
	if (index < 0 || index > 255) return;
	palette[index].r = red;
	palette[index].g = green;
	palette[index].b = blue;

	SDL_Color color = { pal6_to_8(red), pal6_to_8(green),
	                    pal6_to_8(blue), SDL_ALPHA_OPAQUE };
	SDL_SetPaletteColors(PSDL_GlobalPalette(), &color, index, 1);
}

// seg009:969C
int add_palette_bits(byte n_colors) {
	// stub
	return 0;
}

// seg009:9C36
int find_first_pal_row(int which_rows_mask) {
	word which_row = 0;
	word row_mask = 1;
	do {
		if (row_mask & which_rows_mask) {
			return which_row;
		}
		++which_row;
		row_mask <<= 1;
	} while (which_row < 16);
	return 0;
}

// seg009:9C6C
int get_text_color(int cga_color,int low_half,int high_half_mask) {
	if (graphics_mode == gmCga || graphics_mode == gmHgaHerc) {
		return cga_color;
	} else if (graphics_mode == gmMcgaVga && high_half_mask != 0) {
		return (find_first_pal_row(high_half_mask) << 4) + low_half;
	} else {
		return low_half;
	}
}

#ifdef ARV_TANG
// seg009:9F34
void close_dat(dat_type* pointer) {
	dat_type** prev = &dat_chain_ptr;
	dat_type* curr = dat_chain_ptr;
	while (curr != NULL) {
		if (curr == pointer) {
			*prev = curr->next_dat;
			pak_close((struct datfile_s *) curr->df);
			release_dat(curr);
			return;
		}
		curr = curr->next_dat;
		prev = &((*prev)->next_dat);
	}
}

/*
 * A raw resource, read from the card into the heap and kept: the game asks
 * this way only for small tables (palettes, the MIDI instruments) and the
 * sound headers (load_sound keeps only the header, see there).
 */
const char *load_from_opendats_const(int resource_id, int* out_size) {
	const struct datfile_s *df;
	const struct pak_entry *e = find_entry(resource_id, &df);
	if (e == NULL) return NULL;
	char *buf = malloc(e->len ? e->len : 1);
	if (buf == NULL || pak_read(df, e, buf, (int) e->len) != (int) e->len) {
		free(buf);
		return NULL;
	}
	if (out_size) *out_size = (int) e->len;
	return buf;
}

// seg009:A172
int load_from_opendats_to_area(int resource_id, void *area, int length) {
	const struct datfile_s *df;
	const struct pak_entry *e = find_entry(resource_id, &df);
	if (e == NULL) {
		fprintf(stderr, "Resource %d not found\n", resource_id);
		return -1;
	}
	if ((int) e->len != length) {
		fprintf(stderr, "Size mismatch: %d != %d\n", length, (int) e->len);
		return -1;
	}
	return pak_read(df, e, area, length) == length ? 0 : -1;
}
#else
static int load_from_opendats_metadata(int resource_id, const char **data, int* size) {
	// Go through all open DAT files.
	for (dat_type* pointer = dat_chain_ptr; pointer != NULL; pointer = pointer->next_dat) {
		const struct datfile_s *df = pointer->df;
		for (unsigned i = 0; i < df->resources_count; i++) {
			const struct resource_s *r = df->resources[i];
			if (resource_id == r->resource_id) {
				*data = (const char *) r->data;
				*size = (int) r->data_len;
				return 0;
			}
		}
	}
	return -1;
}

// seg009:9F34
void close_dat(dat_type* pointer) {
	dat_type** prev = &dat_chain_ptr;
	dat_type* curr = dat_chain_ptr;
	while (curr != NULL) {
		if (curr == pointer) {
			*prev = curr->next_dat;
			release_dat(curr);
			return;
		}
		curr = curr->next_dat;
		prev = &((*prev)->next_dat);
	}
	// stub
}

// seg009:9F80
const char *load_from_opendats_const(int resource_id, int* out_size) {
	int df_size;
	const char *df_data;
	if (0 == load_from_opendats_metadata(resource_id, &df_data, &df_size)) {
		if (out_size) {
			*out_size = df_size;
		}
		return df_data;
	}
	return NULL;
}

// seg009:A172
int load_from_opendats_to_area(int resource_id, void *area, int length) {
	int df_size;
	const char *df_data;
	if (0 == load_from_opendats_metadata(resource_id, &df_data, &df_size)) {
		if (df_size == length) {
			memcpy(area, df_data, length);
			return 0;
		}
		fprintf(stderr, "Size mismatch: %d != %d\n", length, df_size);
		return -1;
	}
	fprintf(stderr, "Resource %d not found\n", resource_id);
	return -1;
}

#endif

// SDL-specific implementations

void rect_to_sdlrect(const rect_type* rect, SDL_Rect* sdlrect) {
	sdlrect->x = rect->left;
	sdlrect->y = rect->top;
	sdlrect->w = rect->right - rect->left;
	sdlrect->h = rect->bottom - rect->top;
}

/*
 * Copy a rectangle between two surfaces.
 *
 * Same per-call transparency choice as method_6: source_surface may be a const
 * flash sprite, so SDL_SetColorKey on it is not available.
 */
void method_1_blit_rect(surface_type* target_surface,surface_type* source_surface,const rect_type* target_rect, const rect_type* source_rect,int blit) {
#ifdef PICOPOP_ONE_SCREEN_BUFFER
	/*
	 * The game copying offscreen onto the screen *is* the reveal, whatever part of
	 * it is being copied: the fade's per-step blit, and the title sequence's
	 * partial copies of rect_titles, which is how each of the three banners
	 * appears - each blit reveals the banner drawn in the step before.
	 *
	 * With one buffer the copy itself does nothing, since the two surfaces are
	 * views of the same pixels. Only the release matters.
	 *
	 * Not sufficient on its own: a caller that *skips* the copy because it would
	 * be a no-op skips the release with it. copy_screen_rect() does exactly that,
	 * and it is the reveal gameplay and the cutscenes depend on, so it releases
	 * for itself.
	 */
	if (target_surface == onscreen_surface_ && source_surface == offscreen_surface)
		picopop_compose_reveal();
#endif
	SDL_Rect src_rect;
	rect_to_sdlrect(source_rect, &src_rect);
	SDL_Rect dest_rect;
	rect_to_sdlrect(target_rect, &dest_rect);

	if (PSDL_BlitTransp(source_surface, &src_rect, target_surface, &dest_rect,
	                    blit != blitters_0_no_transp) != 0) {
		sdlperror("method_1_blit_rect: PSDL_BlitTransp");
		quit(1);
	}
}

/*
 * Draw every non-transparent pixel of an image as one colour.
 *
 * This is how the built-in font and the depth-1 images are drawn. Upstream had
 * to convert the image to ARGB8888, walk every pixel rewriting RGB while
 * preserving alpha, blit, and free - a full-size temporary surface per glyph.
 * PSDL_BlitMono does it in one pass with no allocation.
 */
image_type* method_3_blit_mono(image_type* image,int xpos,int ypos,int blitter,byte color) {
	(void) blitter;
	if (image == NULL) return NULL;
	SDL_Rect src_rect  = {0, 0, image->w, image->h};
	SDL_Rect dest_rect = {xpos, ypos, image->w, image->h};
	if (PSDL_BlitMono(image, &src_rect, current_target_surface, &dest_rect, color) != 0) {
		sdlperror("method_3_blit_mono: PSDL_BlitMono");
		quit(1);
	}
	return image;
}

#if 0
// Workaround for a bug in SDL2 (before v2.0.4):
// https://bugzilla.libsdl.org/show_bug.cgi?id=2986
// SDL_FillRect onto a 24-bit surface swaps Red and Blue component

bool RGB24_bug_checked = false;
bool RGB24_bug_affected;

bool RGB24_bug_check(void) {
	if (!RGB24_bug_checked) {
		// Check if the bug occurs in this version of SDL.
		SDL_Surface* test_surface = SDL_CreateRGBSurface(0, 1, 1, 24, 0, 0, 0, 0);
		if (NULL == test_surface) sdlperror("SDL_CreateSurface in RGB24_bug_check");
		// Fill with red.
		SDL_FillRect(test_surface, NULL, SDL_MapRGB(test_surface->format, 0xFF, 0, 0));
		if (0 != SDL_LockSurface(test_surface)) sdlperror("SDL_LockSurface in RGB24_bug_check");
		// Read red component of pixel.
		RGB24_bug_affected = (*(Uint32*)test_surface->pixels & test_surface->format->Rmask) == 0;
		SDL_UnlockSurface(test_surface);
		SDL_FreeSurface(test_surface);
		RGB24_bug_checked = true;
	}
	return RGB24_bug_affected;
}
#endif

int safe_SDL_FillRect(SDL_Surface* dst, const SDL_Rect* rect, Uint32 color) {
#if 0
	if (dst->format->BitsPerPixel == 24 && RGB24_bug_check()) {
		// In the buggy version, SDL_FillRect swaps R and B, so we swap it once more.
		color = ((color & 0xFF) << 16) | (color & 0xFF00) | ((color & 0xFF0000) >> 16);
	}
#endif
	return SDL_FillRect(dst, rect, color);
}
// End of workaround.

/*
 * Fill a rectangle with a palette colour.
 *
 * `color` is already an index into the global palette, which is what the
 * framebuffer stores, so the whole map-to-RGB step upstream needed is gone.
 */
const rect_type* method_5_rect(const rect_type* rect,int blit,byte color) {
	(void) blit;
	SDL_Rect dest_rect;
	rect_to_sdlrect(rect, &dest_rect);
	if (safe_SDL_FillRect(current_target_surface, &dest_rect, color) != 0) {
		sdlperror("method_5_rect: SDL_FillRect");
		quit(1);
	}
	return rect;
}

void draw_rect_with_alpha(const rect_type* rect, byte color, byte alpha) {
	SDL_Rect dest_rect;
	rect_to_sdlrect(rect, &dest_rect);
	rgb_type palette_color = palette[color];
	uint32_t rgb_color = SDL_MapRGBA(overlay_surface->format, palette_color.r<<2, palette_color.g<<2, palette_color.b<<2, alpha);
	if (safe_SDL_FillRect(current_target_surface, &dest_rect, rgb_color) != 0) {
		sdlperror("draw_rect_with_alpha: SDL_FillRect");
		quit(1);
	}
}

void draw_rect_contours(const rect_type* rect, byte color) {
	// TODO: handle 24 bit surfaces? (currently, 32 bit surface is assumed)
	if (current_target_surface->format->BitsPerPixel != 32) {
		printf("draw_rect_contours: not implemented for %d bit surfaces\n", current_target_surface->format->BitsPerPixel);
		return;
	}
	SDL_Rect dest_rect;
	rect_to_sdlrect(rect, &dest_rect);
	rgb_type palette_color = palette[color];
	uint32_t rgb_color = SDL_MapRGBA(overlay_surface->format, palette_color.r<<2, palette_color.g<<2, palette_color.b<<2, 0xFF);
	if (SDL_LockSurface(current_target_surface) != 0) {
		sdlperror("draw_rect_contours: SDL_LockSurface");
		quit(1);
	}
	int bytes_per_pixel = current_target_surface->format->BytesPerPixel;
	int pitch = current_target_surface->pitch;
	byte* pixels = current_target_surface->pixels;
	int xmin = MIN(dest_rect.x,               current_target_surface->w);
	int xmax = MIN(dest_rect.x + dest_rect.w, current_target_surface->w);
	int ymin = MIN(dest_rect.y,               current_target_surface->h);
	int ymax = MIN(dest_rect.y + dest_rect.h, current_target_surface->h);
	byte* row = pixels + ymin*pitch;
	uint32_t* pixel =  (uint32_t*) (row + xmin*bytes_per_pixel);
	for (int x = xmin; x < xmax; ++x) {
		*pixel++ = rgb_color;
	}
	for (int y = ymin+1; y < ymax-1; ++y) {
		row += pitch;
		*(uint32_t*)(row + xmin*bytes_per_pixel) = rgb_color;
		*(uint32_t*)(row + (xmax-1)*bytes_per_pixel) = rgb_color;
	}
	pixel = (uint32_t*) (pixels + (ymax-1)*pitch + xmin*bytes_per_pixel);
	for (int x = xmin; x < xmax; ++x) {
		*pixel++ = rgb_color;
	}

	SDL_UnlockSurface(current_target_surface);
}

/*
 * XOR an image onto the target.
 *
 * Upstream built two 24bpp temporaries and XORed them component-wise. On an
 * indexed target the operation is on *indices*, which is what the original DOS
 * game did and what PSDL_BlitXor implements - no temporaries, no conversion.
 */
void blit_xor(SDL_Surface* target_surface, SDL_Rect* dest_rect, SDL_Surface* image, SDL_Rect* src_rect) {
	if (dest_rect->w != src_rect->w || dest_rect->h != src_rect->h) {
		printf("blit_xor: dest_rect and src_rect have different sizes\n");
		quit(1);
	}
	if (PSDL_BlitXor(image, src_rect, target_surface, dest_rect) != 0) {
		sdlperror("blit_xor: PSDL_BlitXor");
		quit(1);
	}
}

#ifdef USE_COLORED_TORCHES
void draw_colored_torch(int color, SDL_Surface* image, int xpos, int ypos) {
	if (SDL_SetColorKey(image, SDL_TRUE, 0) != 0) {
		sdlperror("draw_colored_torch: SDL_SetColorKey");
		quit(1);
	}

	SDL_Surface* colored_image = SDL_ConvertSurfaceFormat(image, SDL_PIXELFORMAT_ARGB8888, 0);
	SDL_SetSurfaceBlendMode(colored_image, SDL_BLENDMODE_NONE);

	if (SDL_LockSurface(colored_image) != 0) {
		sdlperror("draw_colored_torch: SDL_LockSurface");
		quit(1);
	}

	int w = colored_image->w;
	int h = colored_image->h;
	int iRed = ((color >> 4) & 3) * 85;
	int iGreen = ((color >> 2) & 3) * 85;
	int iBlue = ((color >> 0) & 3) * 85;
	uint32_t old_color = SDL_MapRGB(colored_image->format, 0xFC, 0x84, 0x00) & 0xFFFFFF; // the orange in the flame
	uint32_t new_color = SDL_MapRGB(colored_image->format, iRed, iGreen, iBlue) & 0xFFFFFF;
	int stride = colored_image->pitch;
	for (int y = 0; y < h; ++y) {
		uint32_t* pixel_ptr = (uint32_t*) ((byte*)colored_image->pixels + stride * y);
		for (int x = 0; x < w; ++x) {
			if ((*pixel_ptr & 0xFFFFFF) == old_color) {
				// set RGB but leave alpha
				*pixel_ptr = (*pixel_ptr & 0xFF000000) | new_color;
			}
			++pixel_ptr;
		}
	}
	SDL_UnlockSurface(colored_image);

	method_6_blit_img_to_scr(colored_image, xpos, ypos, blitters_0_no_transp);
	SDL_FreeSurface(colored_image);
}
#endif

/*
 * Draw an image at a position, choosing the blend from `blit`.
 *
 * The dispatch is the same as upstream's; what changed is that transparency is
 * chosen *per call* rather than by calling SDL_SetColorKey on the image first.
 * It has to be: the images are const objects in flash and writing a colour key
 * into one would fault. Each carries the transparent index for its palette row
 * already, and PSDL_BlitTransp decides whether to honour it.
 */
image_type* method_6_blit_img_to_scr(image_type* image,int xpos,int ypos,int blit) {
	if (image == NULL) {
		printf("method_6_blit_img_to_scr: image == NULL\n");
		return NULL;
	}

	if (blit == blitters_9_black) {
		method_3_blit_mono(image, xpos, ypos, blitters_9_black, 0);
		return image;
	}

	SDL_Rect src_rect  = {0, 0, image->w, image->h};
	SDL_Rect dest_rect = {xpos, ypos, image->w, image->h};

	if (blit == blitters_3_xor) {
		blit_xor(current_target_surface, &dest_rect, image, &src_rect);
		return image;
	}

#ifdef USE_COLORED_TORCHES
	if (blit >= blitters_colored_flame && blit <= blitters_colored_flame_last) {
		draw_colored_torch(blit - blitters_colored_flame, image, xpos, ypos);
		return image;
	}
#endif

	if (PSDL_BlitTransp(image, &src_rect, current_target_surface, &dest_rect,
	                    blit != blitters_0_no_transp) != 0) {
		sdlperror("method_6_blit_img_to_scr: PSDL_BlitTransp");
	}
	return image;
}

#ifndef USE_COMPAT_TIMER
int fps = BASE_FPS;
float milliseconds_per_tick = (1000.0f / (BASE_FPS*1.0f));
Uint64 timer_last_counter[NUM_TIMERS];
#endif
int wait_time[NUM_TIMERS];


#ifdef USE_COMPAT_TIMER
Uint32 timer_callback(Uint32 interval, void *param) {
	SDL_Event event;
	memset(&event, 0, sizeof(event));
	event.type = SDL_USEREVENT;
	event.user.code = userevent_TIMER;
	event.user.data1 = param;
	SDL_PushEvent(&event);
	return interval;
}
#endif

void reset_timer(int timer_index) {
#ifndef USE_COMPAT_TIMER
	timer_last_counter[timer_index] = SDL_GetPerformanceCounter();
#endif
}

double get_ticks_per_sec(int timer_index) {
	return (double) fps / wait_time[timer_index];
}

void recalculate_feather_fall_timer(double previous_ticks_per_second, double ticks_per_second) {
	if (is_feather_fall <= MAX(previous_ticks_per_second, ticks_per_second) ||
			previous_ticks_per_second == ticks_per_second) {
		return;
	}
	// there are more ticks per second in base mode vs fight mode so
	// feather fall length needs to be recalculated
	is_feather_fall = is_feather_fall / previous_ticks_per_second * ticks_per_second;
}

void set_timer_length(int timer_index, int length) {
	if (!fixes->fix_quicksave_during_feather) {
		wait_time[timer_index] = length;
		return;
	}
	if (is_feather_fall == 0 ||
			wait_time[timer_index] < custom->base_speed ||
			wait_time[timer_index] > custom->fight_speed) {
		wait_time[timer_index] = length;
		return;
	}
	double previous_ticks_per_second, ticks_per_second;
	previous_ticks_per_second = get_ticks_per_sec(timer_index);
	wait_time[timer_index] = length;
	ticks_per_second = get_ticks_per_sec(timer_index);
	recalculate_feather_fall_timer(previous_ticks_per_second, ticks_per_second);
}

void start_timer(int timer_index, int length) {
#ifdef USE_REPLAY
	if (replaying && skipping_replay) return;
#endif
#ifndef USE_COMPAT_TIMER
	timer_last_counter[timer_index] = SDL_GetPerformanceCounter();
#endif
	wait_time[timer_index] = length;
}

void toggle_fullscreen(void) {
	uint32_t flags = SDL_GetWindowFlags(window_);
	if (flags & SDL_WINDOW_FULLSCREEN_DESKTOP) {
		SDL_SetWindowFullscreen(window_, 0);
		SDL_ShowCursor(SDL_ENABLE);
	}
	else {
		SDL_SetWindowFullscreen(window_, SDL_WINDOW_FULLSCREEN_DESKTOP);
		SDL_ShowCursor(SDL_DISABLE);
	}
}

bool ignore_tab = false;


/*
 * Rebind the controller's buttons.
 *
 * This is a control binding, not a hardware fact, which is why it lives here and
 * not in picosdl: the library reports the button the player actually pressed, so
 * "button A" means the one labelled A, and every client sees the pad honestly.
 * Choosing what A *does* is the game's business, and on this target it has to be
 * compiled in - there is no SDLPoP.ini to edit, because the board has no
 * filesystem (SDL_RWFromFile always fails).
 *
 * Upstream binds Y to up, A to down and X to Shift. Wanted here, on a pad whose
 * diamond sits differently under the thumb:
 *
 *     X  ->  jump    so report it as upstream's Y
 *     B  ->  crouch  so report it as upstream's A
 *     A  ->  Shift   so report it as upstream's X
 *     Y  ->  nothing so report it as upstream's B, the one with no action
 *
 * Done once here rather than in both of the switches below, which is the whole
 * reason for a function: the two used to be edited in parallel and would drift.
 * Everything not named - the d-pad, Start, Back - passes through untouched.
 */
static Uint8 picopop_bind_controller_button(Uint8 button) {
#ifdef PICOPOP
	switch (button) {
		case SDL_CONTROLLER_BUTTON_X: return SDL_CONTROLLER_BUTTON_Y;  // jump
		case SDL_CONTROLLER_BUTTON_B: return SDL_CONTROLLER_BUTTON_A;  // crouch
		case SDL_CONTROLLER_BUTTON_A: return SDL_CONTROLLER_BUTTON_X;  // Shift
		case SDL_CONTROLLER_BUTTON_Y: return SDL_CONTROLLER_BUTTON_B;  // unused
		default: return button;
	}
#else
	return button;
#endif
}

void process_events() {
	// Process all events in the queue.
	// Previously, this procedure would wait for *one* event and process it, then return.
	// Much like the x86 HLT instruction.
	// (We still want to process all events in the queue. For instance, there might be
	// simultaneous SDL2 KEYDOWN and TEXTINPUT events.)
	SDL_Event event;
	while (SDL_PollEvent(&event) == 1) { // while there are still events to be processed
		switch (event.type) {
			case SDL_KEYDOWN:
			{
				int modifier = event.key.keysym.mod;
				int scancode = event.key.keysym.scancode;

				// Handle these separately, so they won't interrupt things that are usually interrupted by a keypress. (pause, cutscene)
#ifdef USE_FAST_FORWARD
				if (scancode == SDL_SCANCODE_GRAVE) {
					init_timer(BASE_FPS * FAST_FORWARD_RATIO); // fast-forward on
					audio_speed = FAST_FORWARD_RATIO;
					break;
				}
#endif
#ifdef USE_SCREENSHOT
				if (scancode == SDL_SCANCODE_F12) {
					if (modifier & KMOD_SHIFT) {
						save_level_screenshot((modifier & KMOD_CTRL) != 0);
					} else {
						save_screenshot();
					}
				} else
#endif
#ifdef USE_MENU
				if (escape_key_suppressed &&
						(scancode == SDL_SCANCODE_BACKSPACE || (enable_pause_menu && scancode == SDL_SCANCODE_ESCAPE))
				) {
					break; // Prevent repeated keystrokes opening/closing the menu as long as the key is held down.
				} else
#endif
				if ((modifier & KMOD_ALT) &&
				    scancode == SDL_SCANCODE_RETURN)
				{
					// Only if the Enter key was pressed down right now.
					if ((key_states[scancode] & KEYSTATE_HELD) == 0) {
						// Alt+Enter: toggle fullscreen mode
						toggle_fullscreen();
						key_states[scancode] |= KEYSTATE_HELD | KEYSTATE_HELD_NEW;
					}
				} else {
					last_any_key_scancode = scancode; // for showmessage_any_key
					key_states[scancode] |= KEYSTATE_HELD | KEYSTATE_HELD_NEW;
					switch (scancode) {
						// Keys that are ignored by themselves:
						case SDL_SCANCODE_LCTRL:
						case SDL_SCANCODE_LSHIFT:
						case SDL_SCANCODE_LALT:
						case SDL_SCANCODE_LGUI:
						case SDL_SCANCODE_RCTRL:
						case SDL_SCANCODE_RSHIFT:
						case SDL_SCANCODE_RALT:
						case SDL_SCANCODE_RGUI:
						case SDL_SCANCODE_CAPSLOCK:
						case SDL_SCANCODE_SCROLLLOCK:
						case SDL_SCANCODE_NUMLOCKCLEAR:
						case SDL_SCANCODE_APPLICATION:
						case SDL_SCANCODE_PRINTSCREEN:
						case SDL_SCANCODE_VOLUMEUP:
						case SDL_SCANCODE_VOLUMEDOWN:
						// Why are there two mute key codes?
						case SDL_SCANCODE_MUTE:
						case SDL_SCANCODE_AUDIOMUTE:
						case SDL_SCANCODE_PAUSE:
							break;

						default:
							// If Alt is held down from Alt+Tab: ignore it until it's released.
							if (scancode == SDL_SCANCODE_TAB && ignore_tab) break;

							last_key_scancode = scancode;
							if (modifier & KMOD_SHIFT) last_key_scancode |= WITH_SHIFT;
							if (modifier & KMOD_CTRL ) last_key_scancode |= WITH_CTRL ;
							if (modifier & KMOD_ALT  ) last_key_scancode |= WITH_ALT  ;
					}

#ifdef USE_AUTO_INPUT_MODE
					switch (scancode) {
						// Keys that are used for keyboard control:
						case SDL_SCANCODE_LSHIFT:
						case SDL_SCANCODE_RSHIFT:
						case SDL_SCANCODE_LEFT:
						case SDL_SCANCODE_RIGHT:
						case SDL_SCANCODE_UP:
						case SDL_SCANCODE_DOWN:
						case SDL_SCANCODE_CLEAR:
						case SDL_SCANCODE_HOME:
						case SDL_SCANCODE_PAGEUP:
						case SDL_SCANCODE_KP_2:
						case SDL_SCANCODE_KP_4:
						case SDL_SCANCODE_KP_5:
						case SDL_SCANCODE_KP_6:
						case SDL_SCANCODE_KP_7:
						case SDL_SCANCODE_KP_8:
						case SDL_SCANCODE_KP_9:
							if (!is_keyboard_mode) {
								is_keyboard_mode = 1;
								is_joyst_mode = 0;
							}
					}
#endif
				}
				break;
			}
			case SDL_KEYUP:
				// If Alt was held down from Alt+Tab but now it's released: stop ignoring Tab.
				if (event.key.keysym.scancode == SDL_SCANCODE_TAB && ignore_tab) ignore_tab = false;

#ifdef USE_FAST_FORWARD
				if (event.key.keysym.scancode == SDL_SCANCODE_GRAVE) {
					init_timer(BASE_FPS); // fast-forward off
					audio_speed = 1;
					break;
				}
#endif

				key_states[event.key.keysym.scancode] &= ~KEYSTATE_HELD;
#ifdef USE_MENU
				// Prevent repeated keystrokes opening/closing the menu as long as the key is held down.
				if (event.key.keysym.scancode == SDL_SCANCODE_BACKSPACE || event.key.keysym.scancode == SDL_SCANCODE_ESCAPE) {
					escape_key_suppressed = false;
				}
#endif
				break;
			case SDL_CONTROLLERAXISMOTION:
				if (event.caxis.axis < 6) {
					joy_axis[event.caxis.axis] = event.caxis.value;
					if (abs(event.caxis.value) > abs(joy_axis_max[event.caxis.axis]))
						joy_axis_max[event.caxis.axis] = event.caxis.value;

#ifdef USE_AUTO_INPUT_MODE
					if (!is_joyst_mode && (event.caxis.value >= joystick_threshold || event.caxis.value <= -joystick_threshold)) {
						is_joyst_mode = 1;
						is_keyboard_mode = 0;
					}
#endif
				}
				break;
			case SDL_CONTROLLERDEVICEADDED:
				SDL_GameControllerOpen(event.cdevice.which);
				if (gamecontrollerdb_file[0] != '\0') {
					SDL_GameControllerAddMappingsFromFile(gamecontrollerdb_file);
				}
				is_joyst_mode = 1;
				using_sdl_joystick_interface = 0;
				break;
			case SDL_CONTROLLERDEVICEREMOVED:
				if (sdl_controller_ == SDL_GameControllerFromInstanceID(event.cdevice.which)) {
					sdl_controller_ = NULL;
					is_joyst_mode = 0;
					is_keyboard_mode = 1;
				}
				SDL_GameControllerClose(SDL_GameControllerFromInstanceID(event.cdevice.which));
				break;
			case SDL_CONTROLLERBUTTONDOWN:
				//Make sure sdl_controller_ always points to the active controller
				sdl_controller_ = SDL_GameControllerFromInstanceID(event.cdevice.which);
				event.cbutton.button = picopop_bind_controller_button(event.cbutton.button);
#ifdef PICOPOP
				/*
				 * While the game is paused, any pad button resumes.
				 *
				 * do_paused() blocks in `while (!process_key())`, so resuming needs
				 * a key process_key() recognises. Escape is the wrong one: its case
				 * sets is_paused again, so pressing the pause button a second time
				 * left the loop and immediately re-entered it. With a pad and no
				 * keyboard, Start and Back were the only buttons producing a key at
				 * all, and both produced Escape - so a pause could not be undone.
				 *
				 * F11 is a real scancode that SDLPoP does not map (it uses F6, F9
				 * and F12), so process_key() returns non-zero, falls through its
				 * switch untouched, and the wait ends with the game running.
				 */
				if (picopop_pause_wait) {
					last_key_scancode = SDL_SCANCODE_F11;
					break;
				}
#endif
#ifdef USE_AUTO_INPUT_MODE
				if (!is_joyst_mode) {
					is_joyst_mode = 1;
					is_keyboard_mode = 0;
				}
#endif
				switch (event.cbutton.button)
				{
					case SDL_CONTROLLER_BUTTON_DPAD_LEFT:  joy_button_states[JOYINPUT_DPAD_LEFT] |= KEYSTATE_HELD | KEYSTATE_HELD_NEW; break; // left
					case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: joy_button_states[JOYINPUT_DPAD_RIGHT] |= KEYSTATE_HELD | KEYSTATE_HELD_NEW; break; // right
					case SDL_CONTROLLER_BUTTON_DPAD_UP:    joy_button_states[JOYINPUT_DPAD_UP] |= KEYSTATE_HELD | KEYSTATE_HELD_NEW; break; // up
					case SDL_CONTROLLER_BUTTON_DPAD_DOWN:  joy_button_states[JOYINPUT_DPAD_DOWN] |= KEYSTATE_HELD | KEYSTATE_HELD_NEW; break; // down

					case SDL_CONTROLLER_BUTTON_A:          joy_button_states[JOYINPUT_A] |= KEYSTATE_HELD | KEYSTATE_HELD_NEW; break; /*** A (down) ***/
					case SDL_CONTROLLER_BUTTON_Y:          joy_button_states[JOYINPUT_Y] |= KEYSTATE_HELD | KEYSTATE_HELD_NEW; break; /*** Y (up) ***/
					case SDL_CONTROLLER_BUTTON_X:          joy_button_states[JOYINPUT_X] |= KEYSTATE_HELD | KEYSTATE_HELD_NEW; break; /*** X (Shift) ***/
					case SDL_CONTROLLER_BUTTON_B:          joy_button_states[JOYINPUT_B] |= KEYSTATE_HELD | KEYSTATE_HELD_NEW; break; /*** B (unused) ***/

					case SDL_CONTROLLER_BUTTON_START:
					case SDL_CONTROLLER_BUTTON_BACK:
						if(event.cbutton.button == SDL_CONTROLLER_BUTTON_START)
							joy_button_states[JOYINPUT_START] |= KEYSTATE_HELD | KEYSTATE_HELD_NEW;
						else if(event.cbutton.button == SDL_CONTROLLER_BUTTON_BACK)
							joy_button_states[JOYINPUT_BACK] |= KEYSTATE_HELD | KEYSTATE_HELD_NEW;
#ifdef PICOPOP
						/*
						 * Start carries on after a death rather than pausing.
						 *
						 * Upstream both Start and Back mean "pause", because on a
						 * keyboard Space or Enter dismisses the "Press Button to
						 * Continue" prompt. With a pad and no keyboard attached
						 * there was nothing that could: both buttons paused a game
						 * that was waiting to be restarted, so a death was a dead
						 * end.
						 *
						 * `Kid.alive` is negative while alive and counts up once
						 * dead, so >= 0 is the state the prompt belongs to. Only
						 * Start does this; Back still pauses, which leaves a way to
						 * reach the pause behaviour either way.
						 *
						 * is_restart_level is the same mechanism Ctrl+A uses, down
						 * to the level 15 guard - the potions level has no restart.
						 *
						 * The start_level and current_level guards are not
						 * decoration. `Kid.alive` alone was wrong: at the title
						 * screen and through the attract demo there is no kid to be
						 * alive, the field reads >= 0 anyway, and Start restarted a
						 * level instead of doing what it does there - starting the
						 * game. start_level is negative until the game begins,
						 * which is exactly the state to stay out of.
						 */
						if (event.cbutton.button == SDL_CONTROLLER_BUTTON_START &&
						    start_level >= 0 && current_level != 0 &&
						    current_level != 15 && Kid.alive >= 0) {
							stop_sounds();
							is_restart_level = 1;
							break;
						}
#endif
#ifdef USE_MENU
						last_key_scancode = SDL_SCANCODE_BACKSPACE;  /*** bring up pause menu ***/
#else
						last_key_scancode = SDL_SCANCODE_ESCAPE;  /*** back (pause game) ***/
#endif
						break;

					default: break;
				}
				break;
			case SDL_CONTROLLERBUTTONUP:
				event.cbutton.button = picopop_bind_controller_button(event.cbutton.button);
				switch (event.cbutton.button)
				{
					case SDL_CONTROLLER_BUTTON_DPAD_LEFT:  joy_button_states[JOYINPUT_DPAD_LEFT] &= ~KEYSTATE_HELD; break; // left
					case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: joy_button_states[JOYINPUT_DPAD_RIGHT] &= ~KEYSTATE_HELD; break; // right
					case SDL_CONTROLLER_BUTTON_DPAD_UP:    joy_button_states[JOYINPUT_DPAD_UP] &= ~KEYSTATE_HELD; break; // up
					case SDL_CONTROLLER_BUTTON_DPAD_DOWN:  joy_button_states[JOYINPUT_DPAD_DOWN] &= ~KEYSTATE_HELD; break; // down

					case SDL_CONTROLLER_BUTTON_A:          joy_button_states[JOYINPUT_A] &= ~KEYSTATE_HELD; break; /*** A (down) ***/
					case SDL_CONTROLLER_BUTTON_Y:          joy_button_states[JOYINPUT_Y] &= ~KEYSTATE_HELD; break; /*** Y (up) ***/
					case SDL_CONTROLLER_BUTTON_X:          joy_button_states[JOYINPUT_X] &= ~KEYSTATE_HELD; break; /*** X (Shift) ***/
					case SDL_CONTROLLER_BUTTON_B:          joy_button_states[JOYINPUT_B] &= ~KEYSTATE_HELD; break; /*** B (unused) ***/

					case SDL_CONTROLLER_BUTTON_START:      joy_button_states[JOYINPUT_START] &= ~KEYSTATE_HELD; break;
					case SDL_CONTROLLER_BUTTON_BACK:       joy_button_states[JOYINPUT_BACK] &= ~KEYSTATE_HELD; break;

					default: break;
				}
				break;
			case SDL_JOYBUTTONDOWN:
			case SDL_JOYBUTTONUP:
			case SDL_JOYAXISMOTION:
				// Only handle the event if the joystick is incompatible with the SDL_GameController interface.
				// (Otherwise it will interfere with the normal action of the SDL_GameController API.)
				if (!using_sdl_joystick_interface) {
					break;
				}
				if (event.type == SDL_JOYAXISMOTION) {
					int axis = -1;
					if (event.jaxis.axis == SDL_JOYSTICK_X_AXIS) {
						axis = SDL_CONTROLLER_AXIS_LEFTX;
					}
					else if (event.jaxis.axis == SDL_JOYSTICK_Y_AXIS) {
						axis = SDL_CONTROLLER_AXIS_LEFTY;
					}
					if (axis == -1)
						break;
					joy_axis[axis] = event.jaxis.value;
					if (abs(event.jaxis.value) > abs(joy_axis_max[axis]))
						joy_axis_max[axis] = event.jaxis.value;

					// Disregard SDL_JOYAXISMOTION events within joystick 'dead zone'
					int joy_x = joy_axis[SDL_CONTROLLER_AXIS_LEFTX];
					int joy_y = joy_axis[SDL_CONTROLLER_AXIS_LEFTY];
					if ((dword)(joy_x*joy_x) + (dword)(joy_y*joy_y) < (dword)(joystick_threshold*joystick_threshold)) {
						break;
					}
				}
#ifdef USE_AUTO_INPUT_MODE
				if (!is_joyst_mode) {
					is_joyst_mode = 1;
					is_keyboard_mode = 0;
				}
#endif
				if (event.type == SDL_JOYBUTTONDOWN) {
					if      (event.jbutton.button == SDL_JOYSTICK_BUTTON_Y)   joy_button_states[JOYINPUT_Y] |= KEYSTATE_HELD | KEYSTATE_HELD_NEW; // Y (up)
					else if (event.jbutton.button == SDL_JOYSTICK_BUTTON_X)   joy_button_states[JOYINPUT_X] |= KEYSTATE_HELD | KEYSTATE_HELD_NEW;    // X (Shift)
				}
				else if (event.type == SDL_JOYBUTTONUP) {
					if      (event.jbutton.button == SDL_JOYSTICK_BUTTON_Y)   joy_button_states[JOYINPUT_Y] &= ~KEYSTATE_HELD;  // Y (up)
					else if (event.jbutton.button == SDL_JOYSTICK_BUTTON_X)   joy_button_states[JOYINPUT_X] &= ~KEYSTATE_HELD;    // X (Shift)
				}
				break;

			case SDL_TEXTINPUT:
				last_text_input = event.text.text[0]; // UTF-8 formatted char text input

				// Make the +/- keys work on the main keyboard, on any keyboard layout.
				// We check SDL_TEXTINPUT instead of SDL_KEYDOWN.
				// If '+' is on Shift+something then we can't detect it in SDL_KEYDOWN,
				// because event.key.keysym.sym only tells us what character would the key type without shift.
				switch (last_text_input) {
					case '-': last_key_scancode = SDL_SCANCODE_KP_MINUS; break;
					case '+': last_key_scancode = SDL_SCANCODE_KP_PLUS;  break;
				}

				break;
			case SDL_WINDOWEVENT:
				// In case the user switches away while holding a key: do as if all keys were released.
				// (DOSBox does the same.)

/* // not implemented in SDL2 for now
 *
			if ((event.active.state & SDL_APPINPUTFOCUS) && event.active.gain == 0) {
				memset(key_states, 0, sizeof(key_states));
			}
			// Note: event.active.state can contain multiple flags or'ed.
			// If the game is in full screen, and I switch away (Alt+Tab) and back, most of the screen will be black, until it is redrawn.
			if ((event.active.state & SDL_APPACTIVE) && event.active.gain == 1) {
				update_screen();
			}
*/
				switch (event.window.event) {
#ifdef __amigaos4__
					case SDL_WINDOWEVENT_MINIMIZED: /* pause game */
						if (!is_menu_shown) {
							last_key_scancode = SDL_SCANCODE_BACKSPACE;
						}
						break;
					case SDL_WINDOWEVENT_RESTORED: /* show "game paused/menu" */
						update_screen();
						break;
#endif
					case SDL_WINDOWEVENT_SIZE_CHANGED:
#if defined(USE_RENDERER) && defined(USE_SCALING)
						window_resized();
#elif !defined(USE_RENDERER) && defined(USE_SCALING)
						// Software rendering mode - update window surface after resize
						window_surface_ = SDL_GetWindowSurface(window_);
						if (window_surface_ == NULL) {
							sdlperror("SDL_WINDOWEVENT_SIZE_CHANGED: SDL_GetWindowSurface");
						}
#endif
						// fallthrough!

					//case SDL_WINDOWEVENT_MOVED:
					//case SDL_WINDOWEVENT_RESTORED:
					case SDL_WINDOWEVENT_EXPOSED:
						update_screen();
						break;

					case SDL_WINDOWEVENT_FOCUS_GAINED:
					// Fix for this bug: When playing back a recording, Alt+Tabbing back to SDLPoP stops the replay if Alt is released before Tab.
					{ // If Alt is held down from Alt+Tab: ignore it until it's released.
						const Uint8 *state = SDL_GetKeyboardState(NULL);
						if (state[SDL_SCANCODE_TAB]) ignore_tab = true;
					}
					break;
				}
				break;
			case SDL_USEREVENT:
				if (event.user.code == userevent_TIMER /*&& event.user.data1 == (void*)timer_index*/) {
#ifdef USE_COMPAT_TIMER
					for (int index = 0; index < NUM_TIMERS; ++index) {
						if (wait_time[index] > 0) --wait_time[index];
					}
#endif
				} else if (event.user.code == userevent_SOUND) {
					//sound_timer = 0;
					//stop_sounds();
				}
				break;
#ifdef USE_MENU
			case SDL_MOUSEBUTTONDOWN:
				switch(event.button.button) {
					case SDL_BUTTON_LEFT:
						if (!is_menu_shown) {
							last_key_scancode = SDL_SCANCODE_BACKSPACE;
						} else {
							mouse_clicked = true;
						}
						break;
					case SDL_BUTTON_RIGHT:
					case SDL_BUTTON_X1: // 'Back' button (on mice that have these extra buttons).
						mouse_button_clicked_right = true;
						break;
					default: break;
				}

				break;
			case SDL_MOUSEWHEEL:
				if (is_menu_shown) {
					menu_control_scroll_y = -event.wheel.y;
				}
				break;
#endif
			case SDL_QUIT:
#ifdef USE_MENU
				if (is_menu_shown) {
					menu_was_closed();
				}
#endif
				quit(0);
				break;
		}
	}
}

void idle() {
	process_events();
	update_screen();

	/* Give back a tune that finished inside the audio callback. The callback
	 * runs on core 1 in an interrupt and must not touch the heap, so it only
	 * flags this; core 0 does the freeing here. See midi.c. */
	midi_reclaim();

#ifdef PICOPOP_REPORT_AUDIO_LOAD
	/*
	 * Mixer load, once a second, on core 0 - never from the callback itself,
	 * which runs in an interrupt on core 1.
	 *
	 * Anything at or over 100% means blocks are missing their deadline and
	 * the noise that follows is an underrun, not distortion. See the note on
	 * digi_samplerate.
	 */
	/*
	 * Nothing to report when there is no audio device - picosdl can be built
	 * without one (PICOSDL_AUDIO=OFF), and SDL_OpenAudio() then fails, leaving
	 * digi_unavailable set. Reporting anyway printed "0% of the block budget
	 * (0 Hz, 0-frame blocks)" once a second, which reads like a mixer that has
	 * stalled rather than one that was never built.
	 */
	if (!digi_unavailable) {
		static Uint32 last_report;
		Uint32 now = SDL_GetTicks();
		if (now - last_report >= 1000) {
			last_report = now;
			unsigned avg, peak;
			pop_audio_load(&avg, &peak);
			printf("mixer: %u%% avg, %u%% peak of the block budget (%d Hz, %d-frame blocks)\n",
			       avg, peak,
			       digi_audiospec ? digi_audiospec->freq : 0,
			       digi_audiospec ? digi_audiospec->samples : 0);
		}
	}
#endif
}

void do_simple_wait(int timer_index) {
#ifdef USE_REPLAY
	if ((replaying && skipping_replay) || is_validate_mode) return;
#endif
	update_screen();
	while (! has_timer_stopped(timer_index)) {
		SDL_Delay(1);
		process_events();
	}
}

word word_1D63A = 1;
int do_wait(int timer_index) {
#ifdef USE_REPLAY
	if ((replaying && skipping_replay) || is_validate_mode) return 0;
#endif
	update_screen();
	while (! has_timer_stopped(timer_index)) {
		SDL_Delay(1);
		process_events();
		int key = do_paused();
		if (key != 0 && (word_1D63A != 0 || key == 0x1B)) return 1;
	}
	return 0;
}

#ifdef USE_COMPAT_TIMER
SDL_TimerID global_timer = 0;
#endif
// seg009:78E9
void init_timer(int frequency) {
	perf_frequency = SDL_GetPerformanceFrequency();
#ifndef USE_COMPAT_TIMER
	fps = frequency;
	milliseconds_per_tick = 1000.0f / (float)fps;
	perf_counters_per_tick = perf_frequency / fps;
	milliseconds_per_counter = 1000.0f / perf_frequency;
#else
	global_timer = SDL_AddTimer(1000/frequency, timer_callback, NULL);
	if (global_timer != 0) {
		if (!SDL_RemoveTimer(global_timer)) {
			sdlperror("init_timer: SDL_RemoveTimer");
		}
	}
	if (global_timer == 0) {
		sdlperror("init_timer: SDL_AddTimer");
		quit(1);
	}
#endif
}

// seg009:35F6
void set_clip_rect(const rect_type* rect) {
	SDL_Rect clip_rect;
	rect_to_sdlrect(rect, &clip_rect);
	SDL_SetClipRect(current_target_surface, &clip_rect);
}

// seg009:365C
void reset_clip_rect() {
	SDL_SetClipRect(current_target_surface, NULL);
}

// seg009:1983
void set_bg_attr(int vga_pal_index,int hc_pal_index) {
	// stub
#ifdef USE_FLASH
	//palette[vga_pal_index] = vga_palette[hc_pal_index];
	if (!enable_flash) return;
	if (vga_pal_index == 0) {
		/*
		if (SDL_SetAlpha(offscreen_surface, SDL_SRCALPHA, 0) != 0) {
			sdlperror("set_bg_attr: SDL_SetAlpha");
			quit(1);
		}
		*/
		/*
		 * The screen flash, as a palette operation.
		 *
		 * Upstream fills the whole onscreen surface with the flash colour and
		 * then blits the offscreen over it with black colour-keyed out, so the
		 * flash shows through wherever the frame is black. On an indexed
		 * framebuffer whose palette *is* the CLUT that is the same thing as
		 * writing one CLUT entry: every index-0 pixel becomes the flash colour
		 * the moment entry 0 changes. One palette write instead of a 62.5 KB
		 * fill plus a keyed 62.5 KB blit, every flash frame.
		 *
		 * The game's `palette[]` shadow is deliberately not touched: it is the
		 * source this reads from, and set_bg_attr(0, 0) restores the CLUT from
		 * palette[0] on the way out.
		 */
		SDL_Rect rect = {0, 0, offscreen_surface->w, offscreen_surface->h};
		rgb_type palette_color = palette[hc_pal_index];
		SDL_Color flash = { pal6_to_8(palette_color.r), pal6_to_8(palette_color.g),
		                    pal6_to_8(palette_color.b), SDL_ALPHA_OPAQUE };
		SDL_SetPaletteColors(PSDL_GlobalPalette(), &flash, 0, 1);
		if (upside_down) {
			flip_screen(offscreen_surface);
		}
		// Then draw the offscreen image onto it - opaquely, because index 0 now
		// *is* the flash colour rather than a hole to see it through. With one
		// screen buffer that is a copy onto itself, and the palette write above
		// has already done the whole job.
		picopop_compose_reveal();   /* skipped copy, same reason as copy_screen_rect */
		if (!picopop_same_pixels(offscreen_surface, onscreen_surface_) &&
		    PSDL_BlitTransp(offscreen_surface, &rect, onscreen_surface_, &rect, 0) != 0) {
			sdlperror("set_bg_attr: PSDL_BlitTransp");
			quit(1);
		}
#ifdef USE_LIGHTING
		if (hc_pal_index == 0) update_lighting(&rect_top);
#endif
		if (upside_down) {
			flip_screen(offscreen_surface);
		}
		// And show it!
//		update_screen();
		// Give some time to show the flash.
		//SDL_Flip(onscreen_surface_);
//		if (hc_pal_index != 0) SDL_Delay(2*(1000/60));
		//SDL_Flip(onscreen_surface_);
		/*
		if (SDL_SetAlpha(offscreen_surface, 0, 0) != 0) {
			sdlperror("set_bg_attr: SDL_SetAlpha");
			quit(1);
		}
		*/
		/* No colour key to reset: the blit above chose opacity per call. */
	}
#endif // USE_FLASH
}

// seg009:07EB
rect_type* offset4_rect_add(rect_type* dest,const rect_type* source,int d_left,int d_top,int d_right,int d_bottom) {
	*dest = *source;
	dest->left += d_left;
	dest->top += d_top;
	dest->right += d_right;
	dest->bottom += d_bottom;
	return dest;
}

// seg009:3AA5
rect_type* offset2_rect(rect_type* dest,const rect_type *source,int delta_x,int delta_y) {
	dest->top    = source->top    + delta_y;
	dest->left   = source->left   + delta_x;
	dest->bottom = source->bottom + delta_y;
	dest->right  = source->right  + delta_x;
	return dest;
}

#ifdef USE_FADE
// seg009:19EF
void fade_in_2(surface_type* source_surface,int which_rows) {
	palette_fade_type* palette_buffer;
	if (graphics_mode == gmMcgaVga) {
		palette_buffer = make_pal_buffer_fadein(source_surface, which_rows, 2);
		while (fade_in_frame(palette_buffer) == 0) {
			process_events(); // modified
			do_paused();
		}
		pal_restore_free_fadein(palette_buffer);
	} else {
		// ...
	}
}

// seg009:1A51
/*
 * Fade buffers.
 *
 * Each is 1.5 KB, almost all of it the two 256-entry palette snapshots. Two
 * slots rather than one: a fade-out and a fade-in are never live at the same
 * moment, but the code does not enforce that anywhere, and a pool of two costs
 * 3 KB and removes the need to prove it.
 */
#define POP_MAX_FADE_BUFFERS 2
static palette_fade_type s_fade_pool[POP_MAX_FADE_BUFFERS];
static byte              s_fade_used[POP_MAX_FADE_BUFFERS];

static palette_fade_type* alloc_fade_buffer(void) {
	for (int i = 0; i < POP_MAX_FADE_BUFFERS; ++i) {
		if (!s_fade_used[i]) {
			s_fade_used[i] = 1;
			memset(&s_fade_pool[i], 0, sizeof(s_fade_pool[i]));
			return &s_fade_pool[i];
		}
	}
	printf("more than %d palette fades at once\n", POP_MAX_FADE_BUFFERS);
	quit(1);
	return NULL;
}

static void release_fade_buffer(palette_fade_type* b) {
	for (int i = 0; i < POP_MAX_FADE_BUFFERS; ++i)
		if (&s_fade_pool[i] == b) { s_fade_used[i] = 0; return; }
}

palette_fade_type* make_pal_buffer_fadein(surface_type* source_surface,int which_rows,int wait_time) {
	palette_fade_type* palette_buffer = alloc_fade_buffer();
	palette_buffer->which_rows = which_rows;
	palette_buffer->wait_time = wait_time;
	palette_buffer->fade_pos = 0x40;
	palette_buffer->proc_restore_free = &pal_restore_free_fadein;
	palette_buffer->proc_fade_frame = &fade_in_frame;
	read_palette_256(palette_buffer->original_pal);
	memcpy(palette_buffer->faded_pal, palette_buffer->original_pal, sizeof(palette_buffer->faded_pal));
	for (word curr_row = 0, curr_row_mask = 1; curr_row < 0x10; ++curr_row, curr_row_mask<<=1) {
		if (which_rows & curr_row_mask) {
			memset(palette_buffer->faded_pal + (curr_row<<4), 0, sizeof(rgb_type[0x10]));
			set_pal_arr(curr_row<<4, 0x10, NULL);
		}
	}
	//method_1_blit_rect(onscreen_surface_, source_surface, &screen_rect, &screen_rect, 0);
	// for RGB
	//method_5_rect(&screen_rect, 0, color_0_black);
	return palette_buffer;
}

// seg009:1B64
void pal_restore_free_fadein(palette_fade_type* palette_buffer) {
	set_pal_256(palette_buffer->original_pal);
	release_fade_buffer(palette_buffer);
	// for RGB
	method_1_blit_rect(onscreen_surface_, offscreen_surface, &screen_rect, &screen_rect, 0);
}

// seg009:1B88
int fade_in_frame(palette_fade_type* palette_buffer) {
//	void* var_12;
	/**/start_timer(timer_1, palette_buffer->wait_time); // too slow?

	//printf("start ticks = %u\n",SDL_GetTicks());
	--palette_buffer->fade_pos;
	for (word start=0,current_row_mask=1; start<0x100; start+=0x10, current_row_mask<<=1) {
		if (palette_buffer->which_rows & current_row_mask) {
			//var_12 = palette_buffer->
			rgb_type* original_pal_ptr = palette_buffer->original_pal + start;
			rgb_type* faded_pal_ptr = palette_buffer->faded_pal + start;
			for (word column = 0; column<0x10; ++column) {
				if (original_pal_ptr[column].r > palette_buffer->fade_pos) {
					++faded_pal_ptr[column].r;
				}
				if (original_pal_ptr[column].g > palette_buffer->fade_pos) {
					++faded_pal_ptr[column].g;
				}
				if (original_pal_ptr[column].b > palette_buffer->fade_pos) {
					++faded_pal_ptr[column].b;
				}
			}
		}
	}
	for (word start = 0, current_row_mask = 1; start<0x100; start+=0x10, current_row_mask<<=1) {
		if (palette_buffer->which_rows & current_row_mask) {
			set_pal_arr(start, 0x10, palette_buffer->faded_pal + start);
		}
	}

	/*
	 * Show the frame being faded. The fade itself is entirely in the palette
	 * writes above - on this target the palette *is* the display CLUT, so a
	 * fade costs 256 CLUT entries rather than touching any pixels.
	 *
	 * What used to be here was a second, pixel-level fade:
	 *
	 *     int v = *off_pixel_ptr - fade_pos*4;
	 *
	 * That subtracts a scalar from each byte of the source, which darkens a
	 * 24bpp RGB surface - the format this code was written for. Here the byte
	 * is a *palette index*, so subtracting from it walks backwards through
	 * unrelated rows of the global palette: a title pixel at index 200 (row
	 * 12) becomes 196, 192, 188 and crosses into rows 11 and 10, picking up
	 * whatever colours those hold. It has to be a plain copy.
	 */
	/* The blit below is the reveal - see method_1_blit_rect. */
	method_1_blit_rect(onscreen_surface_, offscreen_surface,
	                   &screen_rect, &screen_rect, blitters_0_no_transp);

	//SDL_UpdateRect(onscreen_surface_, 0, 0, 0, 0); // debug

	/**/do_simple_wait(1); // can interrupt fading of cutscene
	//do_wait(timer_1); // can interrupt fading of main title
	//printf("end ticks = %u\n",SDL_GetTicks());
	return palette_buffer->fade_pos == 0;
}

// seg009:1CC9
void fade_out_2(int rows) {
	palette_fade_type* palette_buffer;
	if (graphics_mode == gmMcgaVga) {
		palette_buffer = make_pal_buffer_fadeout(rows, 2);
		while (fade_out_frame(palette_buffer) == 0) {
			process_events(); // modified
			do_paused();
		}
		pal_restore_free_fadeout(palette_buffer);
	} else {
		// ...
	}
}

// seg009:1D28
palette_fade_type* make_pal_buffer_fadeout(int which_rows,int wait_time) {
	palette_fade_type* palette_buffer = alloc_fade_buffer();
	palette_buffer->which_rows = which_rows;
	palette_buffer->wait_time = wait_time;
	palette_buffer->fade_pos = 00; // modified
	palette_buffer->proc_restore_free = &pal_restore_free_fadeout;
	palette_buffer->proc_fade_frame = &fade_out_frame;
	read_palette_256(palette_buffer->original_pal);
	memcpy(palette_buffer->faded_pal, palette_buffer->original_pal, sizeof(palette_buffer->faded_pal));
	// for RGB
	method_1_blit_rect(onscreen_surface_, offscreen_surface, &screen_rect, &screen_rect, 0);
	return palette_buffer;
}

// seg009:1DAF
void pal_restore_free_fadeout(palette_fade_type* palette_buffer) {
	surface_type* surface;
	surface = current_target_surface;
	current_target_surface = onscreen_surface_;
	draw_rect(&screen_rect, color_0_black);
	current_target_surface = surface;
	set_pal_256(palette_buffer->original_pal);
	release_fade_buffer(palette_buffer);
	// for RGB
	method_5_rect(&screen_rect, 0, color_0_black);
}

// seg009:1DF7
int fade_out_frame(palette_fade_type* palette_buffer) {
	word finished_fading = 1;
	++palette_buffer->fade_pos; // modified
	/**/start_timer(timer_1, palette_buffer->wait_time); // too slow?
	for (word start=0,current_row_mask=1; start<0x100; start+=0x10, current_row_mask<<=1) {
		if (palette_buffer->which_rows & current_row_mask) {
			//var_12 = palette_buffer->
			//original_pal_ptr = palette_buffer->original_pal + start;
			rgb_type* faded_pal_ptr = palette_buffer->faded_pal + start;
			for (word column = 0; column<0x10; ++column) {
				byte* curr_color_ptr = &faded_pal_ptr[column].r;
				if (*curr_color_ptr != 0) {
					--*curr_color_ptr;
					finished_fading = 0;
				}
				curr_color_ptr = &faded_pal_ptr[column].g;
				if (*curr_color_ptr != 0) {
					--*curr_color_ptr;
					finished_fading = 0;
				}
				curr_color_ptr = &faded_pal_ptr[column].b;
				if (*curr_color_ptr != 0) {
					--*curr_color_ptr;
					finished_fading = 0;
				}
			}
		}
	}
	for (word start = 0, current_row_mask = 1; start<0x100; start+=0x10, current_row_mask<<=1) {
		if (palette_buffer->which_rows & current_row_mask) {
			set_pal_arr(start, 0x10, palette_buffer->faded_pal + start);
		}
	}

	/* As in fade_in_frame: the fade is the palette writes above; this just
	 * shows the frame they are fading. See the note there. */
	method_1_blit_rect(onscreen_surface_, offscreen_surface,
	                   &screen_rect, &screen_rect, blitters_0_no_transp);

	do_simple_wait(timer_1); // can interrupt fading of cutscene
	return finished_fading;
}

// seg009:1F28
void read_palette_256(rgb_type* target) {
	for (int i = 0; i < 256; ++i) {
		target[i] = palette[i];
	}
}

// seg009:1F5E
/*
 * Restore the whole palette.
 *
 * This must reach the CLUT, not just the shadow array. Upstream it only wrote
 * `palette[]`, and that was enough because the 24bpp blitters looked up
 * `palette[color]` on every pixel they drew - the shadow *was* the palette.
 *
 * Here the CLUT is the display hardware and `palette[]` is only a copy kept
 * for code that reads entries back. Writing the shadow alone left the hardware
 * holding whatever the fade had put there: make_pal_buffer_fadein() blacks out
 * its `which_rows` up front (rows 0, 3, 7, 9, 10, 13 and 14 for the cutscene
 * fades), so after a fade the kid, the torches and the base colours stayed
 * black while the game carried on playing normally.
 */
void set_pal_256(rgb_type* source) {
	if (source == NULL) return;
	set_pal_arr(0, 256, source);
}
#endif // USE_FADE

/*
 * Recolour a loaded sprite set.
 *
 * Used for the guards, whose shpl palette is entirely black: seg003.c writes
 * one of PRINCE.DAT's guard colour variants over the set's row per room.
 *
 * This used to walk every image in the chtab setting a per-image SDL palette -
 * 34 palettes for a guard. There is one palette now and it is the CLUT, so the
 * whole thing is a single write of sixteen entries at the set's row.
 */
void set_chtab_palette(chtab_type* chtab, const byte* colors, int n_colors) {
	if (chtab == NULL || colors == NULL) return;
	if (n_colors > 16) n_colors = 16;

	SDL_Color scolors[16];
	for (int i = 0; i < n_colors; ++i) {
		scolors[i].r = pal6_to_8(colors[i*3 + 0]);
		scolors[i].g = pal6_to_8(colors[i*3 + 1]);
		scolors[i].b = pal6_to_8(colors[i*3 + 2]);
		scolors[i].a = SDL_ALPHA_OPAQUE;
	}
	// Colour 0 of the palette data is not used; it is the background colour.
	// Needed for correct alternate colours (v1.3) of level 8.
	scolors[0].r = scolors[0].g = scolors[0].b = 0;
	scolors[0].a = SDL_ALPHA_TRANSPARENT;

	int first = chtab->palette_row * 16;
	if (SDL_SetPaletteColors(PSDL_GlobalPalette(), scolors, first, n_colors) != 0) {
		sdlperror("set_chtab_palette: SDL_SetPaletteColors");
		quit(1);
	}
	for (int i = 0; i < n_colors; ++i) {
		palette[first + i].r = colors[i*3 + 0];
		palette[first + i].g = colors[i*3 + 1];
		palette[first + i].b = colors[i*3 + 2];
	}
}

int has_timer_stopped(int timer_index) {
#ifdef USE_COMPAT_TIMER
	return wait_time[timer_index] == 0;
#else
#ifdef USE_REPLAY
	if ((replaying && skipping_replay) || is_validate_mode) return true;
#endif
	//PSP: overshoot always too big, 333mhz mandatory to read input!
	Uint64 current_counter = SDL_GetPerformanceCounter();
	int ticks_elapsed = (int)((current_counter / perf_counters_per_tick) - (timer_last_counter[timer_index] / perf_counters_per_tick));
	int overshoot = ticks_elapsed - wait_time[timer_index];
	if (overshoot >= 0) {
//		float milliseconds_elapsed = (current_counter - timer_last_counter[timer_index]) * milliseconds_per_counter;
//		printf("timer %d:   frametime (ms) = %5.1f    fps = %.1f    timer ticks elapsed = %d\n", timer_index, milliseconds_elapsed, 1000.0f / milliseconds_elapsed, ticks_elapsed);
		if (overshoot > 0 && overshoot <= 3) {
			current_counter -= overshoot * perf_counters_per_tick;
		}
		timer_last_counter[timer_index] = current_counter;
		return true;
	} else {
		return false;
	}
#endif
}
