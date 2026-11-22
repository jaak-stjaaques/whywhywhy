# WHYWHYWHY operator manual

WHYWHYWHY has no words on its screen. Four coloured buoys float on a sheet of paper, and what you hear moves
them. This page is the only place where things have names.

<p align="center"><img src="screenshots/home.png" width="320" alt="HOME: four coloured buoys on slow lines of water, four dials underneath"></p>

## The screen

- **Top:** the sign of the page you are on (left), the **four buoys** (the four parts; the ringed one is selected;
  the small sign inside is its algorithm), then **record** (red when recording), **play** (a triangle when stopped,
  a dot beating when playing), the **octave** (a dot on a little ladder), the **battery** (a spot, as full as the
  battery).
- **Middle:** the page's picture. It moves with the sound.
- **Bottom:** four **dials**, one under each knob. A dial lights up while you turn its knob. Its dots go round as
  the value goes up, and the spot in the middle grows. A dial with a few dots spread all round is a choice: the big
  dot is the one chosen.

## The sound path

`KEYS / ARP  ->  SEQ (4 tracks)  ->  SCALE  ->  ALGORITHM (one per part)  ->  FX (4 in parallel)`

The keys, the ARP and the SEQ only know scale **degrees**. The scale is applied last, so a new scale, mode or key
changes everything at once, the recordings too. No wrong notes.

## The parts

Four parts, four colours: **vermilion, saffron, cobalt, jade**. Each part has its own algorithm, sound, envelope,
LFO, level, pan and track. **SELECT** chooses the part. The keys, the ARP and the EDIT / ENV / LFO pages always
work on the selected part.

## Every control

| Control | What it does |
| --- | --- |
| **Keys** | Play the selected part, snapped to the scale. With the ARP on, they feed the ARP. On the SCALE page they set the key of everything (C4 = no change, the octave buttons add octaves). Keys in the scale glow faintly. |
| **OCT− / OCT+** | The keys' and the ARP's octave, −3 .. +3. |
| **SELECT** | The selected part (1 .. 4). |
| **ALGORITHM** | The selected part's algorithm: GLASS, BIRCH, DOTS, PULSE, FOLD, AIR. Shown as a row of signs for a moment. |
| **PRESETS** | The eight scenes. A saved scene is a little spot painting; an empty one is a dotted circle. **Landing on an empty one makes a new random scene** (four sounds, four melodies, a scale, effects): unexplored water. |
| **SAVE** | Saves everything (sounds, tracks, scale, FX, ARP, tempo) into the current scene, or into the first empty one. **Hold** SAVE: empties the current scene. |
| **PLAY** | Start / stop. |
| **REC** | Record on the selected part's track (it starts the clock if needed). Press again to stop. **Hold** REC: wipe the track. |
| **HOME, EDIT, ENV, LFO, ARP, SEQ, SEL, FX, GLO** | Open their page. **Hold** any of them (except HOME and GLO): throw the dice for that page. |
| **ARP** (on the ARP page) | Press again: the ARP on / off. |
| **HOME held + knobs 1–4** | Pan of parts 1–4. |
| **MIDI** | In: notes play the selected part (snapped), clock and start / stop follow. Out: parts 1–4 on channels 1–4. |

## The pages

### HOME (HOME) — four buoys

Each buoy is a part: its **size** is its level, its **height** follows the last note it played, its **ripple**
spreads with every note, drawn the way its algorithm sounds (thin rings for glass, wood rings for birch, dots for
dots, ticks for pulse, a thick band for fold, a dusty ring for air).

Knobs 1–4: **level** of parts 1–4. Hold HOME: **pan**.

### EDIT (EDIT) — the algorithm, as an object

| Algorithm | Knob 1 | Knob 2 | Knob 3 | Knob 4 | The picture |
| --- | --- | --- | --- | --- | --- |
| **GLASS** — FM, inharmonic: glass, ice, bells | RATIO | BRIGHTNESS | FROST (a shimmering octave) | CRACK (how fast the strike dims) | a cut stone: more facets with the ratio, sharper with the brightness, a trembling ghost for frost, shards fly on each strike |
| **BIRCH** — four struck partials: wood, bars, bowls, skins | MATERIAL (string → bell → bar → marimba → drum) | MALLET (soft → hard) | DECAY | RING (the high partials last) | wood rings: one ring per partial, spaced as the partials are; the strike spreads through the grain |
| **DOTS** — a cloud of sine grains | DENSITY | SCATTER (octaves, fifths) | GRAIN length | SHIMMER (detune) | a dot for every grain, higher for the high ones, bigger for the long ones |
| **PULSE** — a sine, a click, a beat | CLICK | BEAT (a second sine a few Hz away) | DATA (fewer samples, fewer bits) | GATE (off, 1/4 .. 1/64, at the tempo) | a grating; a second red one slides over it as fast as the beat (moiré); DATA makes it coarse; it dims when the gate is closed; clicks flash |
| **FOLD** — a folded sine | FOLD | BIAS | TONE | WOBBLE (hand-made pitch drift) | a strip of tape folded back and forth: more folds, lifted by the bias, thinner when brighter, shaking with the wobble |
| **AIR** — breath through a resonance | BREATH | FOCUS | GUST | HARMONIC (overblow 1 .. 4) | a flowing contour, nested: the contours draw together with focus, flow faster with gusts, more lobes with the harmonic, specks of breath |

