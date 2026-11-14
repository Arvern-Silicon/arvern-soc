# tst_plic_gpio — PLIC and GPIO test

A bare-metal check of the PLIC (`0x0C00_0000`), of the GPIO on the `GPIO_1` header
(`0x1004_1000`, `GPIO_1[25:0]`, header pins 1–28) and of the GPIO on the Arduino header
(`0x1004_2000`, `ARDUINO_IO[15:0]`), usable both in the board-level simulation
(`sim/rtl_sim/run/run tst_plic_gpio`) and on the hardware. It uses the core trap layer
(`arvern/sw`), the `ahb_plic` and `apb_gpio` drivers (`arvern-ips/<ip>/sw`) and the
board support in `../../bsp`.

## Set-up

- GPIO pins 0–7 are inputs with rising-edge interrupts, pins 8–15 outputs; pins 16–25
  stay inputs without interrupts.
- Every Arduino GPIO pin is an output at 0, sampled, with a rising-edge interrupt.
- Every PLIC source has priority 1 and is enabled on context 0 (the hart's M-mode
  external interrupt, `mip.MEIP`), threshold 0.
- The M-mode external interrupt handler claims through `plic_dispatch()` until nothing
  is pending. For a GPIO source it clears the pin's `IRQ_PEND` bit before the
  completion, then shows `{count[3:0], pin[3:0]}` on pins 15:8: the number of GPIO
  interrupts taken so far and the pin just serviced.

PLIC sources on this board: 1–26 GPIO pins 0–25, 27 KEY, 28 SW, 32–47 Arduino GPIO
pins 0–15; 29–31 are tied 0 (`de0_board.h`). KEY and SW reach the hart only through the PLIC.

## Phases

The LEDs show `0x10 | phase` while the test waits for an input.

| Phase | LEDs | Input | Check |
|---|---|---|---|
| 1 count | `0x11` | one rising edge on each of pins 0–7 | one interrupt per pin |
| 2 priority | `0x12` | pins 2 and 5 high (any order) | with `mie.MEIE` masked until both are pending, pin 5 (priority 6) is claimed before pin 2 (priority 2) |
| 3 threshold | `0x13`, then `0x14` | one rising edge on pin 1 | pin 1 at priority 2 under threshold 3: the edge pends but does not interrupt; `0x14` once that is checked, then threshold 0 lets it through |
| 5 KEY | `0x15` | press KEY[1] | the KEY interrupt reaches the hart through PLIC source 27 |
| 6 Arduino | `0x16` | none | the Arduino GPIO drives each pin high in turn; its input path sees the pad, so each pin interrupts once through PLIC source 32+n. The pins return to 0. |

The LEDs then give the verdict: `0xA5` pass, `0xE0 | step` failure at that step
(1 set-up, 2–3 count, 4–5 priority, 6–8 threshold, 9 KEY, 10 unexpected source, 11
final state, 12 Arduino). After a pass, pins 0–7 keep interrupting and pins 15:8 keep counting.

Built with `-DSIMULATION` (the `run` script does it), every wait gives up after a bounded
number of polls; on the board the test waits for the inputs indefinitely. Pins 0–7 need
clean edges there (a bouncing contact counts several interrupts and fails phase 1).
