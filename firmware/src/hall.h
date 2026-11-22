/* SPDX-License-Identifier: GPL-3.0-only */
/* A stereo hall with a 10 kHz, signed 12-bit feedback network (LOOPHEADS' 20 kHz hall at half the rate: half
 * the work and half the memory for the same room, its tail a little darker). */
#include "fx12.h"
#define HALL_ENABLED (1u << 31)
#define HALL_RATE 10000u

enum { HALL_D1 = 541, HALL_D2 = 661, HALL_D3 = 787, HALL_D4 = 937 };   /* (primes: half of 1069, 1327, 1571, 1873) */
enum { HALL_AP1 = 43, HALL_AP2 = 73, HALL_AP3 = 97 };
static const uint16_t hall_tank_size[] = {HALL_D1, HALL_D2, HALL_D3, HALL_D4};
static const uint16_t hall_tank_offset[] = {0, HALL_D1, HALL_D1 + HALL_D2, HALL_D1 + HALL_D2 + HALL_D3};
static const uint16_t hall_diff_size[] = {HALL_AP1, HALL_AP2, HALL_AP3};
static const uint16_t hall_diff_offset[] = {0, HALL_AP1, HALL_AP1 + HALL_AP2};

#ifndef DINGHY_HOST
#define HALL_POOL __attribute__((section(".pool")))
#else
#define HALL_POOL
#endif
static int16_t hall_tank[HALL_D1 + HALL_D2 + HALL_D3 + HALL_D4] HALL_POOL;
static int16_t hall_diff[HALL_AP1 + HALL_AP2 + HALL_AP3] HALL_POOL;
#undef HALL_POOL

static volatile uint32_t hall_command;
static struct {
    uint32_t age;
    fx12_clock_t clock;
    uint16_t tank_head[4], diff_head[3];
    int32_t lp[4], mix, target_mix, feedback, damping;
    int32_t left, right, prev_left, prev_right, smooth;
    uint8_t running;
} hall;

static void hall_configure(uint8_t on, uint8_t decay, uint8_t damping, uint8_t mix)
{
    hall_command = (on ? HALL_ENABLED : 0) | (uint32_t)decay |
                   (uint32_t)damping << 8 | (uint32_t)mix << 16;
}

static int32_t hall_diffuse(int32_t signal)
{
    for (uint32_t i = 0; i < 3; i++) {
        uint16_t head = hall.diff_head[i];
        uint16_t size = hall_diff_size[i];
        int16_t *cell = &hall_diff[hall_diff_offset[i] + head];
        int32_t tap = hall.age >= size ? *cell : 0;
        int32_t spread = tap - signal * 5 / 8;
        *cell = fx12_quantize(signal + spread * 5 / 8);
        signal = fx12_quantize(spread);
        hall.diff_head[i] = head + 1u == size ? 0 : head + 1u;
    }
    return signal;
}

static void hall_step(int32_t input, int32_t feedback, int32_t damping)
{
    int32_t signal = hall_diffuse(fx12_quantize(input / 16)) / 2;
    int32_t tap[4], damped[4];
    for (uint32_t i = 0; i < 4; i++) {
        tap[i] = hall.age >= hall_tank_size[i] ? hall_tank[hall_tank_offset[i] + hall.tank_head[i]] : 0;
        hall.lp[i] += (tap[i] * 256 - hall.lp[i]) * damping / 256;
        damped[i] = hall.lp[i] / 256;
    }

    /* A normalized Hadamard matrix spreads the four returns back through the room. */
    int32_t a = damped[0] + damped[1], b = damped[0] - damped[1];
    int32_t c = damped[2] + damped[3], d = damped[2] - damped[3];
    int32_t returns[] = {a + c, b + d, a - c, b - d};
    for (uint32_t i = 0; i < 4; i++) {
        uint16_t head = hall.tank_head[i];
        int32_t send = i == 2 ? -signal : signal;
        hall_tank[hall_tank_offset[i] + head] = fx12_quantize(send + returns[i] * feedback / (2 * FX12_GAIN_ONE));
        hall.tank_head[i] = head + 1u == hall_tank_size[i] ? 0 : head + 1u;
    }
    if (hall.age < HALL_D4) hall.age++;

    hall.prev_left = hall.left;
    hall.prev_right = hall.right;
    hall.left = fx12_quantize((tap[0] + tap[1] - tap[2] - tap[3]) / 2);
    hall.right = fx12_quantize((tap[0] - tap[1] + tap[2] - tap[3]) / 2);
}

static uint8_t hall_begin_block(void)
{
    uint32_t command = hall_command;
    uint8_t enabled = (command & HALL_ENABLED) != 0;
    if (!enabled && !hall.mix) {
        if (hall.running) {
            /* Restart the cursors and age counters without clearing the delay lines. */
            hall.running = 0;
            hall.age = 0;
            hall.clock = (fx12_clock_t){0};
            hall.left = hall.right = hall.prev_left = hall.prev_right = hall.smooth = 0;
            for (uint32_t i = 0; i < 3; i++) hall.diff_head[i] = 0;
            for (uint32_t i = 0; i < 4; i++) hall.tank_head[i] = 0, hall.lp[i] = 0;
        }
        return 0;
    }
    hall.running = 1;
    hall.feedback = 21000 + (int32_t)(command & 255u) * 100;
    hall.damping = 256 - (int32_t)((command >> 8) & 255u) * 2;
    hall.target_mix = enabled ? (int32_t)((command >> 16) & 255u) * FX12_GAIN_ONE / 100 : 0;
    return 1;
}

static inline void hall_render(int32_t dry, int32_t *left, int32_t *right)
{
    int32_t input;
    hall.smooth += (dry - hall.smooth) / 4;     /* a little darker going in: nothing above 5 kHz folds back */
    if (fx12_input_at(&hall.clock, hall.smooth, &input, HALL_RATE)) hall_step(input, hall.feedback, hall.damping);

    hall.mix = fx12_slew_mix(hall.mix, hall.target_mix);
    if (hall.mix) {
        *left = fx12_output(&hall.clock, hall.prev_left, hall.left);
        *right = fx12_output(&hall.clock, hall.prev_right, hall.right);
    }
}
