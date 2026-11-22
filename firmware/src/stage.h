/* SPDX-License-Identifier: GPL-3.0-only */
/* WHYWHYWHY's pictures. No words: a sheet of paper, colours, dots, rings, curves. What you hear moves them.
 *
 *   y   0.. 29  the header: the page's sign, the four buoys (the selected one ringed), record, play, octave, battery
 *   y  30..189  the stage: 240 x 160, drawn into one of two buffers while the other goes to the screen
 *   y 190..239  the knob band: four dials, one per knob, under the knobs
 *
 * Everything is drawn with soft edges (coverage in 1/16 px) and alpha, in RGB565. Coordinates in the drawing
 * calls are in 1/16 px ("Q4"); angles are turns x 65536. */

#define HEADER_H 30
#define STAGE_Y 30
#define STAGE_W 240
#define STAGE_H 160
#define BAND_Y 190
#define BAND_H 50
#define FRAME_MS 40u

#ifdef DINGHY_HOST
static uint16_t stage_px[2][STAGE_W * STAGE_H];
#else
static uint16_t stage_px[2][STAGE_W * STAGE_H] __attribute__((section(".pool")));
#endif

/* ------------------------------------------------------------------------------------------- colours */
#define PAPER RGB(243, 238, 226)
#define PAPER2 RGB(231, 224, 208)
#define LINE RGB(212, 204, 188)
#define GREY RGB(160, 154, 143)
#define INK RGB(36, 35, 46)
#define CREAM RGB(252, 249, 240)
#define VERMILION RGB(232, 74, 48)
#define SAFFRON RGB(246, 182, 28)
#define COBALT RGB(36, 88, 198)
#define JADE RGB(24, 150, 110)
#define PINK RGB(240, 118, 160)
#define VIOLET RGB(124, 86, 178)
#define SKY RGB(110, 182, 232)
#define TEAL RGB(30, 160, 170)
#define ORANGE RGB(248, 122, 24)
#define LIME RGB(160, 200, 60)
#define BIRCHC RGB(214, 170, 104)
#define BROWN RGB(140, 92, 56)
#define LILAC RGB(186, 166, 226)
#define RED RGB(222, 34, 44)

static const uint16_t part_color[PARTS] = {VERMILION, SAFFRON, COBALT, JADE};
/* the twelve keys as a colour circle: C red, round through yellow, green, blue, violet, back to red */
static const uint16_t pc_color[12] = {
    RGB(226, 52, 52), RGB(240, 110, 60), RGB(246, 152, 36), RGB(244, 196, 40), RGB(214, 214, 52), RGB(140, 196, 64),
    RGB(46, 168, 96), RGB(30, 160, 160), RGB(52, 140, 214), RGB(58, 86, 196), RGB(124, 76, 184), RGB(200, 64, 150)
};

/* ------------------------------------------------------------------------------------- the surface */
static uint16_t *sf_px;
static int32_t sf_w, sf_h;

static void sf_target(uint16_t *px, int32_t w, int32_t h) { sf_px = px; sf_w = w; sf_h = h; }

static inline uint16_t mix565(uint16_t a, uint16_t b, uint32_t t)   /* t 0..256: a -> b */
{
    uint32_t x = (a | (uint32_t)a << 16) & 0x07E0F81Fu, y = (b | (uint32_t)b << 16) & 0x07E0F81Fu;
    uint32_t t5 = t >> 3;
    uint32_t z = ((x * (32u - t5) + y * t5) >> 5) & 0x07E0F81Fu;
    return (uint16_t)(z | z >> 16);
}

static inline void px_blend(int32_t x, int32_t y, uint16_t c, uint32_t a)
{
    if ((uint32_t)x >= (uint32_t)sf_w || (uint32_t)y >= (uint32_t)sf_h || !a) return;
    uint16_t *p = &sf_px[y * sf_w + x];
    if (a >= 256u) { *p = swap16(c); return; }
    *p = swap16(mix565(swap16(*p), c, a));
}

static void sf_clear(uint16_t c)
{
    uint16_t s = swap16(c);
    for (int32_t i = 0; i < sf_w * sf_h; i++) sf_px[i] = s;
}

static void rect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c, uint32_t a)   /* in pixels */
{
    for (int32_t j = y; j < y + h; j++)
        for (int32_t i = x; i < x + w; i++) px_blend(i, j, c, a);
}

/* a disc, soft-edged: centre and radius in Q4 */
static void disc(int32_t cx, int32_t cy, int32_t r, uint16_t c, uint32_t a)
{
    if (r < 4 || !a) return;
    int32_t y0 = (cy - r) >> 4, y1 = (cy + r) >> 4, x0 = (cx - r) >> 4, x1 = (cx + r) >> 4;
    int32_t in2 = r > 8 ? (r - 8) * (r - 8) : 0, out2 = (r + 8) * (r + 8);
    if (y0 < 0) y0 = 0;
    if (y1 >= sf_h) y1 = sf_h - 1;
    if (x0 < 0) x0 = 0;
    if (x1 >= sf_w) x1 = sf_w - 1;
    for (int32_t y = y0; y <= y1; y++) {
        int32_t dy = y * 16 + 8 - cy, dy2 = dy * dy;
        for (int32_t x = x0; x <= x1; x++) {
            int32_t dx = x * 16 + 8 - cx, d2 = dx * dx + dy2;
            if (d2 >= out2) continue;
            if (d2 <= in2) { px_blend(x, y, c, a); continue; }
            int32_t cov = (r * r - d2) / (2 * r) + 8;           /* -8..8 + 8: 0..16 */
            cov = cov < 0 ? 0 : cov > 16 ? 16 : cov;
            px_blend(x, y, c, a * (uint32_t)cov / 16u);
        }
    }
}

/* a ring: radius r, width w (Q4) */
static void ring(int32_t cx, int32_t cy, int32_t r, int32_t w, uint16_t c, uint32_t a)
{
    if (r < 4 || !a) return;
    int32_t ro = r + w / 2, ri = r - w / 2;
    if (ri < 0) ri = 0;
    int32_t y0 = (cy - ro) >> 4, y1 = (cy + ro) >> 4, x0 = (cx - ro) >> 4, x1 = (cx + ro) >> 4;
    int32_t out2 = (ro + 8) * (ro + 8), in2 = ri > 8 ? (ri - 8) * (ri - 8) : -1;
    if (y0 < 0) y0 = 0;
    if (y1 >= sf_h) y1 = sf_h - 1;
    if (x0 < 0) x0 = 0;
    if (x1 >= sf_w) x1 = sf_w - 1;
    for (int32_t y = y0; y <= y1; y++) {
        int32_t dy = y * 16 + 8 - cy, dy2 = dy * dy;
        for (int32_t x = x0; x <= x1; x++) {
            int32_t dx = x * 16 + 8 - cx, d2 = dx * dx + dy2;
            if (d2 >= out2 || d2 <= in2) continue;
            int32_t co = (ro * ro - d2) / (2 * (ro ? ro : 1)) + 8;
            int32_t ci = ri ? (ri * ri - d2) / (2 * ri) + 8 : 0;
            co = co < 0 ? 0 : co > 16 ? 16 : co;
            ci = ci < 0 ? 0 : ci > 16 ? 16 : ci;
            int32_t cov = co - ci;
            if (cov > 0) px_blend(x, y, c, a * (uint32_t)cov / 16u);
        }
    }
}

/* a soft line, Q4 ends */
static void line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t c, uint32_t a)
{
    int32_t dx = x1 - x0, dy = y1 - y0;
    int32_t adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy;
    int32_t n = (adx > ady ? adx : ady) / 16 + 1;
    if (n > 600) n = 600;
    for (int32_t i = 0; i <= n; i++) {
        int32_t x = x0 + dx * i / n - 8, y = y0 + dy * i / n - 8;
        int32_t fx = x & 15, fy = y & 15, ix = x >> 4, iy = y >> 4;
        if (adx > ady) {
            px_blend(ix, iy, c, a * (uint32_t)(16 - fy) / 16u);
            px_blend(ix, iy + 1, c, a * (uint32_t)fy / 16u);
        } else {
            px_blend(ix, iy, c, a * (uint32_t)(16 - fx) / 16u);
            px_blend(ix + 1, iy, c, a * (uint32_t)fx / 16u);
        }
    }
}

/* a filled triangle (pixel centres; Q4 corners), hard edges */
static void tri(int32_t ax, int32_t ay, int32_t bx, int32_t by, int32_t cx, int32_t cy, uint16_t c, uint32_t a)
{
    int32_t minx = ax < bx ? (ax < cx ? ax : cx) : (bx < cx ? bx : cx);
    int32_t maxx = ax > bx ? (ax > cx ? ax : cx) : (bx > cx ? bx : cx);
    int32_t miny = ay < by ? (ay < cy ? ay : cy) : (by < cy ? by : cy);
    int32_t maxy = ay > by ? (ay > cy ? ay : cy) : (by > cy ? by : cy);
    int32_t area = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
    if (!area) return;
    minx >>= 4; maxx >>= 4; miny >>= 4; maxy >>= 4;
    if (minx < 0) minx = 0;
    if (miny < 0) miny = 0;
    if (maxx >= sf_w) maxx = sf_w - 1;
    if (maxy >= sf_h) maxy = sf_h - 1;
    for (int32_t y = miny; y <= maxy; y++) {
        int32_t py = y * 16 + 8;
        for (int32_t x = minx; x <= maxx; x++) {
            int32_t px = x * 16 + 8;
            int32_t w0 = (bx - ax) * (py - ay) - (by - ay) * (px - ax);
            int32_t w1 = (cx - bx) * (py - by) - (cy - by) * (px - bx);
            int32_t w2 = (ax - cx) * (py - cy) - (ay - cy) * (px - cx);
            if (area > 0 ? (w0 >= 0 && w1 >= 0 && w2 >= 0) : (w0 <= 0 && w1 <= 0 && w2 <= 0)) px_blend(x, y, c, a);
        }
    }
}

