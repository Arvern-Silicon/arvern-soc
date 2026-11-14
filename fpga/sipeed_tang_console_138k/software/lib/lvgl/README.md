# LVGL — vendored, pruned

[LVGL](https://github.com/lvgl/lvgl) **v9.6.0** (tag `v9.6.0`, commit `80ca777`), MIT licence
([`LICENCE.txt`](LICENCE.txt); the bundled Montserrat font is OFL-1.1, see
[`COPYRIGHTS.md`](COPYRIGHTS.md)). Used by [`apps/lvgl_demo`](../../apps/lvgl_demo).

## What is here

Only what the configuration of [`apps/lvgl_demo/lv_conf.h`](../../apps/lvgl_demo/lv_conf.h) builds:

- every `src/**/*.c` of the release that produces code or data with that `lv_conf.h`
  (106 of 483 files: core, display, software renderer, the enabled widgets, the default
  theme, flex, Montserrat 14, the built-in allocator, string and `sprintf`), except the
  blur (below);
- every header those files include, and every header `lvgl.h` includes (it includes the
  headers of all modules, enabled or not, so some headers of `src/drivers`, `src/libs`,
  `src/debugging` and the GPU back-ends are here without their sources);
- `include/` (the public headers), the top-level `lvgl.h`, `lvgl_private.h`,
  `lv_version.h`, `LICENCE.txt` and `COPYRIGHTS.md`.

Dropped: `demos/`, `docs/`, `examples/`, `tests/`, `scripts/`, the build-system files,
the other fonts, the libraries in `src/libs`, the drivers in `src/drivers` and the GPU
back-ends (their headers excepted, as above). No LVGL file is modified.

Dropped although they produce code: the blur, `src/draw/lv_draw_blur.c` and
`src/draw/sw/lv_draw_sw_blur.c` (about 4 KB of firmware, LVGL has no option to turn it
off). [`lv_draw_blur_none.c`](lv_draw_blur_none.c) stands in for them (not an LVGL
file): `lv_draw_blur()` adds no draw task, so a style's `blur_radius` and the drop shadows
(`drop_shadow_*`, a blurred layer) are not blurred. The default theme uses neither (LVGL's
`lv_draw_blur()` adds no task for a radius of 0 either: the demo draws the same); box
shadows (`shadow_*`, the LED's glow) are unaffected. The list is `DROP` in `prune.py`.

An application compiles every `src/**/*.c` here and the stand-ins (`*.c` at the top) with
`-DLV_CONF_INCLUDE_SIMPLE` and its own `lv_conf.h` on the include path (see
`apps/lvgl_demo/Makefile`).

## Refreshing, or enabling more of LVGL

A widget or feature that is off in `lv_conf.h` may need sources the pruning dropped. To
change the configuration or move to another release:

```sh
git clone --depth 1 --branch v9.6.0 https://github.com/lvgl/lvgl.git /tmp/lvgl
python3 prune.py /tmp/lvgl ../../apps/lvgl_demo      # rewrites src/ and include/ here
```

[`prune.py`](prune.py) compiles every source of the checkout with the application's
`lv_conf.h` (`riscv-none-elf-gcc`, `-MMD`), keeps the files that produce symbols (less
`DROP`) and the headers their dependency files list, and copies them here (only `src/` and
`include/` are replaced: this README, `prune.py` and the stand-ins stay); the checkout is
only read. Then
rebuild the application and compare its `size` with the previous one; update the version
above.
