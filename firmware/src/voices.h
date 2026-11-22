/* SPDX-License-Identifier: GPL-3.0-only */
/* WHYWHYWHY's sound: six algorithms, eight voices shared by the four parts (the four coloured buoys).
 *
 *   GLASS  two-operator FM, inharmonic ratios: glass, ice, bells. The brightness falls after the strike.
 *   BIRCH  four sine partials, each with its own decay, struck by a mallet: wood, bars, bowls, skins.
 *   DOTS   a cloud of little sine grains around the note: octaves, fifths, a shimmer.
 *   PULSE  a pure sine, a click, a second sine that beats against it, data grit, a tempo gate.
 *   FOLD   a sine folded back on itself, a bias, a tone control, a hand-made wobble.
 *   AIR    breath through a resonant filter: wind, whistles, flutes, overblown partials.
 *
 * Every part has four sound knobs (EDIT), an envelope (ENV), an LFO (LFO), a level and a pan. Integer
 * arithmetic only (docs/RULES.md). The audio interrupt renders in sub-blocks of 32 frames; what changes slowly
 * (the LFO, the decays) is worked out once a sub-block. */
#include "tables.h"

#define VOICES 8u
#define SUB 32u
#define ENV_ONE (1 << 24)

enum { ALG_GLASS, ALG_BIRCH, ALG_DOTS, ALG_PULSE, ALG_FOLD, ALG_AIR, ALGS };
enum { LFO_SINE, LFO_TRIANGLE, LFO_SQUARE, LFO_SAW, LFO_HOLD, LFO_DRIFT, LFO_SHAPES };
enum { LT_PITCH, LT_TIMBRE, LT_VOLUME, LT_PAN, LFO_TARGETS };
enum { ENV_ATTACK, ENV_DECAY, ENV_SUSTAIN, ENV_RELEASE, ENV_OFF };
enum { K_RATE, K_DEPTH, K_SHAPE, K_TARGET };     /* the LFO's four knobs, in part_t.lfo */

/* what is saved of a part: knob values, 0..127 (choices: 0..n-1) */
typedef struct {
    uint8_t alg;
    uint8_t p[4];                               /* EDIT: the algorithm's four knobs */
    uint8_t env[4];                             /* ENV: attack, decay, sustain, release */
    uint8_t lfo[4];                             /* LFO: rate, depth, shape, target */
    uint8_t level;                              /* HOME: 0..127 */
    int8_t pan;                                 /* HOME held: -64..64 */
} part_t;

/* what the audio interrupt needs of a part, worked out by the main loop (part_update) */
typedef struct {
    volatile int32_t attack, decay, sustain, release;   /* Q24 a sample (attack: a step; decay, release: a pole) */
    volatile uint32_t lfo_inc;                  /* a sub-block */
    volatile int32_t lfo_depth, gain;           /* Q15 */
    volatile uint32_t gate_inc;                 /* PULSE: the tempo gate, a sample (0: no gate) */
    /* the interrupt's own */
    uint32_t lfo_phase, lfo_rnd;
    int32_t lfo_from, lfo_to;
    int32_t cur_gain, cur_left, cur_right;
    /* for the screen */
    volatile int32_t lfo_out;                   /* the LFO now, Q15 */
    volatile int32_t meter;                     /* the loudest sample of the last block, Q15 */
    volatile uint32_t grains;                   /* DOTS: grains born, a counter */
    volatile uint8_t grain_step;                /* DOTS: the last grain's interval (an index into dots_steps) */
    volatile uint8_t gate;                      /* PULSE: the gate is open */
    volatile uint32_t clicks;                   /* PULSE: clicks so far; GLASS, BIRCH: strikes */
} part_rt_t;

#define PARTS 4u
static part_t parts[PARTS];
static part_rt_t part_rt[PARTS];

