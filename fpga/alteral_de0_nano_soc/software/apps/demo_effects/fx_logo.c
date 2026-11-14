//----------------------------------------------------------------------------
//          _    _           Family:    aRVern System IPs
//         / \__/ \          File:      fx_logo.c   (DE0-Nano-SoC Belenos effects demo)
//        /   /\   \         --------------------------------------------
//    ===/   /=========      Copyright: (c) 2026, aRVern-dev
//      /   / RV \   \       Contact:   arvernsilicon@gmail.com
//     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
//
// SPDX-License-Identifier: BSD-3-Clause
// Full license text is available in the LICENSE file at the repository root.
//----------------------------------------------------------------------------
// The aRVern logo for the effects: its run tables (shared with
// demo_arvern_logo, compiled here once), drawn at full size or scaled, and its
// colours lit to a given brightness.
//----------------------------------------------------------------------------
// The tables first: fx.h includes logo_data.h for the sizes only
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-const-variable"
#define LOGO_DATA_TABLES
#include "logo_data.h"
#pragma GCC diagnostic pop
#include "fx.h"

void fx_logo_draw(uint32_t base, unsigned pitch, uint8_t colour_or)
{
    for (unsigned l = 0; l < LOGO_LAYERS; l++)
        for (unsigned k = logo_layer_start[l]; k < logo_layer_start[l + 1u]; k++) {
            const uint8_t *r = &logo_runs[3u * k];
            fill(base + r[0] * pitch + r[1], r[2], 1, (uint8_t) (colour_or | logo_layer_idx[l]));
        }
}

void fx_logo_light(uint8_t pal_base, unsigned lit)
{
    for (unsigned c = 1; c < LOGO_PAL_NR; c++)
        pal[pal_base + c] = rgb_lerp(0, logo_pal_light[c], lit, 256);
}

uint32_t fx_logo_colour(unsigned c)
{
    return logo_pal_light[c];
}

void fx_logo_index(fx_logo_rows_t *rows)
{
    unsigned n = 0;

    // runs of each source row, in x order within a colour: order does not
    // matter, the runs of one row never overlap
    for (unsigned y = 0; y < LOGO_H; y++) {
        rows->start[y] = (uint16_t) n;
        for (unsigned l = 0; l < LOGO_LAYERS; l++)
            for (unsigned k = logo_layer_start[l]; k < logo_layer_start[l + 1u]; k++) {
                const uint8_t *r = &logo_runs[3u * k];
                if (r[0] == y) {
                    rows->run[n][0] = r[1];
                    rows->run[n][1] = r[2];
                    rows->run[n][2] = logo_layer_idx[l];
                    n++;
                }
            }
    }
    rows->start[LOGO_H] = (uint16_t) n;
}

void fx_logo_scaled(uint32_t frame, int cx, int cy, unsigned w, const fx_logo_rows_t *rows,
                    uint8_t colour_or, fx_box_t *box)
{
    unsigned h     = w * LOGO_H / LOGO_W;
    int      x0    = cx - (int) w / 2, y0 = cy - (int) h / 2;
    uint32_t scale = (w << 16) / LOGO_W;                       // 16.16, screen per logo pixel

    box->x0 = x0; box->y0 = y0; box->x1 = x0 + (int) w; box->y1 = y0 + (int) h;
    if (h == 0u)
        return;
    for (unsigned yd = 0; yd < h; ) {
        unsigned ys = yd * LOGO_H / h, n = 1;
        while (yd + n < h && (yd + n) * LOGO_H / h == ys)      // rows from the same source row
            n++;
        int y = y0 + (int) yd;
        if (y >= 0 && y + (int) n <= (int) SCR_H)
            for (unsigned k = rows->start[ys]; k < rows->start[ys + 1u]; k++) {
                int xa = x0 + (int) ((rows->run[k][0] * scale) >> 16);
                int xb = x0 + (int) (((rows->run[k][0] + rows->run[k][1]) * scale) >> 16);
                if (xa < 0)
                    xa = 0;
                if (xb > (int) SCR_W)
                    xb = (int) SCR_W;
                if (xb > xa)
                    fill(frame + (uint32_t) y * SCR_W + (uint32_t) xa, (unsigned) (xb - xa), n,
                         (uint8_t) (colour_or | rows->run[k][2]));
            }
        yd += n;
    }
}
