//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          File:      vscreen.cpp
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// Virtual DE0-Nano-SoC board for the Verilator simulation (ARV_VSCREEN): an
// SDL2 window with the panel (fed pixel by pixel by the ILI9341 models through
// vscreen_px), the eight LEDs, the four switches and the two keys.
//
//   mouse      click a switch to flip it, press a key to hold it down
//   1 .. 4     flip SW[0] .. SW[3]
//   space / K  hold KEY[1]          R  hold KEY[0] (board reset)
//   V          panel view: panel / frame buffer (transposed) / rotated
//   S          save the window as vscreen_<n>.bmp in the run directory
//   Q / Esc    close the window (ends the simulation)
//
// With input disabled (ARV_VSCREEN_INPUT=0) the stimulus drives the switches
// and keys; the window shows them greyed and ignores clicks.
// VSCREEN_VIEW (environment) selects the initial view: panel, transpose,
// cw, ccw; VSCREEN_HOLD=0 closes the window when the simulation ends;
// VSCREEN_SNAP_MS=<t> saves a screenshot when the simulated time reaches t ms;
// VSCREEN_EXIT_MS=<t> closes the window at t ms; VSCREEN_SW=<n> sets the
// switches at start-up (scripted runs).
// Everything runs on the simulation's main thread (an SDL
// requirement on macOS): events are pumped from the periodic vscreen_tick.
//----------------------------------------------------------------------------
#include <SDL.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {

const int PW = 240, PH = 320;          // panel memory, portrait
const int SCALE = 2;
const int BOARD_W = 230;
const double CLK_NS = 20.0;            // FPGA_CLK1_50

SDL_Window   *win  = nullptr;
SDL_Renderer *ren  = nullptr;
SDL_Texture  *tex  = nullptr;
uint32_t      panel[2][PW * PH];       // 0 = 8080 (LT24), 1 = SPI module
int           active = 0;              // panel that received the last pixel
bool          dirty  = true;
int           view   = 0;              // 0 panel, 1 transposed, 2 cw, 3 ccw
bool          input_en = true;
bool          quit_req = false;

int      led = 0, key_tb = 3, sw_tb = 0;
int      sw_win = 0;
bool     key_win[2] = {false, false};  // true = pressed (pin low)
bool     mouse_key[2] = {false, false};
uint64_t cycles = 0;
uint32_t wall_start = 0, last_present = 0, last_title = 0;
int      snap_nr = 0;
double   snap_ms = -1.0;
double   exit_ms = -1.0;
bool     snap_req = false;

//---------------------------------------------------------------- tiny font
// 3 x 5 glyphs, one row per nibble (bit 2 = left column)
struct Glyph { char c; uint8_t r[5]; };
const Glyph FONT[] = {
    {'0',{7,5,5,5,7}}, {'1',{2,6,2,2,7}}, {'2',{7,1,7,4,7}}, {'3',{7,1,7,1,7}},
    {'4',{5,5,7,1,1}}, {'5',{7,4,7,1,7}}, {'6',{7,4,7,5,7}}, {'7',{7,1,1,1,1}},
    {'8',{7,5,7,5,7}}, {'9',{7,5,7,1,7}}, {'A',{7,5,7,5,5}}, {'C',{7,4,4,4,7}},
    {'D',{6,5,5,5,6}}, {'E',{7,4,6,4,7}}, {'K',{5,5,6,5,5}}, {'L',{4,4,4,4,7}},
    {'N',{6,5,5,5,5}}, {'O',{7,5,5,5,7}}, {'P',{7,5,7,4,4}}, {'R',{6,5,6,5,5}},
    {'S',{7,4,7,1,7}}, {'T',{7,2,2,2,2}}, {'W',{5,5,7,7,5}}, {'Y',{5,5,2,2,2}},
    {'I',{7,2,2,2,7}}, {'F',{7,4,6,4,4}}, {'U',{5,5,5,5,7}}, {'V',{5,5,5,5,2}},
    {'G',{7,4,5,5,7}}, {'H',{5,5,7,5,5}}, {'M',{5,7,7,5,5}}, {'B',{6,5,6,5,6}},
    {'X',{5,5,2,5,5}}, {'Q',{7,5,5,7,1}}, {'[',{3,2,2,2,3}}, {']',{6,2,2,2,6}}, {'-',{0,0,7,0,0}},
    {' ',{0,0,0,0,0}}, {'/',{1,1,2,4,4}}, {':',{0,2,0,2,0}}, {'.',{0,0,0,0,2}},
};

