/*----------------------------------------------------------------------------
 * The SDL / SDL_mixer subset Wolf4SDL uses, on the Tang Console 138K.
 *
 *   video   Belenos at 8 bpp, 320 x 200, double buffered: the game draws
 *           into its screen buffer, VRAM frame B (pixel address 0x10000); the
 *           blit to its screen surface, frame A (0x00000, the one displayed),
 *           is a GPU copy. The palette is the Belenos LUT; the DPI scaler
 *           fits the frame to the panel or the HDMI picture.
 *   time    the ACLINT machine timer (milliseconds).
 *   input   a USB gamepad on either USB-A port (J15, J16), seen as joystick 0:
 *           the d-pad on axes 0 / 1, A B X Y on buttons 0-3, Start on button
 *           6 (Esc), Select on button 9 (next weapon); Wolf4SDL's default
 *           joystick bindings and menu handling do the rest.
 *   audio   the SDL_mixer calls accepted, nothing played here: the music and
 *           the sound effects are streamed from the card (audio.c).
 *   errors  message boxes go to the TTY register (the simulation log).
 *----------------------------------------------------------------------------*/
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>
#include "SDL_mixer.h"
#include "tang_board.h"
#include "led_key_sw.h"
#include "belenos.h"
#include "ahb_aclint.h"
#include "usb_hid.h"
#include "tang_video.h"

#define FRAME_W  320
#define FRAME_H  200
#define FRAME_A  0x00000u                       /* displayed (the game's screen)      */
#define FRAME_B  0x10000u                       /* drawn (the game's screenBuffer)    */

static blns_t   blns;
static aclint_t aclint;
static lks_t    lks;
static usb_hid_t usb[2];
static uint32_t pad;                            /* buttons latched by SDL_JoystickUpdate */
static int      started;

static void tty_puts(const char *s)
{
    while (*s)
        lks_tty_putc(&lks, *s++);
}

/* HDMI hot-plug (tang_video.h, as the demo kit's): the driver's hot-plug
 * service restarts the link when a monitor appears; KEY[1] restarts it at any
 * time. A palette left waiting for a frame start goes out here too. */
uint32_t hdmi_restarts;

static void hdmi_watch(void)
{
    if (tang_video_watch(&lks) == BLNS_HP_RESTARTED)
        hdmi_restarts++;                        /* the driver placed the picture again */
}

/* stdout / stderr (syscalls.c): the TTY register, shown in the simulation log
 * and readable over the debugger */
void arv_tty_write(const void *buf, size_t len)
{
    const char *c = buf;
    while (len--)
        lks_tty_putc(&lks, *c++);
}

#ifdef WOLF_STREAM_MUSIC
int  arv_audio_init(void);
void arv_audio_pump(void);
#endif

/* The HDMI picture's shape, width / height x 100: 160 (16:10, square pixels,
 * as most source ports show the 320 x 200 frame) or 133 (4:3, the DOS game on
 * a 4:3 screen). BOARD_HDMI_STRETCH: the monitor stretches the 4:3 signal to
 * its full width, so the frame takes h_act * picture / aspect signal pixels
 * across (all the lines), the monitor's aspect from its EDID, else
 * BOARD_HDMI_ASPECT; without it the 4:3 signal keeps its shape. */
#ifndef WOLF_PICTURE_X100
#define WOLF_PICTURE_X100 160u
#endif

static void board_start(void)
{
    const aclint_config_t tcfg = { .base = BOARD_ACLINT_BASE, .num_harts = 1u,
                                   .sswi = true, .mtime_hz = BOARD_TIME_HZ };
    const tang_video_t    vid  = { .w = FRAME_W, .h = FRAME_H, .bpp = BLNS_BPP_8, .frame = FRAME_A,
                                   .picture_x100 = WOLF_PICTURE_X100 };

    if (started)
        return;
    started = 1;
    lks_init(&lks, BOARD_LKS_BASE);
    usb_hid_init(&usb[0], BOARD_USB0_BASE);
    usb_hid_init(&usb[1], BOARD_USB1_BASE);
    aclint_init(&aclint, &tcfg);
    tang_video_init(&blns, &aclint);
    tang_video_config(&vid);
    blns_lut_select(&blns, true, 0);
    tang_video_picture();
    tang_video_start();
#ifdef WOLF_STREAM_MUSIC
    if (arv_audio_init() != 0)
        tty_puts("wolf3d: no I2S, no sound\n");
#endif
}

/*------------------------------- video ------------------------------------*/

