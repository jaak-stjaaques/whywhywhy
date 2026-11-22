/* SPDX-License-Identifier: GPL-3.0-only */
/* WHYWHYWHY's scale stage: the very last step before the sound. The ARP, the SEQ and the keys only deal in scale
 * DEGREES (0 = the root, 1 = the next note of the scale, ...). Here a degree becomes a pitch:
 *
 *   the SCALE (one of 55, common and exotic)
 *   the MODE (the same notes, started on another degree)
 *   the GRAVITY (the far degrees fall onto the near ones: all the notes; every other one; root and fifth; root)
 *   the TRUE TUNING (0: a piano's equal steps; up: the scale's own tuning, pure thirds, a gamelan's pelog or
 *     slendro, the quarter tones of a maqam)
 *   the KEY (the keyboard on the SCALE page: the whole synth moves, recordings too)
 *
 * No names on the screen: the comments keep them. */
enum { T_EQUAL, T_JUST, T_SEPTIMAL, T_PELOG, T_SLENDRO, T_RAST, T_BAYATI, TUNINGS };

/* cents away from the piano's note, for each semitone above the root */
static const int8_t tunings[TUNINGS][12] = {
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0, 12, 4, 16, -14, -2, -10, 2, 14, -16, 18, -12},     /* 5-limit just: 16/15 9/8 6/5 5/4 4/3 45/32 3/2 ... */
    {0, 5, 4, -33, -14, -2, -17, 2, 14, -16, -31, -12},    /* septimal: 7/6, 7/5, 7/4: the blues' own */
    {0, 20, 0, -42, 0, 39, 0, -25, -15, 43, 0, 0},         /* Javanese pelog, near enough */
    {0, 0, 40, 0, 0, -20, 0, 20, 0, 0, -40, 0},            /* slendro: five steps of ~240 cents */
    {0, 0, 0, 0, -50, 0, 0, 0, 0, 0, 0, -50},              /* maqam rast: the third and the seventh half flat */
    {0, 0, -50, 0, 0, 0, 0, 0, 0, 0, 0, 0},                /* maqam bayati: the second half flat */
};

static const struct { uint16_t mask; uint8_t tuning; } scales[] = {
    {0xfff, T_EQUAL},                           /* chromatic */
    {0xab5, T_JUST},                            /* major */
    {0x5ad, T_JUST},                            /* natural minor */
    {0x295, T_JUST},                            /* major pentatonic */
    {0x4a9, T_JUST},                            /* minor pentatonic */
    {0x4e9, T_SEPTIMAL},                        /* blues */
    {0x29d, T_SEPTIMAL},                        /* major blues */
    {0x9ad, T_JUST},                            /* harmonic minor */
    {0xaad, T_JUST},                            /* melodic minor */
    {0x9b5, T_JUST},                            /* harmonic major */
    {0x6ad, T_JUST},                            /* dorian */
    {0x5ab, T_JUST},                            /* phrygian */
    {0xad5, T_JUST},                            /* lydian */
    {0x6b5, T_SEPTIMAL},                        /* mixolydian (a harmonic seventh) */
    {0x56b, T_JUST},                            /* locrian */
    {0x6d5, T_SEPTIMAL},                        /* lydian dominant, the overtone scale */
    {0xeb5, T_JUST},                            /* bebop dominant */
    {0x575, T_EQUAL},                           /* major locrian */
    {0x55b, T_EQUAL},                           /* altered */
    {0x555, T_EQUAL},                           /* whole tone */
    {0x6db, T_EQUAL},                           /* diminished half-whole */
    {0xb6d, T_EQUAL},                           /* diminished whole-half */
    {0x999, T_EQUAL},                           /* augmented */
    {0x333, T_EQUAL},                           /* six-tone symmetrical */
    {0x4d3, T_EQUAL},                           /* tritone */
    {0xd53, T_EQUAL},                           /* enigmatic */
    {0x655, T_SEPTIMAL},                        /* prometheus (Scriabin) */
    {0x18d, T_JUST},                            /* hirajoshi */
    {0x4a3, T_JUST},                            /* in sen */
    {0x463, T_JUST},                            /* iwato */
    {0x28d, T_JUST},                            /* kumoi */
    {0x2a5, T_JUST},                            /* yo */
    {0x8b1, T_JUST},                            /* ryukyu (Okinawa) */
    {0x8d1, T_JUST},                            /* chinese */
    {0x18b, T_PELOG},                           /* pelog */
    {0x4a5, T_SLENDRO},                         /* slendro */
    {0x4a5, T_JUST},                            /* egyptian (slendro's notes, in pure fifths) */
    {0xab5, T_RAST},                            /* maqam rast */
    {0x5ad, T_BAYATI},                          /* maqam bayati */
    {0x5b3, T_JUST},                            /* hijaz */
    {0x9b3, T_JUST},                            /* double harmonic (raga bhairav) */
    {0x9cd, T_JUST},                            /* hungarian minor */
    {0x6d9, T_JUST},                            /* hungarian major */
    {0x6cd, T_JUST},                            /* ukrainian dorian */
    {0x973, T_JUST},                            /* persian */
    {0x9ab, T_JUST},                            /* neapolitan minor */
    {0xaab, T_JUST},                            /* neapolitan major */
    {0x57b, T_JUST},                            /* spanish eight-tone */
    {0x9cb, T_JUST},                            /* raga todi */
    {0xad3, T_JUST},                            /* raga marwa */
    {0x9d3, T_JUST},                            /* raga purvi */
    {0x529, T_JUST},                            /* raga malkauns */
    {0x085, T_JUST},                            /* sus: root, second, fifth */
    {0x421, T_JUST},                            /* quartal: stacked fourths */
    {0x091, T_JUST},                            /* a major triad */
    {0x081, T_JUST},                            /* root and fifth */
};
#define SCALE_COUNT (sizeof scales / sizeof scales[0])
#define SCALE_BASE 48                           /* degree 0, no transposition: C3 */

