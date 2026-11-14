#!/usr/bin/env python3
#----------------------------------------------------------------------------
#          _    _           Family:    aRVern System IPs
#         / \__/ \          Script:    tunes.py (Tang Console 138K demo kit)
#        /   /\   \         --------------------------------------------
#    ===/   /=========      Copyright: (c) 2026, aRVern-dev
#      /   / RV \   \       Contact:   arvernsilicon@gmail.com
#     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
#
# SPDX-License-Identifier: BSD-3-Clause
# Full license text is available in the LICENSE file at the repository root.
#----------------------------------------------------------------------------
# Writes a demo's tunes for the demo kit's synthesizer (music.h) as C tables,
# from a score: a Python file that builds Tune objects with the helpers here
# (chords, arpeggios, bass lines, drum patterns, melodies in note names).
#
#   tools/tunes.py score.py music_data.h
#
# The score defines TUNES, a list of Tune; the header gets one mus_tune_t
# per tune (tune_<its name>) and an array of them in that order, music_tunes.
#----------------------------------------------------------------------------
import re, sys

FS, TICK = 22040, 64
NOTE = {'C': 0, 'D': 2, 'E': 4, 'F': 5, 'G': 7, 'A': 9, 'B': 11}
QUAL = {'': (0, 4, 7), 'm': (0, 3, 7), '7': (0, 4, 7, 10), 'm7': (0, 3, 7, 10), 'maj7': (0, 4, 7, 11),
        'sus2': (0, 2, 7), 'sus4': (0, 5, 7), 'add9': (0, 4, 7, 14), 'madd9': (0, 3, 7, 14), 'dim': (0, 3, 6),
        'm9': (0, 3, 7, 10, 14), '6': (0, 4, 7, 9), 'm6': (0, 3, 7, 9), '5': (0, 7, 12)}
WAVES = ('PULSE12', 'PULSE25', 'PULSE50', 'TRI', 'SAW', 'SINE', 'NOISE', 'KICK')


def midi(name):
    """'C4' -> 60, 'F#3' -> 54, 'Bb2' -> 46"""
    m = re.fullmatch(r'([A-G])([#b]?)(-?\d)', name)
    if not m:
        raise ValueError('note %r' % name)
    n = NOTE[m.group(1)] + {'#': 1, 'b': -1, '': 0}[m.group(2)]
    return 12 * (int(m.group(3)) + 1) + n


def chord(name, octave=3):
    """'Em7' -> the MIDI notes of the chord from its root in that octave"""
    m = re.fullmatch(r'([A-G][#b]?)(.*)', name)
    if not m or m.group(2) not in QUAL:
        raise ValueError('chord %r' % name)
    root = midi(m.group(1) + str(octave))
    return [root + i for i in QUAL[m.group(2)]]


class Inst:
    def __init__(self, wave, level, attack=0, decay=4, sustain=160, release=3, vibrato=0, detune=0, echo=0):
        assert wave in WAVES
        self.f = (wave, attack, decay, sustain, release, vibrato, detune, echo, level)


class Tune:
    def __init__(self, name, bpm, steps_per_beat=4):
        self.name, self.chans = name, []
        self.step_ticks = max(1, round(60.0 / bpm / steps_per_beat * FS / TICK))

    def chan(self, inst, events):
        """events: (note, steps) pairs, note 0 a rest"""
        self.chans.append((inst, [(n, l) for n, l in events if l > 0]))
        return self

    def steps(self):
        return max(sum(l for _, l in ev) for _, ev in self.chans)


# Event builders: each returns a list of (note, steps) ---------------------

def melody(text, transpose=0):
    """'E5:4 G5:2 -:2 B5:8': note:steps, '-' a rest"""
    out = []
    for tok in text.split():
        n, l = tok.split(':')
        out.append((0 if n == '-' else midi(n) + transpose, int(l)))
    return out


