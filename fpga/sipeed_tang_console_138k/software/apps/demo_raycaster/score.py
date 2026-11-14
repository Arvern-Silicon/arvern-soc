# demo_raycaster: the maze, one tense piece in D minor (lib/demo_kit/tools/tunes.py)
#   a low bass pulse like footsteps, a dark pad, a sparse bell melody in the
#   echo, a heartbeat kick and a distant snare; the second half adds a
#   counter-melody

BPM = 92

bell   = Inst('SINE', 2700, decay=5, sustain=40, release=6, echo=180)
lead   = Inst('PULSE25', 1600, attack=4, decay=6, sustain=180, release=5, vibrato=5, echo=140)
padi   = Inst('SAW', 900, attack=70, decay=8, sustain=230, release=8, detune=3, echo=80)
bassi  = Inst('TRI', 3900, decay=4, sustain=120, release=3)
kick   = Inst('KICK', 4600, decay=3, sustain=0, release=3)
noise  = Inst('NOISE', 700, decay=3, sustain=0, release=3)

C = ['Dm', 'Dm', 'Bb', 'A', 'Dm', 'Gm', 'Bb', 'A7',
     'Dm', 'Dm', 'Bb', 'A', 'Gm', 'Bb', 'Gm', 'A7']

maze = Tune('ray_maze', BPM)
maze.chan(bassi, bass(C, 16, [(0, 3), (0, 1), ('r', 4), (0, 3), (0, 1), ('r', 4)]))
maze.chan(padi, pad(C, 16, 1, octave=3))
maze.chan(bell, melody('''
    D5:6 -:2 A5:4 -:4
    F5:6 E5:2 D5:4 -:4
    -:4 D5:2 F5:2 Bb5:6 A5:2
    A5:8 C#5:4 E5:4
    D5:4 F5:4 A5:4 D6:4
    -:4 Bb5:4 G5:4 D5:4
    F5:6 D5:2 Bb4:8
    A4:8 -:8
    -:16
    A5:4 G5:4 F5:4 E5:4
    D5:8 F5:4 Bb5:4
    A5:8 -:8
    G5:6 F5:2 D5:8
    F5:4 Bb5:4 D6:4 -:4
    C6:4 Bb5:4 G5:4 E5:4
    A5:8 C#6:8'''))
maze.chan(lead, rest(128) + melody('''
    -:8 D4:4 E4:4
    F4:8 E4:4 D4:4
    C#4:8 E4:8
    A3:12 -:4
    Bb3:8 D4:8
    G4:8 F4:4 D4:4
    E4:4 F4:4 G4:4 A4:4
    A4:12 -:4'''))
maze.chan(kick, drums('k..k............', 16, 'k'))
maze.chan(noise, drums('........s.......', 16, 's'))

TUNES = [maze]