static SDL_Palette     pal[2];
static SDL_Color       palcolors[2][256];
static SDL_PixelFormat fmt[2];
static SDL_Surface     surf[2];
static int             nsurf;

int SDL_Init(Uint32 flags) { (void) flags; board_start(); return 0; }
void SDL_Quit(void) {}
const char *SDL_GetError(void) { return ""; }
SDL_bool SDL_SetHint(const char *name, const char *value) { (void) name; (void) value; return SDL_TRUE; }

SDL_Window *SDL_CreateWindow(const char *t, int x, int y, int w, int h, Uint32 f)
{
    (void) t; (void) x; (void) y; (void) w; (void) h; (void) f;
    board_start();
    return (SDL_Window *) &blns;
}
/* Raw machine-timer count (BOARD_MTIME_HZ), for profiling counters */
uint32_t arv_mtime(void) { return (uint32_t) aclint_mtime_read(&aclint); }

void SDL_DestroyWindow(SDL_Window *w) { (void) w; }
SDL_Renderer *SDL_CreateRenderer(SDL_Window *w, int i, Uint32 f) { (void) w; (void) i; (void) f; return (SDL_Renderer *) &blns; }
void SDL_DestroyRenderer(SDL_Renderer *r) { (void) r; }
int SDL_SetRenderDrawBlendMode(SDL_Renderer *r, SDL_BlendMode m) { (void) r; (void) m; return 0; }
SDL_Texture *SDL_CreateTexture(SDL_Renderer *r, Uint32 f, int a, int w, int h) { (void) r; (void) f; (void) a; (void) w; (void) h; return (SDL_Texture *) &blns; }
void SDL_DestroyTexture(SDL_Texture *t) { (void) t; }
int SDL_UpdateTexture(SDL_Texture *t, const SDL_Rect *r, const void *p, int pitch) { (void) t; (void) r; (void) p; (void) pitch; return 0; }
int SDL_RenderCopy(SDL_Renderer *r, SDL_Texture *t, const SDL_Rect *s, const SDL_Rect *d) { (void) r; (void) t; (void) s; (void) d; return 0; }
/* Every presented frame: the real time since the previous one (raw machine
 * timer, independent of the simulation build's faster game clock), on the TTY */
void SDL_RenderPresent(SDL_Renderer *r)
{
    static uint32_t t0;
    uint32_t t = aclint_mtime_lo(&aclint);
    uint32_t ms = (t - t0) / (BOARD_MTIME_HZ / 1000u);
    char buf[16], *p = buf + sizeof(buf);
    (void) r;
    t0 = t;
    *--p = 0; *--p = '\n';
    do { *--p = (char) ('0' + ms % 10u); ms /= 10u; } while (ms);
    tty_puts("frame ms: "); tty_puts(p);
}
int SDL_ShowCursor(int toggle) { (void) toggle; return 0; }

SDL_bool SDL_PixelFormatEnumToMasks(Uint32 format, int *bpp, Uint32 *r, Uint32 *g, Uint32 *b, Uint32 *a)
{
    (void) format;
    *bpp = 8; *r = *g = *b = *a = 0;               /* the 8-bit frame is the screen */
    return SDL_TRUE;
}

/* The first surface (screen) is frame A, the second (screenBuffer) frame B */
SDL_Surface *SDL_CreateRGBSurface(Uint32 flags, int w, int h, int depth, Uint32 r, Uint32 g, Uint32 b, Uint32 a)
{
    SDL_Surface *s;
    (void) flags; (void) depth; (void) r; (void) g; (void) b; (void) a;
    if (nsurf >= 2 || w != FRAME_W || h != FRAME_H)
        return NULL;
    s = &surf[nsurf];
    pal[nsurf].ncolors = 256;
    pal[nsurf].colors = palcolors[nsurf];
    fmt[nsurf].BitsPerPixel = 8;
    fmt[nsurf].BytesPerPixel = 1;
    fmt[nsurf].palette = &pal[nsurf];
    s->format = &fmt[nsurf];
    s->w = w;
    s->h = h;
    s->pitch = FRAME_W;
    s->pixels = (void *) (blns_vram8(&blns) + (nsurf ? FRAME_B : FRAME_A));
    nsurf++;
    return s;
}
void SDL_FreeSurface(SDL_Surface *s) { (void) s; }
int SDL_LockSurface(SDL_Surface *s) { (void) s; return 0; }
void SDL_UnlockSurface(SDL_Surface *s) { (void) s; }
/* A rectangle of a surface in VRAM filled by the GPU (WOLF_GPU_CLEAR: the
 * view's ceiling and floor), finished before returning */
