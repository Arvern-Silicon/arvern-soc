/*----------------------------------------------------------------------------
 *          _    _           Family:    aRVern System IPs
 *         / \__/ \          File:      lv_conf.h (lvgl_demo)
 *        /   /\   \         --------------------------------------------
 *    ===/   /=========      Copyright: (c) 2026, aRVern-dev
 *      /   / RV \   \       Contact:   arvernsilicon@gmail.com
 *     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * Full license text is available in the LICENSE file at the repository root.
 *----------------------------------------------------------------------------
 * LVGL v9.6 configuration of the Tang Console demo. Only what differs from
 * LVGL's defaults (src/lv_conf_internal.h) is set here. The firmware must fit
 * the 160 KB program memory: RGB565 rendering only, a small widget set, one
 * font, no logging, no asserts. The vendored library (software/lib/lvgl) is
 * pruned for this configuration: enabling a widget or a feature that was off
 * may need files the pruning dropped (see its README).
 *----------------------------------------------------------------------------*/
#ifndef LV_CONF_H
#define LV_CONF_H

/* Colour: RGB565, Belenos' 16 bpp format */
#define LV_COLOR_FORMAT_DEFAULT             LV_COLOR_FORMAT_RGB565

/* Memory: LVGL's own allocator on a static pool (no libc malloc) */
#define LV_USE_STDLIB_MALLOC                LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_STRING                LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_SPRINTF               LV_STDLIB_BUILTIN
/* LVGL's printf (also board_printf's): no long long conversions (%ll, %j),
 * which nothing prints; saves libgcc's 64-bit unsigned division */
#define PRINTF_DISABLE_SUPPORT_LONG_LONG
#define LV_MEM_SIZE                        (40U * 1024U)

/* Refresh: at most one frame every 20 ms (50 Hz); the CPU sets the pace */
#define LV_DEF_REFR_PERIOD                  20
#define LV_DPI_DEF                          130

#define LV_USE_OS                           LV_OS_NONE

/* Software renderer: RGB565 target, A8 for the font's glyphs; nothing else */
#define LV_DRAW_SW_SUPPORT_RGB565_SWAPPED   0
#define LV_DRAW_SW_SUPPORT_RGB565A8         0
#define LV_DRAW_SW_SUPPORT_RGB888           0
#define LV_DRAW_SW_SUPPORT_XRGB8888         0
#define LV_DRAW_SW_SUPPORT_ARGB8888         0
#define LV_DRAW_SW_SUPPORT_ARGB8888_PREMULTIPLIED 0
#define LV_DRAW_SW_SUPPORT_L8               0
#define LV_DRAW_SW_SUPPORT_AL88             0
#define LV_DRAW_SW_SUPPORT_A8               1
#define LV_DRAW_SW_SUPPORT_I1               0
/* Layers (opacity, transforms) are rendered in chunks of this size */
#define LV_DRAW_LAYER_SIMPLE_BUF_SIZE       (8U * 1024U)

/* No logging, no asserts, no observer */
#define LV_USE_LOG                          0
#define LV_USE_ASSERT_NULL                  0
#define LV_USE_ASSERT_MALLOC                0
#define LV_USE_ASSERT_STYLE                 0
#define LV_USE_ASSERT_MEM_INTEGRITY         0
#define LV_USE_ASSERT_OBJ                   0
#define LV_USE_OBSERVER                     0

/* Themes and layouts: the default theme (dark), flex only */
#define LV_THEME_DEFAULT_DARK               1
#define LV_USE_THEME_SIMPLE                 0
#define LV_USE_THEME_MONO                   0
#define LV_USE_GRID                         0

/* Font: Montserrat 14 only (the default font) */
#define LV_FONT_MONTSERRAT_14               1

/* Widgets: arc, bar, button, checkbox, image, label, LED, line, slider,
 * spinner, switch (LVGL's defaults); the others off */
#define LV_USE_ANIMIMG                      0
#define LV_USE_ARCLABEL                     0
#define LV_USE_BUTTONMATRIX                 0
#define LV_USE_CALENDAR                     0
#define LV_USE_CANVAS                       0
#define LV_USE_CHART                        0
#define LV_USE_DROPDOWN                     0
#define LV_USE_IMAGEBUTTON                  0
#define LV_USE_KEYBOARD                     0
#define LV_USE_LIST                         0
#define LV_USE_MENU                         0
#define LV_USE_MSGBOX                       0
#define LV_USE_ROLLER                       0
#define LV_USE_SCALE                        0
#define LV_USE_SPAN                         0
#define LV_USE_SPINBOX                      0
#define LV_USE_TABLE                        0
#define LV_USE_TABVIEW                      0
#define LV_USE_TEXTAREA                     0
#define LV_USE_TILEVIEW                     0
#define LV_USE_WIN                          0
#define LV_LABEL_TEXT_SELECTION             0
#define LV_LABEL_LONG_TXT_HINT              0

#define LV_BUILD_EXAMPLES                   0
#define LV_BUILD_DEMOS                      0

#endif /* LV_CONF_H */
