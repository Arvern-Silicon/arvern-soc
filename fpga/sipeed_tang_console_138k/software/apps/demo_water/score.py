# demo_water: calm water music in F, a section per scene (lib/demo_kit/tools/tunes.py)
#   rain     drops: plucked sine notes from the pentatonic scale, in the echo
#   sources  two arpeggios of 3 and 4 notes against each other: they drift in
#            and out of phase like the two sources' fringes
#   wake     a flowing bass, a pad and a gentle melody following the source

BPM = 80

drop   = Inst('SINE', 2600, decay=4, sustain=20, release=5, echo=200)
drop2  = Inst('TRI', 1900, decay=4, sustain=20, release=5, echo=180)
lead   = Inst('SINE', 3200, attack=6, decay=6, sustain=200, release=6, vibrato=3, echo=150)
padi   = Inst('SAW', 850, attack=80, decay=8, sustain=230, release=8, detune=3, echo=90)
bassi  = Inst('TRI', 3400, attack=2, decay=6, sustain=170, release=5)
noise  = Inst('NOISE', 450, decay=2, sustain=0, release=2)

C = ['Fmaj7', 'Dm7', 'Bbmaj7', 'C6']

rain = Tune('water_rain', BPM)
rain.chan(drop, melody('''
    C6:3 -:2 A5:3 F5:2 -:3 G5:3
    -:2 D6:3 -:3 C6:2 A5:3 -:3
    F5:2 -:3 G5:3 -:3 A5:2 C6:3
    -:3 D6:2 F6:3 -:3 C6:2 -:3
    A5:3 -:2 F5:3 G5:2 -:3 C6:3
    -:2 A5:3 D6:3 -:2 C6:3 -:3
    G5:2 A5:3 -:3 F5:2 -:3 D5:3
    -:3 C5:2 F5:3 -:5 A5:3'''))
rain.chan(drop2, rest(5) + melody('''
    F6:3 -:6 C6:3 -:5 A5:3 -:7 G5:3 -:4
    D6:3 -:6 F6:3 -:5 C6:3 -:6 A5:3 -:6
    G5:3 -:5 D6:3 -:6 C6:3 -:5 F6:3 -:6
    A5:3 -:6 C6:3 -:5 G5:3'''))
rain.chan(padi, pad(C, 32, 0))
rain.chan(padi, pad(C, 32, 2))
rain.chan(noise, drums('h.....h...h.....h...h...h.....h.', 4, 'h'))

sources = Tune('water_sources', BPM)
sources.chan(drop, sum(([(n, 2)] for n in [65, 69, 72] * 22), [])[:64] * 2)        # F A C: three notes
sources.chan(drop2, sum(([(n, 2)] for n in [77, 76, 72, 74] * 16), [])[:64] * 2)   # four notes
sources.chan(padi, pad(C, 32, 0) * 2)
sources.chan(bassi, bass(C, 32, [(0, 16), (7, 16)]) * 2)

wake = Tune('water_wake', BPM)
wake.chan(lead, melody('''
    A5:6 G5:2 F5:4 C5:4
    D5:6 E5:2 F5:4 A5:4
    Bb5:6 A5:2 F5:4 D5:4
    E5:8 G5:8
    A5:4 C6:4 Bb5:4 A5:4
    F5:6 G5:2 A5:8
    D6:4 C6:4 Bb5:4 F5:4
    G5:12 -:4'''))
wake.chan(bassi, bass(['F', 'D', 'Bb', 'C', 'F', 'D', 'Bb', 'C'], 16, [(0, 4), (7, 4), (12, 4), (7, 4)]))
wake.chan(padi, pad(['Fmaj7', 'Dm7', 'Bbmaj7', 'C6'] * 2, 16, 1))
wake.chan(drop2, arp(['Fmaj7', 'Dm7', 'Bbmaj7', 'C6'] * 2, 16, [0, 2, 3, 1], octave=5, note_steps=4))
wake.chan(noise, drums('....h.......h...', 8, 'h'))

TUNES = [rain, sources, wake]