void arv_gpu_fill(void *dst, unsigned pitch, unsigned width, unsigned height, uint8_t colour)
{
    blns_surf_t d;
    d.addr  = (uint32_t) ((uint8_t *) dst - blns_vram8(&blns));
    d.pitch = (uint16_t) pitch;
    d.swap  = 0;
    blns_gpu_fill(&blns, &d, (uint16_t) width, (uint16_t) height, colour, BLNS_ROP_S);
}

void arv_gpu_wait(void)
{
    blns_gpu_wait(&blns);
}

/* WOLF_GPU_WALLS: one wall post drawn by the GPU's texture engine. The page
 * (4 KB slot of the page cache, in VRAM) is a 64 x 64 texture whose rows are
 * the wall's columns: u runs down the post (0..63), v is the column. n pixels
 * from dst, down; 'half' is the post's half height (2 * half pixels cover the
 * 64 texels), 'skip' the pixels clipped above the view. The texture and the
 * step are sent again only when they change; arv_gpu_walls_begin forgets them
 * (the frame's other GPU commands change the source). */
static uint32_t wall_tex = ~0u;
static int32_t  wall_du  = -1;
static int      wall_synced;
static uint32_t spr_tex = ~0u;            /* WOLF_GPU_SPRITES: the sprite texture set, and its key */
static int      spr_key = -1;
volatile int    arv_gpu_walls_on = 1;      /* a debugger compares the CPU's posts with 0 */

void arv_gpu_walls_begin(void)
{
    wall_tex    = ~0u;
    wall_du     = -1;
    wall_synced = 0;
    spr_tex     = ~0u;
}

/* Before the CPU draws into the view: the GPU's queued work done, once a frame */
void arv_gpu_sync_once(void)
{
    if (!wall_synced) {
        blns_gpu_wait(&blns);
        wall_synced = 1;
    }
}

void arv_gpu_post(uint8_t *dst, unsigned n, const uint8_t *src, unsigned half, unsigned skip)
{
    const uint32_t d    = (uint32_t) (dst - blns_vram8(&blns));
    const uint32_t s    = (uint32_t) (src - blns_vram8(&blns));
    const uint32_t page = s & ~0xFFFu;
    const int32_t  du   = (int32_t) (65536u / half);              /* 64 / (2 half) texels, 5.11 */
    if (page != wall_tex) {
        const blns_tex_t t = { .addr = page, .log2_w = 6, .log2_h = 6, .swap = BLNS_SWAP_CL };
        blns_gpu_tex_begin(&blns, &t, d, FRAME_W, 0);
        wall_tex = page;
        wall_du  = -1;
    }
    if (du != wall_du) {
        blns_gpu_tex_step(&blns, (int16_t) du, 0);
        wall_du = du;
    }
    blns_gpu_tex_span(&blns, d, (uint16_t) n, (uint16_t) ((skip * (uint32_t) du) >> 5), (uint16_t) (((s & 0xFFFu) >> 6) << 6));
}

/* WOLF_GPU_SPRITES: one sprite column drawn by the GPU, as a wall post but
 * with the texels equal to 'key' skipped; the texture (the sprite's 4 KB
 * page slot) and its key are sent again only when they change. The first
 * texel is the CPU scaler's (ScaleLine): pixel p shows the last texel r with
 * floor(r * k) <= p, k = 2 * half / 64, so u0 is (p + 1) / k less one step
 * of the 10.6 coordinate, not p / k */

void arv_gpu_sprite_post(uint8_t *dst, unsigned n, const uint8_t *src, unsigned half, unsigned skip, uint8_t key)
{
    const uint32_t d    = (uint32_t) (dst - blns_vram8(&blns));
    const uint32_t s    = (uint32_t) (src - blns_vram8(&blns));
    const uint32_t page = s & ~0xFFFu;
    const int32_t  du   = (int32_t) (65536u / half);
    if (page != spr_tex || key != spr_key || wall_tex != ~1u) {
        const blns_tex_t t = { .addr = page, .log2_w = 6, .log2_h = 6, .swap = BLNS_SWAP_CL,
                               .transp = true, .transparent = key };
        blns_gpu_tex_begin(&blns, &t, d, FRAME_W, 0);
        spr_tex  = page;
        spr_key  = key;
        wall_tex = ~1u;                     /* the walls' texture is no longer set */
        wall_du  = -1;
    }
    if (du != wall_du) {
        blns_gpu_tex_step(&blns, (int16_t) du, 0);
        wall_du = du;
    }
    blns_gpu_tex_span(&blns, d, (uint16_t) n, (uint16_t) (((skip + 1u) * (uint32_t) du - 1u) >> 5),
                      (uint16_t) (((s & 0xFFFu) >> 6) << 6));
}

