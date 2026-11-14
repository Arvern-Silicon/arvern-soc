# demo_kit — the Tang demos' shared layer

What the Tang Console demos (and the boot menu) share: the display set-up on
Belenos (the panel or HDMI), frames and palettes, the scene runner, the
HDMI link's watch, and the music.

## Display, frames, scenes

`demo_init()` sets up Belenos for the build's display: the RGB panel, or
HDMI (720 x 480p, the monitor's EDID read over DDC for its aspect, the AVI,
audio and SPD InfoFrames). The frame is `SCR_W` x `SCR_H` at 8 bpp, two
frames in the VRAM; `pal[]` is the working palette, `pal_show()` loads it
into the LUT bank the refresh is not showing and swaps the banks at the next
frame start. `demo_run()` runs a list of scenes in a loop; a scene calls
`scene_frame()` once a frame, which applies `KEY[1]` (skip) and `SW[0]`
(hold). [`demo_kit.h`](demo_kit.h) has the rest (load meter, trace marker).

## The HDMI link

A monitor may only accept a link that starts once it is ready, so the
frame wait watches its hot-plug line and restarts the link (off for 1 s,
then on) when needed:

- the line comes back (after 1 s low, not within 5 s of a restart: a TV may
  drop it while it locks) and stays up for 0.5 s: a restart;
- the line stays low with the link on — a monitor that selects this input
  while the link already runs may hold it low and wait, and on another input
  it is low too: every 2 s the link is off for at most 0.2 s, and a monitor
  that is there raises the line at once, which restarts the link (no probe
  within 10 s of a restart: a monitor waking from standby holds the line low
  meanwhile);
- `KEY[1]` held for 1 s: a restart; with the line low, the probes also stop
  until the line changes (for a display that would show the picture with
  the line low).

Nothing happens while the line is up. The watch reads the line's changes
from the sticky `DPI_IRQ_FLAG.DETECT` (polled, not an interrupt), so a
drop and return between two frames still counts. A monitor on another input
or asleep at start-up does not answer the EDID read: the picture then takes
`BOARD_HDMI_ASPECT`, and the next restart reads the EDID again before the
link comes on. `hdmi_restarts` counts the restarts;
[`../../tools/hdmi_log.py`](../../tools/hdmi_log.py) logs the line, the link
and the watch over a J-Link while a program runs.

## Music

A demo with a [`score.py`](../../apps/demo_effects/score.py) plays music, a
section per scene, on the sound output the boot menu selected (board I/O
`AUDIO_OUT`: HDMI, the onboard speaker or a Pmod I2S2):

- [`music.c`](music.c) is the synthesizer: six channels (pulse with three
  duties, triangle, saw with a detuned octave, sine, noise, a kick drum),
  each note with an attack, a decay to a sustain level and a release,
  optional vibrato, and a shared echo; integer only, 22.04 kHz. A new tune
  at the same tempo takes over on the next beat, at the same place in its
  bar; another one after a short fade.
- [`music_i2s.c`](music_i2s.c) streams it through the I2S master, each
  sample sent twice (44.08 kHz, an HDMI audio rate), from its `TX_LOW`
  interrupt (it takes `mtvec`: the demos use no other interrupt).
- [`demo_music.c`](demo_music.c) starts it with the first scene and changes
  the tune with each scene (`demo_run` calls `demo_music_scene`; without
  this file a weak default does nothing). The simulation builds are silent.

A score is Python: [`tools/tunes.py`](tools/tunes.py) gives instruments,
chords, arpeggios, bass lines, drum patterns and melodies in note names,
and writes the demo's `music_data.h` (the Makefile regenerates it when the
score changes). The demo's `main.c` names its tunes:

```c
#include "music_data.h"
const mus_tune_t *const *const demo_tunes    = music_tunes;
const unsigned                 demo_tunes_nr = sizeof(music_tunes) / sizeof(music_tunes[0]);
```

and its Makefile adds `music.c`, `music_i2s.c`, `demo_music.c` and the
`apb_i2s` and `ahb_plic` drivers. Every tune is whole bars of 16 steps; the
script reports channels of unequal length. The synthesizer also builds on
the host, so a score can be heard as a WAV before it goes on the board.