void text(int x, int y, const char *s, int px, SDL_Color c)
{
    SDL_SetRenderDrawColor(ren, c.r, c.g, c.b, 255);
    for (; *s; s++, x += 4 * px) {
        const Glyph *g = nullptr;
        for (const Glyph &f : FONT) if (f.c == *s) { g = &f; break; }
        if (!g) continue;
        for (int r = 0; r < 5; r++)
            for (int b = 0; b < 3; b++)
                if (g->r[r] & (4 >> b)) {
                    SDL_Rect q = {x + b * px, y + r * px, px, px};
                    SDL_RenderFillRect(ren, &q);
                }
    }
}

//---------------------------------------------------------------- geometry
int view_w() { return (view == 0) ? PW : PH; }
int view_h() { return (view == 0) ? PH : PW; }
int panel_x0() { return 16; }
int panel_y0() { return 16; }

SDL_Rect sw_rect(int i)  { return {PH * SCALE + 48 + (3 - i) * 46, 190, 30, 60}; }
SDL_Rect key_rect(int i) { return {PH * SCALE + 48 + (1 - i) * 92, 300, 70, 48}; }

uint32_t rgb565_to_argb(int v)
{
    int r = (v >> 11) & 0x1F, g = (v >> 5) & 0x3F, b = v & 0x1F;
    return 0xFF000000u | (uint32_t)(((r << 3) | (r >> 2)) << 16) |
           (uint32_t)(((g << 2) | (g >> 4)) << 8) | (uint32_t)((b << 3) | (b >> 2));
}

void build_texture()
{
    static uint32_t out[PW * PH];
    const uint32_t *src = panel[active];
    int w = view_w();
    for (int r = 0; r < PH; r++)
        for (int c = 0; c < PW; c++) {
            int x, y;
            switch (view) {
            case 1:  x = r;          y = c;          break;   // transposed
            case 2:  x = PH - 1 - r; y = c;          break;   // rotated clockwise
            case 3:  x = r;          y = PW - 1 - c; break;   // rotated counter-clockwise
            default: x = c;          y = r;          break;   // panel memory as is
            }
            out[y * w + x] = src[r * PW + c];
        }
    if (tex) SDL_DestroyTexture(tex);
    tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                            view_w(), view_h());
    SDL_UpdateTexture(tex, nullptr, out, view_w() * 4);
}

int key_state(int i)   // 1 = released (pin high)
{
    if (!input_en) return (key_tb >> i) & 1;
    return key_win[i] ? 0 : 1;
}

int sw_state(int i) { return input_en ? (sw_win >> i) & 1 : (sw_tb >> i) & 1; }

void snapshot();