/* WOLF_VSYNC: set around the 3D view's copy (ThreeDRefresh), which then runs
 * on while the game goes on; everything that draws into the source next waits
 * for the GPU first (WallRefresh, the CPU's posts, a page miss) */
int arv_blit_async;
uint32_t arv_vs_now, arv_vs_wait;                /* copies at once / after a refresh    */

/* The present's status, once a frame: after a GPU error (or a stream cut by
 * a timeout) every later call would fail and the picture freeze silently, so
 * log what stopped the GPU and recover */
static void gpu_check(blns_status_t st)
{
    if (st != BLNS_ERR_GPU && st != BLNS_ERR_STATE)
        return;
    blns_gpu_err_t e;
    blns_gpu_error(&blns, &e);
    char m[] = "wolf3d: GPU error, cause 0 op 0, recovered\n";
    m[24] = (char) ('0' + (e.cause & 7u));
    m[29] = "0123456789ABCDEF"[e.opcode & 15u];
    tty_puts(m);
    blns_gpu_recover(&blns);
}

/* A whole-surface blit (VH_UpdateScreen, VL_ScreenToScreen): a GPU copy,
 * finished before returning (the game draws into the source next) */
int SDL_UpperBlit(SDL_Surface *s, const SDL_Rect *sr, SDL_Surface *d, SDL_Rect *dr)
{
    blns_surf_t src, dst;
    (void) sr; (void) dr;
    if (s == d)
        return 0;
    src.addr = (uint32_t) ((uint8_t *) s->pixels - blns_vram8(&blns));
    dst.addr = (uint32_t) ((uint8_t *) d->pixels - blns_vram8(&blns));
    src.pitch = dst.pitch = FRAME_W;
    src.swap = dst.swap = 0;
    hdmi_watch();
#ifdef WOLF_VSYNC
    /* the copy starts at the end of a displayed frame: it is done (about
       0.7 ms) inside the vertical blanking, before the scan reaches the frame.
       Adaptive (the driver's present policy): a frame that comes after the
       image's scan (in the blanking or the bottom border) or less than a third
       of the image late is copied at once, a tear at worst, instead of waiting
       for the next refresh */
    bool waited;
    gpu_check(blns_gpu_present_copy(&blns, &src, &dst, FRAME_W, FRAME_H, BLNS_PRESENT_ADAPTIVE, &waited));
    if (waited)
        arv_vs_wait++;
    else
        arv_vs_now++;
    if (!arv_blit_async)
        blns_gpu_wait(&blns);
#else
    gpu_check(blns_gpu_copy(&blns, &src, &dst, FRAME_W, FRAME_H, BLNS_ROP_S));
    blns_gpu_wait(&blns);
#endif
    return 0;
}

/* A fill (VL_ClearScreen): the GPU's, after whatever it still has queued (the
 * view's copy, WOLF_VSYNC) and finished before returning, since the game
 * draws over it with the CPU next */
int SDL_FillRect(SDL_Surface *s, const SDL_Rect *r, Uint32 color)
{
    int x0 = r ? r->x : 0, y0 = r ? r->y : 0, w = r ? r->w : s->w, h = r ? r->h : s->h, y;
    if (w <= 0 || h <= 0)
        return 0;
    {
        const blns_surf_t d = { (uint32_t) ((uint8_t *) s->pixels - blns_vram8(&blns)) +
                                (uint32_t) (y0 * s->pitch + x0), (uint16_t) s->pitch, BLNS_SWAP_NONE };
        if (blns_gpu_fill(&blns, &d, (uint16_t) w, (uint16_t) h, (uint8_t) color, BLNS_ROP_S) == BLNS_OK &&
            blns_gpu_wait(&blns) == BLNS_OK)
            return 0;
    }
    blns_gpu_recover(&blns);                    /* not expected: the CPU */
    for (y = y0; y < y0 + h; y++)
        memset((uint8_t *) s->pixels + y * s->pitch + x0, (int) color, (size_t) w);
    return 0;
}

