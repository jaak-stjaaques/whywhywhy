/* SPDX-License-Identifier: GPL-3.0-only */
/* WHYWHYWHY: an FM-1 firmware on Dinghy's platform, for exploring. No words on the screen: four coloured buoys
 * (the four parts) float on paper, and what you hear moves them.
 *
 * How the notes flow:   KEYS / ARP  ->  SEQ (4 tracks)  ->  SCALE  ->  GLASS BIRCH DOTS PULSE FOLD AIR  ->  FX
 *
 * The keys, the ARP and the SEQ only deal in scale DEGREES; the SCALE stage at the end turns them into pitches, so
 * a new scale, mode, gravity, tuning or key changes everything at once, recordings included.
 *
 * The files: voices.h (the six algorithms, the envelopes, the LFOs), scale.h (the scales), effects.h (echo, hall,
 * chorus, dust: in parallel), stage.h (the pictures), this file (the ARP, the SEQ, the scenes, the panel).
 * platform.c is Dinghy's plumbing. */
#define APP_NAME "WHYWHYWHY"
static void app_idle(void);
#define LCD_IDLE app_idle                       /* while the screen's DMA runs: the clock, the keys, MIDI go on */
#define CV_MAX (240u * 52u)                     /* the header and the knob band */
#include "platform.c"

#include "voices.h"
#include "scale.h"
#include "effects.h"

/* ------------------------------------------------------------------------------------------------ the sound */
static volatile int32_t out_meter;              /* the loudest output sample of the last block (the FX page) */

static int32_t output_level(int32_t sample)
{
    /* Raise quiet notes by about 9.5 dB and soften the peaks before the master volume (as LOOPHEADS). */
    int32_t magnitude = (sample < 0 ? -sample : sample) * 3;
    if (magnitude > 20000) {
        int32_t excess = magnitude - 20000;
        magnitude = 20000 + 12000 * excess / (12000 + excess);
    }
    return sample < 0 ? -magnitude : magnitude;
}

static void app_audio(int32_t *out, uint32_t frames)
{
    voices_render(out, frames);
    int32_t peak = 0;
    for (uint32_t i = 0; i < 2u * frames; i += 2u) {
        int32_t a = out[i] < 0 ? -out[i] : out[i];
        if (a > peak) peak = a;
    }
    out_meter = peak;
    effects_process(out, frames);
    for (uint32_t i = 0; i < 2u * frames; i++) out[i] = output_level(out[i]);
}

/* who started a note (the same one stops it): a track, the ARP, a key, a MIDI note */
enum { OWNER_TRACK = 0, OWNER_ARP = 4, OWNER_KEYS = 8, OWNER_MIDI = 40, OWNERS = OWNER_MIDI + 128 };

/* --------------------------------------------------------------------------------------------- settings */
#define LONG_MS 650u                            /* a press this long is a "hold" */
#define KEY0 53                                 /* key 0 = F3 */
#define REST (-128)

enum { P_HOME, P_EDIT, P_ENV, P_LFO, P_ARP, P_SEQ, P_SCALE, P_FX, P_GLO, PAGES };

static const uint16_t div_ticks[] = {192, 96, 64, 48, 32, 24, 16, 12, 8};    /* 96 ticks a beat: 1/2 .. 1/32 T */
#define DIVS (sizeof div_ticks / sizeof div_ticks[0])
#define STEP_TICKS 24u                          /* the tracks step in sixteenths */

static struct { uint8_t bpm, swing, ch; int8_t tune; } glo = {104, 50, 0, 0};
static struct { uint8_t echo, hall, chorus, dust; } fx = {24, 36, 0, 0};       /* 0..127 */

static uint8_t page = P_HOME, selected;
static uint32_t rnd_state = 0x2f6b9e31u;
static uint32_t rnd(void) { return vrand(&rnd_state) >> 8; }
static uint32_t rnd_n(uint32_t n) { return n ? rnd() % n : 0; }

/* the screen wants to know about each note: a little queue the pictures read */
typedef struct { uint8_t part, vel, owner; int16_t degree; int32_t pitch; uint32_t ms; } note_ev_t;
#define EVQ 32u
static note_ev_t evq[EVQ];
static volatile uint32_t evq_w;

/* ----------------------------------------------------------------------------------------- notes */
static uint8_t sounding[OWNERS], sounding_part[OWNERS];   /* the MIDI note + 1 an owner sends, 0 = none */

static uint8_t midi_out_channel(uint8_t part) { return part; }   /* MIDI out: part 1..4 on channels 1..4 */

static void note_off(uint8_t owner)
{
    voice_stop_owner(owner);
    if (sounding[owner]) {
        midi_send(0x80u | midi_out_channel(sounding_part[owner]), sounding[owner] - 1u, 0);
        sounding[owner] = 0;
    }
}

static void note_on(uint8_t owner, uint8_t part, int32_t degree, uint8_t velocity)
{
    if (sounding[owner]) note_off(owner);
    int32_t pitch = scale_pitch(degree);
    voice_start(owner, part, pitch, velocity);
    uint8_t m = (uint8_t)((pitch + 128) >> 8);
    midi_send(0x90u | midi_out_channel(part), m, velocity);
    sounding[owner] = (uint8_t)(m + 1u);
    sounding_part[owner] = part;
    note_ev_t *e = &evq[evq_w % EVQ];
    e->part = part; e->vel = velocity; e->owner = owner; e->degree = (int16_t)degree; e->pitch = pitch; e->ms = fm1_ms;
    evq_w++;
}

