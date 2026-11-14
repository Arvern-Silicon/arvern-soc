/*----------------------------------------------------------------------------
 *          _    _           Family:    aRVern System IPs
 *         / \__/ \          File:      main.c (lvgl_demo)
 *        /   /\   \         --------------------------------------------
 *    ===/   /=========      Copyright: (c) 2026, aRVern-dev
 *      /   / RV \   \       Contact:   arvernsilicon@gmail.com
 *     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * Full license text is available in the LICENSE file at the repository root.
 *----------------------------------------------------------------------------
 * LVGL v9.6 on the Tang Console 138K: one screen of widgets, rendered by
 * LVGL's software renderer and the Belenos GPU draw unit into two draw buffers
 * in the spare VRAM, each finished area copied into the 16 bpp Belenos frame
 * by the GPU (lv_belenos.c, GPU flush).
 *
 *   left card    a button (counts its clicks), a switch (the LED), a checkbox
 *                (the arc's animation), a slider and the bar it drives
 *   right card   an arc swinging 0..100 (or set by hand), an LED pulsing, a
 *                spinner, the CPU load of the last 24 seconds as a line
 *   top          the frame rate and the CPU load, once a second
 *
 * Pointer: the Belenos hardware cursor, an LVGL pointer input device: moving
 * it redraws nothing (the refresh lays the cursor over the frame it sends).
 * Until the first gamepad input an attract mode drives it: it clicks the
 * button, flips the switch and drags the slider, again and again. A USB
 * gamepad then moves it with the d-pad (faster while held) and A presses.
 *
 * Frame rate: frames rendered per second (LVGL redraws only what changed,
 * at most every LV_DEF_REFR_PERIOD). CPU load: the share of the time spent in
 * lv_timer_handler (rendering, flushing, animations, input).
 *
 * The simulation build reports every rendered frame on the TTY ("lvgl_demo:
 * frame <n> ..."; the first one is the whole screen) and LVGL's memory and the
 * stack after SIM_REPORT_FRAMES frames; its attract mode makes one pass
 * (shorter moves), then rests on the button ("lvgl_demo: attract done").
 * Other ways to draw (DEFS or SIM_FW_DEFINES; see lv_belenos.h):
 *   LVGL_FLUSH_CPU     a 24-line draw buffer in the SRAM, copied into the
 *                      frame by the CPU (no GPU drawing: the GPU cannot read
 *                      the SRAM)
 *   LVGL_FLUSH_DIRECT  DIRECT mode: LVGL renders straight into the displayed
 *                      frame (no draw buffer, no copy; the panel can show an
 *                      area while it is redrawn: animated widgets flicker)
 *   LVGL_DRAW_SW       no GPU draw unit (the software renderer draws all)
 * The default, the GPU flush, has its two draw buffers in the spare VRAM (less
 * the cursor's shape block at its end).
 * Simulation only, to compare them:
 *   LVGL_SIM_LOCKSTEP  LVGL's clock moves by one refresh period per pass of
 *                      the main loop, and the loop waits for three frame ends
 *                      after each rendered frame: frame n shows the same scene
 *                      in every build, and a whole panel frame shows it (the
 *                      stimulus saves every panel frame with -D LVGL_LOCKSTEP)
 *----------------------------------------------------------------------------*/
#include "lvgl.h"
#include "lv_belenos.h"
#include "board.h"

#ifndef SIM_REPORT_FRAMES
#define SIM_REPORT_FRAMES 12u
#endif

/*------------------------------- display ------------------------------------*/

#if defined(LVGL_FLUSH_CPU)
#ifndef DRAW_LINES
#define DRAW_LINES      24u
#endif
#define DRAW_BUF_BYTES  (FB_W * DRAW_LINES * 2u)
static uint32_t draw_buf[DRAW_BUF_BYTES / 4u];
#endif

/* Frame statistics, in machine timer ticks */
static uint32_t frames, frame_t0, frame_flush, frame_px, frame_areas;

#ifdef SIMULATION
static uint32_t ticks_us(uint32_t t)
{
    const uint32_t per_ms = board_ticks_hz() / 1000u;
    return t < 4000000u ? t * 1000u / per_ms : t / per_ms * 1000u;
}
#endif

