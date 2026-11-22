/* SPDX-License-Identifier: GPL-3.0-only */
/* Shared 12-bit delay at 20 kHz. */
#include "fx12.h"
#define DELAY_CAPACITY (3u * FX12_RATE)
#define DELAY_TIME_MASK 0x1ffffu
#define DELAY_ENABLED (1u << 31)
#define DELAY_FADE_SAMPLES 256u

_Static_assert(DELAY_CAPACITY <= DELAY_TIME_MASK, "Delay time must fit its audio command");
#ifdef DINGHY_HOST
static int16_t delay_ring[DELAY_CAPACITY];
#else
static int16_t delay_ring[DELAY_CAPACITY] __attribute__((section(".pool")));
#endif

/* The main loop publishes all settings together for the audio interrupt. */
static volatile uint32_t delay_command;
static struct {
    uint32_t head, valid, time, old_time, fade;
    fx12_clock_t clock;
    int32_t previous_wet, wet, mix, target_mix, feedback;
} delay_state;

static uint32_t delay_tempo_samples(uint32_t bpm, uint8_t clocks_per_echo)
{
    uint32_t clocks_per_minute = bpm * 24u;
    return (FX12_RATE * 60u * clocks_per_echo + clocks_per_minute / 2u) / clocks_per_minute;
}

static void delay_configure(uint8_t on, uint32_t time, uint8_t feedback, uint8_t mix)
{
    delay_command = (on ? DELAY_ENABLED : 0) | time |
                    (uint32_t)feedback << 17 | (uint32_t)mix << 24;
}

static int32_t delay_tap(uint32_t time)
{
    if (time > delay_state.valid) return 0;
    uint32_t head = delay_state.head;
    uint32_t index = head >= time ? head - time : head + DELAY_CAPACITY - time;
    return delay_ring[index];
}

static int32_t delay_read(void)
{
    int32_t wet = delay_tap(delay_state.time);
    if (delay_state.fade) {
        int32_t old = delay_tap(delay_state.old_time);
        wet += (old - wet) * (int32_t)delay_state.fade / (int32_t)DELAY_FADE_SAMPLES;
        delay_state.fade--;
    }
    return wet;
}

static uint8_t delay_begin_block(void)
{
    uint32_t command = delay_command;
    uint8_t enabled = (command & DELAY_ENABLED) != 0;
    if (!enabled && !delay_state.mix) {
        if (delay_state.time) {
            /* Discard history without clearing the large buffer in the interrupt. */
            delay_state.time = delay_state.valid = delay_state.fade = 0;
            delay_state.clock = (fx12_clock_t){0};
            delay_state.previous_wet = delay_state.wet = 0;
        }
        return 0;
    }

    uint32_t time = command & DELAY_TIME_MASK;
    if (!delay_state.time) delay_state.time = time;
    if (!delay_state.fade && delay_state.time != time) {
        delay_state.old_time = delay_state.time;
        delay_state.time = time;
        delay_state.fade = DELAY_FADE_SAMPLES;
    }

    delay_state.feedback = (int32_t)((command >> 17) & 127u) * FX12_GAIN_ONE / 100;
    delay_state.target_mix = enabled ? (int32_t)((command >> 24) & 127u) * FX12_GAIN_ONE / 100 : 0;
    return 1;
}

static inline int32_t delay_render(int32_t dry)
{
    int32_t input;
    if (fx12_input(&delay_state.clock, dry, &input)) {
        delay_state.previous_wet = delay_state.wet;
        delay_state.wet = delay_read();
        int32_t write = input / 16 + delay_state.wet * delay_state.feedback / FX12_GAIN_ONE;
        delay_ring[delay_state.head] = fx12_quantize(write);
        if (++delay_state.head == DELAY_CAPACITY) delay_state.head = 0;
        if (delay_state.valid < DELAY_CAPACITY) delay_state.valid++;
    }
    delay_state.mix = fx12_slew_mix(delay_state.mix, delay_state.target_mix);
    return delay_state.mix ? fx12_output(&delay_state.clock, delay_state.previous_wet, delay_state.wet) : 0;
}