/* --------------------------------------------------------------------------------------- the SEQ: 4 tracks
 * One track a part, up to 32 sixteenths. The SEQ page's knobs set each track's length (0: the track is off):
 * four lengths, four loops drifting in and out of phase. A step holds a degree, a velocity and a length. */
#define STEPS 32u
typedef struct {
    int8_t deg[STEPS];
    uint8_t vel[STEPS], len[STEPS];
    uint8_t length;
} track_t;
static track_t tracks[PARTS];
static uint8_t rec_on;                          /* REC: the selected track records what is played */
static uint32_t tracks_version;
static uint8_t track_skip[PARTS];               /* a step just recorded late: do not play it again this pass (+1) */

static void track_clear(uint32_t t)
{
    for (uint32_t i = 0; i < STEPS; i++) { tracks[t].deg[i] = REST; tracks[t].vel[i] = 0; tracks[t].len[i] = 1; }
    tracks_version++;
}

/* --------------------------------------------------------------------------------------- the ARP
 * Hold keys: they are kept (latched) and played in a pattern, at a rate, over 1..4 octaves. Let go of all and
 * play again: a new chord. WANDER lets a step stray to a neighbour, an octave, or a rest. */
enum { ARP_UP, ARP_DOWN, ARP_UPDOWN, ARP_CONVERGE, ARP_DIVERGE, ARP_RANDOM, ARP_PLAYED, ARP_STAR, ARP_GROW, ARP_PATTERNS };
#define ARP_MAX 16u
static struct {
    uint8_t on, pattern, rate, range, wander;   /* range 0..3 = 1..4 octaves */
    int16_t notes[ARP_MAX];                     /* the latched degrees, in the order played */
    uint8_t n, held;
    uint32_t step;
    int16_t last;                               /* the last degree played (the screen) */
    uint8_t last_index, last_rest;
} arp = {0, ARP_UP, 5, 1, 0};

static void arp_add(int32_t degree)
{
    if (!arp.held) { arp.n = 0; arp.step = 0; }  /* all keys were up: a new chord */
    arp.held++;
    for (uint32_t i = 0; i < arp.n; i++) if (arp.notes[i] == degree) return;
    if (arp.n < ARP_MAX) arp.notes[arp.n++] = (int16_t)degree;
}

static void arp_release(void)
{
    if (arp.held) arp.held--;
}

static uint32_t arp_order(uint32_t s, uint32_t total)   /* step s of the pattern -> an index 0..total-1 */
{
    uint32_t p = s % total;
    switch (arp.pattern) {
    case ARP_DOWN: return total - 1u - p;
    case ARP_UPDOWN: {
        if (total < 2u) return 0;
        uint32_t q = s % (2u * total - 2u);
        return q < total ? q : 2u * total - 2u - q;
    }
    case ARP_CONVERGE: return p & 1u ? total - 1u - p / 2u : p / 2u;
    case ARP_DIVERGE: { uint32_t q = total - 1u - p; return q & 1u ? total - 1u - q / 2u : q / 2u; }
    case ARP_RANDOM: return rnd_n(total);
    case ARP_STAR: {                            /* every second (or third) note: a star drawn through the chord */
        uint32_t k = total % 2u ? 2u : total % 3u ? 3u : 1u;
        return (s * k) % total;
    }
    case ARP_GROW: {                            /* 1, 1 2, 1 2 3, ...: the pattern grows and starts again */
        uint32_t period = total * (total + 1u) / 2u, q = s % period, row = 1;
        while (q >= row) { q -= row; row++; }
        return q;
    }
    default: return p;                          /* UP, PLAYED */
    }
}

static int32_t arp_next(void)                   /* the next degree, or REST */
{
    if (!arp.n) return REST;
    int16_t list[ARP_MAX];
    uint32_t n = arp.n;
    for (uint32_t i = 0; i < n; i++) list[i] = arp.notes[i];
    if (arp.pattern != ARP_PLAYED)               /* low to high */
        for (uint32_t i = 1; i < n; i++) {
            int16_t x = list[i];
            uint32_t j = i;
            while (j && list[j - 1u] > x) { list[j] = list[j - 1u]; j--; }
            list[j] = x;
        }
    uint32_t total = n * (arp.range + 1u);
    uint32_t i = arp_order(arp.step++, total);
    arp.last_index = (uint8_t)i;
    int32_t degree = list[i % n] + (int32_t)(i / n) * scale_now.size;
    arp.last_rest = 0;
    if (arp.wander && rnd_n(256) < arp.wander * 3u / 2u) {   /* WANDER: up to 3 steps in 4 stray at the top */
        switch (rnd_n(4)) {
        case 0: arp.last_rest = 1; return REST;
        case 1: degree += 1; break;
        case 2: degree -= 1; break;
        default: degree += rnd_n(2) ? (int32_t)scale_now.size : -(int32_t)scale_now.size; break;
        }
    }
    arp.last = (int16_t)degree;
    return degree;
}

/* ------------------------------------------------------------------------------------------- the clock
 * 96 ticks a beat, from the tempo or a MIDI clock (4 ticks a MIDI clock). Five players: the four tracks (in
 * sixteenths) and the ARP (at its rate). SWING pushes every second step late. */
enum { PLAYER_ARP = PARTS, PLAYERS };
static struct {
    uint32_t start, next, off, idx;
    uint8_t on;
} players[PLAYERS];
static struct {
    uint8_t running;
    uint32_t tick, acc, last_ms, ext_ticks, last_midi_clock;
} clk;
static uint8_t key_held_owner_on[27];