void draw()
{
    SDL_SetRenderDrawColor(ren, 0x1E, 0x46, 0x2E, 255);            // PCB green
    SDL_RenderClear(ren);

    // panel bezel and picture
    SDL_Rect bezel = {panel_x0() - 6, panel_y0() - 6, PH * SCALE + 12, PH * SCALE + 12};
    SDL_SetRenderDrawColor(ren, 0x10, 0x10, 0x10, 255);
    SDL_RenderFillRect(ren, &bezel);
    if (dirty) { build_texture(); dirty = false; }
    SDL_Rect dst = {panel_x0() + (PH - view_w()) * SCALE / 2, panel_y0() + (PH - view_h()) * SCALE / 2,
                    view_w() * SCALE, view_h() * SCALE};
    SDL_RenderCopy(ren, tex, nullptr, &dst);

    const SDL_Color white = {230, 230, 230, 255}, grey = {120, 140, 125, 255};
    int bx = PH * SCALE + 40;
    text(bx, 20, active ? "SPI PANEL" : "LT24 8080", 3, white);
    const char *vn[] = {"VIEW PANEL", "VIEW FB", "VIEW CW", "VIEW CCW"};
    text(bx, 44, vn[view], 2, grey);

    // LEDs
    text(bx, 80, "LED 7-0", 2, white);
    for (int i = 0; i < 8; i++) {
        SDL_Rect l = {bx + (7 - i) * 22, 100, 14, 22};
        bool on = (led >> i) & 1;
        SDL_SetRenderDrawColor(ren, on ? 255 : 70, on ? 40 : 20, on ? 30 : 20, 255);
        SDL_RenderFillRect(ren, &l);
    }

    // switches
    text(bx, 170, input_en ? "SW 3-0" : "SW 3-0  STIMULUS", 2, input_en ? white : grey);
    for (int i = 0; i < 4; i++) {
        SDL_Rect s = sw_rect(i);
        SDL_SetRenderDrawColor(ren, 0x30, 0x30, 0x30, 255);
        SDL_RenderFillRect(ren, &s);
        SDL_Rect k = {s.x + 4, sw_state(i) ? s.y + 4 : s.y + 32, 22, 24};
        int v = input_en ? 220 : 120;
        SDL_SetRenderDrawColor(ren, v, v, v, 255);
        SDL_RenderFillRect(ren, &k);
    }

    // keys
    text(bx, 280, input_en ? "KEY 1-0" : "KEY 1-0  STIMULUS", 2, input_en ? white : grey);
    for (int i = 0; i < 2; i++) {
        SDL_Rect k = key_rect(i);
        bool pressed = key_state(i) == 0;
        int v = input_en ? (pressed ? 90 : 200) : (pressed ? 60 : 110);
        SDL_SetRenderDrawColor(ren, v, v, v, 255);
        SDL_RenderFillRect(ren, &k);
        text(k.x + 8, k.y + 18, i == 0 ? "RESET" : "KEY1", 2, {20, 20, 20, 255});
    }

    text(bx, PH * SCALE - 70, "1-4 SW  K KEY1  R RESET", 2, grey);
    text(bx, PH * SCALE - 50, "V VIEW  S SNAP  Q QUIT", 2, grey);
    if (snap_req) {                     // read the frame before it is presented
        snapshot();
        snap_req = false;
    }
    SDL_RenderPresent(ren);
}

void update_title()
{
    double sim_ms  = cycles * CLK_NS * 1e-6;
    double wall_s  = (SDL_GetTicks() - wall_start) / 1000.0;
    double slow    = (sim_ms > 0.0) ? (wall_s * 1000.0) / sim_ms : 0.0;
    char t[200];
    snprintf(t, sizeof t, "aRVern DE0-Nano-SoC  -  sim %.1f ms  -  %.0fx slower than real time%s",
             sim_ms, slow, input_en ? "" : "  -  input from the stimulus");
    SDL_SetWindowTitle(win, t);
}

void snapshot()
{
    int w, h;
    SDL_GetRendererOutputSize(ren, &w, &h);
    SDL_Surface *sf = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!sf) return;
    SDL_RenderReadPixels(ren, nullptr, SDL_PIXELFORMAT_ARGB8888, sf->pixels, sf->pitch);
    char name[64];
    snprintf(name, sizeof name, "vscreen_%d.bmp", snap_nr++);
    SDL_SaveBMP(sf, name);
    SDL_FreeSurface(sf);
    printf("vscreen: saved %s\n", name);
    fflush(stdout);
}

