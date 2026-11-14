//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          File:      fx_torus.c  (DE0-Nano-SoC Belenos effects demo)
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// 3: TORUS
//
// A flat-shaded torus of 16 x 8 quads, double-buffered: each frame is drawn in
// the hidden frame, then the refresh frame pointer flips to it.
//
// Per frame the CPU rotates the 128 vertices and the 128 face normals with a
// Q14 rotation matrix, projects the vertices with perspective, drops the faces
// that turn away from the viewer, sorts the others far to near and shades each
// one from the angle between its normal and the light. The GPU does the pixels:
// a gradient background (64 fills), then every face filled as one convex quad
// in horizontal spans, one GPU span per row.
//
// Four panels of 2 x 2 quads around the outer belt carry the aRVern logo: a 64 x 45
// texture sampled from the logo's runs at start-up and kept in the VRAM after
// the two frames; each row of a logo face is one textured GPU span (affine,
// transparent) over the face colour, in the same far-to-near order.
//
// Palette: 0..63 the faces (4 hues x 16 shades), 64..127 the background,
// 128..247 the logo (6 shades x 20 colours, the circuit traces as one).
//----------------------------------------------------------------------------
#include "fx.h"

#define TU               16u                   // segments around the ring
#define TV               8u                    // segments around the tube
#define FACE_NR          (TU * TV)
#define RING_R           70                    // ring radius (world units)
#define TUBE_R           32                    // tube radius
#define FOCAL            220
#define SHADES           16u
#define PAL_BG           64u
#define PAL_TEX          128u
#define TEX_W            64
#define TEX_H            45
#define TEX_COLS         20u                   // logo colours 1..19, then the traces
#define TEX_VRAM         VRAM_SPARE            // the texture, 128 x 64, after the two frames
#define TEX_SHADES       6u
#define PANEL_U          2u                    // ring segments per panel
#define PANEL_EVERY      4u                    // a panel every 4 ring segments: 4 logos

typedef struct { int32_t x, y, z; } v3_t;

typedef struct {
    v3_t     vtx[FACE_NR], nrm[FACE_NR];       // object space
    int16_t  px[FACE_NR], py[FACE_NR];         // projected vertices
    int32_t  vz[FACE_NR];                      // rotated vertex depth
    int32_t  fz[FACE_NR];                      // face depth (for the sort)
    uint8_t  fcol[FACE_NR];                    // face colour index
    uint8_t  order[FACE_NR];
    uint8_t  tex[TEX_H][TEX_W];                // logo texture, colour 0..19, 255 = none
    fx_logo_rows_t rows;                       // logo runs by row (texture building only)
} torus_mem_t;
FX_MEM_CHECK(torus_mem_t);
static torus_mem_t *mem;
#define vtx              (mem->vtx)
#define nrm              (mem->nrm)
#define px               (mem->px)
#define py               (mem->py)
#define vz               (mem->vz)
#define fz               (mem->fz)
#define fcol             (mem->fcol)
#define order            (mem->order)
#define tex              (mem->tex)

//----------------------------------------------------------------------------
// Triangle as horizontal spans (16.16 edges, top-left fill rule): one
// EXEC_SPAN word per span, the colour set once per triangle
//----------------------------------------------------------------------------
static void span(uint32_t frame, int y, int xa, int xb)
{
    if (y < 0 || y >= (int) SCR_H)
        return;
    if (xa < 0)
        xa = 0;
    if (xb > (int) SCR_W)
        xb = (int) SCR_W;
    if (xb > xa)
        blns_gpu_span(&blns, frame + (uint32_t) y * SCR_W + (uint32_t) xa, (uint16_t) (xb - xa));
}