static uint32_t player_div(uint32_t p) { return p == PLAYER_ARP ? div_ticks[arp.rate] : STEP_TICKS; }

static uint32_t player_time(uint32_t p, uint32_t idx)
{
    uint32_t div = player_div(p), t = players[p].start + idx * div;
    if (idx & 1u) t += div * (glo.swing - 50u) / 50u;
    return t;
}

static void player_restart(uint32_t p, uint32_t at)
{
    players[p].start = at;
    players[p].idx = 0;
    players[p].next = at;
}

static uint8_t player_owner(uint32_t p) { return (uint8_t)(p == PLAYER_ARP ? OWNER_ARP : OWNER_TRACK + p); }

/* where a track is now: the step that played last */
static uint32_t track_pos(uint32_t t)
{
    uint32_t len = tracks[t].length;
    if (!len) return 0;
    return players[t].idx ? (players[t].idx - 1u) % len : 0;
}

/* REC: what the selected part plays goes onto its track, on the step it falls on (a note played in the second
 * half of a step goes onto the next one) */
static int32_t rec_note(int32_t degree, uint8_t velocity)
{
    uint32_t t = selected;
    track_t *k = &tracks[t];
    if (!k->length) k->length = 16;
    uint32_t cur = players[t].idx ? players[t].idx - 1u : 0;
    uint32_t into = clk.tick - player_time(t, cur);
    uint32_t s = (cur + (into > STEP_TICKS / 2u && players[t].idx ? 1u : 0u)) % k->length;
    if (into > STEP_TICKS / 2u && players[t].idx) track_skip[t] = (uint8_t)(s + 1u);
    if (degree < -127) degree = -127;
    if (degree > 127) degree = 127;
    k->deg[s] = (int8_t)degree;
    k->vel[s] = velocity;
    k->len[s] = 1;
    tracks_version++;
    return (int32_t)s;
}

static void player_fire(uint32_t p)
{
    if (p == PLAYER_ARP) {
        if (!arp.on) return;
        int32_t degree = arp_next();
        if (degree == REST) return;
        uint8_t velocity = (uint8_t)(88u + rnd_n(28));
        note_on(OWNER_ARP, selected, degree, velocity);
        players[p].on = 1;
        players[p].off = clk.tick + player_div(p) * 55u / 100u;
        if (rec_on) rec_note(degree, velocity);
        return;
    }
    track_t *k = &tracks[p];
    if (!k->length) return;
    uint32_t s = players[p].idx % k->length;
    if (track_skip[p] == s + 1u) { track_skip[p] = 0; return; }
    if (k->deg[s] == REST) return;
    note_on(player_owner(p), (uint8_t)p, k->deg[s], k->vel[s]);
    players[p].on = 1;
    players[p].off = clk.tick + k->len[s] * STEP_TICKS - 4u;
}

static void clock_tick(void)
{
    clk.tick++;
    for (uint32_t p = 0; p < PLAYERS; p++) {
        if (players[p].on && (int32_t)(clk.tick - players[p].off) >= 0) {
            note_off(player_owner(p));
            players[p].on = 0;
        }
        if ((int32_t)(clk.tick - players[p].next) >= 0) {
            player_fire(p);
            players[p].idx++;
            players[p].next = player_time(p, players[p].idx);
        }
    }
}

static uint32_t external_bpm_value;
static int external_clock(void) { return fm1_ms - clk.last_midi_clock < 500u; }

static void transport_start(void)
{
    clk.running = 1;
    clk.acc = 0;
    clk.ext_ticks = 0;
    clk.last_ms = fm1_ms;
    arp.step = 0;
    for (uint32_t p = 0; p < PLAYERS; p++) {
        if (players[p].on) note_off(player_owner(p));
        players[p].on = 0;
        player_restart(p, 0);
    }
    clk.tick = (uint32_t)-1;                    /* the first tick is tick 0 */
    clock_tick();
}

static void transport_stop(void)
{
    clk.running = 0;
    rec_on = 0;
    for (uint32_t p = 0; p < PLAYERS; p++) {
        if (players[p].on) note_off(player_owner(p));
        players[p].on = 0;
    }
}

static void arp_set_rate(uint8_t rate)
{
    if (rate == arp.rate) return;
    uint32_t at = players[PLAYER_ARP].next;     /* the step already due keeps its time; the new grid starts there */
    arp.rate = rate;
    player_restart(PLAYER_ARP, clk.running ? at : 0);
}

static void clock_service(void)
{
    uint32_t now = fm1_ms, elapsed = now - clk.last_ms;
    clk.last_ms = now;
    if (!clk.running) { clk.ext_ticks = 0; return; }
    if (external_clock()) {
        while (clk.ext_ticks) { clk.ext_ticks--; clock_tick(); }
        return;
    }
    clk.ext_ticks = 0;
    if (elapsed > 100u) elapsed = 100u;         /* after a long pause (a save), do not rush */
    clk.acc += elapsed * glo.bpm * 96u;         /* in 1/60000 ticks */
    while (clk.acc >= 60000u) {
        clk.acc -= 60000u;
        clock_tick();
    }
}

/* ------------------------------------------------------------------------------------------------ MIDI in */
static uint32_t idle_ok, clk_n, clk_t0;