/* The colour table is the screen surface's palette (frame A is the one
 * displayed): the screen buffer's copy is kept, not loaded (the game sets
 * both at start). Tear-free: into the bank not shown, selected at a frame
 * start (tang_video_palette; a fade step or a flash of the same frame goes
 * at the next poll, SDL_GetTicks) */
int SDL_SetPaletteColors(SDL_Palette *p, const SDL_Color *c, int first, int n)
{
    static uint32_t rgb[256];
    int i;
    if (first < 0 || n <= 0 || first + n > 256)
        return -1;
    memcpy(&p->colors[first], c, (size_t) n * sizeof(*c));
    if (p != &pal[0])
        return 0;
    for (i = 0; i < n; i++)
        rgb[first + i] = ((uint32_t) c[i].r << 16) | ((uint32_t) c[i].g << 8) | c[i].b;
    tang_video_palette(rgb, 256u);
    return 0;
}

Uint32 SDL_MapRGBA(const SDL_PixelFormat *f, Uint8 r, Uint8 g, Uint8 b, Uint8 a)
{
    (void) f; (void) a;
    return ((Uint32) r << 16) | ((Uint32) g << 8) | b;
}

int SDL_SaveBMP_RW(SDL_Surface *s, SDL_RWops *d, int f) { (void) s; (void) d; (void) f; return -1; }

/*------------------------------- time -------------------------------------*/

/* Milliseconds from 32-bit timer deltas (no 64-bit division): the low timer
 * word wraps every 13 minutes at the board's 5.6 MHz, far longer than the
 * game ever goes without asking the time */
Uint32 SDL_GetTicks(void)
{
    static uint32_t last, rem, ms;
    const uint32_t  now = aclint_mtime_lo(&aclint);
    hdmi_watch();
#ifdef WOLF_STREAM_MUSIC
    arv_audio_pump();
#endif
    rem  += now - last;
    last  = now;
    ms   += rem / (BOARD_TIME_HZ / 1000u);
    rem  %= BOARD_TIME_HZ / 1000u;
    return ms;
}

void SDL_Delay(Uint32 ms)
{
    Uint32 t0 = SDL_GetTicks();
    while (SDL_GetTicks() - t0 < ms)
        ;
}

/*------------------------------- input ------------------------------------*/

int SDL_PollEvent(SDL_Event *e) { (void) e; return 0; }
/* The gamepad is polled (SDL_JoystickUpdate): waiting for an event returns a
 * neutral one after a millisecond, so the callers poll the controls again */
int SDL_WaitEvent(SDL_Event *e) { SDL_Delay(1); if (e) { memset(e, 0, sizeof(*e)); e->type = SDL_USEREVENT; } return 1; }
Uint8 SDL_EventState(Uint32 t, int s) { (void) t; (void) s; return 0; }
SDL_Keymod SDL_GetModState(void) { return KMOD_NONE; }
Uint32 SDL_GetMouseState(int *x, int *y) { if (x) *x = 0; if (y) *y = 0; return 0; }
Uint32 SDL_GetRelativeMouseState(int *x, int *y) { if (x) *x = 0; if (y) *y = 0; return 0; }
int SDL_SetRelativeMouseMode(SDL_bool on) { (void) on; return 0; }
void SDL_SetWindowGrab(SDL_Window *w, SDL_bool g) { (void) w; (void) g; }
void SDL_WarpMouseInWindow(SDL_Window *w, int x, int y) { (void) w; (void) x; (void) y; }
/* Joystick 0: the gamepads on both ports, ORed */
static const uint32_t joy_button[10] = {
    USB_BTN_A, USB_BTN_B, USB_BTN_X, USB_BTN_Y, 0u, 0u, USB_BTN_START, 0u, 0u, USB_BTN_SELECT
};

int SDL_NumJoysticks(void) { return 1; }
SDL_Joystick *SDL_JoystickOpen(int i) { (void) i; return (SDL_Joystick *) &pad; }
void SDL_JoystickClose(SDL_Joystick *j) { (void) j; }
int SDL_JoystickNumButtons(SDL_Joystick *j) { (void) j; return 10; }
int SDL_JoystickNumHats(SDL_Joystick *j) { (void) j; return 0; }
Uint8 SDL_JoystickGetHat(SDL_Joystick *j, int h) { (void) j; (void) h; return SDL_HAT_CENTERED; }

/* Buttons a debugger adds to the gamepads' (USB_BTN_* bits), for scripted tests */
volatile uint32_t arv_pad_inject;