typedef struct {
    volatile uint8_t active, released;
    uint8_t part, owner, note, alg, stage;
    int32_t pitch;                              /* Q8 semitones (MIDI note x 256), the scale's tuning included */
    uint32_t age;
    int32_t vel;                                /* Q15 */
    int32_t env;                                /* Q24 */
    uint32_t ph[5], inc[5];
    int32_t a[5], k[5];                         /* amplitudes (Q15 << 8) and their decays (Q16 a sub-block) */
    uint32_t gp[5], gs[5];                      /* DOTS: grain positions and steps */
    int32_t s0, s1, s2, x0, x1;                 /* filters and the like */
    uint32_t t, rnd;
    uint16_t ratio[4];                          /* Q8 */
    uint16_t hold, hold_n;
    int16_t held;
} voice_t;

static voice_t voices[VOICES];
static uint32_t voice_clock;
static volatile int32_t tuning_q8;               /* GLO: tuning, Q8 semitones */

/* ------------------------------------------------------------------------------------------------ helpers */
static inline int32_t sin_q15(uint32_t phase)
{
    uint32_t i = phase >> 22;
    int32_t a = sine1024[i], b = sine1024[i + 1u];
    return a + (((b - a) * (int32_t)((phase >> 6) & 0xFFFFu)) >> 16);
}

/* a pitch (Q8 semitones, MIDI) -> a phase step a sample */
static uint32_t pitch_inc(int32_t q8)
{
    if (q8 < 0) q8 = 0;
    if (q8 > 130 * 256) q8 = 130 * 256;
    uint32_t n = (uint32_t)q8 >> 8, f = (uint32_t)q8 & 255u;
    uint32_t a = inc_top[n % 12u] >> (10u - n / 12u);
    uint32_t m = n + 1u;
    uint32_t b = m >= 132u ? a : inc_top[m % 12u] >> (10u - m / 12u);
    return a + (uint32_t)(((uint64_t)(b - a) * f) >> 8);
}

static inline uint32_t vrand(uint32_t *s)
{
    *s = *s * 1664525u + 1013904223u;
    return *s;
}

static inline int32_t clamp16(int32_t x)
{
    return x < -32768 ? -32768 : x > 32767 ? 32767 : x;
}

/* the decay of a sub-block that falls by 60 dB in `ms`, Q16 */
static int32_t decay_k(uint32_t ms)
{
    if (ms < 2u) ms = 2u;
    int32_t k = 65536 - (int32_t)(145000000u / (ms * 441u));   /* 32 frames x 6.9 / (ms x 44.1), Q16 */
    return k < 1000 ? 1000 : k;
}

/* ------------------------------------------------------------------------------------- knobs -> numbers */
static uint32_t knob_sq(uint8_t k, uint32_t lo, uint32_t hi)     /* a square-law knob from lo to hi */
{
    return lo + (uint32_t)k * k * (hi - lo) / 16129u;
}

static int32_t env_step(uint32_t ms)            /* a linear attack of `ms`, Q24 a sample */
{
    return ms ? (int32_t)(167772160u / (ms * 441u)) + 1 : ENV_ONE / 16;
}

static int32_t env_pole(uint32_t ms)            /* a pole that falls by 60 dB in `ms`, Q24 */
{
    if (!ms) ms = 1;
    uint32_t k = 1157627904u / (ms * 441u);
    return (int32_t)(k > 8000000u ? 8000000u : k);
}

static uint32_t env_ms(const part_t *s, uint32_t stage)
{
    switch (stage) {
    case 0: return knob_sq(s->env[0], 0, 5000);
    case 1: return knob_sq(s->env[1], 8, 8000);
    case 3: return knob_sq(s->env[3], 8, 10000);
    default: return 0;
    }
}

static uint16_t level_gain(uint8_t level)       /* the level knob -> Q15, an audio taper */
{
    static const struct { uint8_t level; uint16_t gain; } points[] = {
        {0, 0}, {1, 130}, {4, 654}, {13, 2603}, {32, 7336}, {64, 13045}, {96, 20675}, {127, 32767}
    };
    for (uint32_t i = 1; i < sizeof points / sizeof points[0]; i++) {
        if (level > points[i].level) continue;
        uint32_t span = points[i].level - points[i - 1].level, step = level - points[i - 1].level;
        return (uint16_t)(points[i - 1].gain + ((points[i].gain - points[i - 1].gain) * step + span / 2u) / span);
    }
    return 32767;
}