static void triangle(uint32_t frame, int x0, int y0, int x1, int y1, int x2, int y2, uint8_t c)
{
    int t;
    // sort by y: y0 <= y1 <= y2
    if (y1 < y0) { t = x0; x0 = x1; x1 = t; t = y0; y0 = y1; y1 = t; }
    if (y2 < y0) { t = x0; x0 = x2; x2 = t; t = y0; y0 = y2; y2 = t; }
    if (y2 < y1) { t = x1; x1 = x2; x2 = t; t = y1; y1 = y2; y2 = t; }
    if (y2 == y0)
        return;
    blns_gpu_span_begin(&blns, frame + (uint32_t) ((y0 > 0) ? y0 : 0) * SCR_W, 0, c);

    int32_t dl = ((x2 - x0) << 16) / (y2 - y0);                // long edge 0 -> 2
    int32_t xl = x0 << 16;
    int32_t xs, ds;
    for (int pass = 0; pass < 2; pass++) {
        int ya = pass ? y1 : y0;
        int yb = pass ? y2 : y1;
        if (yb == ya)
            continue;
        xs = (pass ? x1 : x0) << 16;
        ds = (((pass ? x2 : x1) - (pass ? x1 : x0)) << 16) / (yb - ya);
        for (int y = ya; y < yb; y++) {
            int a = (int) (xl >> 16), b = (int) (xs >> 16);
            if (a < b) span(frame, y, a, b); else span(frame, y, b, a);
            xl += dl;
            xs += ds;
        }
    }
}

//----------------------------------------------------------------------------

static void torus_init(void)
{
    for (unsigned u = 0; u < TU; u++)
        for (unsigned v = 0; v < TV; v++) {
            unsigned i  = u * TV + v;
            unsigned au = u * (256u / TU), av = v * (256u / TV);
            int32_t  rr = RING_R + (TUBE_R * fx_cos(av) >> 14);
            vtx[i].x = rr * fx_cos(au) >> 14;
            vtx[i].y = rr * fx_sin(au) >> 14;
            vtx[i].z = TUBE_R * fx_sin(av) >> 14;
            // face normal at the middle of the quad, Q14
            unsigned mu = au + 128u / TU, mv = av + 128u / TV;
            nrm[i].x = fx_cos(mv) * fx_cos(mu) >> 14;
            nrm[i].y = fx_cos(mv) * fx_sin(mu) >> 14;
            nrm[i].z = fx_sin(mv);
            // hue: alternate bands around the ring, a stripe around the tube
            fcol[i] = (uint8_t) ((((u >> 1) & 1u) + ((v == 0u) ? 2u : 0u)) * SHADES);
        }
}

static void torus_palette(void)
{
    static const uint32_t hue[4] = { 0xe0702au, 0x4286bdu, 0xf0c060u, 0x80d0ffu };
    for (unsigned h = 0; h < 4u; h++)
        for (unsigned s = 0; s < SHADES; s++)
            pal[h * SHADES + s] = (s < 12u) ? rgb_lerp(0x050505u, hue[h], s, 11)
                                            : rgb_lerp(hue[h], 0xffffffu, s - 11u, 6);
    for (unsigned i = 0; i < 64u; i++)
        pal[PAL_BG + i] = rgb_lerp(0x241040u, 0x02020au, i, 63);
}

static void draw_background(uint32_t frame)
{
    for (unsigned b = 0; b < 64u; b++) {
        unsigned y0 = b * SCR_H / 64u, y1 = (b + 1u) * SCR_H / 64u;
        fill(frame + y0 * SCR_W, SCR_W, y1 - y0, (uint8_t) (PAL_BG + b));
    }
}