void SDL_JoystickUpdate(void)
{
    uint32_t b = arv_pad_inject;
    int i;
    for (i = 0; i < 2; i++) {
        (void) usb_hid_service(&usb[i]);
        if (usb_hid_type(&usb[i]) == USB_HID_GAMEPAD)
            b |= usb_hid_buttons(&usb[i]);
    }
    pad = b;
}

Sint16 SDL_JoystickGetAxis(SDL_Joystick *j, int a)
{
    uint32_t neg = a ? USB_BTN_UP : USB_BTN_LEFT, pos = a ? USB_BTN_DOWN : USB_BTN_RIGHT;
    (void) j;
    if (a > 1)
        return 0;
    return (pad & neg) ? -32768 : (pad & pos) ? 32767 : 0;
}

Uint8 SDL_JoystickGetButton(SDL_Joystick *j, int b)
{
    (void) j;
    return (b >= 0 && b < 10 && (pad & joy_button[b])) ? 1u : 0u;
}

/*------------------------------- messages ---------------------------------*/

int SDL_ShowSimpleMessageBox(Uint32 flags, const char *title, const char *message, SDL_Window *w)
{
    (void) flags; (void) w;
    tty_puts(title); tty_puts(": "); tty_puts(message); tty_puts("\n");
    return 0;
}

/*------------------------------- files ------------------------------------*/

SDL_RWops *SDL_RWFromFile(const char *f, const char *m) { (void) f; (void) m; return NULL; }
SDL_RWops *SDL_RWFromMem(void *m, int s) { (void) m; (void) s; return NULL; }
int mkdir(const char *path, unsigned mode) { (void) path; (void) mode; return 0; }

/*------------------------------- audio (silent) ---------------------------*/

int Mix_OpenAudio(int f, Uint16 fmt_, int c, int s) { (void) f; (void) fmt_; (void) c; (void) s; return 0; }
int Mix_OpenAudioDevice(int f, Uint16 fmt_, int c, int s, const char *d, int a) { (void) f; (void) fmt_; (void) c; (void) s; (void) d; (void) a; return 0; }
void Mix_CloseAudio(void) {}
int Mix_QuerySpec(int *f, Uint16 *fmt_, int *c) { if (f) *f = 44100; if (fmt_) *fmt_ = AUDIO_S16LSB; if (c) *c = 2; return 1; }
int Mix_AllocateChannels(int n) { return n; }
int Mix_ReserveChannels(int n) { return n; }
int Mix_GroupChannels(int a, int b, int t) { (void) a; (void) b; (void) t; return 0; }
int Mix_GroupAvailable(int t) { (void) t; return 0; }
int Mix_GroupOldest(int t) { (void) t; return -1; }
int Mix_PlayChannel(int c, Mix_Chunk *k, int l) { (void) k; (void) l; return c < 0 ? 0 : c; }
int Mix_HaltChannel(int c) { (void) c; return 0; }
int Mix_Playing(int c) { (void) c; return 0; }
int Mix_SetPanning(int c, Uint8 l, Uint8 r) { (void) c; (void) l; (void) r; return 0; }
Mix_Chunk *Mix_LoadWAV_RW(SDL_RWops *s, int f) { (void) s; (void) f; return NULL; }
void Mix_FreeChunk(Mix_Chunk *k) { (void) k; }
void Mix_SetPostMix(void (*m)(void *, Uint8 *, int), void *a) { (void) m; (void) a; }
void Mix_HookMusic(void (*m)(void *, Uint8 *, int), void *a) { (void) m; (void) a; }
void Mix_ChannelFinished(void (*f)(int)) { (void) f; }
const char *Mix_GetError(void) { return ""; }

/*------------------------------- entry ------------------------------------*/

int wolf_main(int argc, char *argv[]);
int arv_chdir(const char *path);
int wolf_tables_load(void);

#define APP_DIR "/APPS/WOLF3D"

int main(void)
{
    /* the program's folder on the card: data, configuration and saved games */
    static char *argv[] = { "wolf3d", "--res", "320", "200", "--configdir", APP_DIR, NULL };
    board_start();
    tty_puts("wolf3d: start\n");
    if (arv_chdir(APP_DIR) != 0) {
        tty_puts("wolf3d: no " APP_DIR " on the card\n");
        for (;;)
            ;
    }
#ifdef WOLF_SD_TABLES
    if (wolf_tables_load() != 0) {
        tty_puts("wolf3d: no valid TABLES.BIN in " APP_DIR "\n");
        for (;;)
            ;
    }
#endif
    return wolf_main(6, argv);
}