bool inside(const SDL_Rect &r, int x, int y)
{
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

void pump()
{
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_QUIT:
            quit_req = true;
            break;
        case SDL_KEYDOWN:
        case SDL_KEYUP: {
            bool down = (e.type == SDL_KEYDOWN);
            SDL_Keycode k = e.key.keysym.sym;
            if (down && (k == SDLK_q || k == SDLK_ESCAPE)) quit_req = true;
            if (down && k == SDLK_v) { view = (view + 1) % 4; dirty = true; }
            if (down && k == SDLK_s) snap_req = true;
            if (!input_en) break;
            if (down && !e.key.repeat && k >= SDLK_1 && k <= SDLK_4) sw_win ^= 1 << (k - SDLK_1);
            if (k == SDLK_SPACE || k == SDLK_k) key_win[1] = down || mouse_key[1];
            if (k == SDLK_r) key_win[0] = down || mouse_key[0];
            break;
        }
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP: {
            if (!input_en) break;
            bool down = (e.type == SDL_MOUSEBUTTONDOWN);
            int x = e.button.x, y = e.button.y;
            for (int i = 0; i < 4; i++)
                if (down && inside(sw_rect(i), x, y)) sw_win ^= 1 << i;
            for (int i = 0; i < 2; i++) {
                if (down && inside(key_rect(i), x, y)) mouse_key[i] = key_win[i] = true;
                if (!down && mouse_key[i]) mouse_key[i] = key_win[i] = false;
            }
            break;
        }
        default:
            break;
        }
    }
}

void service(bool force)
{
    if (!win) return;
    pump();
    uint32_t now = SDL_GetTicks();
    if (exit_ms >= 0.0 && cycles * CLK_NS * 1e-6 >= exit_ms)
        quit_req = true;
    if (snap_ms >= 0.0 && cycles * CLK_NS * 1e-6 >= snap_ms) {
        snap_ms = -1.0;
        snap_req = true;
    }
    if (force || snap_req || now - last_present >= 33) {   // about 30 frames per second
        draw();
        last_present = now;
    }
    if (now - last_title >= 500) {
        update_title();
        last_title = now;
    }
}

} // namespace

//---------------------------------------------------------------- DPI
extern "C" void vscreen_init(int input_enable)
{
    input_en = input_enable != 0;
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "vscreen: SDL_Init failed: %s\n", SDL_GetError());
        return;
    }
    const char *sm = getenv("VSCREEN_SNAP_MS");
    if (sm) snap_ms = atof(sm);
    const char *em = getenv("VSCREEN_EXIT_MS");
    if (em) exit_ms = atof(em);
    const char *sw = getenv("VSCREEN_SW");
    if (sw) sw_win = atoi(sw) & 0xF;
    const char *v = getenv("VSCREEN_VIEW");
    if (v) {
        if (!strcmp(v, "transpose")) view = 1;
        else if (!strcmp(v, "cw")) view = 2;
        else if (!strcmp(v, "ccw")) view = 3;
    }
    memset(panel, 0, sizeof panel);
    for (auto &p : panel) for (uint32_t &q : p) q = 0xFF000000u;
    win = SDL_CreateWindow("aRVern DE0-Nano-SoC", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                           PH * SCALE + 32 + BOARD_W, PH * SCALE + 32, SDL_WINDOW_SHOWN);
    ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
    wall_start = SDL_GetTicks();
    service(true);
}

extern "C" void vscreen_px(int p, int x, int y, int rgb565)
{
    if (x < 0 || x >= PW || y < 0 || y >= PH || p < 0 || p > 1) return;
    panel[p][y * PW + x] = rgb565_to_argb(rgb565);
    if (p != active) active = p;
    dirty = true;
}

extern "C" void vscreen_tick(int n, int led_v, int key_tb_v, int sw_tb_v, int *key, int *sw, int *quit)
{
    cycles += (uint64_t)n;
    led = led_v;  key_tb = key_tb_v;  sw_tb = sw_tb_v;
    service(false);
    *key  = key_state(0) | (key_state(1) << 1);
    *sw   = input_en ? sw_win : sw_tb;
    *quit = quit_req ? 1 : 0;
}

extern "C" void vscreen_done()
{
    if (!win) return;
    const char *hold = getenv("VSCREEN_HOLD");          // 0: close at the end
    if (!quit_req && !(hold && !strcmp(hold, "0"))) {
        printf("vscreen: simulation ended -- close the window to exit\n");
        fflush(stdout);
        update_title();
        while (!quit_req) {
            service(true);
            SDL_Delay(30);
        }
    }
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    win = nullptr;
}