/* the LFO's rate knob -> Hz x 100 (0.02 Hz .. 30 Hz, a semitone a step) */
static uint32_t lfo_centihertz(uint8_t rate)
{
    static const uint16_t ratio[12] = {256, 271, 287, 304, 323, 342, 362, 384, 406, 431, 456, 483};
    return (2u * ratio[rate % 12u] << (rate / 12u)) / 256u;
}

static uint32_t gate_bpm = 110;                 /* the tempo the PULSE gate follows (dinghy.c) */
static const uint8_t gate_ticks[] = {0, 96, 48, 32, 24, 16, 12, 6};   /* off, 1/4, 1/8, 1/8T, 1/16, 1/16T, 1/32, 1/64 */

static void part_update(uint32_t i)
{
    const part_t *s = &parts[i];
    part_rt_t *r = &part_rt[i];
    r->attack = env_step(env_ms(s, 0));
    r->decay = env_pole(env_ms(s, 1));
    r->sustain = (int32_t)((uint32_t)s->env[2] * s->env[2] * (uint32_t)(ENV_ONE / 16129));
    r->release = env_pole(env_ms(s, 3));
    /* centi-Hz -> a phase step a sub-block: cHz x 2^32 x 32 / (100 x 44100) */
    r->lfo_inc = lfo_centihertz(s->lfo[K_RATE]) * 31166u;   /* (2^37 / 4410000: no 64-bit division on the chip) */
    r->lfo_depth = (int32_t)((uint32_t)s->lfo[K_DEPTH] * s->lfo[K_DEPTH] * 32767u / 16129u);
    r->gain = level_gain(s->level);
    uint32_t g = s->alg == ALG_PULSE ? gate_ticks[s->p[3] * 8u / 128u] : 0;
    /* gates a second: bpm x 96 / 60 / g, x 2^32 / 44100 a sample: bpm x 155835 / g */
    r->gate_inc = g ? gate_bpm * 155835u / g : 0;
}

/* --------------------------------------------------------------------------------------- the algorithms */
static const uint16_t glass_ratio[] = {128, 256, 362, 512, 706, 768, 896, 1042, 1329, 1825};
#define GLASS_RATIOS (sizeof glass_ratio / sizeof glass_ratio[0])

/* BIRCH: four partials (x 256 of the note): the MATERIAL knob slides from one set to the next */
static const uint16_t birch_sets[5][4] = {
    {256, 512, 768, 1024},                      /* a string: harmonic */
    {128, 256, 307, 512},                       /* a bell: hum, prime, tierce, nominal */
    {256, 706, 1383, 2287},                     /* a free bar: glockenspiel, kalimba */
    {256, 1006, 2442, 1536},                    /* a tuned bar: marimba */
    {256, 408, 547, 588},                       /* a skin: the modes of a drum */
};

static const int8_t dots_steps[8] = {0, 12, 7, 19, -12, 24, 5, -5};

static void birch_ratios(uint8_t material, uint16_t out[4])
{
    uint32_t x = (uint32_t)material * 4u, seg = x / 128u, frac = x % 128u;
    if (seg > 3u) { seg = 3u; frac = 127u; }
    for (uint32_t i = 0; i < 4; i++)
        out[i] = (uint16_t)((birch_sets[seg][i] * (128u - frac) + birch_sets[seg + 1u][i] * frac) / 128u);
}