static void midi_in(void)
{
    uint8_t st, d1, d2;
    while (midi_get(&st, &d1, &d2) != 0) {
        uint32_t ty = st & 0xF0u, ch = st & 15u;
        if (st == 0xF8u) {                      /* clock: 24 a beat */
            clk.last_midi_clock = fm1_ms;
            clk.ext_ticks += 4u;
            if (clk_n % 24u == 0u) {
                if (clk_n) {
                    uint32_t ms = fm1_ms - clk_t0;
                    external_bpm_value = ms ? (60000u + ms / 2u) / ms : 0;
                }
                clk_t0 = fm1_ms;
            }
            clk_n++;
            continue;
        }
        if (st == 0xFAu) { clk_n = 0; transport_start(); continue; }
        if (st == 0xFCu) { transport_stop(); continue; }
        if (st == 0xFBu) { clk.running = 1; continue; }
        if (glo.ch && ch != glo.ch - 1u) continue;
        uint8_t owner = (uint8_t)(OWNER_MIDI + (d1 & 127u));
        if (ty == 0x90u && d2) {
            int32_t degree = scale_degree_of(d1);
            if (arp.on) { arp_add(degree); continue; }
            note_on(owner, selected, degree, d2);
            if (rec_on && clk.running) rec_note(degree, d2);
        } else if (ty == 0x80u || ty == 0x90u) {
            if (arp.on) arp_release(); else note_off(owner);
        }
    }
}

/* ----------------------------------------------------------------------------------------------- the keys
 * read here and from app_idle too, so a key is never late because the screen is busy. The SCALE page: the keys
 * move the key of everything (C4 = no change). Elsewhere: they play the selected part, snapped to the scale;
 * with the ARP on, they feed it. */
static uint32_t keys_prev;
static int32_t key_rec_step[27];                /* REC: the step a held key went onto, + 1 */

static void keys_service(void)
{
    uint32_t keys = fm1_in.notes, down = keys & ~keys_prev, up = keys_prev & ~keys;
    keys_prev = keys;
    if (!down && !up) return;
    for (uint32_t k = 0; k < 27u; k++) {
        uint32_t bit = 1u << k;
        if (down & bit) {
            if (page == P_SCALE) {
                scale_now.transpose = (int8_t)((int32_t)k - 7 + 12 * scale_now.octave);
                if (scale_now.transpose < -24) scale_now.transpose = -24;
                if (scale_now.transpose > 36) scale_now.transpose = 36;
                key_held_owner_on[k] = 0;
                continue;
            }
            int32_t degree = scale_degree_of(KEY0 + (int32_t)k + 12 * scale_now.octave);
            if (arp.on) {
                arp_add(degree);
                key_held_owner_on[k] = 2;
                if (!clk.running) transport_start();
                continue;
            }
            uint8_t velocity = (uint8_t)(96u + rnd_n(16));
            note_on((uint8_t)(OWNER_KEYS + k), selected, degree, velocity);
            key_held_owner_on[k] = 1;
            key_rec_step[k] = rec_on && clk.running ? rec_note(degree, velocity) + 1 : 0;
        }
        if (up & bit) {
            if (key_held_owner_on[k] == 2) arp_release();
            if (key_held_owner_on[k] == 1) {
                note_off((uint8_t)(OWNER_KEYS + k));
                if (key_rec_step[k] && rec_on) {      /* a held key: the step's length */
                    track_t *t = &tracks[selected];
                    uint32_t s = (uint32_t)key_rec_step[k] - 1u, now = track_pos(selected);
                    uint32_t len = t->length ? (now + t->length - s) % t->length + 1u : 1u;
                    if (len > 16u) len = 16u;
                    if (s < STEPS) t->len[s] = (uint8_t)len;
                    tracks_version++;
                }
            }
            key_held_owner_on[k] = 0;
            key_rec_step[k] = 0;
        }
    }
}

static void app_idle(void)
{
    if (!idle_ok) return;
    midi_in();
    keys_service();
    clock_service();
}

/* ---------------------------------------------------------------------------------- dice and scenes
 * Hold a page's button: the dice are thrown for that page. PRESETS: eight scenes; one not saved yet is a new
 * random scene each time you land on it (unexplored water). SAVE keeps the scene you are in. */
static uint8_t r8(uint32_t lo, uint32_t hi) { return (uint8_t)(lo + rnd_n(hi - lo + 1u)); }

static void dice_edit(uint32_t p)
{
    /* each algorithm's knobs where they sound good to begin with: the knobs still go everywhere */
    static const uint8_t range[ALGS][4][2] = {
        {{0, 127}, {20, 100}, {0, 80}, {20, 110}},      /* GLASS */
        {{0, 127}, {10, 100}, {30, 110}, {0, 90}},      /* BIRCH */
        {{40, 110}, {0, 90}, {30, 110}, {0, 70}},       /* DOTS */
        {{0, 60}, {0, 80}, {0, 40}, {0, 127}},          /* PULSE */
        {{10, 90}, {0, 80}, {20, 100}, {0, 60}},        /* FOLD */
        {{20, 90}, {50, 127}, {0, 80}, {0, 90}},        /* AIR */
    };
    part_t *s = &parts[p];
    for (uint32_t i = 0; i < 4; i++) s->p[i] = r8(range[s->alg][i][0], range[s->alg][i][1]);
    if (s->alg == ALG_PULSE) {
        if (rnd_n(2)) s->p[2] = 0;              /* half the time a clean sine, half the time no gate */
        if (rnd_n(2)) s->p[3] = 0;
    }
}

static void dice_env(uint32_t p)
{
    part_t *s = &parts[p];
    s->env[0] = rnd_n(3) ? r8(0, 25) : r8(20, 90);
    s->env[1] = r8(25, 100);
    s->env[2] = rnd_n(2) ? 0 : r8(30, 110);
    s->env[3] = r8(20, 90);
}