//----------------------------------------------------------------------------
// The logo texture and the textured triangles
//----------------------------------------------------------------------------
static void texture_init(void)
{
    fx_logo_index(&mem->rows);
    for (unsigned y = 0; y < (unsigned) TEX_H; y++) {
        unsigned sy = y * LOGO_H / TEX_H;
        for (unsigned x = 0; x < (unsigned) TEX_W; x++) {
            unsigned sx = x * LOGO_W / TEX_W, c = 0;
            for (unsigned k = mem->rows.start[sy]; k < mem->rows.start[sy + 1u]; k++)
                if (sx >= mem->rows.run[k][0] && sx < (unsigned) (mem->rows.run[k][0] + mem->rows.run[k][1]))
                    c = mem->rows.run[k][2];
            tex[y][x] = (uint8_t) ((c == 0u) ? 255u : (c < LOGO_TRACE_BASE) ? c - 1u : TEX_COLS - 1u);
        }
    }
    // Into the VRAM after the frames as a 128 x 64 texture, the padding
    // transparent: coordinates just outside the logo wrap onto transparent texels
    volatile uint8_t *t = blns_vram8(&blns) + TEX_VRAM;
    for (unsigned y = 0; y < 64u; y++)
        for (unsigned x = 0; x < 128u; x++)
            t[y * 128u + x] = (y < (unsigned) TEX_H && x < (unsigned) TEX_W) ? tex[y][x] : 255u;
}

// Texture coordinates of a panel quad's vertex: ring position du (0..PANEL_U)
// across, tube row (TV-1, 0 or 1) down
static int32_t tex_u(unsigned du) { return (int32_t) (du * (TEX_W << 16) / PANEL_U); }
static int32_t tex_v(unsigned v)  { return (v == TV - 1u) ? 0 : (v == 0u) ? (TEX_H << 15) : (TEX_H << 16) - 1; }

typedef struct { int x, y; int32_t u, v; } tv_t;

// Each scanline is one EXEC_TEXSPAN: start (u, v) and step (du, dv) per pixel
static int16_t step_511(int32_t d)                 // 16.16 texels to signed 5.11
{
    d >>= 5;
    return (int16_t) ((d > 0x7FFF) ? 0x7FFF : (d < -0x8000) ? -0x8000 : d);
}

static void tex_triangle(uint32_t frame, tv_t p0, tv_t p1, tv_t p2, unsigned shade)
{
    static const blns_tex_t logo_tex = { .addr = TEX_VRAM, .log2_w = 7, .log2_h = 6,
                                         .transp = true, .transparent = 255u,
                                         .swap = BLNS_SWAP_NONE };
    tv_t t;
    if (p1.y < p0.y) { t = p0; p0 = p1; p1 = t; }
    if (p2.y < p0.y) { t = p0; p0 = p2; p2 = t; }
    if (p2.y < p1.y) { t = p1; p1 = p2; p2 = t; }
    if (p2.y == p0.y)
        return;

    int     dy  = p2.y - p0.y;
    int32_t xl  = p0.x << 16, ul = p0.u, vl = p0.v;
    int32_t dxl = ((p2.x - p0.x) << 16) / dy, dul = (p2.u - p0.u) / dy, dvl = (p2.v - p0.v) / dy;
    blns_gpu_tex_begin(&blns, &logo_tex, frame, 0, (uint8_t) (PAL_TEX + shade * TEX_COLS));

    for (int pass = 0; pass < 2; pass++) {
        tv_t a = pass ? p1 : p0, b = pass ? p2 : p1;
        if (b.y == a.y)
            continue;
        int     n  = b.y - a.y;
        int32_t xs = a.x << 16, us = a.u, vs = a.v;
        int32_t dxs = ((b.x - a.x) << 16) / n, dus = (b.u - a.u) / n, dvs = (b.v - a.v) / n;
        for (int y = a.y; y < b.y; y++) {
            int32_t x0 = xl, x1 = xs, u0 = ul, u1 = us, v0 = vl, v1 = vs;
            if (x0 > x1) { int32_t q; q = x0; x0 = x1; x1 = q; q = u0; u0 = u1; u1 = q; q = v0; v0 = v1; v1 = q; }
            int xa = (int) (x0 >> 16), xb = (int) (x1 >> 16);
            if (y >= 0 && y < (int) SCR_H && xb > xa) {
                int32_t du = (u1 - u0) / (xb - xa), dv = (v1 - v0) / (xb - xa);
                if (xa < 0) {
                    u0 += -xa * du;
                    v0 += -xa * dv;
                    xa  = 0;
                }
                if (xb > (int) SCR_W)
                    xb = (int) SCR_W;
                if (xb > xa) {
                    blns_gpu_tex_step(&blns, step_511(du), step_511(dv));
                    blns_gpu_tex_span(&blns, frame + (uint32_t) y * SCR_W + (uint32_t) xa,
                                      (uint16_t) (xb - xa), (uint16_t) (u0 >> 10), (uint16_t) (v0 >> 10));
                }
            }
            xl += dxl; ul += dul; vl += dvl;
            xs += dxs; us += dus; vs += dvs;
        }
    }
}

