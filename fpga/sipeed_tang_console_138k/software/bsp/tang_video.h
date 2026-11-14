/*----------------------------------------------------------------------------
 *          _    _           Family:    aRVern System IPs
 *         / \__/ \          File:      tang_video.h
 *        /   /\   \         --------------------------------------------
 *    ===/   /=========      Copyright: (c) 2026, aRVern-dev
 *      /   / RV \   \       Contact:   arvernsilicon@gmail.com
 *     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * Full license text is available in the LICENSE file at the repository root.
 *----------------------------------------------------------------------------
 * The display set-up of the Tang Console 138K programs that draw with Belenos
 * (the demo kit, the games, the LVGL demo), in one place. One firmware runs on
 * the RGB panel bitstream (DIF_TYPE 4) and on the HDMI one (DIF_TYPE 5), told
 * apart from the driver's capabilities once Belenos is up:
 *   panel  the board timing (BOARD_LCD_TIMING, on the build's own pixel clock
 *          when it has one), the frame scaled to the panel, read from its end
 *          (the panel sits turned by half a turn)
 *   HDMI   720 x 480, the picture placed by the driver's policy
 *          (blns_dpi_picture), AVI InfoFrame, audio and SPD packets, and the
 *          hot-plug service (blns_dpi_hotplug_poll): the link restarted when a
 *          monitor appears, or when the program asks (KEY[1])
 * and a tear-free palette that never waits (tang_video_palette).
 *
 * Order of the calls (the program's own steps in between):
 *   tang_video_init      blns_init
 *   tang_video_config    timing, depth, size, orientation, frame pointer 0,
 *                        GPU on
 *   tang_video_picture   the picture in the panel or the HDMI signal, the
 *                        border, the HDMI InfoFrames
 *   tang_video_start     the display on, the hot-plug service started
 * then tang_video_poll (or tang_video_watch) between frames.
 *----------------------------------------------------------------------------*/
#ifndef TANG_VIDEO_H
#define TANG_VIDEO_H

#include <stdbool.h>
#include <stdint.h>
#include "belenos.h"
#include "ahb_aclint.h"
#include "led_key_sw.h"

typedef struct {
    uint16_t w, h;              /* the frame                                     */
    uint8_t  bpp;               /* BLNS_BPP_*                                    */
    uint32_t frame;             /* pixel address of frame pointer 0              */
    uint16_t picture_x100;      /* HDMI picture shape, width / height x 100
                                 * (0: the frame's own, square pixels)           */
} tang_video_t;

/* Belenos up (blns_init) with the board's addresses; the timer gives the
 * millisecond count of the hot-plug service and the DDC bit time */
blns_status_t tang_video_init(blns_t *dev, aclint_t *timer);
/* The HDMI bitstream (caps.dif_type), after tang_video_init */
bool          tang_video_hdmi(void);
/* The refresh's reading order for an upright picture: the panel's half turn,
 * none on HDMI (blns_set_orientation; XOR a program's own swap with it) */
uint8_t       tang_video_swap(void);
void          tang_video_config(const tang_video_t *v);
void          tang_video_picture(void);
void          tang_video_start(void);

/* Between frames: the HDMI hot-plug service (restart: restart the link now),
 * and a palette left waiting by tang_video_palette. BLNS_HP_RESTARTED when
 * the link restarted (the picture placed again) */
blns_hp_event_t tang_video_poll(bool restart);
/* tang_video_poll with a press of KEY[1] as the restart */
blns_hp_event_t tang_video_watch(lks_t *lks);
/* The hot-plug service's millisecond count (the program's time base) */
uint32_t      tang_video_ms(void);

/* Tear-free palette: entries 0 .. count-1 of rgb (kept by the program, read
 * when shown) into the colour table bank the refresh does not use, then that
 * bank selected (blns_lut_present): shown from a frame start. It never waits:
 * when the palette before has not been taken yet (the same frame), the new one
 * goes at the next tang_video_poll after a frame start. Every palette change
 * of the program goes through here: the bank shown alternates. */
void          tang_video_palette(const uint32_t *rgb, unsigned count);

#endif /* TANG_VIDEO_H */