static void render_start_cb(lv_event_t *e)
{
    (void) e;
    frame_t0    = board_ticks();
    frame_flush = frame_px = frame_areas = 0;
}

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px)
{
    const uint32_t t0 = board_ticks();
    lv_belenos_flush_cb(disp, area, px);
    frame_flush += board_ticks() - t0;
    frame_px    += (uint32_t) lv_area_get_size(area);
    frame_areas++;
    if (!lv_display_flush_is_last(disp))
        return;
    frames++;
    board_leds(frames);
#ifdef SIMULATION
    board_printf("lvgl_demo: frame %u at %u ms: render %u us, flush %u us, %u px in %u areas\n",
                 (unsigned) frames, (unsigned) board_ms(), (unsigned) ticks_us(board_ticks() - frame_t0),
                 (unsigned) ticks_us(frame_flush), (unsigned) frame_px, (unsigned) frame_areas);
    if (frames == SIM_REPORT_FRAMES) {
        lv_mem_monitor_t m;
        lv_mem_monitor(&m);
        board_printf("lvgl_demo: memory: pool %u, used %u (peak %u), frag %u%%; stack %u of %u\n",
                     (unsigned) m.total_size, (unsigned) (m.total_size - m.free_size),
                     (unsigned) m.max_used, (unsigned) m.frag_pct, (unsigned) board_stack_used(),
                     (unsigned) board_stack_size());
    }
#endif
}

static lv_display_t *display_create(void)
{
    lv_belenos_config_t cfg = {
        .dev = &board_blns, .frame = FB_PIXEL, .pitch = FB_W, .width = FB_W, .height = FB_H,
#if defined(LVGL_FLUSH_CPU)
        .flush = LV_BELENOS_FLUSH_CPU,
        .buf = { draw_buf, NULL }, .buf_bytes = DRAW_BUF_BYTES,
#elif defined(LVGL_FLUSH_DIRECT)
        .flush = LV_BELENOS_FLUSH_DIRECT,
#else
        .flush = LV_BELENOS_FLUSH_GPU,
#endif
#if !defined(LVGL_FLUSH_CPU) && !defined(LVGL_DRAW_SW)
        .gpu_draw = true,
#endif
    };
#if !defined(LVGL_FLUSH_CPU) && !defined(LVGL_FLUSH_DIRECT)
    /* the GPU flush: two draw buffers in the VRAM after the frame, before the cursor's shape */
    cfg.buf_bytes = ((board_blns.caps.vram_words * 4u - BLNS_CURSOR_WORDS * 4u - VRAM_SPARE) / 2u) & ~3u;
    cfg.buf[0]    = (void *) (blns_vram8(&board_blns) + VRAM_SPARE);
    cfg.buf[1]    = (void *) (blns_vram8(&board_blns) + VRAM_SPARE + cfg.buf_bytes);
#endif
    lv_display_t *disp = lv_belenos_create(&cfg);
    if (disp) {
        lv_display_set_flush_cb(disp, flush_cb);
        lv_display_add_event_cb(disp, render_start_cb, LV_EVENT_RENDER_START, NULL);
        board_printf("lvgl_demo: start, %s, %ux%u, %s flush, GPU drawing %s, draw buffer %u bytes\n",
                     board_hdmi() ? "HDMI" : "panel", (unsigned) FB_W, (unsigned) FB_H,
                     cfg.flush == LV_BELENOS_FLUSH_CPU ? "CPU" :
                     cfg.flush == LV_BELENOS_FLUSH_GPU ? "GPU" : "direct",
                     cfg.gpu_draw ? "on" : "off", (unsigned) cfg.buf_bytes);
    }
    return disp;
}

/*------------------------------- pointer ------------------------------------*/

/* The pointer is the Belenos hardware cursor (32 x 32, 2 bpp, its shape in the
 * last 128 words of the VRAM): blns_cursor_move() takes effect at the next
 * frame start, and LVGL redraws nothing for it. An LVGL pointer reads its
 * position and button. */