//----------------------------------------------------------------------------
// Convex quad as horizontal spans: one span per row (two triangles would take
// two), flat or textured. The two sides walk from the top vertex to the bottom
// one, round the quad either way; a quad that is not y-monotone on both sides
// (seen almost edge-on) is drawn as two triangles.
//----------------------------------------------------------------------------
typedef struct { int i, step, yb; int32_t x, dx, u, du, v, dv; } side_t;

static void side_seek(side_t *sd, const tv_t *p, int y)
{
    while (y >= sd->yb) {
        const tv_t *a = &p[sd->i], *b = &p[(sd->i + sd->step) & 3];
        int n = b->y - a->y;
        sd->yb = b->y;
        sd->i  = (sd->i + sd->step) & 3;
        sd->x  = a->x << 16;
        sd->u  = a->u;
        sd->v  = a->v;
        if (n > 0) {
            sd->dx = ((b->x - a->x) << 16) / n;
            sd->du = (b->u - a->u) / n;
            sd->dv = (b->v - a->v) / n;
        }
    }
}

static bool monotone(const tv_t *p, int top, int bot, int step)
{
    for (int i = top; i != bot; i = (i + step) & 3)
        if (p[(i + step) & 3].y < p[i].y)
            return false;
    return true;
}

// shade < 0: flat (colour c set by the caller's span_begin), else textured
static void quad(uint32_t frame, const tv_t *p, uint8_t c, int shade)
{
    static const blns_tex_t logo_tex = { .addr = TEX_VRAM, .log2_w = 7, .log2_h = 6,
                                         .transp = true, .transparent = 255u,
                                         .swap = BLNS_SWAP_NONE };
    int top = 0, bot = 0;
    for (int i = 1; i < 4; i++) {
        if (p[i].y < p[top].y) top = i;
        if (p[i].y > p[bot].y) bot = i;
    }
    if (p[bot].y == p[top].y)
        return;
    if (!monotone(p, top, bot, 1) || !monotone(p, top, bot, 3)) {
        if (shade < 0) {
            triangle(frame, p[0].x, p[0].y, p[1].x, p[1].y, p[2].x, p[2].y, c);
            triangle(frame, p[0].x, p[0].y, p[2].x, p[2].y, p[3].x, p[3].y, c);
        } else {
            tex_triangle(frame, p[0], p[1], p[2], (unsigned) shade);
            tex_triangle(frame, p[0], p[2], p[3], (unsigned) shade);
        }
        return;
    }
    side_t l = { top, 1, p[top].y, 0, 0, 0, 0, 0, 0 }, r = { top, 3, p[top].y, 0, 0, 0, 0, 0, 0 };
    int y = p[top].y;
    if (shade < 0)
        blns_gpu_span_begin(&blns, frame + (uint32_t) ((y > 0) ? y : 0) * SCR_W, 0, c);
    else
        blns_gpu_tex_begin(&blns, &logo_tex, frame, 0, (uint8_t) (PAL_TEX + (unsigned) shade * TEX_COLS));
    for (; y < p[bot].y; y++) {
        side_seek(&l, p, y);
        side_seek(&r, p, y);
        int32_t x0 = l.x, x1 = r.x, u0 = l.u, u1 = r.u, v0 = l.v, v1 = r.v;
        if (x0 > x1) { int32_t q; q = x0; x0 = x1; x1 = q; q = u0; u0 = u1; u1 = q; q = v0; v0 = v1; v1 = q; }
        int xa = (int) (x0 >> 16), xb = (int) (x1 >> 16);
        if (shade < 0)
            span(frame, y, xa, xb);
        else if (y >= 0 && y < (int) SCR_H && xb > xa) {
            int32_t du = (u1 - u0) / (xb - xa), dv = (v1 - v0) / (xb - xa);
            if (xa < 0) {
                u0 += -xa * du;
                v0 += -xa * dv;
                xa  = 0;
            }
            if (xb > (int) SCR_W)
                xb = (int) SCR_W;
            if (xb > xa) {
                blns_gpu_tex_step(&blns, step_511(du), step_511(dv));
                blns_gpu_tex_span(&blns, frame + (uint32_t) y * SCR_W + (uint32_t) xa,
                                  (uint16_t) (xb - xa), (uint16_t) (u0 >> 10), (uint16_t) (v0 >> 10));
            }
        }
        l.x += l.dx; l.u += l.du; l.v += l.dv;
        r.x += r.dx; r.u += r.du; r.v += r.dv;
    }
}