/* angles: turns x 65536 */
static inline int32_t cosq(uint32_t a) { return sin_q15((a + 16384u) << 16); }
static inline int32_t sinq(uint32_t a) { return sin_q15(a << 16); }
static inline int32_t polx(int32_t cx, int32_t r, uint32_t a) { return cx + (r * cosq(a) >> 15); }
static inline int32_t poly(int32_t cy, int32_t r, uint32_t a) { return cy + (r * sinq(a) >> 15); }
#define Q(v) ((int32_t)(v) * 16)
#define TOP 49152u                              /* twelve o'clock */

/* -------------------------------------------------------------------------------------- page signs
 * a small picture per page, in the header and nowhere else */
static const uint16_t page_color[PAGES] = {INK, 0, 0, 0, ORANGE, TEAL, VIOLET, PINK, INK};

static uint16_t page_tint(uint8_t p) { return page_color[p] ? page_color[p] : part_color[selected]; }

/* the sign of an algorithm, centre (cx, cy), size s (Q4) */
static void alg_sign(uint8_t alg, int32_t cx, int32_t cy, int32_t s, uint16_t c, uint32_t a)
{
    switch (alg) {
    case ALG_GLASS:                             /* a cut stone */
        for (uint32_t i = 0; i < 6; i++) {
            uint32_t a0 = i * 65536u / 6u + TOP, a1 = a0 + 65536u / 6u;
            line(polx(cx, s, a0), poly(cy, s, a0), polx(cx, s, a1), poly(cy, s, a1), c, a);
            if (i % 2u == 0) line(cx, cy, polx(cx, s, a0), poly(cy, s, a0), c, a / 2u);
        }
        break;
    case ALG_BIRCH:                             /* growth rings */
        ring(cx, cy, s, 18, c, a);
        ring(cx + s / 8, cy, s * 5 / 8, 16, c, a);
        disc(cx + s / 6, cy, s / 4, c, a);
        break;
    case ALG_DOTS:                              /* polka dots */
        for (int32_t j = -1; j <= 1; j++)
            for (int32_t i = -1; i <= 1; i++)
                if ((i + j) & 1 || (!i && !j)) disc(cx + i * s * 2 / 3, cy + j * s * 2 / 3, s / 4 + 4, c, a);
        break;
    case ALG_PULSE:                             /* a grating and its sine */
        for (int32_t i = -2; i <= 2; i++) line(cx + i * s / 3, cy - s, cx + i * s / 3, cy + s, c, i ? a / 2u : a);
        ring(cx, cy, s * 2 / 3, 16, c, a);
        break;
    case ALG_FOLD:                              /* a folded strip */
        for (int32_t i = 0; i < 4; i++) {
            int32_t xa = cx - s + i * s / 2, xb = xa + s / 2;
            int32_t ya = cy + (i & 1 ? -s / 2 : s / 2), yb = cy + (i & 1 ? s / 2 : -s / 2);
            line(xa, ya, xb, yb, c, a);
            line(xa, ya + 20, xb, yb + 20, c, a);
        }
        break;
    default:                                    /* air: a breath */
        for (int32_t k = 0; k < 2; k++) {
            int32_t px = cx - s, py = cy - s / 4 + k * s / 2;
            for (int32_t i = 1; i <= 12; i++) {
                int32_t x = cx - s + i * s / 6, y = cy - s / 4 + k * s / 2 + (s / 4 * sinq((uint32_t)(i * 65536 / 12 + k * 20000)) >> 15);
                line(px, py, x, y, c, a);
                px = x; py = y;
            }
        }
        break;
    }
}

static void page_sign(uint8_t p, int32_t cx, int32_t cy, uint16_t c)
{
    int32_t s = Q(10);
    switch (p) {
    case P_HOME:                                /* four buoys on a line */
        for (int32_t i = 0; i < 4; i++) disc(cx - s + i * s * 2 / 3, cy + ((i & 1) ? -24 : 8), 44, part_color[i], 256);
        line(cx - s - 16, cy + 64, cx + s + 16, cy + 64, c, 200);
        break;
    case P_EDIT: alg_sign(parts[selected].alg, cx, cy, s, c, 256); break;
    case P_ENV: disc(cx, cy, s * 6 / 10, c, 256); ring(cx, cy, s, 20, c, 160); break;
    case P_LFO:
        for (int32_t j = 0; j < 2; j++)
            for (int32_t i = 0; i < 3; i++) disc(cx - s * 2 / 3 + i * s * 2 / 3, cy - s / 3 + j * s * 2 / 3 + (i == 1 ? -24 : 0), 40, c, 256);
        break;
    case P_ARP:
        ring(cx, cy, s * 7 / 10, 18, c, 200);
        disc(polx(cx, s * 7 / 10, 60000), poly(cy, s * 7 / 10, 60000), 60, c, 256);
        disc(polx(cx, s * 7 / 10, 20000), poly(cy, s * 7 / 10, 20000), 32, c, 256);
        disc(polx(cx, s * 7 / 10, 38000), poly(cy, s * 7 / 10, 38000), 32, c, 256);
        break;
    case P_SEQ:
        for (int32_t i = 1; i <= 3; i++) ring(cx, cy, s * i / 3, 16, c, 220);
        disc(cx, cy - s, 40, c, 256);
        break;
    case P_SCALE:
        for (uint32_t i = 0; i < 7; i++) {
            uint32_t a = TOP + i * 65536u / 7u;
            disc(polx(cx, s * 3 / 4, a), poly(cy, s * 3 / 4, a), i ? 30 : 46, i ? c : INK, 256);
        }
        break;
    case P_FX:
        disc(cx - s, cy, 36, c, 256);
        for (int32_t i = 0; i < 4; i++) line(cx - s / 2, cy - s * 3 / 4 + i * s / 2, cx + s, cy - s * 3 / 4 + i * s / 2, c, 256);
        break;
    default:                                    /* GLO: a sun */
        disc(cx, cy, s / 2, c, 256);
        for (uint32_t i = 0; i < 8; i++) {
            uint32_t a = i * 8192u;
            disc(polx(cx, s, a), poly(cy, s, a), 22, c, 256);
        }
        break;
    }
}

/* ------------------------------------------------------------------------------------------ particles */
enum { PK_RIPPLE, PK_DOT, PK_SHARD, PK_SPARK, PK_TRAIL };
typedef struct {
    int16_t x, y, vx, vy;                       /* Q4; Q4 a second */
    uint32_t born;
    uint16_t life;
    uint8_t kind, style, size;
    uint16_t color;
} pt_t;
#define NPT 56u
static pt_t pts[NPT];

static pt_t *pt_new(uint32_t now)
{
    pt_t *o = &pts[0];
    for (uint32_t i = 0; i < NPT; i++) {
        if (now - pts[i].born >= pts[i].life) return &pts[i];
        if (pts[i].born < o->born) o = &pts[i];
    }
    return o;
}

static void pts_clear(void) { for (uint32_t i = 0; i < NPT; i++) pts[i].life = 0; }

/* a ripple's look follows the algorithm of the part that made it */
static void ripple(uint8_t style, int32_t cx, int32_t cy, int32_t r, uint16_t c, uint32_t a)
{
    switch (style) {
    case ALG_GLASS: ring(cx, cy, r, 14, c, a); break;
    case ALG_BIRCH: ring(cx, cy, r, 22, c, a); ring(cx, cy, r * 3 / 4, 12, c, a / 2u); break;
    case ALG_DOTS:
        for (uint32_t i = 0; i < 12; i++) disc(polx(cx, r, i * 5461u), poly(cy, r, i * 5461u), 26, c, a);
        break;
    case ALG_PULSE:
        for (uint32_t i = 0; i < 16; i++) {
            uint32_t ang = i * 4096u;
            line(polx(cx, r, ang), poly(cy, r, ang), polx(cx, r + 48, ang), poly(cy, r + 48, ang), c, a);
        }
        break;
    case ALG_FOLD: ring(cx, cy, r, 44, c, a * 3u / 4u); break;
    default:
        for (uint32_t i = 0; i < 28; i++) disc(polx(cx, r, i * 2341u + (uint32_t)r * 40u), poly(cy, r, i * 2341u + (uint32_t)r * 40u), 14, c, a);
        break;
    }
}

static void pts_draw(uint32_t now)
{
    for (uint32_t i = 0; i < NPT; i++) {
        pt_t *p = &pts[i];
        uint32_t age = now - p->born;
        if (age >= p->life) continue;
        uint32_t fade = 256u - age * 256u / p->life;
        int32_t x = p->x + p->vx * (int32_t)age / 1000, y = p->y + p->vy * (int32_t)age / 1000;
        switch (p->kind) {
        case PK_RIPPLE: ripple(p->style, x, y, Q(p->size) + (int32_t)age * Q(30) / 1000, p->color, fade * 200u / 256u); break;
        case PK_DOT: {
            uint32_t grow = age * 4u < p->life ? age * 4u * 256u / p->life : 256u;
            disc(x, y, Q(p->size) * (int32_t)grow / 256, p->color, fade);
            break;
        }
        case PK_SHARD: {
            int32_t len = Q(p->size);
            int32_t ex = x + p->vx * len / (Q(60)), ey = y + p->vy * len / (Q(60));
            line(x, y, ex, ey, p->color, fade);
            break;
        }
        case PK_TRAIL: disc(x, y, Q(p->size) * (int32_t)fade / 256, p->color, fade * 3u / 4u); break;
        default: disc(x, y, Q(p->size), p->color, fade); break;
        }
    }
}

/* ------------------------------------------------------------------------------------------ state */
enum { OV_NONE, OV_SLOTS, OV_SAVE, OV_ALG, OV_SELECT };
static struct {
    uint8_t cur, page, overlay, booting;
    uint32_t last_frame, overlay_at, boot_at, dice_at, wipe_at, page_at;
    uint32_t ev_read, clicks[PARTS], grains[PARTS];
    int32_t meter[PARTS], out;                  /* smoothed, Q15 */
    int32_t head_x, head_y;                     /* ARP: the moving spot */
    int32_t wheel;                              /* SCALE: the colour circle's turn, Q8 of a key */
    int32_t last_pitch[PARTS];
    uint32_t quiet_since, header_state, band_state;
    uint32_t wob_at;
    int8_t wob[24];
} st;

