/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef LOOPHEADS_FX12_H
#define LOOPHEADS_FX12_H

#define FX12_RATE 20000u
#define FX12_GAIN_ONE 32768

typedef struct {
    uint32_t phase;
    int32_t filtered;
} fx12_clock_t;

static inline int32_t fx12_slew_mix(int32_t mix, int32_t target)
{
    int32_t delta = target - mix;
    return mix + (delta < -128 ? -128 : delta > 128 ? 128 : delta);
}

static inline int16_t fx12_quantize(int32_t sample)
{
    return (int16_t)(sample < -2048 ? -2048 : sample > 2047 ? 2047 : sample);
}

/* Run one step at `rate` (20 kHz for the delay, 10 kHz for the hall) when its sample time falls between two dry
 * frames. */
static inline int fx12_input_at(fx12_clock_t *clock, int32_t dry, int32_t *input, uint32_t rate)
{
    int32_t previous = clock->filtered;
    clock->filtered += (dry - clock->filtered) / 2;
    clock->phase += rate;
    if (clock->phase < FS) return 0;
    clock->phase -= FS;
    *input = clock->filtered + (previous - clock->filtered) *
             (int32_t)clock->phase / (int32_t)rate;
    return 1;
}

static inline int fx12_input(fx12_clock_t *clock, int32_t dry, int32_t *input)
{
    return fx12_input_at(clock, dry, input, FX12_RATE);
}

static inline int32_t fx12_output(const fx12_clock_t *clock, int32_t previous, int32_t sample)
{
    return 16 * (previous + (sample - previous) * (int32_t)clock->phase / (int32_t)FS);
}

#endif
