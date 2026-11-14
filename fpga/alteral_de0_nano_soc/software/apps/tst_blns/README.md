# tst_blns — Belenos graphic controller smoke test

A bare-metal check of the Belenos graphic controller on this board, usable both in
the board-level simulation (`sim/rtl_sim/run/run tst_blns`) and on the hardware.

## What it does

1. Reads `BLNS_ID` and `BLNS_CFG` through the register port (`0x1005_0000`).
2. Writes and reads back VRAM through the window (`0x8200_0000`), word and byte
   accesses.
3. Initialises the ILI9341 panel (reset, sleep out, 16 bpp, display on) of the
   Terasic LT24 module on `GPIO_0` (8080-16 bus).
4. Draws eight vertical colour bars at 16 bpp and sends one manual refresh, checking
   the `REFR_DONE` interrupt on the core's platform IRQ 0 (`mip[16]`, `BOARD_IRQ_BELENOS`).
5. On the board, keeps the panel refreshed from the Belenos period timer (40 ms).

6. Checks the GPU features of Belenos 0.1.0-preview (the classic persona), each drawn into
   the VRAM after the frame shown and read back, or read back from the registers:
   - **10 lines** in x/y mode: horizontal, vertical and diagonal, the end pixel left
     out (`no_last`), a dash pattern with its gaps skipped and drawn in a gap colour;
   - **11 hardware cursor**: the shape block (by rows and by columns), the colours,
     the hotspot, the position and `show` read back; then shown over the frame
     (shape codes 1-3 and transparent pixels): a manual refresh sends it to the
     panel (the stimulus checks the panel model's pixels while the LEDs read
     `0x0C`), then it is hidden and a third refresh sends the frame alone;
   - **12 mask plane** (a copy where the mask bit is 1, and inverted) and **palette
     remap** at 8 bpp (a copy through the table, `remap_rect`; refused at 16 bpp);
   - **13 immediate source** (a 16 bpp copy and an opaque 1 bpp expansion pushed behind
     their command) and **`blns_gpu_discard`** (a raw fill whose last command misses
     its operand: dropped, the work before it drawn, the stream in step after it);
   - **14 command list**: a fill and a copy recorded, called twice, its fence reached;
   - **15 FIFO room**: `blns_gpu_wait_space` on an empty FIFO, `blns_gpu_fifo_low_set`
     (the `GPU_FIFO_LOW` threshold and its event), then the FIFO filled behind a long
     fill: no room and no event until `blns_gpu_wait_space` returns; the frame shown
     checked again.

The LEDs give the verdict: `0xA5` pass, `0xE0 | step` failure at that step
(1 ID, 2 configuration, 3–4 VRAM, 5–7 interrupt, 9 frame counter, fence and
palette flip, 10–15 the GPU features above; the failed sub-check in `blns_check`).

Built with `-DSIMULATION` (the `run` script does it), the frame is 8 × 2 pixels and
the panel delays run 500 times faster (`BOARD_TIME_HZ`, ACLINT driver), so the testbench can check every pixel the panel model
receives.

## Panel wiring

The LT24 plugs directly onto `GPIO_0`; its pin map is in
[`arvern_fpga.v`](../../../rtl/verilog/arvern_fpga.v).