static void stage_overlay(uint8_t kind) { st.overlay = kind; st.overlay_at = fm1_ms; }
static void stage_dice(void) { st.dice_at = fm1_ms; }
static void stage_wipe(void) { st.wipe_at = fm1_ms; }

/* ---------------------------------------------------------------------------------------- the pages */
static int32_t meter_of(uint32_t p) { return st.meter[p]; }

static int32_t buoy_x(uint32_t v) { return Q(30 + (int32_t)v * 60) + parts[v].pan * Q(10) / 64; }

static void anim_home(uint32_t t)
{
    /* the water: three slow curves */
    for (int32_t k = 0; k < 3; k++) {
        int32_t px = 0, py = 0;
        for (int32_t x = 0; x <= 240; x += 6) {
            int32_t y = Q(112 + k * 16) + (Q(5) * sinq((uint32_t)(x * 190 + (int32_t)(t * (uint32_t)(5 + k * 3) / 4u) + k * 9000)) >> 15)
                        + (Q(2) * sinq((uint32_t)(x * 470 - (int32_t)t * 3)) >> 15);
            if (x) line(px, py, Q(x), y, k ? LINE : GREY, k ? 256u : 150u);
            px = Q(x); py = y;
        }
    }
    for (uint32_t v = 0; v < PARTS; v++) {
        int32_t m = meter_of(v);
        int32_t x = buoy_x(v);
        int32_t bob = Q(3) * sinq(t * 22u + v * 14000u) >> 15;
        int32_t lift = st.last_pitch[v] ? (st.last_pitch[v] - 60 * 256) * Q(10) / (24 * 256) : 0;
        lift = lift > Q(14) ? Q(14) : lift < -Q(14) ? -Q(14) : lift;
        int32_t y = Q(76) + bob - lift;
        int32_t r = Q(5) + parts[v].level * Q(13) / 127 + m * Q(7) / 32768;
        disc(x + 24, y + 40, r, PAPER2, 256);   /* a shadow on the paper */
        if (parts[v].level) disc(x, y, r, part_color[v], 256); else ring(x, y, r, 20, part_color[v], 256);
        alg_sign(parts[v].alg, x, y, r / 2, CREAM, parts[v].level ? 200u : 0u);
        if (v == selected) ring(x, y, r + Q(5), 22, INK, 256);
    }
}

/* EDIT: the selected part's algorithm, as a thing on paper */
static void anim_glass(const part_t *s, int32_t m, uint32_t t)
{
    uint32_t n = 3u + s->p[0] * GLASS_RATIOS / 128u;
    int32_t R = Q(42) + m * Q(18) / 32768;
    int32_t inner = R * (230 - s->p[1] * 150 / 127) / 256;
    uint32_t rot = t * 3u;
    static const uint16_t facet[4] = {SKY, TEAL, LILAC, CREAM};
    int32_t cx = Q(120), cy = Q(80);
    for (uint32_t i = 0; i < 2u * n; i++) {
        uint32_t a0 = rot + i * 65536u / (2u * n), a1 = rot + (i + 1u) * 65536u / (2u * n);
        int32_t r0 = i & 1u ? inner : R, r1 = i & 1u ? R : inner;
        tri(cx, cy, polx(cx, r0, a0), poly(cy, r0, a0), polx(cx, r1, a1), poly(cy, r1, a1), facet[i % 4u], 190);
        line(polx(cx, r0, a0), poly(cy, r0, a0), polx(cx, r1, a1), poly(cy, r1, a1), INK, 120);
        if (!(i & 1u)) line(cx, cy, polx(cx, r0, a0), poly(cy, r0, a0), CREAM, 160);
    }
    if (s->p[2]) {                              /* FROST: a ghost of the stone, a little off, trembling */
        uint32_t off = rot + 900u + (uint32_t)(sinq(t * 61u) * (int32_t)s->p[2] / 4096);
        int32_t R2 = R + Q(4);
        for (uint32_t i = 0; i < 2u * n; i++) {
            uint32_t a0 = off + i * 65536u / (2u * n), a1 = off + (i + 1u) * 65536u / (2u * n);
            int32_t r0 = i & 1u ? inner + Q(3) : R2, r1 = i & 1u ? R2 : inner + Q(3);
            line(polx(cx, r0, a0), poly(cy, r0, a0), polx(cx, r1, a1), poly(cy, r1, a1), SKY, s->p[2] * 2u);
        }
    }
    disc(cx, cy, Q(5), part_color[selected], 256);
}

static int32_t log2_q4(uint32_t x)              /* log2(x) x 16, near enough (x > 0) */
{
    int32_t n = 0;
    while (x >> (n + 1)) n++;
    return n * 16 + (int32_t)((x << 4) >> n) - 16;
}

static void anim_birch(const part_t *s, int32_t m, uint32_t t)
{
    uint16_t ratio[4];
    birch_ratios(s->p[0], ratio);
    int32_t cx = Q(120), cy = Q(80);
    /* the grain of the wood: many thin rings, a little off centre */
    for (int32_t k = 1; k < 16; k++) {
        int32_t wob = (sinq(t * 2u + (uint32_t)k * 7000u) * 10) >> 15;
        ring(cx + Q(k) / 3 + wob, cy - Q(k) / 5, Q(5 * k), 10, k & 1 ? LINE : PAPER2, 256);
    }
    static const uint16_t wood[4] = {BROWN, BIRCHC, ORANGE, SAFFRON};
    uint32_t thick = 14u + s->p[1] / 4u;
    for (int32_t i = 3; i >= 0; i--) {
        /* each partial: a ring, its radius from how high it rings (an octave: 16 px) */
        int32_t r = Q(10) + (log2_q4(ratio[i]) - 7 * 16) * Q(16) / 16;
        r = r < Q(8) ? Q(8) : r > Q(74) ? Q(74) : r;
        uint32_t a = i ? 120u + s->p[3] : 256u;
        ring(cx, cy, r, (int32_t)thick, i ? wood[i] : part_color[selected], a > 256u ? 256u : a);
    }
    disc(cx, cy, Q(6) + m * Q(10) / 32768, part_color[selected], 256);
}

static void anim_dots(const part_t *s, int32_t m, uint32_t t)
{
    (void)t;
    for (int32_t y = 10; y < STAGE_H; y += 20)
        for (int32_t x = 10 + ((y / 20) & 1) * 10; x < STAGE_W; x += 20) disc(Q(x), Q(y), Q(2), PAPER2, 256);
    disc(Q(120), Q(80), Q(6) + m * Q(8) / 32768 + s->p[2] * Q(4) / 127, part_color[selected], 256);
}

static void anim_pulse(const part_t *s, int32_t m, uint32_t t)
{
    const part_rt_t *r = &part_rt[selected];
    uint32_t bright = 60u + (uint32_t)m * 190u / 32768u;
    if (r->gate_inc && !r->gate) bright /= 3u;
    int32_t block = 1 + s->p[2] / 16;           /* DATA: the picture goes coarse */
    int32_t wide = 1 + s->p[2] / 64, top = 14 / block * block;
    for (int32_t x = 6; x < STAGE_W - 4; x += 6) {
        int32_t bx = x / block * block;
        for (int32_t y = top; y < STAGE_H - 14; y += block)
            rect(bx, y, wide, block > 2 ? block - 1 : block, INK, bright);
    }
    if (s->p[1]) {                              /* BEAT: a second grating, a little wider, sliding: moiré */
        int32_t spacing = 96 + s->p[1] * 12 / 127;     /* Q4 */
        int32_t slide = (int32_t)(t * (uint32_t)s->p[1] * (uint32_t)s->p[1] / 4000u) % spacing;
        for (int32_t x = slide; x < Q(STAGE_W - 4); x += spacing) {
            int32_t bx = (x >> 4) / block * block;
            rect(bx, 24, wide, STAGE_H - 48, RED, bright * 2u / 3u);
        }
    }
    ring(Q(120), Q(80), Q(30) + m * Q(22) / 32768, 20, CREAM, 230);
    ring(Q(120), Q(80), Q(30) + m * Q(22) / 32768, 8, INK, 200);
}

static int32_t isqrt32(uint32_t x)
{
    uint32_t r = 0, bit = 1u << 30;
    while (bit > x) bit >>= 2;
    while (bit) {
        if (x >= r + bit) { x -= r + bit; r = (r >> 1) + bit; } else r >>= 1;
        bit >>= 2;
    }
    return (int32_t)r;
}

/* FOLD: a strip of tape folded back and forth; more folds, the higher the FOLD; BIAS lifts it; TONE thins it;
 * WOBBLE shakes the hand that drew it */
static void anim_fold(const part_t *s, int32_t m, uint32_t t)
{
    uint32_t folds = 1u + s->p[0] / 18u;
    int32_t amp = Q(10) + Q(36) / (int32_t)folds + m * Q(16) / 32768;
    int32_t lift = s->p[1] * Q(18) / 127;
    int32_t th = Q(5) + (127 - s->p[2]) * Q(7) / 127;
    if (t - st.wob_at > 120u) {
        st.wob_at = t;
        for (uint32_t i = 0; i < 24u; i++) st.wob[i] = (int8_t)((int32_t)rnd_n(33) - 16);
    }
    uint32_t n = 2u * folds + 1u;
    int32_t vx[24], vy[24];
    for (uint32_t i = 0; i < n && i < 24u; i++) {
        vx[i] = Q(20) + (int32_t)i * Q(200) / (int32_t)(n - 1u) + st.wob[23u - i] * s->p[3] / 12;
        vy[i] = Q(78) + (i & 1u ? -amp - lift : amp - lift / 3) + st.wob[i] * s->p[3] / 14;
        vy[i] += Q(3) * sinq(t * 12u + i * 9000u) >> 15;
    }
    for (uint32_t i = 1; i < n && i < 24u; i++) {
        int32_t dx = vx[i] - vx[i - 1u], dy = vy[i] - vy[i - 1u];
        int32_t len = isqrt32((uint32_t)(dx * dx + dy * dy));
        if (!len) continue;
        int32_t nx = -dy * th / len, ny = dx * th / len;
        int32_t ax = vx[i - 1u], ay = vy[i - 1u], bx = vx[i], by = vy[i];
        tri(ax + nx, ay + ny, bx + nx, by + ny, bx - nx, by - ny, ORANGE, 256);
        tri(ax + nx, ay + ny, bx - nx, by - ny, ax - nx, ay - ny, ORANGE, 256);
        if (i & 1u) {                           /* the folds' light and shade */
            tri(ax + nx, ay + ny, bx + nx, by + ny, bx - nx, by - ny, CREAM, 80);
            tri(ax + nx, ay + ny, bx - nx, by - ny, ax - nx, ay - ny, CREAM, 80);
        }
        line(ax + nx, ay + ny, bx + nx, by + ny, INK, 170);
        line(ax - nx, ay - ny, bx - nx, by - ny, INK, 170);
        line(ax + nx / 3, ay + ny / 3, bx + nx / 3, by + ny / 3, COBALT, 256);
        line(ax - nx / 3, ay - ny / 3, bx - nx / 3, by - ny / 3, COBALT, 256);
    }
    disc(Q(120), Q(146), Q(4) + m * Q(5) / 32768, part_color[selected], 256);
}