Hold EDIT: random sound knobs.

### ENV (ENV) — blooms

Knobs: **attack, decay, sustain, release.** Every note of the selected part opens a bloom, holds it, and lets it
close: the bloom is the note's loudness, right now. When nothing plays, a faint ghost bloom repeats the envelope so
you can see what the knobs do. Hold ENV: random envelope.

### LFO (LFO) — a field that moves

Knobs: **rate, depth, shape** (sine, triangle, square, saw, random steps, random drift), **target** (pitch,
timbre, volume, pan). The field of dots does what the sound does: it rises and falls (pitch), glows light and dark
(timbre: each algorithm's main colour knob), swells (volume) or sways left and right (pan). The two dials on the
right show the shape and the target as little signs. Hold LFO: random LFO.

### ARP (ARP) — an orbit

Hold keys: they are kept, even when you let go; let go of all and play again for a new chord. The kept notes sit
around a circle, one ring per octave of range; the big spot jumps between them as they play.

Knobs: **pattern** (up, down, up-down, inwards, outwards, random, as played, star, growing), **rate** (1/2 .. 1/32
triplets), **range** (1 .. 4 octaves), **wander** (steps stray to a neighbour, an octave, or a rest). The ARP plays
the selected part. Press ARP on the ARP page: on / off. Hold ARP: random ARP.

### SEQ (SEQ) — four rings

One ring per track, track 1 inside. Each ring has as many dots as its track has steps; the notes are coloured, the
step playing is circled. Knobs 1–4: the **length** of tracks 1–4, 1 .. 32 sixteenths, or **0: off**. Four lengths,
four loops drifting in and out of phase. The selected track's ring is underlined; red while recording.

REC records what the selected part plays (keys, ARP or MIDI) into its track, on the step it falls on, over what is
there. Hold SEQ: random lengths.

### SCALE (SEL) — the colour circle

The twelve keys as a circle of colours, the root always at the top. The scale's notes are big; the others are pin
pricks; the root is circled; a line joins the scale's notes in order: the scale's shape. Press a key: the key of
everything changes and the circle turns.

Knobs:

1. **scale** — 55 scales, common and exotic: major and minor, the pentatonics, the blues, the modes, bebop,
   altered, whole tone, the diminished, augmented, Scriabin's Prometheus, hirajoshi, in sen, iwato, kumoi, yo,
   Ryukyu, Chinese, pelog, slendro, Egyptian, maqam rast and bayati, hijaz, the double harmonic, Hungarian, Ukrainian
   dorian, Persian, the Neapolitans, Spanish, ragas todi, marwa, purvi and malkauns, and four little ones (sus,
   fourths, a triad, root and fifth). The dial shows the scale's shape.
2. **mode** — the same notes, started on another degree.
3. **gravity** — all the notes; every other one; root and fifth; the root only. The notes that fall become rings.
4. **true tuning** — from a piano's equal steps to the scale's own tuning: pure thirds and fifths, the bluesy
   seventh, a gamelan's pelog and slendro, the quarter tones of the maqams. The dots move by their cents.

Hold SEL: a random scale.

### FX (FX) — four streams, side by side

The sound comes in on the left, splits into four streams that never meet, and joins again on the right: the effects
are **in parallel**, each hears the dry sound only. Knobs: **echo** (a dotted eighth at the tempo; more echo, more
repeats), **hall**, **chorus**, **dust** (a coarse, crackly copy of the sound). Hold FX: random effects.

### GLO (GLO) — the sun

Knobs: **tempo** (40 .. 240; the sun beats), **swing** (the off-beat marks move), **MIDI channel** (omni = all
sixteen dots grey, or one dot black), **tuning** (±50 cents, the red mark).

## Nothing to learn by heart

Turn a knob and watch what moves. Hold a page's button and see what the dice bring. Turn PRESETS to an empty scene
and press PLAY. When you like what you hear, SAVE.
