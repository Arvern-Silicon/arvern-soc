# demo_arvern_logo: one piece in E, a section per scene (lib/demo_kit/tools/tunes.py)
#   paint    the pad and a slow arpeggio come in, no drums
#   power    the arpeggio in sixteenths, the bass, kick and hats: powering up
#   themes   the theme on the lead, the whole band
#   spin     driving: octave bass in sixteenths, a faster arpeggio, backbeat
#   sprites  playful: a staccato tune over light drums
#   credits  the theme again, slow and soft, on a sine with echo

BPM = 100

lead    = Inst('PULSE50', 2400, attack=2, decay=5, sustain=170, release=4, vibrato=4, echo=90)
lead_hi = Inst('PULSE25', 2000, attack=1, decay=4, sustain=120, release=3, vibrato=3, echo=110)
soft    = Inst('SINE', 3400, attack=8, decay=6, sustain=210, release=6, vibrato=3, echo=150)
padi    = Inst('SAW', 1000, attack=60, decay=8, sustain=230, release=7, detune=3, echo=70)
arpi    = Inst('PULSE25', 1200, decay=3, sustain=50, release=3, echo=120)
arps    = Inst('TRI', 1900, decay=4, sustain=40, release=4, echo=160)
bassi   = Inst('TRI', 3600, decay=5, sustain=150, release=3)
bassp   = Inst('PULSE25', 1400, decay=4, sustain=110, release=2)
kick    = Inst('KICK', 5200, decay=3, sustain=0, release=3)
noise   = Inst('NOISE', 1100, decay=2, sustain=0, release=2)

A = ['E', 'C#m7', 'Amaj7', 'B']                      # the intro progression, a bar each
T = ['E', 'B', 'C#m', 'A', 'E', 'B', 'A', 'B']       # the theme's

THEME = melody('''
    B4:4 E5:2 F#5:2 G#5:4 F#5:2 E5:2
    D#5:4 F#5:4 B5:6 A5:2
    G#5:4 E5:2 C#5:2 E5:4 G#5:4
    A5:6 G#5:2 F#5:4 E5:4
    B4:2 E5:2 G#5:4 B5:4 G#5:2 B5:2
    C#6:4 B5:2 A5:2 F#5:8
    E5:2 F#5:2 G#5:4 A5:4 C#6:4
    B5:8 D#5:4 F#5:4''')

paint = Tune('logo_paint', BPM)
paint.chan(padi, pad(A, 16, 0) * 2)
paint.chan(padi, pad(A, 16, 2) * 2)
paint.chan(arps, arp(A, 16, [0, 1, 2, 3, 2, 1], octave=4, note_steps=2) * 2)
paint.chan(bassi, bass(A, 16, [(0, 16)]) * 2)

power = Tune('logo_power', BPM)
power.chan(padi, pad(A, 16, 1) * 2)
power.chan(arpi, arp(A, 16, [0, 1, 2, 3, 4, 3, 2, 1], octave=4) * 2)
power.chan(bassp, bass(A, 16, [(0, 2), (12, 2)] * 4) * 2)
power.chan(kick, drums('k.......k.......', 8, 'k'))
power.chan(noise, drums('..h...h...h...hh', 8, 'h'))

themes = Tune('logo_themes', BPM)
themes.chan(lead, THEME)
themes.chan(padi, pad(T, 16, 0))
themes.chan(arpi, arp(T, 16, [0, 1, 2, 1], octave=4, note_steps=2))
themes.chan(bassi, bass(T, 16, [(0, 4), (0, 2), (7, 2), (12, 4), (7, 4)]))
themes.chan(kick, drums('k.......k.k.....', 8, 'k'))
themes.chan(noise, drums('..h.s.h...h.s.hh', 8, 'hs'))

S = ['C#m', 'A', 'E', 'B']
spin = Tune('logo_spin', BPM)
spin.chan(lead_hi, melody('''
    G#5:2 C#6:2 E6:4 D#6:2 C#6:2 B5:4
    A5:2 C#6:2 E6:4 C#6:2 A5:2 B5:4
    G#5:2 B5:2 E6:4 B5:2 G#5:2 E5:4
    F#5:2 A5:2 D#6:4 F#6:4 D#6:4''') * 2)
spin.chan(arpi, arp(S, 16, [0, 1, 2, 3, 2, 1], octave=4) * 2)
spin.chan(bassp, bass(S, 16, [(0, 1), (12, 1)] * 8) * 2)
spin.chan(padi, pad(S, 16, 2) * 2)
spin.chan(kick, drums('k...k...k...k.k.', 8, 'k'))
spin.chan(noise, drums('hhhshhhhhhhshhsh', 8, 'hs'))

P = ['A', 'E', 'B', 'C#m', 'A', 'E', 'B', 'E']
sprites = Tune('logo_sprites', BPM)
sprites.chan(lead_hi, melody('''
    C#5:1 -:1 E5:1 -:1 A5:2 -:2 G#5:1 -:1 F#5:1 -:1 E5:4
    B4:1 -:1 E5:1 -:1 G#5:2 -:2 B5:1 -:1 A5:1 -:1 G#5:4
    F#5:1 -:1 D#5:1 -:1 F#5:2 -:2 B5:1 -:1 A5:1 -:1 F#5:4
    E5:1 -:1 G#5:1 -:1 C#6:2 -:2 B5:1 -:1 G#5:1 -:1 E5:4
    C#5:1 -:1 E5:1 -:1 A5:2 -:2 C#6:1 -:1 B5:1 -:1 A5:4
    G#5:1 -:1 B5:1 -:1 E6:2 -:2 D#6:1 -:1 B5:1 -:1 G#5:4
    F#5:1 -:1 A5:1 -:1 B5:2 -:2 D#6:1 -:1 C#6:1 -:1 B5:4
    E6:4 B5:2 G#5:2 E5:8'''))
sprites.chan(arps, arp(P, 16, [0, 2, 1, 2], octave=4, note_steps=2))
sprites.chan(bassi, bass(P, 16, [(0, 2), ('r', 2), (7, 2), ('r', 2)] * 2))
sprites.chan(kick, drums('k.....k.k.......', 8, 'k'))
sprites.chan(noise, drums('..h...h...h.h.h.', 8, 'h'))

credits = Tune('logo_credits', BPM)
credits.chan(soft, THEME)
credits.chan(padi, pad(T, 16, 0))
credits.chan(padi, pad(T, 16, 2))
credits.chan(arps, arp(T, 16, [0, 1, 2, 3], octave=4, note_steps=4))
credits.chan(bassi, bass(T, 16, [(0, 16)]))

TUNES = [paint, power, themes, spin, sprites, credits]