static void alg_start(voice_t *v, const part_t *s)
{
    for (uint32_t i = 0; i < 5; i++) { v->ph[i] = 0; v->a[i] = 0; v->gp[i] = 0; v->gs[i] = 0; }
    v->s0 = v->s1 = v->s2 = v->x0 = v->x1 = 0;
    v->t = 0;
    v->hold = 0;
    v->held = 0;
    part_rt_t *r = &part_rt[v->part];
    switch (v->alg) {
    case ALG_GLASS:
        v->ratio[0] = glass_ratio[s->p[0] * GLASS_RATIOS / 128u];
        v->a[0] = 65536;                        /* the strike's brightness, Q16, falls to 0 */
        v->k[0] = decay_k(knob_sq((uint8_t)(127u - s->p[3]), 15, 2500));
        r->clicks++;
        break;
    case ALG_BIRCH: {
        birch_ratios(s->p[0], v->ratio);
        int32_t h = 4096 * (s->p[1] + 12) / 139;   /* the mallet: how loud the upper partials start, Q12 */
        int32_t a = 16384 << 8;
        uint32_t t0 = knob_sq(s->p[2], 60, 7000);
        for (uint32_t i = 0; i < 4; i++) {
            v->a[i] = a;
            a = (int32_t)(((int64_t)a * h) >> 12) * 7 / 8;
            /* the upper partials die sooner, unless RING is up */
            uint32_t over = v->ratio[i] > 256u ? v->ratio[i] - 256u : 0u;
            uint32_t t = t0 * 256u / (256u + over * (127u - s->p[3]) / 48u);
            v->k[i] = decay_k(t);
        }
        v->x0 = 40 + s->p[1] * 4;               /* the click: samples */
        v->x1 = 2000 + s->p[1] * 150;           /* and its loudness */
        r->clicks++;
        break;
    }
    case ALG_DOTS:
        v->t = 1;                               /* a grain at once */
        break;
    case ALG_PULSE:
        v->x0 = 70;                             /* a click at the start */
        v->hold_n = (uint16_t)(1u + knob_sq(s->p[2], 0, 24));
        r->clicks++;
        break;
    case ALG_FOLD:
        v->rnd = 0x2545f491u ^ v->age;
        break;
    case ALG_AIR:
        v->rnd = 0x9e3779b9u ^ v->age * 7u;
        break;
    }
}

static int32_t dots_norm(const part_t *s)        /* the grain cloud's loudness: about the same, sparse or dense */
{
    uint32_t interval = 6615u >> (s->p[0] / 22u);
    uint32_t len = knob_sq(s->p[2], 265, 17640);
    uint32_t overlap = len / (interval ? interval : 1u);
    if (overlap > 5u) overlap = 5u;
    static const uint16_t inv_sqrt[6] = {32767, 32767, 23170, 18918, 16384, 14654};
    return inv_sqrt[overlap];
}

