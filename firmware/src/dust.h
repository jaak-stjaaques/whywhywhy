/* SPDX-License-Identifier: GPL-3.0-only */
/* DUST: WHYWHYWHY's fourth effect, in parallel with the others. A coarse copy of the dry sound (fewer samples, fewer
 * bits, as if printed on cheap paper) and a few crackles of dust, more of both as the knob goes up. */
static volatile uint8_t dust_amount;            /* 0..100, from the main loop */
static struct {
    int32_t held, mix, hp, crackle_l, crackle_r, follow;
    uint32_t count, rnd;
} dust = {.rnd = 0x51ed2701u};

static uint8_t dust_begin_block(void)
{
    if (!dust_amount && !dust.mix) return 0;
    return 1;
}

static inline void dust_render(int32_t dry, int32_t *left, int32_t *right)
{
    int32_t target = dust_amount * FX12_GAIN_ONE / 100;
    uint32_t hold = 3u + dust_amount / 9u;      /* 3 .. 14 samples: 15 kHz .. 3 kHz */
    if (++dust.count >= hold) {
        dust.count = 0;
        int32_t x = dry < -32768 ? -32768 : dry > 32767 ? 32767 : dry;
        dust.held = x & ~0x3FF;                 /* 6 bits */
    }
    dust.hp += (dust.held - dust.hp) >> 6;      /* (no rumble) */
    int32_t x = dust.held - dust.hp;
    int32_t level = dry < 0 ? -dry : dry;      /* the dust only crackles while something plays */
    dust.follow += (level - dust.follow) >> (level > dust.follow ? 6 : 12);
    dust.rnd = dust.rnd * 1664525u + 1013904223u;
    if ((dust.rnd >> 20) < (uint32_t)dust_amount / 8u && dust.follow > 64) {   /* a speck: a click, left or right */
        int32_t f = dust.follow > 8192 ? 8192 : dust.follow;
        int32_t pop = (((int32_t)((dust.rnd >> 4) & 0x1FFF) - 0x1000) * f) >> 13;
        if (dust.rnd & 8u) dust.crackle_l += pop * 3; else dust.crackle_r += pop * 3;
    }
    dust.crackle_l -= dust.crackle_l >> 3;
    dust.crackle_r -= dust.crackle_r >> 3;
    *left = x + dust.crackle_l;
    *right = x + dust.crackle_r;
    dust.mix = fx12_slew_mix(dust.mix, target);
}