static lv_obj_t  *obj_btn, *obj_sw, *obj_sl;      /* the attract mode's targets */
static int32_t    ptr_x = FB_W / 2, ptr_y = FB_H / 2;
static bool       ptr_down;

/* The arrow, 12 x 17 at 2 bpp (pixel x in bits 2x+1:2x of its row): the
 * outline '.' colour 1, the inside '#' colour 2; the hotspot its inner tip
 *   ...         .#########.
 *   .#..        .#####.....
 *   .##..       .##.##..
 *   .###..      .#...##.
 *   .####..     ... .##..
 *   .#####..        ..##.
 *   .######..        .##.
 *   .#######..       ....
 *   .########.. */
static void cursor_init(void)
{
    static const uint32_t arrow[17] = {
        0x000015, 0x000059, 0x000169, 0x0005A9, 0x0016A9, 0x005AA9, 0x016AA9, 0x05AAA9, 0x16AAA9,
        0x1AAAA9, 0x155AA9, 0x005A69, 0x006959, 0x016915, 0x01A500, 0x01A400, 0x015400,
    };
    uint32_t rows[64] = { 0 };
    for (unsigned y = 0; y < 17u; y++)
        rows[2u * y] = arrow[y];
    blns_cursor_shape(&board_blns, board_blns.caps.vram_words - BLNS_CURSOR_WORDS, rows);
    blns_cursor_colours(&board_blns, 0x101018u, 0xFFFFFFu, 0u);
    blns_cursor_hotspot(&board_blns, 1, 1);
    blns_cursor_move(&board_blns, (int16_t) ptr_x, (int16_t) ptr_y);
    blns_cursor_show(&board_blns, true);
}

/* Attract mode: steps of the pointer, each one 'reads' pointer reads long
 * (about 30 ms each on the board), towards its target or holding there,
 * the button down or up */
#ifdef SIMULATION
#define MOVE_READS  4u
#define HOLD_READS  3u
#define DRAG_READS  6u
#else
#define MOVE_READS  24u
#define HOLD_READS  5u
#define DRAG_READS  30u
#endif
#define REST_READS  90u
enum { AT_BTN, AT_SW, AT_KNOB, AT_DRAG };
static const struct { uint8_t at, reads; bool down; } attract[] = {
    { AT_BTN,  MOVE_READS, false }, { AT_BTN,  HOLD_READS, true }, { AT_BTN,  HOLD_READS, false },
    { AT_SW,   MOVE_READS, false }, { AT_SW,   HOLD_READS, true }, { AT_SW,   HOLD_READS, false },
    { AT_KNOB, MOVE_READS, false }, { AT_KNOB, HOLD_READS, true }, { AT_DRAG, DRAG_READS, true },
    { AT_DRAG, HOLD_READS, false }, { AT_BTN,  MOVE_READS, false }, { AT_BTN,  REST_READS, false },
};
#define ATTRACT_N   (sizeof(attract) / sizeof(attract[0]))

static void attract_step(void)
{
    static unsigned step, n;
    static int32_t  x0, y0, x1, y1;
    if (step == ATTRACT_N)
        return;                             /* (the simulation's single pass) */
    x0 = ptr_x;                             /* the rest of the way at each read (easing */
    y0 = ptr_y;                             /* out); a hold or a release stays put */
    if (n == 0u && (step == 0u || attract[step].at != attract[step - 1u].at)) {
        lv_area_t a;                        /* a new target */
        lv_obj_t *o = attract[step].at == AT_BTN ? obj_btn : attract[step].at == AT_SW ? obj_sw : obj_sl;
        lv_obj_get_coords(o, &a);
        x1 = (a.x1 + a.x2) / 2;
        y1 = (a.y1 + a.y2) / 2;
        if (attract[step].at >= AT_KNOB) {  /* the knob, then the other side: 80 % or 20 % */
            int32_t v = lv_slider_get_value(obj_sl);
            if (attract[step].at == AT_DRAG)
                v = (v < 50) ? 80 : 20;
            x1 = a.x1 + v * (a.x2 - a.x1) / 100;
        }
    }
    n++;
    ptr_x    = x0 + (x1 - x0) / (int32_t) (attract[step].reads + 1u - n);
    ptr_y    = y0 + (y1 - y0) / (int32_t) (attract[step].reads + 1u - n);
    ptr_down = attract[step].down;
    if (n == attract[step].reads) {
        n = 0;
#ifdef SIMULATION
        if (++step == ATTRACT_N - 1u) {
            step = ATTRACT_N;
            board_printf("lvgl_demo: attract done\n");
        }
#else
        step = (step + 1u) % ATTRACT_N;
#endif
    }
}