/* one sub-block of a voice, before the envelope: into buf, Q15. pmod: pitch (Q8), tmod: timbre (Q15) */
static void alg_render(voice_t *v, const part_t *s, int32_t *buf, int32_t pmod, int32_t tmod)
{
    uint32_t inc = pitch_inc(v->pitch + pmod + tuning_q8);
    part_rt_t *r = &part_rt[v->part];
    switch (v->alg) {
    case ALG_GLASS: {
        uint32_t minc = (uint32_t)(((uint64_t)inc * v->ratio[0]) >> 8);
        uint32_t c2 = inc * 2u + (inc >> 9) * (uint32_t)s->p[2] / 32u;   /* FROST: an octave up, a little sharp */
        int32_t frost = s->p[2] * 200;
        int32_t peak = s->p[1] * 1150, floor = peak / 5;
        int32_t idx = floor + (int32_t)(((int64_t)(peak - floor) * v->a[0]) >> 16);
        idx = idx * (32768 + tmod) >> 15;
        idx = idx * (20000 + (v->vel >> 2)) >> 15;
        if (idx < 0) idx = 0;
        v->a[0] = (int32_t)(((int64_t)v->a[0] * v->k[0]) >> 16);
        for (uint32_t n = 0; n < SUB; n++) {
            int32_t m = sin_q15(v->ph[1]);
            uint32_t off = (uint32_t)((int64_t)m * idx);
            int32_t x = sin_q15(v->ph[0] + off);
            if (frost) x += sin_q15(v->ph[2] + off) * frost >> 15;
            v->ph[0] += inc; v->ph[1] += minc; v->ph[2] += c2;
            buf[n] = x * 3 / 4;
        }
        break;
    }
    case ALG_BIRCH: {
        uint32_t pinc[4];
        int32_t up = 32768 + tmod;              /* the LFO's timbre: the upper partials */
        for (uint32_t i = 0; i < 4; i++) {
            uint64_t x = ((uint64_t)inc * v->ratio[i]) >> 8;
            pinc[i] = x < 0x70000000u ? (uint32_t)x : 0;   /* (above ~19 kHz: silent) */
        }
        int32_t amp[4];
        for (uint32_t i = 0; i < 4; i++) {
            amp[i] = (v->a[i] >> 8) * (i ? up : 32768) >> 15;
            if (!pinc[i]) amp[i] = 0;
            v->a[i] = (int32_t)(((int64_t)v->a[i] * v->k[i]) >> 16);
        }
        for (uint32_t n = 0; n < SUB; n++) {
            int32_t x = 0;
            for (uint32_t i = 0; i < 4; i++) {
                x += sin_q15(v->ph[i]) * amp[i] >> 15;
                v->ph[i] += pinc[i];
            }
            if (v->x0 > 0) {                    /* the mallet's click: a short burst of filtered noise */
                int32_t nz = (int32_t)(vrand(&v->rnd) >> 16) - 32768;
                v->s0 += (nz - v->s0) >> 1;
                x += v->s0 * v->x1 / 32768 * v->x0 / 400;
                v->x0--;
            }
            buf[n] = x;
        }
        if (v->stage != ENV_ATTACK && v->a[0] < 20 << 8 && v->a[1] < 20 << 8 && v->x0 <= 0) v->stage = ENV_OFF;
        break;
    }
    case ALG_DOTS: {
        int32_t norm = dots_norm(s);
        uint32_t interval = 6615u >> (s->p[0] / 22u);
        interval = (uint32_t)((int32_t)interval * (32768 - tmod / 2) >> 15);
        if (interval < 64u) interval = 64u;
        for (uint32_t n = 0; n < SUB; n++) {
            if (v->t && !--v->t) {               /* a new grain */
                uint32_t slot = 0;
                for (uint32_t g = 0; g < 5; g++) if (!v->gs[g]) { slot = g; break; } else if (v->gp[g] > v->gp[slot]) slot = g;
                uint32_t rr = vrand(&v->rnd);
                uint32_t choices = 1u + s->p[1] * 8u / 128u;
                uint32_t step = (rr >> 8) % choices;
                int32_t detune = (int32_t)((rr >> 20) % 201u) - 100;
                detune = detune * s->p[3] * 64 / (127 * 100);   /* up to +-64/256 of a semitone */
                v->gs[slot] = 0;
                v->inc[slot] = pitch_inc(v->pitch + pmod + tuning_q8 + dots_steps[step] * 256 + detune);
                uint32_t len = knob_sq(s->p[2], 265, 17640);
                len = len * (192u + (rr >> 24) % 128u) / 256u;
                v->gs[slot] = 0x80000000u / len;
                v->gp[slot] = 0;
                v->ph[slot] = rr;
                r->grains++;
                r->grain_step = (uint8_t)step;
                v->t = interval / 2u + (vrand(&v->rnd) >> 8) % interval;
                if (v->released) v->t = 0;      /* (released: no more grains, the last ones fade) */
            }
            int32_t x = 0;
            for (uint32_t g = 0; g < 5; g++) {
                if (!v->gs[g]) continue;
                int32_t e = sin_q15(v->gp[g]);  /* half a sine: the grain's shape */
                x += sin_q15(v->ph[g]) * e >> 15;
                v->ph[g] += v->inc[g];
                v->gp[g] += v->gs[g];
                if (v->gp[g] >= 0x80000000u) v->gs[g] = 0;
            }
            buf[n] = x * norm >> 15;
        }
        break;
    }
    case ALG_PULSE: {
        uint32_t beat = inc + (uint32_t)s->p[1] * s->p[1] * 48u;   /* up to 8 Hz away: it beats */
        int32_t click = s->p[0] * 220;
        uint32_t hold_n = v->hold_n + (uint32_t)(tmod > 0 ? tmod * 24 >> 15 : 0);
        uint32_t gate_inc = r->gate_inc;
        uint8_t bits = (uint8_t)(s->p[2] * 10u / 127u);
        for (uint32_t n = 0; n < SUB; n++) {
            int32_t x = sin_q15(v->ph[0]);
            if (s->p[1]) x = (x + sin_q15(v->ph[1])) / 2;
            v->ph[0] += inc; v->ph[1] += beat;
            if (gate_inc) {                     /* the gate: open half of each division, a click as it opens */
                uint32_t before = v->ph[2];
                v->ph[2] += gate_inc;
                uint8_t open = v->ph[2] < 0x80000000u;
                if (open && before >= 0x80000000u) { v->x0 = 70; r->clicks++; }
                int32_t target = open ? 32767 : 0;
                v->s1 += (target - v->s1) >> 5;
                x = x * v->s1 >> 15;
                r->gate = open;
            }
            if (v->x0 > 0) {                    /* the click: a square burst, falling */
                x += ((v->x0 & 4) ? click : -click) * v->x0 / 70;
                v->x0--;
            }
            if (hold_n > 1u) {                  /* DATA: hold every n samples, fewer bits */
                if (!v->hold) { v->held = (int16_t)clamp16(x); v->hold = (uint16_t)hold_n; }
                v->hold--;
                x = v->held;
            }
            if (bits) x &= ~((1 << (bits + 3)) - 1);
            buf[n] = x * 3 / 4;
        }
        break;
    }
    case ALG_FOLD: {
        if ((v->t++ & 7u) == 0) v->x1 = (int32_t)(vrand(&v->rnd) >> 16) - 32768;   /* WOBBLE: a new aim ~every 6 ms */
        v->s2 += (v->x1 - v->s2) >> 4;
        int32_t wob = (v->s2 * s->p[3] >> 15) * 48 >> 7;   /* up to ~ +-12/256 x 4 */
        inc = pitch_inc(v->pitch + pmod + tuning_q8 + wob);
        int32_t drive = 256 + s->p[0] * 2048 / 127; /* x1 .. x9 */
        drive = drive * (16384 + (v->env >> 10)) >> 15;    /* louder: more folds */
        drive = drive * (32768 + tmod) >> 15;
        if (drive < 128) drive = 128;
        int32_t bias = s->p[1] * 190;
        int32_t c = 1500 + (int32_t)knob_sq(s->p[2], 0, 30000);
        for (uint32_t n = 0; n < SUB; n++) {
            int32_t x = (sin_q15(v->ph[0]) * drive >> 8) + bias;
            uint32_t u = (uint32_t)(x + 32768) & 0x1FFFFu;
            x = u < 65536u ? (int32_t)u - 32768 : 98303 - (int32_t)u;
            v->ph[0] += inc;
            v->s0 += (x - v->s0) * c >> 15;     /* TONE */
            v->s1 += (v->s0 - v->s1) >> 9;      /* (the bias's DC, taken out) */
            buf[n] = (v->s0 - v->s1) * 5 / 8;
        }
        break;
    }
    case ALG_AIR: {
        uint32_t h = 1u + s->p[3] * 4u / 128u;
        uint32_t f_inc = inc * h;
        int32_t shift = tmod * 3 >> 15;         /* the LFO's timbre: up to +- 3/4 octave on the filter */
        if (shift > 0) f_inc += (f_inc >> 2) * (uint32_t)shift;
        if (shift < 0) f_inc -= (f_inc >> 3) * (uint32_t)(-shift);
        while (f_inc > 680000000u) f_inc >>= 1;  /* the filter stays below ~7 kHz */
        if ((v->t++ & 15u) == 0) v->x1 = (int32_t)((vrand(&v->rnd) >> 17) & 32767);   /* GUST */
        v->s2 += (v->x1 - v->s2) >> 3;
        int32_t gust = 32768 - ((32768 - v->s2) * s->p[2] >> 7) / 2;
        int32_t f = sin_q15(f_inc >> 1);        /* 2 sin(pi fc / fs), Q14 */
        int32_t q = 32768 >> (s->p[1] * 7 / 128);
        q = q - (q - (q >> 1)) * (s->p[1] * 7 % 128) / 128;
        if (q < 200) q = 200;
        int32_t breath = s->p[0], tone = 127 - s->p[0];
        uint32_t tinc = inc * h;
        int32_t in_gain = q < 4000 ? 4000 : q;  /* the narrower, the louder the band: keep it level */
        int32_t lp = v->s0, bp = v->s1;
        for (uint32_t n = 0; n < SUB; n++) {
            int32_t nz = (int32_t)(vrand(&v->rnd) >> 16) - 32768;
            int32_t in = nz * in_gain >> 15;
            lp += f * bp >> 14;
            int32_t hp = in - lp - (q * bp >> 14);
            bp += f * hp >> 14;
            bp = clamp16(bp * 2) / 2;
            int32_t x = (bp * 3 / 2) * breath / 127;
            x += (sin_q15(v->ph[0]) / 2) * tone / 127;
            v->ph[0] += tinc;
            buf[n] = x * gust >> 15;
        }
        v->s0 = lp; v->s1 = bp;
        break;
    }
    }
}

