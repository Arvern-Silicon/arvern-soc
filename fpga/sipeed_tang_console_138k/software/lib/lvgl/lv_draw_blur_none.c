/*----------------------------------------------------------------------------
 *          _    _           Family:    aRVern System IPs
 *         / \__/ \          File:      lv_draw_blur_none.c (software/lib/lvgl)
 *        /   /\   \         --------------------------------------------
 *    ===/   /=========      Copyright: (c) 2026, aRVern-dev
 *      /   / RV \   \       Contact:   arvernsilicon@gmail.com
 *     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * Full license text is available in the LICENSE file at the repository root.
 *----------------------------------------------------------------------------
 * LVGL without blur: stands in for src/draw/lv_draw_blur.c and
 * src/draw/sw/lv_draw_sw_blur.c, which prune.py leaves out (DROP, see
 * README.md). LVGL has no option to turn blur off, and its code (about 4 KB)
 * stays in the firmware even when nothing blurs.
 *
 * lv_draw_blur() adds no draw task: a style's blur (blur_radius) and drop
 * shadows (drop_shadow_*, drawn through a blurred layer) are not drawn; the
 * shadow's layer is still composited, unblurred. The default theme sets
 * neither; LVGL's own lv_draw_blur() adds no task either when the radius is 0,
 * so a configuration without them draws exactly the same. Box shadows
 * (shadow_*, src/draw/sw/lv_draw_sw_box_shadow.c) are not affected.
 *----------------------------------------------------------------------------*/
#include "src/draw/lv_draw_private.h"
#include "src/draw/sw/lv_draw_sw.h"

void lv_draw_blur_dsc_init(lv_draw_blur_dsc_t * dsc)
{
    lv_memzero(dsc, sizeof(lv_draw_blur_dsc_t));
    dsc->base.dsc_size = sizeof(lv_draw_blur_dsc_t);
}

lv_draw_blur_dsc_t * lv_draw_task_get_blur_dsc(lv_draw_task_t * task)
{
    (void) task;
    return NULL;                            /* no task is a blur task */
}

void lv_draw_blur(lv_layer_t * layer, const lv_draw_blur_dsc_t * dsc, const lv_area_t * coords)
{
    (void) layer;
    (void) dsc;
    (void) coords;
}

#if LV_USE_DRAW_SW
void lv_draw_sw_blur(lv_draw_task_t * t, const lv_draw_blur_dsc_t * dsc, const lv_area_t * coords)
{
    (void) t;
    (void) dsc;
    (void) coords;
}
#endif