static void anim_air(const part_t *s, int32_t m, uint32_t t)
{
    uint32_t lobes = 2u + s->p[3] * 4u / 128u;
    int32_t R = Q(44) + m * Q(16) / 32768;
    uint32_t flow = t * (6u + s->p[2] / 6u);
    int32_t gust = 2000 + s->p[2] * 40;
    int32_t gap = 18 + (127 - s->p[1]) * 40 / 127;     /* FOCUS: the contours draw together */
    static const uint16_t c4[4] = {COBALT, TEAL, SKY, LILAC};
    for (int32_t j = 0; j < 4; j++) {
        int32_t scale = 256 - j * gap;
        int32_t px = 0, py = 0, fx = 0, fy = 0;
        for (uint32_t i = 0; i <= 64u; i++) {
            uint32_t a = i * 1024u;
            int32_t w = 32768 + (sinq(a * lobes + flow) * 5 / 32) + ((sinq(a * (lobes + 1u) - flow * 2u + (uint32_t)j * 3000u) * gust) >> 15);
            int32_t r = (int32_t)(((int64_t)R * w >> 15) * scale >> 8);
            int32_t x = polx(Q(120), r, a), y = poly(Q(80), r * 5 / 6, a);
            if (i) line(px, py, x, y, j ? c4[j] : part_color[selected], 256);
            else { fx = x; fy = y; }
            px = x; py = y;
        }
        (void)fx; (void)fy;
    }
    uint32_t specks = s->p[0] / 6u;             /* BREATH: grains of air going by */
    for (uint32_t i = 0; i < specks; i++) {
        uint32_t h = (i * 2654435761u) ^ (t / 50u + i * 37u) * 40503u;
        int32_t x = (int32_t)((h >> 8) % 240u), y = (int32_t)((h >> 20) % 160u);
        disc(Q(x), Q(y), 18, INK, 90);
    }
}

static void anim_edit(uint32_t t)
{
    const part_t *s = &parts[selected];
    int32_t m = meter_of(selected);
    switch (s->alg) {
    case ALG_GLASS: anim_glass(s, m, t); break;
    case ALG_BIRCH: anim_birch(s, m, t); break;
    case ALG_DOTS: anim_dots(s, m, t); break;
    case ALG_PULSE: anim_pulse(s, m, t); break;
    case ALG_FOLD: anim_fold(s, m, t); break;
    default: anim_air(s, m, t); break;
    }
}

/* ENV: every note of the selected part is a bloom: it opens, holds, closes as the note does */
static int32_t ghost_env(const part_t *s, uint32_t t)   /* a pretend note, on repeat, when nothing plays: Q15 */
{
    uint32_t a = env_ms(s, 0), d = env_ms(s, 1) / 2u, r = env_ms(s, 3) / 2u, hold = 700u;
    int32_t sus = (int32_t)((uint32_t)s->env[2] * s->env[2] * 32767u / 16129u);
    uint32_t period = a + d + hold + r + 500u;
    if (period > 9000u) period = 9000u;
    uint32_t x = t % period;
    if (x < a) return (int32_t)(x * 32767u / (a ? a : 1u));
    x -= a;
    if (x < d + hold) {
        uint32_t u = x < d ? x * 256u / (d ? d : 1u) : 256u;
        int32_t f = (int32_t)((256u - u) * (256u - u) / 256u);
        return sus + (32767 - sus) * f / 256;
    }
    x -= d + hold;
    if (x < r) { uint32_t u = 256u - x * 256u / (r ? r : 1u); return sus * (int32_t)(u * u / 256u) / 256; }
    return 0;
}

static void bloom(int32_t x, int32_t y, int32_t level, uint16_t c, uint8_t releasing, uint32_t a)
{
    int32_t r = Q(4) + level * Q(44) / 32768;
    disc(x, y, r, c, a);
    for (uint32_t i = 0; i < 5; i++) {          /* polka dots on the bloom */
        uint32_t ang = i * 13107u + 4000u;
        disc(polx(x, r * 11 / 20, ang), poly(y, r * 11 / 20, ang), r / 7, PAPER, a);
    }
    if (releasing) ring(x, y, r + Q(6), 14, c, a / 2u);
}

static void anim_env(uint32_t t)
{
    uint16_t c = part_color[selected];
    uint8_t any = 0;
    for (uint32_t i = 0; i < VOICES; i++) {
        const voice_t *v = &voices[i];
        if (!v->active || v->part != selected) continue;
        any = 1;
        int32_t note = v->pitch >> 8;
        int32_t x = Q(48) + (note - 36) * Q(144) / 60;
        x = x < Q(48) ? Q(48) : x > Q(192) ? Q(192) : x;
        bloom(x, Q(80), v->env >> 9, c, v->stage == ENV_RELEASE, 230);
    }
    if (any) st.quiet_since = t;
    else if (t - st.quiet_since > 600u) {
        int32_t g = ghost_env(&parts[selected], t - st.quiet_since);
        ring(Q(120), Q(80), Q(4) + g * Q(52) / 32768, 14, c, 150);
        disc(Q(120), Q(80), Q(4) + g * Q(52) / 32768, c, 50);
    }
}

/* LFO: a field of dots that sways, swells, shifts or glows with the LFO, whatever it is sent to */
static void anim_lfo(uint32_t t)
{
    (void)t;
    const part_t *s = &parts[selected];
    int32_t w = part_rt[selected].lfo_out;
    int32_t m = (int32_t)(((int64_t)w * part_rt[selected].lfo_depth) >> 15);   /* Q15 */
    uint8_t target = s->lfo[K_TARGET];
    uint16_t c = part_color[selected];
    int32_t dx = target == LT_PAN ? m * Q(22) / 32768 : 0;
    int32_t dy = target == LT_PITCH ? -m * Q(22) / 32768 : 0;
    for (int32_t j = 0; j < 4; j++)
        for (int32_t i = 0; i < 7; i++) {
            int32_t x = Q(30 + i * 30) + dx, y = Q(35 + j * 30) + dy;
            int32_t r = Q(5);
            if (target == LT_VOLUME) r = Q(5) + m * Q(4) / 32768;
            uint16_t cc = c;
            if (target == LT_TIMBRE) cc = mix565(c, m > 0 ? CREAM : INK, (uint32_t)(m < 0 ? -m : m) * 200u / 32768u);
            if (i == 3 && (j == 1 || j == 2)) r += Q(2);
            disc(x, y, r, cc, 256);
        }
}

/* ARP: the kept notes around a circle (one ring an octave); a spot jumps from note to note */
static void arp_pos(int32_t degree, int32_t lowest_octave, int32_t *x, int32_t *y)
{
    int32_t d, o;
    degree_split(degree, &d, &o);
    int32_t ringn = o - lowest_octave;
    ringn = ringn < 0 ? 0 : ringn > 4 ? 4 : ringn;
    int32_t r = Q(24) + ringn * Q(15);
    uint32_t a = TOP + (uint32_t)d * 65536u / scale_now.size;
    *x = polx(Q(120), r, a);
    *y = poly(Q(80), r, a);
}

static void anim_arp(uint32_t t)
{
    uint16_t c = part_color[selected];
    uint32_t rings = arp.range + 1u;
    for (uint32_t k = 0; k < rings; k++) ring(Q(120), Q(80), Q(24) + (int32_t)k * Q(15), 12, LINE, 256);
    if (!arp.n) { disc(Q(120), Q(80), Q(3), GREY, 256); return; }
    int32_t lowest = 99;
    for (uint32_t i = 0; i < arp.n; i++) {
        int32_t d, o;
        degree_split(arp.notes[i], &d, &o);
        if (o < lowest) lowest = o;
    }
    for (uint32_t k = 0; k < rings; k++)
        for (uint32_t i = 0; i < arp.n; i++) {
            int32_t x, y;
            arp_pos(arp.notes[i] + (int32_t)k * scale_now.size, lowest, &x, &y);
            disc(x, y, Q(3) + (k ? 0 : Q(1)), arp.on ? c : GREY, arp.on ? 256u : 160u);
        }
    if (!arp.on) return;
    int32_t tx, ty;
    arp_pos(arp.last, lowest, &tx, &ty);
    if (!st.head_x) { st.head_x = tx; st.head_y = ty; }
    st.head_x += (tx - st.head_x) * 2 / 5;
    st.head_y += (ty - st.head_y) * 2 / 5;
    if (clk.running && (t / 40u) % 2u == 0) {   /* a trail */
        pt_t *p = pt_new(t);
        p->kind = PK_TRAIL; p->x = (int16_t)st.head_x; p->y = (int16_t)st.head_y; p->vx = p->vy = 0;
        p->born = t; p->life = 500; p->size = 4; p->color = c;
    }
    if (arp.last_rest) ring(st.head_x, st.head_y, Q(7), 20, c, 256);
    else disc(st.head_x, st.head_y, Q(7), c, 256);
    ring(st.head_x, st.head_y, Q(10), 12, INK, 200);
}

/* SEQ: four rings of steps, track 1 inside; each ring as long as its track; the steps playing light up */
static void seq_dot(uint32_t tr, uint32_t i, int32_t *x, int32_t *y)
{
    const track_t *k = &tracks[tr];
    int32_t r = Q(17) + (int32_t)tr * Q(18);
    if (k->deg[i] != REST) r += clampi(k->deg[i], -6, 6) * 7;
    uint32_t a = TOP + i * 65536u / (k->length ? k->length : 1u);
    *x = polx(Q(120), r, a);
    *y = poly(Q(80), r, a);
}