def arp(chords, steps_per_chord, pattern, octave=4, note_steps=1, up=0):
    """each chord broken along pattern (indexes into the chord's notes, an
    index past its size wraps an octave up) for steps_per_chord steps"""
    out = []
    for c in chords:
        notes = chord(c, octave)
        k = 0
        for _ in range(0, steps_per_chord, note_steps):
            i = pattern[k % len(pattern)]
            out.append((notes[i % len(notes)] + 12 * (i // len(notes)) + up, note_steps))
            k += 1
    return out


def bass(chords, steps_per_chord, rhythm, octave=2):
    """rhythm: per chord, (offset from the root in semitones or 'r' a rest, steps)"""
    out = []
    for c in chords:
        root = chord(c, octave)[0]
        for o, l in rhythm:
            out.append((0 if o == 'r' else root + o, l))
    return out


def bass_steps(chords, rhythm):
    return sum(l for _, l in rhythm) * len(chords)


def pad(chords, steps_per_chord, voice, octave=3):
    """one voice of the chords held: voice is the index of the chord note"""
    out = []
    for c in chords:
        notes = chord(c, octave)
        out.append((notes[voice % len(notes)] + 12 * (voice // len(notes)), steps_per_chord))
    return out


DRUM = {'k': 36, 's': 74, 'h': 110, 'o': 100}   # kick (W_KICK), snare, hat, open hat (W_NOISE)


def drums(pattern, bars=1, kit='ksho'):
    """'k...s...k.k.s...': one step a character, '.' nothing; the letters
    in kit play on this channel (one channel per instrument)"""
    out = []
    p = pattern.replace(' ', '') * bars
    i = 0
    while i < len(p):
        j = i + 1
        while j < len(p) and p[j] == '.':
            j += 1
        out.append((DRUM[p[i]] if p[i] in kit else 0, j - i))
        i = j
    return out


def rest(steps):
    return [(0, steps)]


def repeat(events, n):
    return events * n


# Output -------------------------------------------------------------------

def write(tunes, path, guard):
    o = ['/* Generated by lib/demo_kit/tools/tunes.py: do not edit. */',
         '#ifndef %s' % guard, '#define %s' % guard, '', '#include "music.h"', '']
    for t in tunes:
        t.name = 'tune_' + t.name if not t.name.startswith('tune_') else t.name
        assert len(t.chans) <= 6, t.name
        assert t.steps() % 16 == 0, '%s: %d steps, not whole bars' % (t.name, t.steps())
        o.append('static const mus_inst_t %s_inst[] = {' % t.name)
        for inst, _ in t.chans:
            f = inst.f
            o.append('    { W_%s, %d, %d, %d, %d, %d, %d, %d, %d },' % f)
        o.append('};')
        for c, (_, ev) in enumerate(t.chans):
            o.append('static const mus_ev_t %s_c%d[] = {' % (t.name, c))
            line = '   '
            for n, l in ev:
                while l > 255:
                    line += ' {%d,255},' % n
                    n, l = 0, l - 255
                line += ' {%d,%d},' % (n, l)
                if len(line) > 100:
                    o.append(line)
                    line = '   '
            o.append(line + ' {0,0}')
            o.append('};')
        chs = ', '.join('%s_c%d' % (t.name, c) for c in range(len(t.chans)))
        o.append('static const mus_tune_t %s = { %d, %d, %s_inst, { %s } };' %
                 (t.name, t.step_ticks, len(t.chans), t.name, chs))
        o.append('')
    o.append('static const mus_tune_t *const music_tunes[] = { %s };' % ', '.join('&' + t.name for t in tunes))
    o += ['', '#endif', '']
    open(path, 'w').write('\n'.join(o))


def main():
    score, out = sys.argv[1], sys.argv[2]
    env = dict(globals())
    exec(open(score).read(), env)
    write(env['TUNES'], out, 'MUSIC_DATA_H')
    for t in env['TUNES']:
        lens = [sum(l for _, l in ev) for _, ev in t.chans]
        odd = '' if len(set(lens)) == 1 else '   channel lengths differ: %s' % lens
        print('%-18s %4d steps, %5.1f s%s' % (t.name, t.steps(), t.steps() * t.step_ticks * TICK / FS, odd))


if __name__ == '__main__':
    main()
