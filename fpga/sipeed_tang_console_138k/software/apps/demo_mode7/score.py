# demo_mode7: racing music in D, a section per scene (lib/demo_kit/tools/tunes.py)
#   flight    an up-tempo racing theme: driving octave bass, a soaring lead
#   rotozoom  dizzy: arpeggios turning in contrary motion over a pedal bass

BPM = 140

lead   = Inst('PULSE25', 2300, attack=1, decay=5, sustain=170, release=4, vibrato=4, echo=90)
lead2  = Inst('PULSE50', 1500, attack=1, decay=5, sustain=140, release=4, vibrato=3, echo=120)
padi   = Inst('SAW', 950, attack=40, decay=8, sustain=230, release=7, detune=3, echo=70)
arpi   = Inst('PULSE12', 1300, decay=3, sustain=60, release=3, echo=120)
arps   = Inst('TRI', 1700, decay=3, sustain=50, release=3, echo=150)
bassp  = Inst('PULSE25', 1500, decay=4, sustain=120, release=2)
bassi  = Inst('TRI', 3700, decay=5, sustain=160, release=3)
kick   = Inst('KICK', 5400, decay=3, sustain=0, release=3)
noise  = Inst('NOISE', 1100, decay=2, sustain=0, release=2)

C1 = ['D', 'Bm', 'G', 'A', 'D', 'Bm', 'Em', 'A']
flight = Tune('m7_flight', BPM)
flight.chan(lead, melody('''
    F#5:2 A5:2 D6:4 C#6:2 A5:2 F#5:4
    F#5:2 B5:2 D6:4 F#6:4 E6:4
    D6:2 B5:2 G5:4 B5:2 D6:2 G6:4
    E6:4 C#6:4 A5:4 E6:4
    F#6:2 E6:2 D6:4 A5:2 D6:2 F#6:4
    G6:2 F#6:2 D6:4 B5:4 D6:4
    E6:2 D6:2 B5:4 G5:2 B5:2 E6:4
    A6:4 G6:2 F#6:2 E6:8'''))
flight.chan(bassp, bass(C1, 16, [(0, 1), (12, 1)] * 8))
flight.chan(arpi, arp(C1, 16, [0, 1, 2, 1], octave=4))
flight.chan(padi, pad(C1, 16, 1))
flight.chan(kick, drums('k...k...k...k...', 8, 'k'))
flight.chan(noise, drums('..h.s.h...h.s.hh', 8, 'hs'))

C2 = ['Dmaj7', 'Gmaj7', 'Dmaj7', 'Gmaj7', 'Bm7', 'Em7', 'Gmaj7', 'A7']
rotozoom = Tune('m7_rotozoom', BPM)
rotozoom.chan(arpi, arp(C2, 16, [0, 1, 2, 3, 4, 3, 2, 1], octave=4))
rotozoom.chan(arps, arp(C2, 16, [4, 3, 2, 1, 0, 1, 2, 3], octave=5))
rotozoom.chan(lead2, melody('''
    A5:8 F#5:8
    B5:8 D6:8
    C#6:8 A5:8
    F#6:12 E6:4
    D6:8 B5:8
    G5:8 E5:8
    F#5:8 B5:8
    A5:8 C#6:4 E6:4'''))
rotozoom.chan(bassi, bass(['D', 'D', 'D', 'D', 'B', 'E', 'G', 'A'], 16, [(0, 6), (0, 2), (12, 4), (7, 4)]))
rotozoom.chan(kick, drums('k.......k..k....', 8, 'k'))
rotozoom.chan(noise, drums('h.h.h.h.h.h.h.hh', 8, 'h'))

TUNES = [flight, rotozoom]