static void anim_seq(uint32_t t)
{
    for (uint32_t tr = 0; tr < PARTS; tr++) {
        const track_t *k = &tracks[tr];
        int32_t r = Q(17) + (int32_t)tr * Q(18);
        uint16_t c = part_color[tr];
        if (tr == selected) {
            uint16_t g = rec_on ? RED : INK;
            uint32_t a = rec_on ? 120u + (uint32_t)(sinq(t * 40u) + 32768) / 512u : 60u;
            ring(Q(120), Q(80), r, rec_on ? 40 : 18, g, a);
        }
        if (!k->length) {
            for (uint32_t i = 0; i < 24u; i++) disc(polx(Q(120), r, i * 2731u), poly(Q(80), r, i * 2731u), 10, LINE, 256);
            continue;
        }
        uint32_t pos = track_pos(tr);
        for (uint32_t i = 0; i < k->length; i++) {
            int32_t x, y;
            seq_dot(tr, i, &x, &y);
            uint8_t note = k->deg[i] != REST;
            uint8_t now = clk.running && i == pos;
            if (note) disc(x, y, Q(2) + k->vel[i] * 24 / 127 + (now ? Q(2) : 0), c, 256);
            else disc(x, y, now ? Q(2) : 18, now ? GREY : LINE, 256);
            if (now) ring(x, y, Q(6), 14, INK, 220);
        }
    }
    disc(Q(120), Q(80), Q(3), clk.running ? INK : GREY, 256);
}

/* SCALE: the twelve keys as a colour circle, the root always at the top: a new key turns the circle. The notes of
 * the scale are big, the others pin pricks; gravity leaves only rings of the fallen ones; true tuning moves them
 * off their places, by their cents. */
static void anim_scale(uint32_t t)
{
    (void)t;
    int32_t root = scale_root_pc();
    int32_t target = root * 256;
    int32_t d = target - st.wheel;
    while (d > 6 * 256) d -= 12 * 256;
    while (d < -6 * 256) d += 12 * 256;
    st.wheel += d / 4;
    if (d && d / 4 == 0) st.wheel = target;
    st.wheel = (st.wheel + 12 * 256) % (12 * 256);
    int32_t cx = Q(120), cy = Q(82), R = Q(60);
    /* the scale's shape: its notes joined in order */
    int32_t fx = 0, fy = 0, px = 0, py = 0;
    for (uint32_t i = 0; i <= scale_now.size; i++) {
        uint32_t k = i % scale_now.size;
        int32_t cents = scale_now.cents[k] * scale_now.truth / 127;
        uint32_t a = TOP + (uint32_t)((scale_now.offset[k] * 100 + cents) * 65536 / 1200);
        int32_t x = polx(cx, R, a), y = poly(cy, R, a);
        if (i) line(px, py, x, y, LINE, 256); else { fx = x; fy = y; }
        px = x; py = y;
    }
    (void)fx; (void)fy;
    for (int32_t pc = 0; pc < 12; pc++) {       /* pc: semitones above the root as shown now */
        int32_t rel = (pc * 256 - (st.wheel - root * 256) + 12 * 256) % (12 * 256);
        int32_t abs_pc = (root + pc) % 12;
        int32_t deg = -1;
        for (uint32_t i = 0; i < scale_now.size; i++) if (scale_now.offset[i] == pc) deg = (int32_t)i;
        int32_t cents = deg >= 0 ? scale_now.cents[deg] * scale_now.truth / 127 : 0;
        uint32_t a = TOP + (uint32_t)((rel * 100 / 256 + cents) * 65536 / 1200);
        int32_t x = polx(cx, R, a), y = poly(cy, R, a);
        if (deg < 0) { disc(x, y, Q(2), GREY, 200); continue; }
        if (scale_now.keep[deg] != deg) ring(x, y, Q(7), 22, pc_color[abs_pc], 256);
        else disc(x, y, Q(10), pc_color[abs_pc], 256);
        if (pc == 0) ring(x, y, Q(14), 22, INK, 256);
    }
    disc(cx, cy, Q(16), pc_color[root], 256);
    ring(cx, cy, Q(16) + Q(scale_now.octave + 3), 12, INK, 120);
}

/* FX: the sound on the left, four streams side by side (parallel), together again on the right */
static void anim_fx(uint32_t t)
{
    int32_t lvl = st.out;
    int32_t sx = Q(22), sy = Q(80), ox = Q(218);
    static const int32_t lane_y[4] = {26, 62, 98, 134};
    const uint8_t amt[4] = {fx.echo, fx.hall, fx.chorus, fx.dust};
    static const uint16_t lc[4] = {ORANGE, LILAC, TEAL, BROWN};
    for (uint32_t k = 0; k < 4; k++) {
        int32_t ly = Q(lane_y[k]);
        /* the branches: soft bends (Aalto's bent wood) */
        int32_t px = sx, py = sy;
        for (int32_t i = 1; i <= 10; i++) {
            int32_t x = sx + (Q(52) - sx) * i / 10;
            int32_t u = i * 256 / 10, e = u * u * (768 - 2 * u) / 65536;
            int32_t y = sy + (ly - sy) * e / 256;
            line(px, py, x, y, amt[k] ? lc[k] : LINE, 256);
            px = x; py = y;
        }
        px = Q(190); py = ly;
        for (int32_t i = 1; i <= 10; i++) {
            int32_t x = Q(190) + (ox - Q(190)) * i / 10;
            int32_t u = i * 256 / 10, e = u * u * (768 - 2 * u) / 65536;
            int32_t y = ly + (sy - ly) * e / 256;
            line(px, py, x, y, amt[k] ? lc[k] : LINE, 256);
            px = x; py = y;
        }
        if (!amt[k]) {
            for (int32_t x = 56; x < 190; x += 8) disc(Q(x), ly, 12, LINE, 256);
            continue;
        }
        int32_t swell = lvl * Q(4) / 32768;
        switch (k) {
        case 0: {                               /* ECHO: dots marching right, smaller and smaller */
            uint32_t beat = 60000u / glo.bpm * 3u / 4u;
            int32_t slide = (int32_t)(t % beat) * Q(18) / (int32_t)beat;
            uint32_t n = 1u + amt[k] / 18u;
            for (uint32_t j = 0; j < n; j++) {
                int32_t x = Q(58) + (int32_t)j * Q(18) + slide;
                if (x > Q(188)) break;
                disc(x, ly, Q(5) - (int32_t)j * Q(5) / (int32_t)(n + 1u) + swell, lc[k], 256u - j * 200u / n);
            }
            break;
        }
        case 1:                                 /* HALL: soft clouds, breathing */
            for (int32_t j = 0; j < 5; j++) {
                int32_t x = Q(70 + j * 28), r = Q(6) + amt[k] * Q(8) / 127 + (sinq(t * 9u + (uint32_t)j * 13000u) * Q(2) >> 15) + swell;
                disc(x, ly, r, lc[k], 70u + amt[k] / 3u);
            }
            break;
        case 2: {                               /* CHORUS: two strands, swaying apart */
            int32_t sw = amt[k] * Q(7) / 127;
            for (int32_t s2 = -1; s2 <= 1; s2 += 2) {
                int32_t qx = Q(56), qy = ly;
                for (int32_t x = 60; x <= 188; x += 4) {
                    int32_t y = ly + s2 * (sw * sinq((uint32_t)(x * 500) + t * 26u * (uint32_t)(s2 + 2)) >> 15) + (swell * s2) / 2;
                    line(qx, qy, Q(x), y, lc[k], 256);
                    qx = Q(x); qy = y;
                }
            }
            break;
        }
        default: {                              /* DUST: specks, here and gone */
            uint32_t n = 3u + amt[k] / 5u;
            for (uint32_t j = 0; j < n; j++) {
                uint32_t h = (j * 2654435761u) ^ (t / 70u) * 40503u ^ j * 977u;
                int32_t x = 58 + (int32_t)((h >> 7) % 130u), y = lane_y[k] - 10 + (int32_t)((h >> 18) % 20u);
                rect(x, y, 1 + (int32_t)(h & 1u), 1 + (int32_t)(h >> 30 & 1u), h & 4u ? INK : lc[k], 220);
            }
            break;
        }
        }
    }
    disc(sx, sy, Q(6) + lvl * Q(9) / 32768, INK, 256);
    ring(ox, sy, Q(7) + lvl * Q(6) / 32768, 20, INK, 256);
}

/* GLO: a sun that beats with the tempo, four marks for the beats (swing moves the off-beats), the MIDI
 * channel as one dot of sixteen, the tuning as a red mark off the top */
static void anim_glo(uint32_t t)
{
    uint32_t bpm = external_clock() && external_bpm_value ? external_bpm_value : glo.bpm;
    uint32_t beat_ms = 60000u / (bpm ? bpm : 1u);
    uint32_t phase = clk.running ? (clk.tick % 96u) * 65536u / 96u : (t % beat_ms) * 65536u / beat_ms;
    uint32_t bar = clk.running ? (clk.tick % 384u) * 65536u / 384u : (t % (beat_ms * 4u)) * 65536u / (beat_ms * 4u);
    int32_t pulse = (int32_t)(65535u - phase) * Q(9) / 65536;
    pulse = pulse * (int32_t)(65535u - phase) / 65536;
    int32_t cx = Q(120), cy = Q(80);
    disc(cx, cy, Q(24) + pulse, SAFFRON, 256);
    for (uint32_t i = 0; i < 4; i++) {
        uint32_t a = TOP + i * 16384u;
        if (i & 1u) a += (uint32_t)(glo.swing - 50u) * 16384u / 50u;
        disc(polx(cx, Q(44), a), poly(cy, Q(44), a), Q(4), INK, 256);
    }
    disc(polx(cx, Q(44), TOP + bar), poly(cy, Q(44), TOP + bar), Q(2), RED, 256);
    for (uint32_t c = 0; c < 16; c++) {
        uint32_t a = TOP + c * 4096u;
        uint8_t on = !glo.ch || glo.ch == c + 1u;
        disc(polx(cx, Q(64), a), poly(cy, Q(64), a), on ? Q(3) : Q(1) + 8, glo.ch ? (on ? INK : GREY) : GREY, 256);
    }
    uint32_t ta = TOP + (uint32_t)(glo.tune * 182);
    line(polx(cx, Q(28), ta), poly(cy, Q(28), ta), polx(cx, Q(36), ta), poly(cy, Q(36), ta), RED, 256);
}