static void pointer_read(lv_indev_t *indev, lv_indev_data_t *d)
{
    static bool     manual;
    static unsigned held;
    const uint32_t  p = board_pad();
    const int32_t   x = ptr_x, y = ptr_y;
    (void) indev;
    manual |= (p != 0u);                    /* the first gamepad input ends the attract mode */
    if (!manual) {
        attract_step();
    } else {
        const int32_t dx = !!(p & PAD_RIGHT) - !!(p & PAD_LEFT), dy = !!(p & PAD_DOWN) - !!(p & PAD_UP);
        const int32_t v  = 2 + (int32_t) (held < 24u ? held : 24u) / 3;   /* 2 .. 10 pixels a read */
        held     = (dx | dy) ? held + 1u : 0u;
        ptr_x    = LV_CLAMP(0, ptr_x + dx * v, (int32_t) FB_W - 1);
        ptr_y    = LV_CLAMP(0, ptr_y + dy * v, (int32_t) FB_H - 1);
        ptr_down = (p & PAD_A) != 0u;
#ifdef SIMULATION
        static bool moving;
        if (moving && !(dx | dy))
            board_printf("lvgl_demo: pointer %d,%d\n", (int) ptr_x, (int) ptr_y);
        moving = (dx | dy) != 0;
#endif
    }
    if (ptr_x != x || ptr_y != y)
        blns_cursor_move(&board_blns, (int16_t) ptr_x, (int16_t) ptr_y);
    d->point.x = ptr_x;
    d->point.y = ptr_y;
    d->state   = ptr_down ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

/*------------------------------- simulation aids ----------------------------*/

#if defined(SIMULATION) && defined(LVGL_SIM_LOCKSTEP)
#define LOCKSTEP
static uint32_t lockstep_ms;            /* LVGL's clock (busy_ticks stays 0: CPU 0 %) */
static uint32_t lockstep_tick(void) { return lockstep_ms; }
#endif

/*------------------------------- the screen ---------------------------------*/

#define HIST_N 24

static lv_obj_t *lbl_stats, *lbl_click, *lbl_level, *lbl_arc, *bar, *arc, *led, *line;
static lv_anim_t arc_anim;
static lv_point_precise_t hist[HIST_N];
static uint32_t busy_ticks;

static void click_cb(lv_event_t *e)
{
    static unsigned n;
    (void) e;
    lv_label_set_text_fmt(lbl_click, "Clicked %u", ++n);
    board_printf("lvgl_demo: button %u\n", n);
}

static void switch_cb(lv_event_t *e)
{
    const bool on = lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED);
    lv_obj_set_hidden(led, !on);
    board_printf("lvgl_demo: switch %s\n", on ? "on" : "off");
}

static void arc_set(void *obj, int32_t v)
{
    lv_arc_set_value(obj, v);
    lv_label_set_text_fmt(lbl_arc, "%d%%", (int) v);
}

static void arc_cb(lv_event_t *e)
{
    lv_label_set_text_fmt(lbl_arc, "%d%%", (int) lv_arc_get_value(lv_event_get_target_obj(e)));
}

static void animate_cb(lv_event_t *e)
{
    if (lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED))
        lv_anim_start(&arc_anim);
    else
        lv_anim_delete(arc, arc_set);
}

static void slider_cb(lv_event_t *e)
{
    const int32_t v = lv_slider_get_value(lv_event_get_target_obj(e));
    if (lv_event_get_code(e) == LV_EVENT_RELEASED) {
        board_printf("lvgl_demo: slider %d\n", (int) v);
        return;
    }
    lv_bar_set_value(bar, v, LV_ANIM_ON);
    lv_label_set_text_fmt(lbl_level, "Level %d%%", (int) v);
}