/* the loudness of each algorithm, matched by ear and by tools/measure_levels (Q12) */
static const uint16_t alg_gain[ALGS] = {997, 2667, 1770, 1725, 2064, 3366};

/* ----------------------------------------------------------------------------------------- the voices */
static void voice_start(uint8_t owner, uint8_t part, int32_t pitch_q8, uint8_t velocity)
{
    voice_t *v = &voices[0];
    for (uint32_t i = 0; i < VOICES; i++) {     /* the same note again, a free voice, or the oldest */
        voice_t *c = &voices[i];
        if (c->active && c->owner == owner && c->part == part && c->note == (uint8_t)(pitch_q8 >> 8)) { v = c; break; }
        if (!c->active) { if (v->active) v = c; continue; }
        if (v->active && (c->released > v->released || (c->released == v->released && c->age < v->age))) v = c;
    }
    fm1_irq_off();
    v->active = 0;
    fm1_irq_on();
    const part_t *s = &parts[part];
    v->part = part;
    v->owner = owner;
    v->note = (uint8_t)(pitch_q8 >> 8);
    v->pitch = pitch_q8;
    v->alg = s->alg;
    v->age = ++voice_clock;
    v->vel = 8000 + velocity * 194;
    v->rnd = 0x6d2b79f5u * v->age;
    v->released = 0;
    v->env = 0;
    v->stage = ENV_ATTACK;
    alg_start(v, s);
    fm1_irq_off();
    v->active = 1;
    fm1_irq_on();
}