/* ------------------------------------------------------------------------------------------- overlays */
static void slot_spot(uint32_t i, int32_t x, int32_t y, int32_t r, uint32_t a)
{
    if (scene_saved >> i & 1u) {                /* a saved scene: a small spot painting of its four parts */
        disc(x, y, r, CREAM, a);
        for (uint32_t p = 0; p < PARTS; p++) {
            int32_t ox = (p & 1u) ? r / 3 : -r / 3, oy = (p & 2u) ? r / 3 : -r / 3;
            disc(x + ox, y + oy, r / 4 + 8, part_color[(p + scene_algs[i][p]) % PARTS], a);
        }
        ring(x, y, r, 16, INK, a * 3u / 4u);
    } else {                                    /* unexplored: a dotted circle */
        for (uint32_t k = 0; k < 16; k++) disc(polx(x, r, k * 4096u), poly(y, r, k * 4096u), 14, GREY, a);
    }
}

static void draw_overlay(uint32_t t)
{
    if (!st.overlay) return;
    uint32_t age = t - st.overlay_at, life = st.overlay == OV_SAVE ? 1500u : 1300u;
    if (age >= life) { st.overlay = OV_NONE; return; }
    uint32_t a = age < 120u ? age * 256u / 120u : age > life - 250u ? (life - age) * 256u / 250u : 256u;
    int32_t band_top = st.overlay == OV_SLOTS || st.overlay == OV_SAVE ? 16 : 52;
    rect(0, band_top, STAGE_W, STAGE_H - 2 * band_top, PAPER, a * 235u / 256u);
    line(0, Q(band_top), Q(STAGE_W), Q(band_top), LINE, a);
    line(0, Q(STAGE_H - band_top), Q(STAGE_W), Q(STAGE_H - band_top), LINE, a);
    switch (st.overlay) {
    case OV_SLOTS:
    case OV_SAVE:
        for (uint32_t i = 0; i < SCENES; i++) {
            int32_t x = Q(45 + (int32_t)(i % 4u) * 50), y = Q(52 + (int32_t)(i / 4u) * 56);
            uint8_t cur = scene_now == (int8_t)i;
            slot_spot(i, x, y, cur ? Q(19) : Q(14), a);
            if (cur) ring(x, y, Q(23), 20, INK, a);
            if (cur && st.overlay == OV_SAVE)   /* the stamp */
                ring(x, y, Q(23) + (int32_t)age * Q(30) / 1500, 30, part_color[selected], (1500u - age) * a / 1500u);
        }
        break;
    case OV_ALG:
        for (uint32_t i = 0; i < ALGS; i++) {
            int32_t x = Q(30 + (int32_t)i * 36), y = Q(80);
            uint8_t cur = parts[selected].alg == i;
            if (cur) disc(x, y, Q(16), part_color[selected], a);
            else ring(x, y, Q(12), 14, LINE, a);
            alg_sign((uint8_t)i, x, y, cur ? Q(9) : Q(7), cur ? CREAM : GREY, a);
        }
        break;
    default:
        for (uint32_t v = 0; v < PARTS; v++) {
            int32_t x = Q(45 + (int32_t)v * 50), y = Q(80);
            disc(x, y, v == selected ? Q(18) : Q(10), part_color[v], a);
            alg_sign(parts[v].alg, x, y, v == selected ? Q(9) : Q(5), CREAM, a);
        }
        break;
    }
}

/* -------------------------------------------------------------------------------------- the header */
static uint8_t battery_level(uint32_t now)
{
#if BOARD_SMK37
    (void)now;
    return UINT8_MAX;
#else
    static uint32_t last_read;
    static int32_t average;
    static uint8_t level = UINT8_MAX;
    if (now - last_read >= 250u) {
        last_read = now;
        int32_t raw = fm1_adc_read(FM1_ADC_BATT);
        if (raw > 0) {
            average = average ? average + (raw * 16 - average) / 4 : raw * 16;
            level = average >= 591 * 16 ? 3 : average >= 561 * 16 ? 2 : average >= 531 * 16 ? 1 : 0;
        }
    }
    return level;
#endif
}

static void draw_header(uint32_t now)
{
    uint8_t batt = battery_level(now);
    uint32_t beat = clk.running ? clk.tick / 24u % 4u : 9u;
    uint32_t hs = (uint32_t)st.page | (uint32_t)selected << 4 | (uint32_t)rec_on << 6 | (uint32_t)clk.running << 7 |
                  (uint32_t)(scale_now.octave + 3) << 8 | (uint32_t)batt << 11 | beat << 14 | (uint32_t)arp.on << 18 |
                  (uint32_t)(rec_on && (now / 250u) % 2u) << 19;
    for (uint32_t p = 0; p < PARTS; p++) hs = hs * 31u + parts[p].alg * 7u + parts[p].level / 16u;
    if (hs == st.header_state) return;
    st.header_state = hs;
    cv_begin(240, HEADER_H, PAPER);
    sf_target(cv_px, 240, HEADER_H);
    page_sign(st.page, Q(20), Q(15), page_tint(st.page));
    for (uint32_t v = 0; v < PARTS; v++) {
        int32_t x = Q(76 + (int32_t)v * 26), y = Q(15);
        int32_t r = v == selected ? Q(9) : Q(7);
        if (parts[v].level) disc(x, y, r, part_color[v], 256); else ring(x, y, r, 18, part_color[v], 256);
        alg_sign(parts[v].alg, x, y, r / 2, CREAM, parts[v].level ? 230u : 0u);
        if (v == selected) ring(x, y, r + Q(3), 16, INK, 256);
    }
    /* record, play, octave, battery */
    if (rec_on) disc(Q(184), Q(15), (now / 250u) % 2u ? Q(5) : Q(4), RED, 256);
    else ring(Q(184), Q(15), Q(4), 14, GREY, 256);
    if (clk.running) {
        disc(Q(200), Q(15), beat == 0 ? Q(5) : Q(3) + 8, INK, 256);
    } else {
        tri(Q(196), Q(10), Q(196), Q(20), Q(205), Q(15), GREY, 256);
    }
    for (int32_t o = -3; o <= 3; o++) {         /* the octave: a dot on a little ladder */
        int32_t y = Q(15) - o * Q(3);
        if (o == scale_now.octave) disc(Q(214), y, Q(2) + 4, INK, 256);
        else if (!o) disc(Q(214), y, 10, GREY, 256);
    }
    if (batt != UINT8_MAX) {                    /* the battery: a spot, as full as the battery */
        ring(Q(228), Q(15), Q(7), 12, GREY, 256);
        disc(Q(228), Q(15), Q(2) + (int32_t)batt * Q(5) / 3, batt ? JADE : RED, 256);
    }
    line(Q(8), Q(29), Q(232), Q(29), LINE, 256);
    cv_blit(0, 0);
}

/* ----------------------------------------------------------------------------------------- the dials */
enum { DIAL_AMOUNT, DIAL_CHOICE, DIAL_BIPOLAR, DIAL_LENGTH, DIAL_SCALE, DIAL_MODE, DIAL_COUNT };
typedef struct { uint8_t kind; int16_t value, max; uint16_t color; } dial_t;

static void dial_draw(const dial_t *d, int32_t cx, int32_t cy, uint8_t lit)
{
    int32_t R = Q(17);
    uint16_t c = d->color;
    uint32_t base = lit ? 256u : 220u;
    if (lit) disc(cx, cy, R + Q(5), PAPER2, 256);
    switch (d->kind) {
    case DIAL_AMOUNT: {                         /* a 3/4 circle of dots, lit up to the value; a spot in the middle as big */
        int32_t n = 15, on = d->value * (n - 1) / (d->max ? d->max : 1);
        for (int32_t i = 0; i < n; i++) {
            uint32_t a = 24576u + (uint32_t)i * 49152u / (uint32_t)(n - 1);
            disc(polx(cx, R, a), poly(cy, R, a), i <= on && d->value ? (i == on ? Q(3) : Q(2)) : 18, i <= on && d->value ? c : LINE, base);
        }
        disc(cx, cy, Q(2) + d->value * Q(8) / (d->max ? d->max : 1), c, base);
        break;
    }
    case DIAL_BIPOLAR: {                        /* from the top, left or right */
        int32_t n = 15, mid = 7, on = mid + d->value * mid / (d->max ? d->max : 1);
        for (int32_t i = 0; i < n; i++) {
            uint32_t a = 24576u + (uint32_t)i * 49152u / (uint32_t)(n - 1);
            uint8_t lit_i = (i >= mid && i <= on) || (i <= mid && i >= on);
            disc(polx(cx, R, a), poly(cy, R, a), lit_i ? (i == on ? Q(3) : Q(2)) : 18, lit_i ? c : LINE, base);
        }
        disc(cx + d->value * Q(7) / (d->max ? d->max : 1), cy, Q(4), c, base);
        break;
    }
    case DIAL_LENGTH: {                         /* a ring of as many dots as steps */
        if (!d->value) { for (uint32_t i = 0; i < 12; i++) disc(polx(cx, R, i * 5461u), poly(cy, R, i * 5461u), 12, LINE, 256); break; }
        for (int32_t i = 0; i < d->value; i++) {
            uint32_t a = TOP + (uint32_t)i * 65536u / (uint32_t)d->value;
            disc(polx(cx, R, a), poly(cy, R, a), d->value > 20 ? 20 : 28, c, base);
        }
        disc(cx, cy, Q(2) + d->value * Q(6) / 32, c, base);
        break;
    }
    case DIAL_SCALE: {                          /* the scale's shape: twelve places, its notes filled */
        uint16_t mask = scales[d->value].mask;
        for (uint32_t i = 0; i < 12; i++) {
            uint32_t a = TOP + i * 5461u;
            if (mask >> i & 1u) disc(polx(cx, R, a), poly(cy, R, a), i ? Q(3) : Q(4), i ? c : INK, base);
            else disc(polx(cx, R, a), poly(cy, R, a), 14, LINE, 256);
        }
        disc(cx, cy, Q(2) + d->value * Q(6) / (int32_t)SCALE_COUNT, c, base);
        break;
    }
    case DIAL_MODE: {                           /* the scale's degrees, the one it starts on filled */
        for (int32_t i = 0; i < d->max; i++) {
            uint32_t a = TOP + (uint32_t)i * 65536u / (uint32_t)d->max;
            disc(polx(cx, R, a), poly(cy, R, a), i == d->value ? Q(4) : 22, i == d->value ? c : GREY, base);
        }
        ring(cx, cy, Q(6), 16, c, base);
        break;
    }
    case DIAL_COUNT: {                          /* 1..4: rings inside rings */
        for (int32_t i = 0; i <= d->value; i++) ring(cx, cy, Q(5) + i * Q(4), 18, c, base);
        for (uint32_t i = 0; i < 4; i++) disc(polx(cx, R, TOP + i * 16384u), poly(cy, R, TOP + i * 16384u), (int32_t)i <= d->value ? 30 : 14, (int32_t)i <= d->value ? c : LINE, base);
        break;
    }
    default: {                                  /* choices: as many dots around as there are, the one chosen big */
        for (int32_t i = 0; i < d->max; i++) {
            uint32_t a = TOP + (uint32_t)i * 65536u / (uint32_t)d->max;
            disc(polx(cx, R, a), poly(cy, R, a), i == d->value ? Q(4) : 22, i == d->value ? c : LINE, base);
        }
        break;
    }
    }
}