static void led_pulse(void *obj, int32_t v) { lv_led_set_brightness(obj, (uint8_t) v); }

/* Once a second: the frame rate, the CPU load and its history */
static void stats_cb(lv_timer_t *t)
{
    static uint32_t frames_q, busy_q, ticks_q, ms_q;
    const uint32_t now = board_ticks(), ms = lv_tick_get();   /* LVGL's clock */
    (void) t;
    if (ticks_q) {
        const uint32_t dt   = now - ticks_q, dms = ms - ms_q;
        const uint32_t load = (busy_ticks - busy_q) / (dt / 100u + 1u);
        const uint32_t fps  = dms ? ((frames - frames_q) * 10000u / dms) : 0u;   /* tenths */
        lv_label_set_text_fmt(lbl_stats, "%u.%u fps  CPU %u%%", (unsigned) (fps / 10u),
                              (unsigned) (fps % 10u), (unsigned) load);
        for (unsigned i = 0; i + 1u < HIST_N; i++)
            hist[i].y = hist[i + 1u].y;
        hist[HIST_N - 1].y = (lv_value_precise_t) (60 - (int32_t) (load > 100u ? 100u : load) * 60 / 100);
        lv_line_set_points_mutable(line, hist, HIST_N);
    }
    frames_q = frames;
    busy_q   = busy_ticks;
    ticks_q  = now;
    ms_q     = ms;
}

static lv_obj_t *card(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h)
{
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_set_pos(c, x, y);
    lv_obj_set_size(c, w, h);
    lv_obj_set_scrollable(c, false);
    lv_obj_set_style_pad_all(c, 10, 0);
    return c;
}