// Is quad (u, v) part of a logo panel? Returns its ring position in the panel
static int panel_of(unsigned u, unsigned v)
{
    if (v != TV - 1u && v != 0u)
        return -1;
    return ((u % PANEL_EVERY) < PANEL_U) ? (int) (u % PANEL_EVERY) : -1;
}

static void torus_frame(uint32_t frame, unsigned ax, unsigned ay, unsigned az, int dist, int cx, int cy)
{
    // rotation matrix R = Rz * Ry * Rx, Q14
    int32_t sx = fx_sin(ax), cxa = fx_cos(ax), sy = fx_sin(ay), cya = fx_cos(ay);
    int32_t sz = fx_sin(az), cza = fx_cos(az);
    int32_t m00 = cza * cya >> 14,  m01 = ((cza * sy >> 14) * sx >> 14) - (sz * cxa >> 14);
    int32_t m02 = ((cza * sy >> 14) * cxa >> 14) + (sz * sx >> 14);
    int32_t m10 = sz * cya >> 14,   m11 = ((sz * sy >> 14) * sx >> 14) + (cza * cxa >> 14);
    int32_t m12 = ((sz * sy >> 14) * cxa >> 14) - (cza * sx >> 14);
    int32_t m20 = -sy,              m21 = cya * sx >> 14, m22 = cya * cxa >> 14;
    // light from the upper left, towards the scene, Q14 unit vector
    const int32_t lx = -7000, ly = -9000, lz = -11000;
    unsigned n = 0;

    for (unsigned i = 0; i < FACE_NR; i++) {
        int32_t x = vtx[i].x, y = vtx[i].y, z = vtx[i].z;
        int32_t rx = (m00 * x + m01 * y + m02 * z) >> 14;
        int32_t ry = (m10 * x + m11 * y + m12 * z) >> 14;
        int32_t rz = ((m20 * x + m21 * y + m22 * z) >> 14) + dist;
        px[i] = (int16_t) (cx + rx * FOCAL / rz);
        py[i] = (int16_t) (cy + ry * FOCAL / rz);
        vz[i] = rz;
    }

    draw_background(frame);
    for (unsigned u = 0; u < TU; u++)
        for (unsigned v = 0; v < TV; v++) {
            unsigned a = u * TV + v, b = ((u + 1u) % TU) * TV + v;
            unsigned c = ((u + 1u) % TU) * TV + (v + 1u) % TV, d = u * TV + (v + 1u) % TV;
            // facing the viewer: clockwise on screen (y down)
            int32_t cross = (px[b] - px[a]) * (py[d] - py[a]) - (py[b] - py[a]) * (px[d] - px[a]);
            if (cross >= 0)
                continue;
            fz[a] = vz[a] + vz[b] + vz[c] + vz[d];
            order[n++] = (uint8_t) a;
        }
    // far to near (insertion sort: the order changes little between frames)
    for (unsigned i = 1; i < n; i++) {
        uint8_t k = order[i];
        unsigned j = i;
        while (j > 0u && fz[order[j - 1u]] < fz[k]) {
            order[j] = order[j - 1u];
            j--;
        }
        order[j] = k;
    }
    for (unsigned i = 0; i < n; i++) {
        unsigned a = order[i], u = a / TV, v = a % TV;
        unsigned b = ((u + 1u) % TU) * TV + v;
        unsigned c = ((u + 1u) % TU) * TV + (v + 1u) % TV, d = u * TV + (v + 1u) % TV;
        int32_t nx = (m00 * nrm[a].x + m01 * nrm[a].y + m02 * nrm[a].z) >> 14;
        int32_t ny = (m10 * nrm[a].x + m11 * nrm[a].y + m12 * nrm[a].z) >> 14;
        int32_t nz = (m20 * nrm[a].x + m21 * nrm[a].y + m22 * nrm[a].z) >> 14;
        int32_t lit = (nx * lx + ny * ly + nz * lz) >> 14;      // Q14 cosine
        unsigned s = 2u + (unsigned) ((lit > 0 ? lit : 0) * (int32_t) (SHADES - 3u) >> 14);
        uint8_t col = (uint8_t) (fcol[a] + s);
        int pu = panel_of(u, v);
        if (pu >= 0) {
            // logo face: the face colour underneath, the texture on top
            unsigned ts = (TEX_SHADES - 1u) - (s - 2u) * TEX_SHADES / (SHADES - 2u);
            if (ts >= TEX_SHADES)
                ts = TEX_SHADES - 1u;
            const tv_t tq[4] = { { px[a], py[a], tex_u((unsigned) pu),      tex_v(v) },
                                 { px[b], py[b], tex_u((unsigned) pu + 1u), tex_v(v) },
                                 { px[c], py[c], tex_u((unsigned) pu + 1u), tex_v((v + 1u) % TV) },
                                 { px[d], py[d], tex_u((unsigned) pu),      tex_v((v + 1u) % TV) } };
            quad(frame, tq, col, -1);
            quad(frame, tq, 0, (int) ts);
            continue;
        }
        const tv_t q[4] = { { px[a], py[a], 0, 0 }, { px[b], py[b], 0, 0 },
                            { px[c], py[c], 0, 0 }, { px[d], py[d], 0, 0 } };
        quad(frame, q, col, -1);
    }
}