static void dice_lfo(uint32_t p)
{
    part_t *s = &parts[p];
    s->lfo[K_RATE] = r8(10, 90);
    s->lfo[K_DEPTH] = rnd_n(3) ? r8(15, 60) : r8(50, 100);
    s->lfo[K_SHAPE] = r8(0, LFO_SHAPES - 1);
    s->lfo[K_TARGET] = r8(0, LFO_TARGETS - 1);
    if (s->lfo[K_TARGET] == LT_PITCH) s->lfo[K_DEPTH] = r8(8, 30);
}

static void dice_sound(uint32_t p)
{
    parts[p].alg = r8(0, ALGS - 1);
    dice_edit(p);
    dice_env(p);
    dice_lfo(p);
}

static void dice_arp(void)
{
    arp.pattern = r8(0, ARP_PATTERNS - 1);
    arp_set_rate(r8(3, 7));
    arp.range = r8(0, 2);
    arp.wander = rnd_n(2) ? 0 : r8(10, 80);
}

static void dice_lengths(void)
{
    static const uint8_t lens[] = {3, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 16, 16, 16, 20, 24, 32};
    for (uint32_t t = 0; t < PARTS; t++)
        if (tracks[t].length) tracks[t].length = lens[rnd_n(sizeof lens)];
}

static void dice_scale(void)
{
    scale_now.scale = r8(1, SCALE_COUNT - 5);   /* (not the chromatic, nor the little chord scales) */
    scale_now.mode = 0;
    scale_update();
    scale_now.mode = r8(0, scale_now.size - 1u);
    scale_now.gravity = rnd_n(4) ? 0 : r8(32, 63);
    scale_now.truth = rnd_n(2) ? 0 : r8(60, 127);
    scale_update();
}

static void dice_fx(void)
{
    fx.echo = rnd_n(3) ? r8(0, 60) : 0;
    fx.hall = r8(10, 80);
    fx.chorus = rnd_n(2) ? 0 : r8(10, 70);
    fx.dust = rnd_n(3) ? 0 : r8(5, 50);
}

/* a melody of its own for a track: a walk on the scale, sparse or busy, low or high */
static void track_compose(uint32_t t)
{
    static const uint8_t lens[] = {5, 6, 7, 8, 8, 10, 12, 12, 16, 16, 16, 9, 11, 13};
    track_t *k = &tracks[t];
    track_clear(t);
    k->length = lens[rnd_n(sizeof lens)];
    uint32_t density = 20u + rnd_n(55);
    int32_t home = t == 0 ? -(int32_t)scale_now.size : t == 3 ? (int32_t)scale_now.size : 0, d = home;
    for (uint32_t i = 0; i < k->length; i++) {
        if (rnd_n(100) >= density && i) continue;
        d += (int32_t)rnd_n(5) - 2;
        if (d > home + 6) d = home + 3;
        if (d < home - 4) d = home - 2;
        k->deg[i] = (int8_t)d;
        k->vel[i] = (uint8_t)(70u + rnd_n(50));
        k->len[i] = (uint8_t)(1u + (rnd_n(4) == 0 ? rnd_n(4) : 0));
    }
}

static void scene_random(void)
{
    for (uint32_t p = 0; p < PARTS; p++) {
        dice_sound(p);
        parts[p].level = 100;
        parts[p].pan = (int8_t)((int32_t)p * 24 - 36);
        track_compose(p);
    }
    /* one part low and long, one short and bright: a little orchestra, not four of the same */
    parts[0].env[2] = r8(40, 100);
    parts[0].env[0] = r8(0, 30);
    parts[2].env[1] = r8(20, 60);
    parts[2].env[2] = 0;
    dice_scale();
    dice_fx();
    dice_arp();
    scale_now.transpose = (int8_t)((int32_t)rnd_n(12) - 5);
}

#define SCENES 8u
#define SCENE_MAGIC 0x31594857u                 /* "WHY1" */
typedef struct {
    uint32_t magic;
    part_t parts[PARTS];
    track_t tracks[PARTS];
    uint8_t scale, mode, gravity, truth;
    int8_t transpose;
    uint8_t bpm, swing, fx[4];
    uint8_t arp_pattern, arp_rate, arp_range, arp_wander;
} scene_t;
static scene_t scene_buf;
static uint8_t scene_saved;                     /* bit i: slot i holds a scene */
static uint8_t scene_algs[SCENES][PARTS];       /* (the pictures of the slots) */
static int8_t scene_now = -1;                   /* the slot the PRESETS knob is on, -1: none yet */

static void scene_store(scene_t *s)
{
    memset(s, 0, sizeof *s);
    s->magic = SCENE_MAGIC;
    memcpy(s->parts, parts, sizeof parts);
    memcpy(s->tracks, tracks, sizeof tracks);
    s->scale = scale_now.scale; s->mode = scale_now.mode; s->gravity = scale_now.gravity; s->truth = scale_now.truth;
    s->transpose = scale_now.transpose;
    s->bpm = glo.bpm; s->swing = glo.swing;
    s->fx[0] = fx.echo; s->fx[1] = fx.hall; s->fx[2] = fx.chorus; s->fx[3] = fx.dust;
    s->arp_pattern = arp.pattern; s->arp_rate = arp.rate; s->arp_range = arp.range; s->arp_wander = arp.wander;
}