static void voice_stop(uint8_t owner, uint8_t note)
{
    for (uint32_t i = 0; i < VOICES; i++)
        if (voices[i].active && voices[i].owner == owner && voices[i].note == note) voices[i].released = 1;
}

static void voice_stop_owner(uint8_t owner)
{
    for (uint32_t i = 0; i < VOICES; i++)
        if (voices[i].active && voices[i].owner == owner) voices[i].released = 1;
}

/* the envelope, a sample at a time: 0 when the voice is done */
static inline int env_tick(voice_t *v, const part_rt_t *r)
{
    if (v->released && v->stage < ENV_RELEASE) v->stage = ENV_RELEASE;
    switch (v->stage) {
    case ENV_ATTACK:
        v->env += r->attack;
        if (v->env >= ENV_ONE) { v->env = ENV_ONE; v->stage = ENV_DECAY; }
        break;
    case ENV_DECAY: {
        int32_t s = r->sustain;
        v->env -= (int32_t)(((int64_t)(v->env - s) * r->decay) >> 24) + 1;
        if (v->env <= s + 256) { v->env = s; v->stage = ENV_SUSTAIN; }
        break;
    }
    case ENV_SUSTAIN:
        v->env = r->sustain;
        if (v->env < 4096) return 0;
        break;
    case ENV_RELEASE:
        v->env -= (int32_t)(((int64_t)v->env * r->release) >> 24) + 1;
        if (v->env < 4096) return 0;
        break;
    default:
        return 0;
    }
    return 1;
}

static int32_t lfo_wave(part_rt_t *r, uint8_t shape)
{
    uint32_t p = r->lfo_phase;
    int32_t x = (int32_t)(p >> 16) - 32768;
    int32_t m = x < 0 ? -x : x;
    switch (shape) {
    case LFO_SINE: return sin_q15(p);
    case LFO_TRIANGLE: return clamp16(32767 - 2 * m);
    case LFO_SQUARE: return p < 0x80000000u ? 32767 : -32767;
    case LFO_SAW: return clamp16(-x);
    case LFO_HOLD: return r->lfo_to;
    default:                                    /* DRIFT: from one random value to the next, smoothly */
        return r->lfo_from + (int32_t)(((int64_t)(r->lfo_to - r->lfo_from) * (32768 - sin_q15(p / 2u + 0x40000000u))) >> 16);
    }
}

