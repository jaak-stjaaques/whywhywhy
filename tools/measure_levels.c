/* SPDX-License-Identifier: GPL-3.0-only */
/* How loud is each of WHYWHYWHY's algorithms? A note at three octaves, with the knobs at a few places: the RMS and the
 * peak of its first half second. Used to set alg_gain[] in firmware/src/voices.h.
 *   cc -O2 -Ifirmware/src -o build/measure tools/measure_levels.c && build/measure [wav-prefix] */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#define FS 44100u
static void fm1_irq_off(void) {}
static void fm1_irq_on(void) {}
#include "voices.h"

static const char *names[ALGS] = {"GLASS", "BIRCH", "DOTS", "PULSE", "FOLD", "AIR"};

static double measure(uint8_t alg, const uint8_t p[4], uint8_t note, double *peak_out, FILE *wav)
{
    memset(voices, 0, sizeof voices);
    memset(part_rt, 0, sizeof part_rt);
    part_t *s = &parts[0];
    s->alg = alg;
    memcpy(s->p, p, 4);
    s->env[0] = 0; s->env[1] = 70; s->env[2] = 90; s->env[3] = 40;
    s->lfo[K_DEPTH] = 0;
    s->level = 127;
    s->pan = 0;
    part_update(0);
    voice_start(1, 0, note * 256, 100);
    double sum = 0, peak = 0;
    uint32_t n = FS / 2u;
    static int32_t out[2 * 256];
    for (uint32_t done = 0; done < n; done += 256) {
        memset(out, 0, sizeof out);
        voices_render(out, 256);
        for (uint32_t i = 0; i < 256; i++) {
            double x = out[2 * i];
            sum += x * x;
            if (fabs(x) > peak) peak = fabs(x);
            if (wav) { int16_t y = (int16_t)(x > 32767 ? 32767 : x < -32768 ? -32768 : x); fwrite(&y, 2, 1, wav); }
        }
    }
    *peak_out = peak;
    return sqrt(sum / n);
}

int main(int argc, char **argv)
{
    static const uint8_t knobs[3][4] = {{30, 30, 30, 30}, {64, 64, 64, 64}, {110, 100, 100, 100}};
    FILE *wav = 0;
    if (argc > 1) {
        char name[256];
        snprintf(name, sizeof name, "%s.raw", argv[1]);
        wav = fopen(name, "wb");
    }
    for (uint32_t a = 0; a < ALGS; a++) {
        double total = 0, worst = 0;
        printf("%-6s", names[a]);
        for (uint32_t k = 0; k < 3; k++)
            for (uint32_t o = 0; o < 3; o++) {
                double peak;
                double rms = measure((uint8_t)a, knobs[k], (uint8_t)(48 + 12 * o), &peak, wav);
                total += rms;
                if (peak > worst) worst = peak;
                printf(" %5.0f", rms);
            }
        printf("   mean %5.0f  peak %5.0f  gain for 3000: %4.0f\n", total / 9, worst, 4096.0 * 3000 / (total / 9));
    }
    if (wav) fclose(wav);
    return 0;
}