/* a little sign in the middle of a choice dial */
static void choice_sign(uint8_t pg, uint32_t k, int32_t cx, int32_t cy, uint16_t c)
{
    int32_t s = Q(7);
    if (pg == P_LFO && k == 2) {                /* the LFO's shape, as a shape */
        switch (parts[selected].lfo[K_SHAPE]) {
        case LFO_SINE: disc(cx, cy, s * 3 / 4, c, 256); break;
        case LFO_TRIANGLE: tri(cx, cy - s, cx - s, cy + s * 3 / 4, cx + s, cy + s * 3 / 4, c, 256); break;
        case LFO_SQUARE: rect((cx - s * 2 / 3) >> 4, (cy - s * 2 / 3) >> 4, s * 4 / 3 >> 4, s * 4 / 3 >> 4, c, 256); break;
        case LFO_SAW: tri(cx - s, cy + s * 3 / 4, cx + s, cy - s, cx + s, cy + s * 3 / 4, c, 256); break;
        case LFO_HOLD:
            for (int32_t j = -1; j <= 1; j += 2) for (int32_t i = -1; i <= 1; i += 2) disc(cx + i * s / 2, cy + j * s / 2, 30, c, 256);
            break;
        default: disc(cx - s / 3, cy, s / 2, c, 256); disc(cx + s / 3, cy + s / 4, s / 2, c, 256); break;
        }
    } else if (pg == P_LFO && k == 3) {         /* where it goes: up-down, a sparkle, a swell, left-right */
        switch (parts[selected].lfo[K_TARGET]) {
        case LT_PITCH: disc(cx, cy - s * 2 / 3, 34, c, 256); disc(cx, cy + s * 2 / 3, 34, c, 256); line(cx, cy - s / 2, cx, cy + s / 2, c, 256); break;
        case LT_TIMBRE: for (uint32_t i = 0; i < 4; i++) line(polx(cx, s, i * 8192u), poly(cy, s, i * 8192u), polx(cx, s, i * 8192u + 32768u), poly(cy, s, i * 8192u + 32768u), c, 256); break;
        case LT_VOLUME: ring(cx, cy, s, 16, c, 256); disc(cx, cy, s / 2, c, 256); break;
        default: disc(cx - s * 2 / 3, cy, 34, c, 256); disc(cx + s * 2 / 3, cy, 34, c, 256); line(cx - s / 2, cy, cx + s / 2, cy, c, 256); break;
        }
    } else if (pg == P_ARP && k == 0) {         /* the pattern: four dots in its order, joined */
        static const int8_t order[ARP_PATTERNS][4] = {
            {0, 1, 2, 3}, {3, 2, 1, 0}, {0, 2, 3, 1}, {0, 3, 1, 2}, {1, 2, 0, 3}, {2, 0, 3, 1}, {1, 3, 0, 2}, {0, 2, 1, 3}, {0, 0, 1, 2}
        };
        int32_t px = 0, py = 0;
        for (uint32_t i = 0; i < 4; i++) {
            int32_t h = order[arp.pattern][i];
            int32_t x = cx - s + (int32_t)i * s * 2 / 3, y = cy + s - h * s * 2 / 3;
            if (arp.pattern == ARP_RANDOM) y = cy + ((int32_t)((i * 7u + 3u) % 5u) - 2) * s / 3;
            disc(x, y, 26, c, 256);
            if (i) line(px, py, x, y, c, 180);
            px = x; py = y;
        }
    } else if (pg == P_ARP && k == 1) {         /* the rate: the more dots, the faster */
        uint32_t n = 1u + arp.rate;
        for (uint32_t i = 0; i < n && i < 9u; i++) {
            uint32_t a = TOP + i * 65536u / n;
            disc(polx(cx, s * 2 / 3, a), poly(cy, s * 2 / 3, a), 18, c, 256);
        }
    }
}

static void band_dials(dial_t d[4])
{
    const part_t *s = &parts[selected];
    uint16_t c = page_tint(st.page);
    for (uint32_t k = 0; k < 4; k++) { d[k].kind = DIAL_AMOUNT; d[k].max = 127; d[k].color = c; d[k].value = 0; }
    switch (st.page) {
    case P_HOME:
        for (uint32_t k = 0; k < 4; k++) {
            d[k].color = part_color[k];
            if (fm1_in.buttons & (1u << BTN_HOME)) { d[k].kind = DIAL_BIPOLAR; d[k].value = parts[k].pan; d[k].max = 64; }
            else d[k].value = parts[k].level;
        }
        break;
    case P_EDIT: for (uint32_t k = 0; k < 4; k++) d[k].value = s->p[k]; break;
    case P_ENV: for (uint32_t k = 0; k < 4; k++) d[k].value = s->env[k]; break;
    case P_LFO:
        d[0].value = s->lfo[K_RATE]; d[1].value = s->lfo[K_DEPTH];
        d[2].kind = DIAL_CHOICE; d[2].value = s->lfo[K_SHAPE]; d[2].max = LFO_SHAPES;
        d[3].kind = DIAL_CHOICE; d[3].value = s->lfo[K_TARGET]; d[3].max = LFO_TARGETS;
        break;
    case P_ARP:
        d[0].kind = DIAL_CHOICE; d[0].value = arp.pattern; d[0].max = ARP_PATTERNS;
        d[1].kind = DIAL_CHOICE; d[1].value = arp.rate; d[1].max = DIVS;
        d[2].kind = DIAL_COUNT; d[2].value = arp.range; d[2].max = 3;
        d[3].value = arp.wander;
        break;
    case P_SEQ:
        for (uint32_t k = 0; k < 4; k++) { d[k].kind = DIAL_LENGTH; d[k].value = tracks[k].length; d[k].max = STEPS; d[k].color = part_color[k]; }
        break;
    case P_SCALE:
        d[0].kind = DIAL_SCALE; d[0].value = scale_now.scale; d[0].max = SCALE_COUNT;
        d[1].kind = DIAL_MODE; d[1].value = scale_now.mode; d[1].max = scale_now.size;
        d[2].kind = DIAL_CHOICE; d[2].value = scale_now.gravity / 32; d[2].max = 4;
        d[3].value = scale_now.truth;
        break;
    case P_FX:
        d[0].value = fx.echo; d[1].value = fx.hall; d[2].value = fx.chorus; d[3].value = fx.dust;
        d[0].color = ORANGE; d[1].color = LILAC; d[2].color = TEAL; d[3].color = BROWN;
        break;
    default:
        d[0].value = (int16_t)(glo.bpm - 40); d[0].max = 200;
        d[1].value = (int16_t)(glo.swing - 50); d[1].max = 25;
        d[2].kind = DIAL_CHOICE; d[2].value = glo.ch; d[2].max = 17;
        d[3].kind = DIAL_BIPOLAR; d[3].value = glo.tune; d[3].max = 50;
        break;
    }
}

static void draw_band(uint32_t now)
{
    dial_t d[4];
    band_dials(d);
    uint32_t h = (uint32_t)st.page * 977u + selected;
    for (uint32_t k = 0; k < 4; k++) {
        uint8_t lit = now - knob_touched[k] < 700u;
        h = h * 31u + (uint32_t)d[k].kind * 7u + (uint32_t)(d[k].value + 1000) * 13u + (uint32_t)d[k].max * 3u + lit + d[k].color;
    }
    if (st.page == P_SCALE) h = h * 31u + scale_now.size;
    if (st.page == P_LFO) h = h * 31u + parts[selected].lfo[K_SHAPE] * 5u + parts[selected].lfo[K_TARGET];
    if (h == st.band_state) return;
    st.band_state = h;
    cv_begin(240, BAND_H, PAPER);
    sf_target(cv_px, 240, BAND_H);
    for (uint32_t k = 0; k < 4; k++) {
        int32_t cx = Q(30 + (int32_t)k * 60), cy = Q(26);
        dial_draw(&d[k], cx, cy, now - knob_touched[k] < 700u);
        if (d[k].kind == DIAL_CHOICE) choice_sign(st.page, k, cx, cy, d[k].color);
    }
    cv_blit(0, BAND_Y);
}