static void voices_render(int32_t *out, uint32_t frames)
{
    static int32_t pbuf[PARTS][SUB], vbuf[SUB];
    int32_t peak[PARTS] = {0};
    for (uint32_t base = 0; base < frames; base += SUB) {
        int32_t pmod[PARTS], tmod[PARTS], vol[PARTS], pan[PARTS];
        uint8_t used[PARTS] = {0};
        for (uint32_t p = 0; p < PARTS; p++) {
            part_rt_t *r = &part_rt[p];
            const part_t *s = &parts[p];
            uint32_t before = r->lfo_phase;
            r->lfo_phase += r->lfo_inc;
            if (r->lfo_phase < before) {        /* a new cycle: new random values */
                r->lfo_from = r->lfo_to;
                r->lfo_rnd = r->lfo_rnd * 1664525u + 1013904223u + p;
                r->lfo_to = (int32_t)(r->lfo_rnd >> 16) - 32768;
            }
            int32_t w = lfo_wave(r, s->lfo[K_SHAPE]);
            r->lfo_out = w;
            int32_t m = (int32_t)(((int64_t)w * r->lfo_depth) >> 15);
            uint8_t target = s->lfo[K_TARGET];
            pmod[p] = target == LT_PITCH ? m * 3072 / 32768 : 0;
            tmod[p] = target == LT_TIMBRE ? m : 0;
            vol[p] = target == LT_VOLUME ? 32767 - (int32_t)(((int64_t)(32767 - w) * r->lfo_depth) >> 16) : 32767;
            pan[p] = s->pan * 512 + (target == LT_PAN ? m : 0);
            pan[p] = pan[p] < -32767 ? -32767 : pan[p] > 32767 ? 32767 : pan[p];
        }
        for (uint32_t i = 0; i < VOICES; i++) {
            voice_t *v = &voices[i];
            if (!v->active) continue;
            uint32_t p = v->part;
            const part_rt_t *r = &part_rt[p];
            alg_render(v, &parts[p], vbuf, pmod[p], tmod[p]);
            if (!used[p]) { for (uint32_t n = 0; n < SUB; n++) pbuf[p][n] = 0; used[p] = 1; }
            int32_t g = (int32_t)((v->vel * alg_gain[v->alg]) >> 12);
            for (uint32_t n = 0; n < SUB; n++) {
                if (!env_tick(v, r)) { v->active = 0; break; }
                pbuf[p][n] += (int32_t)(((int64_t)vbuf[n] * (v->env >> 9) * g) >> 30);
            }
            if (v->stage == ENV_OFF) v->active = 0;
        }
        for (uint32_t p = 0; p < PARTS; p++) {
            part_rt_t *r = &part_rt[p];
            int32_t gain = (int32_t)(((int64_t)r->gain * vol[p]) >> 15);
            /* balance: the centre keeps its level, only the far side falls */
            int32_t lt = gain, rt = gain;
            if (pan[p] > 0) lt = (int32_t)(((int64_t)gain * (32767 - pan[p])) >> 15);
            if (pan[p] < 0) rt = (int32_t)(((int64_t)gain * (32767 + pan[p])) >> 15);
            if (!used[p]) { r->cur_left = lt; r->cur_right = rt; continue; }
            int32_t dl = (lt - r->cur_left) / (int32_t)SUB, dr = (rt - r->cur_right) / (int32_t)SUB;
            int32_t *o = &out[2u * base];
            for (uint32_t n = 0; n < SUB; n++) {
                int32_t x = pbuf[p][n];
                int32_t a = x < 0 ? -x : x;
                if (a > peak[p]) peak[p] = a;
                r->cur_left += dl; r->cur_right += dr;
                o[2u * n] += (int32_t)(((int64_t)x * r->cur_left) >> 15);
                o[2u * n + 1u] += (int32_t)(((int64_t)x * r->cur_right) >> 15);
            }
            r->cur_left = lt; r->cur_right = rt;
        }
    }
    for (uint32_t p = 0; p < PARTS; p++) {
        int32_t g = (int32_t)(((int64_t)peak[p] * part_rt[p].gain) >> 15);
        part_rt[p].meter = g > 32767 ? 32767 : g;
    }
}