static struct {
    uint8_t scale, mode, gravity, truth;        /* the SCALE page's knobs: scale, mode, gravity 0..127, true tuning 0..127 */
    int8_t transpose;                           /* semitones: the SCALE page's keys */
    int8_t octave;                              /* OCT- / OCT+: the keys and the ARP */
    /* worked out by scale_update() */
    uint8_t size;                               /* notes in an octave (2 .. 12) */
    uint8_t offset[12];                         /* semitones of each degree above the root */
    int8_t cents[12];                           /* each degree's true tuning, at full TRUE TUNING */
    uint8_t keep[12];                           /* GRAVITY: the degree each degree falls onto */
} scale_now = {1, 0, 0, 0, 0, 0, 7, {0, 2, 4, 5, 7, 9, 11}, {0}, {0, 1, 2, 3, 4, 5, 6}};

static void scale_update(void)
{
    if (scale_now.scale >= SCALE_COUNT) scale_now.scale = 0;
    uint16_t mask = scales[scale_now.scale].mask;
    const int8_t *dev = tunings[scales[scale_now.scale].tuning];
    uint8_t base[12], n = 0;
    for (uint8_t i = 0; i < 12; i++)
        if (mask >> i & 1u) base[n++] = i;
    if (scale_now.mode >= n) scale_now.mode = (uint8_t)(n - 1u);
    /* the MODE: start on another degree, the root stays where it is */
    uint8_t r = base[scale_now.mode];
    for (uint8_t i = 0; i < n; i++) {
        uint8_t s = base[(i + scale_now.mode) % n];
        scale_now.offset[i] = (uint8_t)((s + 12u - r) % 12u);
        scale_now.cents[i] = (int8_t)(dev[s] - dev[r]);
    }
    scale_now.size = n;
    /* GRAVITY: the degrees that stay, the others fall onto the nearest one below that stays */
    uint8_t g = scale_now.gravity / 32u;
    uint8_t fifth = 0, best = 12;
    for (uint8_t i = 1; i < n; i++) {
        uint8_t d = scale_now.offset[i] > 7u ? scale_now.offset[i] - 7u : 7u - scale_now.offset[i];
        if (d < best) { best = d; fifth = i; }
    }
    for (uint8_t i = 0; i < n; i++) {
        uint8_t k = i;
        if (g == 1) k = (uint8_t)(i & ~1u);
        if (g == 2) k = i >= fifth ? fifth : 0;
        if (g == 3) k = 0;
        scale_now.keep[i] = k;
    }
}

static void scale_select(uint8_t scale)
{
    scale_now.scale = scale;
    scale_now.mode = 0;
    scale_update();
}

static uint8_t scale_root_pc(void)              /* the key the scale stands on, 0 = C .. 11 = B */
{
    return (uint8_t)((SCALE_BASE + scale_now.transpose + 120) % 12);
}

/* a degree (any integer) -> its degree in the octave and its octave */
static void degree_split(int32_t degree, int32_t *in_octave, int32_t *octave)
{
    int32_t n = scale_now.size, o = 0;
    while (degree < 0) { degree += n; o--; }
    o += degree / n;
    *in_octave = degree % n;
    *octave = o;
}

/* a degree -> a pitch, Q8 semitones (MIDI x 256), within 12 .. 120 */
static int32_t scale_pitch(int32_t degree)
{
    int32_t d, o;
    degree_split(degree, &d, &o);
    d = scale_now.keep[d];
    int32_t note = SCALE_BASE + scale_now.transpose + 12 * o + scale_now.offset[d];
    while (note < 12) note += 12;
    while (note > 120) note -= 12;
    return note * 256 + scale_now.cents[d] * scale_now.truth * 256 / (127 * 100);
}

/* a key (MIDI) -> the nearest degree of the scale (the lower one at equal distance), with the key's own
 * transposition taken out, so that it follows the key and the scale from then on */
static int32_t scale_degree_of(int32_t midi)
{
    int32_t rel = midi - (SCALE_BASE + scale_now.transpose);
    int32_t o = rel >= 0 ? rel / 12 : -((-rel + 11) / 12);
    int32_t s = rel - 12 * o, best = 0, dist = 99;
    for (int32_t i = 0; i < scale_now.size; i++) {
        int32_t d = s - scale_now.offset[i];
        int32_t ad = d < 0 ? -d : d;
        if (ad < dist) { dist = ad; best = i; }
    }
    /* (the octave's top: closer to the next root) */
    if (12 - s < dist) { best = 0; o++; }
    return o * scale_now.size + best;
}

static int scale_contains_pc(uint8_t pc)        /* pc 0..11 above the root */
{
    for (uint8_t i = 0; i < scale_now.size; i++)
        if (scale_now.offset[i] == pc) return 1;
    return 0;
}