static void scene_apply(const scene_t *s)
{
    for (uint32_t p = 0; p < PLAYERS; p++) if (players[p].on) { note_off(player_owner(p)); players[p].on = 0; }
    memcpy(parts, s->parts, sizeof parts);
    memcpy(tracks, s->tracks, sizeof tracks);
    for (uint32_t p = 0; p < PARTS; p++) {
        if (parts[p].alg >= ALGS) parts[p].alg = 0;
        if (tracks[p].length > STEPS) tracks[p].length = 16;
    }
    scale_now.scale = s->scale; scale_now.mode = s->mode; scale_now.gravity = s->gravity; scale_now.truth = s->truth;
    scale_now.transpose = s->transpose;
    scale_update();
    glo.bpm = s->bpm < 40 ? 104 : s->bpm; glo.swing = s->swing < 50 || s->swing > 75 ? 50 : s->swing;
    fx.echo = s->fx[0]; fx.hall = s->fx[1]; fx.chorus = s->fx[2]; fx.dust = s->fx[3];
    arp.pattern = s->arp_pattern % ARP_PATTERNS; arp_set_rate((uint8_t)(s->arp_rate % DIVS));
    arp.range = s->arp_range & 3u; arp.wander = s->arp_wander;
    tracks_version++;
}

static int scene_save(uint32_t slot)
{
    scene_store(&scene_buf);
    if (blob_save(slot, &scene_buf, sizeof scene_buf)) return -1;
    scene_saved |= (uint8_t)(1u << slot);
    for (uint32_t p = 0; p < PARTS; p++) scene_algs[slot][p] = parts[p].alg;
    return 0;
}

static void scene_erase(uint32_t slot)
{
    scene_store(&scene_buf);
    scene_buf.magic = 0;                        /* (a scene with no magic is an empty slot) */
    blob_save(slot, &scene_buf, sizeof scene_buf);
    scene_saved &= (uint8_t)~(1u << slot);
}

static void scene_goto(uint32_t slot)
{
    if ((scene_saved >> slot & 1u) && !blob_load(slot, &scene_buf, sizeof scene_buf) && scene_buf.magic == SCENE_MAGIC)
        scene_apply(&scene_buf);
    else
        scene_random();
}

static void scenes_scan(void)
{
    for (uint32_t i = 0; i < SCENES; i++)
        if (!blob_load(i, &scene_buf, sizeof scene_buf) && scene_buf.magic == SCENE_MAGIC) {
            scene_saved |= (uint8_t)(1u << i);
            for (uint32_t p = 0; p < PARTS; p++) scene_algs[i][p] = scene_buf.parts[p].alg % ALGS;
        }
}

/* a new seed every power-on. The online emulator starts the chip's random numbers at the same value each time,
 * so there (only there) a counter kept in the settings sector is mixed in, as LOOPHEADS does. */
static uint32_t boot_seed(void)
{
    uint32_t seed = fm1_random_word();
    if (seed != 0 && seed != 0x0bf34dadu) return seed;
    uint32_t second = fm1_random_word();
    if ((seed || second) && (seed != 0x0bf34dadu || second != 0x026e6076u)) return seed ? seed : second;
    uint32_t saved = 0;
    settings_load(&saved, sizeof saved);
    saved += 0x9e3779b9u;
    settings_save(&saved, sizeof saved);
    return saved ^ seed;
}

static uint32_t entropy(void)
{
#ifdef DINGHY_HOST
    return fm1_random_word() ^ fm1_ms;
#else
    return fm1_ticks() ^ fm1_ms << 16 ^ fm1_random_word();
#endif
}

/* ------------------------------------------------------------------------------------- knobs and pages */
static int32_t clampi(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }

/* a knob turned d detents: a bigger step when turned fast */
static uint8_t knob(uint8_t v, int32_t d, int32_t step, int32_t lo, int32_t hi)
{
    if (!d) return v;
    int32_t a = d < 0 ? -d : d;
    return (uint8_t)clampi(v + d * step * (a > 2 ? 2 : 1), lo, hi);
}

static uint32_t knob_touched[4];                /* when each knob last moved (the dials light up) */