/* ------------------------------------------------------------------------------------------ the boot */
static void boot_frame(uint32_t age)
{
    sf_clear(PAPER);
    /* a long bent line, like a wave of bent birch */
    int32_t px = 0, py = 0;
    uint32_t drawn = age * 240u / 600u;
    for (int32_t x = 0; x <= 240 && (uint32_t)x <= drawn; x += 3) {
        int32_t y = Q(104) + (Q(9) * sinq((uint32_t)(x * 300 + 6000)) >> 15) + (Q(4) * sinq((uint32_t)(x * 690)) >> 15);
        if (x) { line(px, py, Q(x), y, INK, 256); line(px, py + 12, Q(x), y + 12, INK, 160); }
        px = Q(x); py = y;
    }
    for (uint32_t v = 0; v < PARTS; v++) {      /* four buoys drop onto it, one after the other */
        uint32_t start = 350u + v * 170u;
        if (age < start) continue;
        uint32_t u = age - start;
        int32_t x = 54 + (int32_t)v * 44;
        int32_t rest = Q(104) + (Q(9) * sinq((uint32_t)(x * 300 + 6000)) >> 15) + (Q(4) * sinq((uint32_t)(x * 690)) >> 15) - Q(13);
        int32_t y;
        if (u < 300u) { int32_t k = (int32_t)(300u - u); y = rest - k * k * Q(90) / 90000; }
        else y = rest - (Q(4) * sinq((u - 300u) * 30u) >> 15) * (int32_t)(u < 1100u ? 1100u - u : 0u) / 800;
        int32_t r = Q(13) - (int32_t)v * Q(1);
        if (u >= 300u && u < 1200u) ring(Q(x), rest + Q(13), r + (int32_t)(u - 300u) * Q(30) / 900, 14, part_color[v], (1200u - (u - 300u > 900u ? 900u : u - 300u)) * 256u / 1200u);
        disc(Q(x), y, r, part_color[v], 256);
    }
}

static void stage_present(void)
{
    lcd_blit(0, STAGE_Y, STAGE_W, STAGE_H, stage_px[st.cur]);
    st.cur ^= 1u;
}

static void stage_boot(void)
{
    lcd_fill(0, 0, 240, 240, PAPER);
    st.booting = 1;
    st.boot_at = fm1_ms;
    st.page = 0xFF;
}

/* ------------------------------------------------------------------------------------------ the frame */
static void stage_events(uint32_t now)
{
    while (st.ev_read != evq_w) {
        const note_ev_t *e = &evq[st.ev_read % EVQ];
        st.ev_read++;
        if (e->part >= PARTS) continue;
        st.last_pitch[e->part] = e->pitch;
        pt_t *p;
        switch (st.page) {
        case P_HOME:
            p = pt_new(now);
            p->kind = PK_RIPPLE; p->style = parts[e->part].alg; p->color = part_color[e->part];
            p->x = (int16_t)buoy_x(e->part); p->y = Q(76) - (int16_t)clampi((e->pitch - 60 * 256) * Q(10) / (24 * 256), -Q(14), Q(14));
            p->vx = p->vy = 0; p->born = now; p->life = (uint16_t)(500u + e->vel * 4u); p->size = 10;
            break;
        case P_SEQ:
            if (e->owner < OWNER_ARP && tracks[e->part].length) {
                int32_t x, y;
                seq_dot(e->part, track_pos(e->part), &x, &y);
                p = pt_new(now);
                p->kind = PK_RIPPLE; p->style = parts[e->part].alg; p->color = part_color[e->part];
                p->x = (int16_t)x; p->y = (int16_t)y; p->vx = p->vy = 0; p->born = now; p->life = 300; p->size = 2;
            }
            break;
        case P_SCALE: {
            p = pt_new(now);
            int32_t rel = ((e->pitch + 128) >> 8) - (SCALE_BASE + scale_now.transpose);
            rel = ((rel % 12) + 12) % 12;
            uint32_t a = TOP + (uint32_t)rel * 65536u / 12u;
            p->kind = PK_RIPPLE; p->style = ALG_GLASS; p->color = pc_color[(rel + scale_root_pc()) % 12];
            p->x = (int16_t)polx(Q(120), Q(60), a); p->y = (int16_t)poly(Q(82), Q(60), a);
            p->vx = p->vy = 0; p->born = now; p->life = 400; p->size = 9;
            break;
        }
        default: break;
        }
    }
    /* the algorithms' own little events, on the EDIT page */
    for (uint32_t v = 0; v < PARTS; v++) {
        uint32_t c = part_rt[v].clicks, g = part_rt[v].grains;
        uint32_t newc = c - st.clicks[v], newg = g - st.grains[v];
        st.clicks[v] = c; st.grains[v] = g;
        if (st.page != P_EDIT || v != selected) continue;
        const part_t *s = &parts[v];
        if (s->alg == ALG_GLASS && newc) {      /* CRACK: shards fly off the stone */
            uint32_t n = 2u + s->p[3] / 22u;
            for (uint32_t i = 0; i < n; i++) {
                pt_t *p = pt_new(now);
                uint32_t a = rnd_n(65536);
                p->kind = PK_SHARD; p->x = (int16_t)Q(120); p->y = (int16_t)Q(80);
                p->vx = (int16_t)(cosq(a) * Q(90) >> 15); p->vy = (int16_t)(sinq(a) * Q(90) >> 15);
                p->born = now; p->life = (uint16_t)(250u + (127u - s->p[3]) * 3u); p->size = (uint8_t)(6u + rnd_n(14)); p->color = i & 1u ? INK : SKY;
            }
        }
        if (s->alg == ALG_BIRCH && newc) {      /* the strike spreads through the wood */
            pt_t *p = pt_new(now);
            p->kind = PK_RIPPLE; p->style = ALG_BIRCH; p->x = (int16_t)Q(120); p->y = (int16_t)Q(80); p->vx = p->vy = 0;
            p->born = now; p->life = (uint16_t)(300u + s->p[2] * 8u); p->size = 6; p->color = BROWN;
        }
        if (s->alg == ALG_PULSE && newc) {      /* CLICK: a flash line */
            pt_t *p = pt_new(now);
            p->kind = PK_SHARD; p->x = (int16_t)Q(rnd_n(240)); p->y = (int16_t)Q(4); p->vx = 0; p->vy = (int16_t)Q(600);
            p->born = now; p->life = (uint16_t)(60u + s->p[0]); p->size = 150; p->color = RED;
        }
        if (s->alg == ALG_DOTS && newg) {       /* a dot for each grain, up for the high ones */
            if (newg > 4u) newg = 4u;
            for (uint32_t i = 0; i < newg; i++) {
                pt_t *p = pt_new(now);
                int32_t step = dots_steps[part_rt[v].grain_step & 7u];
                int32_t spread = 20 + s->p[1] * 80 / 127;
                p->kind = PK_DOT;
                p->x = (int16_t)Q(120 + (int32_t)rnd_n((uint32_t)spread * 2u) - spread);
                p->y = (int16_t)(Q(80) - step * Q(3) + Q((int32_t)rnd_n(20) - 10));
                p->vx = (int16_t)((int32_t)rnd_n(Q(10)) - Q(5)); p->vy = (int16_t)(-Q(4));
                p->born = now; p->life = (uint16_t)(160u + s->p[2] * 5u); p->size = (uint8_t)(3u + s->p[2] / 16u);
                p->color = (rnd_n(3) == 0) ? INK : part_color[v];
            }
        }
    }
}

static void stage_service(uint32_t now)
{
    /* the meters, smoothed: up at once, down slowly */
    for (uint32_t p = 0; p < PARTS; p++) {
        int32_t m = part_rt[p].meter * 3;
        m = m > 32767 ? 32767 : m;
        st.meter[p] = m > st.meter[p] ? m : st.meter[p] - (st.meter[p] >> 3) - 1;
        if (st.meter[p] < 0) st.meter[p] = 0;
    }
    {
        int32_t m = out_meter * 3;
        m = m > 32767 ? 32767 : m;
        st.out = m > st.out ? m : st.out - (st.out >> 3) - 1;
        if (st.out < 0) st.out = 0;
    }
    if (now - st.last_frame < FRAME_MS) return;
    st.last_frame = now;

    sf_target(stage_px[st.cur], STAGE_W, STAGE_H);
    if (st.booting) {
        uint32_t age = now - st.boot_at;
        boot_frame(age);
        stage_present();
        if (age < 1900u && !(fm1_in.buttons || fm1_in.notes) && !st.dice_at) return;
        st.booting = 0;
        lcd_sync();
    }
    if (st.page != page) {
        st.page = page;
        st.page_at = now;
        pts_clear();
        st.head_x = 0;
        st.header_state = ~0u;
        st.band_state = ~0u;
    }
    stage_events(now);
    sf_target(stage_px[st.cur], STAGE_W, STAGE_H);
#ifdef STAGE_PROFILE
    PROF_BEGIN
#endif
    sf_clear(PAPER);
    switch (st.page) {
    case P_HOME: anim_home(now); break;
    case P_EDIT: anim_edit(now); break;
    case P_ENV: anim_env(now); break;
    case P_LFO: anim_lfo(now); break;
    case P_ARP: anim_arp(now); break;
    case P_SEQ: anim_seq(now); break;
    case P_SCALE: anim_scale(now); break;
    case P_FX: anim_fx(now); break;
    default: anim_glo(now); break;
    }
    pts_draw(now);
    if (now - st.dice_at < 700u && st.dice_at) {   /* the dice: a handful of confetti */
        uint32_t age = now - st.dice_at;
        for (uint32_t i = 0; i < 24u; i++) {
            uint32_t h = (i + 1u) * 2654435761u ^ st.dice_at * 7u;
            uint32_t a = h & 0xFFFFu;
            int32_t r = (int32_t)age * Q(110) / 700;
            disc(polx(Q(120), r, a), poly(Q(80), r * 3 / 4, a), Q(3) + (int32_t)(h >> 28), part_color[(h >> 20) % PARTS], 256u - age * 256u / 700u);
        }
    }
    if (now - st.wipe_at < 500u && st.wipe_at) {  /* a track wiped: a paper sweep */
        int32_t x = (int32_t)(now - st.wipe_at) * 260 / 500 - 10;
        rect(0, 0, x, STAGE_H, PAPER, 200);
        rect(x, 0, 3, STAGE_H, part_color[selected], 256);
    }
    draw_overlay(now);
#ifdef STAGE_PROFILE
    PROF_END(st.page)
#endif
    stage_present();
    draw_header(now);
    draw_band(now);
}