static void screen_create(void)
{
    lv_obj_t *scr = lv_screen_active();
    const int32_t w = lv_display_get_horizontal_resolution(NULL);
    const int32_t h = lv_display_get_vertical_resolution(NULL);
    const int32_t cw = (w - 24) / 2, ch = h - 40;

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, LV_SYMBOL_HOME "  LVGL 9.6 on aRVern + Belenos");
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 8, 8);
    lbl_stats = lv_label_create(scr);
    lv_label_set_text(lbl_stats, "-- fps  CPU --%");
    lv_obj_align(lbl_stats, LV_ALIGN_TOP_RIGHT, -8, 8);

    /* Left card: controls */
    lv_obj_t *l = card(scr, 8, 32, cw, ch);
    lv_obj_set_flex_flow(l, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(l, 8, 0);

    lv_obj_t *row = lv_obj_create(l);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *btn = obj_btn = lv_button_create(row);
    lv_label_set_text(lv_label_create(btn), "Click me");
    lv_obj_add_event_cb(btn, click_cb, LV_EVENT_CLICKED, NULL);
    lbl_click = lv_label_create(row);
    lv_label_set_text(lbl_click, "Clicked 0");

    row = lv_obj_create(l);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *sw = obj_sw = lv_switch_create(row);
    lv_obj_add_state(sw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(sw, switch_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_t *cb = lv_checkbox_create(row);
    lv_checkbox_set_text(cb, "Animate");
    lv_obj_add_state(cb, LV_STATE_CHECKED);
    lv_obj_add_event_cb(cb, animate_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lbl_level = lv_label_create(l);
    lv_obj_t *sl = obj_sl = lv_slider_create(l);
    lv_obj_set_width(sl, LV_PCT(95));
    lv_obj_add_event_cb(sl, slider_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(sl, slider_cb, LV_EVENT_RELEASED, NULL);
    bar = lv_bar_create(l);
    lv_obj_set_width(bar, LV_PCT(100));
    lv_slider_set_value(sl, 40, LV_ANIM_OFF);
    lv_obj_send_event(sl, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_t *note = lv_label_create(l);
    lv_obj_set_width(note, LV_PCT(100));
    lv_label_set_text(note, LV_SYMBOL_GPS " The pointer is the Belenos hardware cursor: moving it redraws nothing.");
    lv_obj_set_style_text_color(note, lv_palette_lighten(LV_PALETTE_GREY, 1), 0);

    /* Right card: the arc, the LED, the spinner, the load history */
    lv_obj_t *r = card(scr, 16 + cw, 32, cw, ch);
    arc = lv_arc_create(r);
    lv_obj_set_size(arc, 110, 110);
    lv_obj_align(arc, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_add_event_cb(arc, arc_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lbl_arc = lv_label_create(arc);
    lv_obj_center(lbl_arc);
    lv_anim_init(&arc_anim);
    lv_anim_set_var(&arc_anim, arc);
    lv_anim_set_exec_cb(&arc_anim, arc_set);
    lv_anim_set_values(&arc_anim, 0, 100);
    lv_anim_set_duration(&arc_anim, 2000);
    lv_anim_set_reverse_duration(&arc_anim, 2000);
    lv_anim_set_repeat_count(&arc_anim, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&arc_anim);

    led = lv_led_create(r);
    lv_obj_set_size(led, 28, 28);
    lv_obj_align(led, LV_ALIGN_TOP_RIGHT, -8, 6);
    lv_led_set_color(led, lv_palette_main(LV_PALETTE_ORANGE));
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, led);
    lv_anim_set_exec_cb(&a, led_pulse);
    lv_anim_set_values(&a, 40, 255);
    lv_anim_set_duration(&a, 700);
    lv_anim_set_reverse_duration(&a, 700);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&a);

    lv_obj_t *sp = lv_spinner_create(r);
    lv_obj_set_size(sp, 44, 44);
    lv_obj_align(sp, LV_ALIGN_TOP_RIGHT, 0, 60);

    lv_obj_t *cap = lv_label_create(r);
    lv_label_set_text(cap, "CPU load, last 24 s");
    lv_obj_align(cap, LV_ALIGN_BOTTOM_LEFT, 0, -66);
    for (unsigned i = 0; i < HIST_N; i++) {
        hist[i].x = (lv_value_precise_t) (i * (unsigned) (cw - 24) / (HIST_N - 1u));
        hist[i].y = 60;
    }
    line = lv_line_create(r);
    lv_line_set_points_mutable(line, hist, HIST_N);
    lv_obj_set_style_line_width(line, 2, 0);
    lv_obj_set_style_line_color(line, lv_palette_main(LV_PALETTE_LIGHT_GREEN), 0);
    lv_obj_align(line, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    lv_timer_ready(lv_timer_create(stats_cb, 1000, NULL));  /* the first call takes the baseline */
}

/*------------------------------- main ---------------------------------------*/

int main(void)
{
    board_stack_paint();
    board_init();
    lv_init();
#ifdef LOCKSTEP
    lv_tick_set_cb(lockstep_tick);
#else
    lv_tick_set_cb(board_ms);
#endif
    lv_display_t *disp = display_create();
    if (!disp) {
        board_printf("lvgl_demo: no display\n");
        for (;;)
            ;
    }
    screen_create();
    lv_obj_update_layout(lv_screen_active());  /* the attract mode's targets placed */
    cursor_init();
    lv_indev_t *ptr = lv_indev_create();
    lv_indev_set_type(ptr, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(ptr, pointer_read);

    for (;;) {
#ifdef LOCKSTEP
        const uint32_t f0 = frames;
        lv_timer_handler();
        lockstep_ms += LV_DEF_REFR_PERIOD;
        const uint16_t n0 = blns_frame_num(&board_blns);
        do
            board_poll();
        while (frames != f0 && (uint16_t) (blns_frame_num(&board_blns) - n0) < 3u);
        continue;
#endif
        const uint32_t t0   = board_ticks();
        uint32_t       wait = lv_timer_handler();
        busy_ticks += board_ticks() - t0;
        if (wait > 5u)
            wait = 5u;                          /* the gamepad and the HDMI line meanwhile */
        const uint32_t until = board_ms() + wait;
        do
            board_poll();
        while ((int32_t) (board_ms() - until) < 0);
    }
}