static void edit_page(const int32_t s[4], uint8_t home_held)
{
    part_t *p = &parts[selected];
    switch (page) {
    case P_HOME:
        for (uint32_t t = 0; t < PARTS; t++) {
            if (home_held) parts[t].pan = (int8_t)clampi(parts[t].pan + s[t] * 4, -64, 64);
            else parts[t].level = knob(parts[t].level, s[t], 3, 0, 127);
        }
        break;
    case P_EDIT:
        for (uint32_t i = 0; i < 4; i++) p->p[i] = knob(p->p[i], s[i], 3, 0, 127);
        break;
    case P_ENV:
        for (uint32_t i = 0; i < 4; i++) p->env[i] = knob(p->env[i], s[i], 3, 0, 127);
        break;
    case P_LFO:
        p->lfo[K_RATE] = knob(p->lfo[K_RATE], s[0], 2, 0, 127);
        p->lfo[K_DEPTH] = knob(p->lfo[K_DEPTH], s[1], 3, 0, 127);
        p->lfo[K_SHAPE] = (uint8_t)clampi(p->lfo[K_SHAPE] + s[2], 0, LFO_SHAPES - 1);
        p->lfo[K_TARGET] = (uint8_t)clampi(p->lfo[K_TARGET] + s[3], 0, LFO_TARGETS - 1);
        break;
    case P_ARP:
        arp.pattern = (uint8_t)clampi(arp.pattern + s[0], 0, ARP_PATTERNS - 1);
        if (s[1]) arp_set_rate((uint8_t)clampi(arp.rate + s[1], 0, DIVS - 1));
        arp.range = (uint8_t)clampi(arp.range + s[2], 0, 3);
        arp.wander = knob(arp.wander, s[3], 4, 0, 127);
        break;
    case P_SEQ:
        for (uint32_t t = 0; t < PARTS; t++) {
            if (!s[t]) continue;
            tracks[t].length = (uint8_t)clampi(tracks[t].length + s[t], 0, STEPS);
            if (!tracks[t].length && players[t].on) { note_off(player_owner(t)); players[t].on = 0; }
            tracks_version++;
        }
        break;
    case P_SCALE:
        if (s[0]) { scale_now.scale = (uint8_t)clampi(scale_now.scale + s[0], 0, SCALE_COUNT - 1); scale_now.mode = 0; }
        if (s[1]) scale_now.mode = (uint8_t)clampi(scale_now.mode + s[1], 0, scale_now.size - 1);
        scale_now.gravity = knob(scale_now.gravity, s[2], 4, 0, 127);
        scale_now.truth = knob(scale_now.truth, s[3], 4, 0, 127);
        scale_update();
        break;
    case P_FX:
        fx.echo = knob(fx.echo, s[0], 3, 0, 127);
        fx.hall = knob(fx.hall, s[1], 3, 0, 127);
        fx.chorus = knob(fx.chorus, s[2], 3, 0, 127);
        fx.dust = knob(fx.dust, s[3], 3, 0, 127);
        break;
    case P_GLO:
        glo.bpm = (uint8_t)clampi(glo.bpm + s[0], 40, 240);
        glo.swing = (uint8_t)clampi(glo.swing + s[1], 50, 75);
        glo.ch = (uint8_t)clampi(glo.ch + s[2], 0, 16);
        if (s[3]) { glo.tune = (int8_t)clampi(glo.tune + s[3], -50, 50); tuning_q8 = glo.tune * 256 / 100; }
        break;
    }
}

static void dice_page(uint8_t p)
{
    switch (p) {
    case P_EDIT: dice_edit(selected); break;
    case P_ENV: dice_env(selected); break;
    case P_LFO: dice_lfo(selected); break;
    case P_ARP: dice_arp(); break;
    case P_SEQ: dice_lengths(); tracks_version++; break;
    case P_SCALE: dice_scale(); break;
    case P_FX: dice_fx(); break;
    default: break;
    }
}

static void fx_update(void)
{
    static uint32_t previous = ~0u, previous_dust = ~0u;
    uint32_t bpm = external_clock() && external_bpm_value >= 30 && external_bpm_value <= 300 ? external_bpm_value : glo.bpm;
    uint32_t state = bpm | (uint32_t)fx.echo << 9 | (uint32_t)fx.hall << 16 | (uint32_t)fx.chorus << 23;
    if (state == previous && fx.dust == previous_dust) return;
    previous = state;
    previous_dust = fx.dust;
    uint8_t echo = (uint8_t)(fx.echo * 100u / 127u);
    /* ECHO: a dotted eighth; the knob brings in the echo, then more and more repeats */
    delay_configure(echo != 0, delay_tempo_samples(bpm, 18), (uint8_t)(25u + echo * 45u / 100u), echo);
    hall_configure(fx.hall != 0, 74, 30, (uint8_t)(fx.hall * 100u / 127u));
    chorus_amount = (uint8_t)(fx.chorus * 100u / 127u);
    dust_amount = (uint8_t)(fx.dust * 100u / 127u);
    gate_bpm = bpm;
}

static void parts_update(void)                  /* the knobs -> the numbers the sound uses (cheap: every loop) */
{
    for (uint32_t p = 0; p < PARTS; p++) part_update(p);
}

/* the pictures */
#include "stage.h"

static uint8_t button_page(uint32_t button)
{
    switch (button) {
    case BTN_HOME: return P_HOME;
    case BTN_EDIT: return P_EDIT;
    case BTN_ENV: return P_ENV;
    case BTN_LFO: return P_LFO;
    case BTN_ARP: return P_ARP;
    case BTN_SEQ: return P_SEQ;
    case BTN_SEL: return P_SCALE;
    case BTN_FX: return P_FX;
    case BTN_GLO: return P_GLO;
    default: return 0xFF;
    }
}