void fx_torus(void)
{
    mem = fx_mem();
    const unsigned frames = FRAMES(720u);
    uint32_t back = F1;

    fx_clear();
    torus_init();
    texture_init();
    torus_palette();
    for (unsigned sh = 0; sh < TEX_SHADES; sh++)
        for (unsigned c = 0; c < TEX_COLS; c++)
            pal[PAL_TEX + sh * TEX_COLS + c] =
                rgb_lerp(fx_logo_colour((c < TEX_COLS - 1u) ? c + 1u : LOGO_TRACE_BASE), 0x050505u,
                         sh, TEX_SHADES + 1u);
    pal_show();

    for (unsigned f = 0; f < frames; f++) {
        unsigned t = f;
#ifdef SIMULATION
        t *= 8u;
#endif
        // fly in from far away, then tumble with a slow drift
        int dist = 230 + ((f < FRAMES(90u)) ? (int) ((FRAMES(90u) - f) * 900u / FRAMES(90u)) : 0);
        int cx   = (int) (SCR_W / 2u) + (int) (fx_sin(t / 2u) * 50 >> 14);
        int cy   = (int) (SCR_H / 2u) + (int) (fx_sin(t / 3u + 64u) * 20 >> 14);
        torus_frame(back, t * 2u, t * 3u, t, dist, cx, cy);
        blns_gpu_wait(&blns);
        blns_set_frame(&blns, 0, back);
        back = (back == F0) ? F1 : F0;
        if (scene_frame())
            break;
    }
    blns_set_frame(&blns, 0, F0);
}
