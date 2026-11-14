/*----------------------------------------------------------------------------
 *          _    _           Family:    aRVern System IPs
 *         / \__/ \          File:      tang_video.c
 *        /   /\   \         --------------------------------------------
 *    ===/   /=========      Copyright: (c) 2026, aRVern-dev
 *      /   / RV \   \       Contact:   arvernsilicon@gmail.com
 *     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * Full license text is available in the LICENSE file at the repository root.
 *----------------------------------------------------------------------------
 * The display set-up of the Tang Console 138K Belenos programs (tang_video.h).
 *----------------------------------------------------------------------------*/
#include "tang_video.h"
#include "tang_board.h"

static blns_t        *dev;
static aclint_t      *tmr;
static blns_hotplug_t hp;
static uint32_t       ms, tick_q;               /* a millisecond count from the machine timer */
static int            key_q;

static void          (*pal_service)(void);      /* palette_service, once the program has one */
static const uint32_t *pal_rgb;                 /* a palette waiting for a frame start      */
static unsigned        pal_count;
static uint16_t        pal_frame;               /* frame number of the last one shown       */
static bool            pal_sent;

static void ddc_quarter(void)                   /* a quarter of a 100 kHz DDC bit */
{
    const uint32_t ticks = BOARD_TIME_HZ / 400000u;     /* 0 in the simulation build */
    const uint32_t t0    = aclint_mtime_lo(tmr);
    while (aclint_mtime_lo(tmr) - t0 < ticks)
        ;
}

/* The HDMI picture (the driver's policy, blns_dpi_picture): the frame at its
 * own shape (or picture_x100) on a monitor that stretches the signal to its
 * EDID shape (else BOARD_HDMI_ASPECT); without BOARD_HDMI_STRETCH the largest
 * image of its shape, letterboxed. Also placed again by the hot-plug service
 * when the link restarts. */
static blns_picture_t pic = { .stretch = BOARD_HDMI_STRETCH, .fallback_x100 = BOARD_HDMI_ASPECT,
                              .quarter = ddc_quarter };

blns_status_t tang_video_init(blns_t *d, aclint_t *timer)
{
    const blns_config_t cfg = { .reg_base = BOARD_BLNS_BASE, .vram_base = BOARD_VRAM_BASE,
                                .hclk_hz = BOARD_HCLK_HZ };
    dev = d;
    tmr = timer;
    return blns_init(dev, &cfg);
}

bool tang_video_hdmi(void)
{
    return dev->caps.dif_type == BLNS_DIF_HDMI;
}

uint8_t tang_video_swap(void)
{
    return tang_video_hdmi() ? BLNS_SWAP_NONE : (uint8_t) (BLNS_SWAP_X | BLNS_SWAP_Y);
}

void tang_video_config(const tang_video_t *v)
{
    const blns_dpi_timing_t lcd  = BOARD_LCD_TIMING_SEP((dev->caps.dpi_caps & BLNS_DPI_CAPS_PCLK_SEP_Msk) != 0u);
    const blns_dpi_timing_t hdmi = BOARD_HDMI_TIMING;

    pic.src_w        = v->w;
    pic.src_h        = v->h;
    pic.picture_x100 = v->picture_x100;
    blns_dpi_config(dev, tang_video_hdmi() ? &hdmi : &lcd);
    blns_set_mode(dev, (blns_mode_t) v->bpp);
    blns_set_size(dev, v->w, v->h);
    blns_set_orientation(dev, tang_video_swap());
    blns_set_frame(dev, 0, v->frame);
    blns_gpu_enable(dev, true);
}

void tang_video_picture(void)
{
    if (tang_video_hdmi()) {
        hp.picture = &pic;
        blns_dpi_picture(dev, &pic, &hp.settled);
    } else {
        blns_dpi_fit(dev, pic.src_w, pic.src_h);
    }
    blns_dpi_border(dev, 0x000000u);
    if (tang_video_hdmi()) {
        /* HDMI signalling with an AVI InfoFrame (BOARD_HDMI_VIC); letterboxed,
         * the active part is the 16:9 frame in its centre */
        const blns_avi_t avi = { .vic     = BOARD_HDMI_VIC,
                                 .picture = BOARD_HDMI_PICTURE,
                                 .active  = BOARD_HDMI_STRETCH ? BLNS_AVI_ACTIVE_SAME : BLNS_AVI_ACTIVE_16_9,
                                 .quant   = BLNS_AVI_QUANT_DEFAULT };
        blns_dpi_hdmi(dev, &avi);
        const blns_audio_t aud = { BOARD_HDMI_AUDIO_N, BLNS_AUD_FS_44K1, BLNS_AUD_WL_16 };
        (void) blns_dpi_hdmi_audio(dev, &aud);  /* BLNS_ERR_ABSENT without HDMI audio */
        (void) blns_dpi_hdmi_spd(dev, BOARD_HDMI_VENDOR, BOARD_HDMI_PRODUCT, BLNS_SPD_GAME);
    }
}

void tang_video_start(void)
{
    blns_dpi_enable(dev, true);
    tick_q = aclint_mtime_lo(tmr);
    blns_dpi_hotplug_init(dev, &hp, 0u);        /* clears a change seen by the previous program */
}

uint32_t tang_video_ms(void)
{
    const uint32_t now = aclint_mtime_lo(tmr);
    const uint32_t n   = (now - tick_q) / (BOARD_TIME_HZ / 1000u);
    tick_q += n * (BOARD_TIME_HZ / 1000u);
    ms     += n;
    return ms;
}

/* The palette waiting, shown once the one before has been taken (a frame
 * start since, or no frames): blns_lut_present then never waits */
static void palette_service(void)
{
    if (pal_rgb == NULL)
        return;
    if (pal_sent && blns_frames_flowing(dev) && blns_frame_num(dev) == pal_frame)
        return;
    (void) blns_lut_present(dev, pal_rgb, pal_count);
    pal_frame = blns_frame_num(dev);
    pal_sent  = true;
    pal_rgb   = NULL;
}

void tang_video_palette(const uint32_t *rgb, unsigned count)
{
    pal_rgb     = rgb;
    pal_count   = count;
    pal_service = palette_service;              /* (a program without palette changes links none of it) */
    palette_service();
}

blns_hp_event_t tang_video_poll(bool restart)
{
    if (pal_service != NULL)
        pal_service();
    if (!tang_video_hdmi())
        return BLNS_HP_NONE;
    return blns_dpi_hotplug_poll(dev, &hp, tang_video_ms(), restart);
}

blns_hp_event_t tang_video_watch(lks_t *lks)
{
    const int key = lks_key_pressed(lks, 1);
    const blns_hp_event_t ev = tang_video_poll(key && !key_q);
    key_q = key;
    return ev;
}