/* ----------------------------------------------------------------------------------------------- the loop */
static void app_main(void)
{
    uint32_t held_since[16] = {0};
    uint16_t long_done = 0, was_on_page = 0;
    uint8_t stirred = 0;

    rnd_state = boot_seed() | 1u;
    scenes_scan();
    scene_random();
    for (uint32_t p = 0; p < PARTS; p++) part_rt[p].lfo_rnd = 0x9e3779b9u * (p + 1u);
    tuning_q8 = 0;
    parts_update();
    fx_update();

    stage_boot();
    clk.last_ms = fm1_ms;
    idle_ok = 1;
    for (;;) {
        platform_poll();
        uint32_t now = fm1_ms, rel;
        uint32_t b = fm1_input_edges(&rel);
        /* The first touch throws the dice again with the exact moment it came: no two power-ons alike, even in
         * the emulator, whose random numbers repeat. */
        if (!stirred && (b || fm1_in.notes)) {
            stirred = 1;
            rnd_state ^= entropy() * 2654435761u;
            if (!clk.running && scene_now < 0) scene_random();
        }
        uint32_t held = fm1_in.buttons;

        /* ---- buttons: a page on press, the dice when held, the rest on release */
        for (uint32_t i = 0; i < 14; i++) {
            uint32_t bit = 1u << i;
            if (b & bit) {
                held_since[i] = now;
                long_done &= (uint16_t)~bit;
                uint8_t to = button_page(i);
                if (to != 0xFF) {
                    if (page == to) was_on_page |= (uint16_t)bit; else was_on_page &= (uint16_t)~bit;
                    page = to;
                }
                if (i == BTN_PLAY) { if (clk.running) transport_stop(); else transport_start(); }
                if (i == BTN_OCT_DOWN && scale_now.octave > -3) scale_now.octave--;
                if (i == BTN_OCT_UP && scale_now.octave < 3) scale_now.octave++;
            }
            if ((held & bit) && !(long_done & bit) && now - held_since[i] >= LONG_MS && !(b & bit)) {
                long_done |= (uint16_t)bit;
                uint8_t to = button_page(i);
                if (to != 0xFF && to != P_HOME && to != P_GLO) { dice_page(to); stage_dice(); }
                if (i == BTN_REC) {             /* hold REC: the selected track is wiped */
                    if (players[selected].on) { note_off(player_owner(selected)); players[selected].on = 0; }
                    track_clear(selected);
                    rec_on = 0;
                    stage_wipe();
                }
                if (i == BTN_SAVE && scene_now >= 0) { scene_erase((uint32_t)scene_now); stage_overlay(OV_SLOTS); }
            }
            if ((rel & bit) && !(long_done & bit)) {
                if (i == BTN_ARP && (was_on_page & bit)) {
                    arp.on = !arp.on;
                    if (!arp.on) { if (players[PLAYER_ARP].on) note_off(OWNER_ARP); players[PLAYER_ARP].on = 0; }
                    else if (!clk.running && arp.n) transport_start();
                }
                if (i == BTN_REC) {
                    rec_on = !rec_on;
                    if (rec_on && !clk.running) transport_start();
                    if (rec_on && !tracks[selected].length) tracks[selected].length = 16;
                }
                if (i == BTN_SAVE) {
                    uint32_t slot = 0;
                    if (scene_now >= 0) slot = (uint32_t)scene_now;
                    else { while (slot < SCENES - 1u && (scene_saved >> slot & 1u)) slot++; }
                    if (!scene_save(slot)) { scene_now = (int8_t)slot; stage_overlay(OV_SAVE); }
                }
            }
        }

        /* ---- encoders: SELECT the part, ALGORITHM its algorithm, PRESETS the scene */
        int32_t e = fm1_enc_take(ENC_SELECT);
        if (e) {
            uint8_t next = (uint8_t)clampi(selected + e, 0, PARTS - 1);
            if (next != selected) {
                if (players[PLAYER_ARP].on) { note_off(OWNER_ARP); players[PLAYER_ARP].on = 0; }
                selected = next;
                stage_overlay(OV_SELECT);
            }
        }
        e = fm1_enc_take(ENC_ALGORITHM);
        if (e) {
            parts[selected].alg = (uint8_t)clampi(parts[selected].alg + e, 0, ALGS - 1);
            stage_overlay(OV_ALG);
        }
        e = fm1_enc_take(ENC_PRESETS);
        if (e) {
            int32_t s = scene_now < 0 ? (e > 0 ? 0 : (int32_t)SCENES - 1) : clampi(scene_now + e, 0, SCENES - 1);
            if (s != scene_now) {
                scene_now = (int8_t)s;
                scene_goto((uint32_t)s);
            }
            stage_overlay(OV_SLOTS);
        }
        int32_t steps[4];
        for (uint32_t k = 0; k < 4; k++) {
            steps[k] = fm1_enc_take(ENC_KNOB1 + k);
            if (steps[k]) knob_touched[k] = now;
        }
        if (steps[0] || steps[1] || steps[2] || steps[3]) edit_page(steps, (held & (1u << BTN_HOME)) != 0);

        keys_service();
        midi_in();
        clock_service();
        parts_update();
        fx_update();

        /* ---- LEDs */
        uint8_t root = scale_root_pc();
        for (uint32_t k = 0; k < 27u; k++) {
            uint32_t pc = (uint32_t)(KEY0 + k + 120 - root) % 12u;
            uint8_t in = (uint8_t)scale_contains_pc((uint8_t)pc);
            uint8_t on = (fm1_in.notes >> k & 1u) != 0;
            if (page == P_SCALE) on = on || pc == 0;
            fm1_led_key(KEY_LED(k), on);
            fm1_led_key_glow(KEY_LED(k), in);
        }
        fm1_led_key(BTN_OCT_DOWN, scale_now.octave < 0);
        fm1_led_key(BTN_OCT_UP, scale_now.octave > 0);
        fm1_led_key(BTN_PLAY, clk.running && (clk.tick / 48u) % 2u == 0);
        fm1_led_key(BTN_REC, rec_on && (now / 200u) % 2u == 0);
        fm1_led_key(BTN_ARP, page == P_ARP || arp.on);
        fm1_led_key(BTN_SEQ, page == P_SEQ);
        fm1_led_key(BTN_HOME, page == P_HOME);
        fm1_led_key(BTN_SEL, page == P_SCALE);
        fm1_led_key(BTN_EDIT, page == P_EDIT);
        fm1_led_key(BTN_ENV, page == P_ENV);
        fm1_led_key(BTN_LFO, page == P_LFO);
        fm1_led_key(BTN_FX, page == P_FX || fx.echo || fx.hall || fx.chorus || fx.dust);
        fm1_led_key(BTN_GLO, page == P_GLO);
        fm1_led_key(BTN_SAVE, scene_now >= 0 && (scene_saved >> scene_now & 1u));

        /* ---- the screen */
        stage_service(now);
    }
}
