/* SPDX-License-Identifier: GPL-3.0-only */
/* WHYWHYWHY's effects run in parallel: the dry sound passes untouched, and the output with all three effects
 * is the dry sound plus what each effect adds on its own (none feeds another). */
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#define DINGHY_HOST 1
#define FS 44100u
#include "effects.h"

#define N 8192u
static void reset(void)
{
    memset(&delay_state, 0, sizeof delay_state);
    memset(&hall, 0, sizeof hall);
    memset(&chorus, 0, sizeof chorus);
    memset(delay_ring, 0, sizeof delay_ring);
    memset(hall_tank, 0, sizeof hall_tank);
    memset(hall_diff, 0, sizeof hall_diff);
    memset(chorus_ring, 0, sizeof chorus_ring);
}

/* an impulse through the effects at these amounts: the result in out[] (stereo) */
static void render(int32_t *out, uint8_t d, uint8_t h, uint8_t c)
{
    reset();
    delay_configure(d != 0, 2000, 40, d);
    hall_configure(h != 0, 70, 35, h);
    chorus_amount = c;
    int32_t settle[2 * 256] = {0};
    for (int i = 0; i < 4; i++) effects_process(settle, 256);   /* let the amounts fade in */
    reset();
    delay_state.mix = d * FX12_GAIN_ONE / 100; hall.mix = h * FX12_GAIN_ONE / 100; chorus.mix = c * FX12_GAIN_ONE / 100;
    memset(out, 0, 2 * N * sizeof *out);
    for (uint32_t f = 0; f < 64; f++) out[2 * f] = out[2 * f + 1] = f < 32 ? 12000 : -12000;
    for (uint32_t at = 0; at < N; at += 256) effects_process(out + 2 * at, 256);
}

static int32_t all[2 * N], dry[2 * N], d[2 * N], h[2 * N], c[2 * N];

int main(void)
{
    render(dry, 0, 0, 0);
    for (uint32_t f = 0; f < N; f++) {                   /* nothing on: the sound passes untouched */
        int32_t want = f < 64 ? (f < 32 ? 12000 : -12000) : 0;
        assert(dry[2 * f] == want && dry[2 * f + 1] == want);
    }
    render(d, 40, 0, 0);
    render(h, 0, 40, 0);
    render(c, 0, 0, 40);
    render(all, 40, 40, 40);
    int64_t wet_d = 0, wet_h = 0, wet_c = 0, worst = 0;
    for (uint32_t i = 0; i < 2 * N; i++) {
        int32_t sum = dry[i] + (d[i] - dry[i]) + (h[i] - dry[i]) + (c[i] - dry[i]);
        int32_t diff = all[i] - sum;
        if (diff < 0) diff = -diff;
        if (diff > worst) worst = diff;
        wet_d += (d[i] - dry[i]) * (int64_t)(d[i] - dry[i]);
        wet_h += (h[i] - dry[i]) * (int64_t)(h[i] - dry[i]);
        wet_c += (c[i] - dry[i]) * (int64_t)(c[i] - dry[i]);
    }
    assert(wet_d > 0 && wet_h > 0 && wet_c > 0);   /* each effect adds something */
    assert(worst <= 2);                             /* together = the sum of each alone (rounding only) */
    printf("parallel effects: each adds its own wet sound, together they only add up (worst %lld)\n", (long long)worst);
    return 0;
}
