/* SPDX-License-Identifier: GPL-3.0-only */
/* A stereo chorus: two copies of the sound, each a few milliseconds late, the lateness slowly swaying in
 * opposite directions left and right. 1024 samples of memory (23 ms) at 44.1 kHz. */
#define CHORUS_SIZE 1024u
#define CHORUS_CENTRE (400u << 8)               /* 9 ms, in 1/256 samples */
#define CHORUS_DEPTH (150u << 8)                /* +- 3.4 ms */
#ifdef DINGHY_HOST
static int16_t chorus_ring[CHORUS_SIZE];
#else
static int16_t chorus_ring[CHORUS_SIZE] __attribute__((section(".pool")));
#endif
static volatile uint8_t chorus_amount;          /* 0..100, from the main loop */
static struct {
    uint32_t head, phase;
    int32_t mix;
    uint8_t running;
} chorus;

static int32_t chorus_tap(uint32_t delay)       /* delay in 1/256 samples */
{
    uint32_t whole = delay >> 8, frac = delay & 255u;
    uint32_t a = (chorus.head - whole) & (CHORUS_SIZE - 1u), b = (a - 1u) & (CHORUS_SIZE - 1u);
    return (chorus_ring[a] * (int32_t)(256u - frac) + chorus_ring[b] * (int32_t)frac) >> 8;
}

static uint8_t chorus_begin_block(void)
{
    int32_t target = chorus_amount * FX12_GAIN_ONE / 100;
    if (!target && !chorus.mix) {
        if (chorus.running) {                   /* forget the old sound, cheaply */
            chorus.running = 0;
            for (uint32_t i = 0; i < CHORUS_SIZE; i++) chorus_ring[i] = 0;
        }
        return 0;
    }
    chorus.running = 1;
    return 1;
}

static inline void chorus_render(int32_t dry, int32_t *left, int32_t *right)
{
    int32_t target = chorus_amount * FX12_GAIN_ONE / 100;
    chorus.head = (chorus.head + 1u) & (CHORUS_SIZE - 1u);
    chorus_ring[chorus.head] = (int16_t)(dry < -32768 ? -32768 : dry > 32767 ? 32767 : dry);
    chorus.phase += 58000u;                     /* the sway: about 0.6 Hz */
    uint32_t p = chorus.phase >> 16;            /* 0..65535: a triangle */
    int32_t tri = p < 32768u ? (int32_t)p * 2 - 32768 : 32767 - ((int32_t)p - 32768) * 2;   /* -32768..32767 */
    int32_t swing = tri * (int32_t)(CHORUS_DEPTH >> 8) / 32768 * 256;
    *left = chorus_tap(CHORUS_CENTRE + (uint32_t)swing);
    *right = chorus_tap(CHORUS_CENTRE - (uint32_t)swing);
    chorus.mix = fx12_slew_mix(chorus.mix, target);
}
