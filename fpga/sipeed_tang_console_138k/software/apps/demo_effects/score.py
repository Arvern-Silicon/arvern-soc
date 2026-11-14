# demo_effects: demoscene chiptune in A minor, a section per effect (lib/demo_kit/tools/tunes.py)
#   stars    space: a held pad, a sine arpeggio in the echo, light hats
#   plasma   a funky groove: syncopated bass, chord stabs, sixteenth hats
#   torus    driving: four on the floor, a fast arpeggio, a minor riff
#   fire     dramatic: heavy kick, a saw riff, a lead in harmonic minor
#   scroll   the classic scroller tune: major, a catchy lead over the band

BPM = 125

lead    = Inst('PULSE25', 2300, attack=1, decay=5, sustain=160, release=4, vibrato=4, echo=100)
lead50  = Inst('PULSE50', 2100, attack=1, decay=5, sustain=170, release=4, vibrato=5, echo=90)
bell    = Inst('SINE', 3000, decay=5, sustain=40, release=5, echo=170)
padi    = Inst('SAW', 950, attack=50, decay=8, sustain=230, release=7, detune=3, echo=80)
stab    = Inst('SAW', 1300, decay=3, sustain=30, release=3, detune=2, echo=60)
arpi    = Inst('PULSE12', 1300, decay=3, sustain=60, release=3, echo=110)
riff    = Inst('SAW', 1500, attack=1, decay=4, sustain=150, release=3, detune=4)
bassi   = Inst('TRI', 3700, decay=5, sustain=150, release=3)
bassp   = Inst('PULSE25', 1500, decay=4, sustain=120, release=2)
kick    = Inst('KICK', 5400, decay=3, sustain=0, release=3)
noise   = Inst('NOISE', 1100, decay=2, sustain=0, release=2)

# stars
C1 = ['Am', 'F', 'C', 'G']
stars = Tune('fx_stars', BPM)
stars.chan(padi, pad(C1, 32, 0))
stars.chan(padi, pad(['Am9', 'Fmaj7', 'Cmaj7', 'Gsus4'], 32, 3))
stars.chan(bell, arp(['Am9', 'Fmaj7', 'Cmaj7', 'G6'], 32, [0, 2, 4, 1, 3, 2], octave=5, note_steps=2))
stars.chan(bassi, bass(C1, 32, [(0, 12), (7, 4), (12, 16)]))
stars.chan(noise, drums('..h...h...h...h.', 8, 'h'))

# plasma
C2 = ['Am7', 'D7', 'Am7', 'D7', 'Fmaj7', 'Em7', 'Dm7', 'E7']
plasma = Tune('fx_plasma', BPM)
plasma.chan(bassp, bass(C2, 16, [(0, 3), (0, 1), ('r', 2), (12, 2), ('r', 1), (10, 1), (7, 2), (12, 2), (10, 2)]))
plasma.chan(stab, sum(([(n, 1), (0, 3), (n, 1), (0, 5), (n, 1), (0, 1), (n, 2), (0, 2)]
                       for n in [chord(c, 4)[1] for c in C2]), []))
plasma.chan(stab, sum(([(n, 1), (0, 3), (n, 1), (0, 5), (n, 1), (0, 1), (n, 2), (0, 2)]
                       for n in [chord(c, 4)[2] for c in C2]), []))
plasma.chan(lead, melody('''
    -:4 E5:2 G5:2 A5:4 G5:2 E5:2
    F#5:3 E5:1 D5:4 -:8
    -:4 E5:2 G5:2 A5:2 C6:2 B5:2 A5:2
    G5:2 F#5:2 E5:2 D5:2 E5:8
    A5:2 C6:2 A5:2 G5:2 F5:4 E5:4
    G5:4 E5:2 D5:2 E5:8
    F5:2 A5:2 C6:4 B5:2 A5:2 F5:4
    G#5:4 B5:4 E6:8'''))
plasma.chan(kick, drums('k.....k...k.....', 8, 'k'))
plasma.chan(noise, drums('hhhhshhhhhhhshhh', 8, 'hs'))

# torus
C3 = ['Am', 'F', 'G', 'E']
torus = Tune('fx_torus', BPM)
torus.chan(arpi, arp(['Am', 'F', 'G', 'E7'], 16, [0, 1, 2, 3, 2, 1, 0, 2], octave=4) * 2)
torus.chan(bassp, bass(C3, 16, [(0, 1), (0, 1), (12, 1), (0, 1)] * 4) * 2)
torus.chan(riff, melody('''
    A4:2 -:1 A4:1 C5:2 -:1 A4:1 E5:4 D5:2 C5:2
    F4:2 -:1 F4:1 A4:2 -:1 F4:1 C5:4 B4:2 A4:2
    G4:2 -:1 G4:1 B4:2 -:1 G4:1 D5:4 C5:2 B4:2
    E4:2 -:1 E4:1 G#4:2 -:1 E4:1 B4:4 D5:4''') * 2)
torus.chan(padi, pad(C3, 16, 1) * 2)
torus.chan(kick, drums('k...k...k...k...', 8, 'k'))
torus.chan(noise, drums('..h...h.s.h...hh', 8, 'hs'))

# fire
C4 = ['Am', 'G', 'F', 'E', 'Am', 'G', 'F', 'E7']
fire = Tune('fx_fire', BPM)
fire.chan(lead50, melody('''
    A5:6 B5:2 C6:4 B5:2 A5:2
    G5:6 A5:2 B5:4 D6:4
    C6:4 A5:4 F5:4 A5:4
    G#5:8 B5:4 E6:4
    E6:4 D6:2 C6:2 B5:4 A5:4
    D6:4 C6:2 B5:2 G5:8
    F5:2 A5:2 C6:4 D6:4 C6:4
    B5:4 G#5:4 E5:8'''))
fire.chan(riff, sum(([(r, 2), (r + 7, 2), (r + 12, 2), (r + 7, 2)] * 2 for r in [chord(c, 2)[0] for c in C4]), []))
fire.chan(padi, pad(C4, 16, 2))
fire.chan(bassi, bass(C4, 16, [(0, 2), (0, 2), (0, 4), (0, 2), (0, 2), (12, 4)]))
fire.chan(kick, drums('k.k.....k.k.k...', 8, 'k'))
fire.chan(noise, drums('....s.......s..s', 8, 's'))

# scroll
C5 = ['C', 'G', 'Am', 'F', 'C', 'G', 'F', 'G']
scroll = Tune('fx_scroll', BPM)
scroll.chan(lead, melody('''
    E5:2 G5:2 C6:4 B5:2 G5:2 E5:4
    D5:2 G5:2 B5:4 A5:2 G5:2 D5:4
    C5:2 E5:2 A5:4 G5:2 E5:2 C5:4
    F5:4 A5:4 C6:4 A5:4
    E5:2 G5:2 C6:4 D6:2 E6:2 C6:4
    B5:2 D6:2 G6:4 F6:2 E6:2 D6:4
    C6:2 A5:2 F5:4 A5:2 C6:2 F6:4
    E6:4 D6:4 B5:4 G5:4'''))
scroll.chan(arpi, arp(C5, 16, [0, 1, 2, 1], octave=4, note_steps=2))
scroll.chan(bassi, bass(C5, 16, [(0, 2), (12, 2)] * 4))
scroll.chan(padi, pad(C5, 16, 2))
scroll.chan(kick, drums('k.......k.......', 8, 'k'))
scroll.chan(noise, drums('h.h.s.h.h.h.s.hh', 8, 'hs'))

TUNES = [stars, plasma, torus, fire, scroll]
