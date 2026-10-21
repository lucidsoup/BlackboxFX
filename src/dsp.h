/* Shared DSP building blocks. Internal to the engine. */
#ifndef BFX_DSP_H
#define BFX_DSP_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

#define BFX_PI 3.14159265358979f
#define BFX_TWO_PI 6.28318530717959f

static inline float bfx_clampf(float x, float lo, float hi)
{
    return x < lo ? lo : (x > hi ? hi : x);
}

static inline float bfx_lerpf(float a, float b, float t) { return a + (b - a) * t; }

/* Exponential map of 0..1 onto lo..hi (both > 0). */
static inline float bfx_expmap(float t, float lo, float hi)
{
    return lo * powf(hi / lo, t);
}

/* Rational tanh approximation, exact at 0 and saturating at +-1. */
static inline float bfx_sat(float x)
{
    x = bfx_clampf(x, -3.0f, 3.0f);
    float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

/* sin(2*pi*p) for p in [0,1), parabolic approximation with one refinement
 * step (max error ~0.001). Avoids libm in the per-sample path. */
static inline float bfx_sin01(float p)
{
    float x = p - (float)(int)p;           /* wrap to [0,1) for p >= 0 */
    x = (x < 0.5f) ? x : x - 1.0f;         /* [-0.5, 0.5) */
    float y = 8.0f * x - 16.0f * x * (x < 0.0f ? -x : x);
    return 0.225f * (y * (y < 0.0f ? -y : y) - y) + y;
}

static inline uint32_t bfx_next_pow2(uint32_t n)
{
    uint32_t p = 1;
    while (p < n) p <<= 1;
    return p;
}

/* ---- arena ---------------------------------------------------------- */

/* With base == NULL the arena only counts bytes; allocations return NULL. */
typedef struct bfx_arena {
    uint8_t *base;
    size_t used;
    size_t cap;
    int overflow;
} bfx_arena;

static inline void *bfx_alloc(bfx_arena *a, size_t bytes)
{
    size_t start = (a->used + 7u) & ~(size_t)7u;
    a->used = start + bytes;
    if (!a->base) return NULL;
    if (a->used > a->cap) { a->overflow = 1; return NULL; }
    memset(a->base + start, 0, bytes);
    return a->base + start;
}

/* ---- random --------------------------------------------------------- */

typedef struct bfx_rng { uint32_t s; } bfx_rng;

static inline uint32_t bfx_rng_u32(bfx_rng *r)
{
    uint32_t x = r->s;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return r->s = x;
}

/* Uniform in [0,1). */
static inline float bfx_rng_f(bfx_rng *r)
{
    return (float)(bfx_rng_u32(r) >> 8) * (1.0f / 16777216.0f);
}

/* ---- int16 ring buffer ---------------------------------------------- */

/* Samples are stored with 6 dB of headroom: +-2.0 full scale. */
#define BFX_RING_SCALE 16384.0f

typedef struct bfx_ring {
    int16_t *d;
    uint32_t mask;
    uint32_t w;      /* next write index */
} bfx_ring;

static inline void bfx_ring_alloc(bfx_ring *r, bfx_arena *a, uint32_t min_len)
{
    uint32_t len = bfx_next_pow2(min_len < 2 ? 2 : min_len);
    r->d = (int16_t *)bfx_alloc(a, len * sizeof(int16_t));
    r->mask = len - 1;
    r->w = 0;
}

static inline void bfx_ring_clear(bfx_ring *r)
{
    if (r->d) memset(r->d, 0, (r->mask + 1) * sizeof(int16_t));
    r->w = 0;
}

static inline int16_t bfx_to_i16(float x)
{
    float s = bfx_clampf(x * BFX_RING_SCALE, -32767.0f, 32767.0f);
    return (int16_t)(s < 0.0f ? s - 0.5f : s + 0.5f);
}

static inline float bfx_ring_at(const bfx_ring *r, uint32_t idx)
{
    return (float)r->d[idx & r->mask] * (1.0f / BFX_RING_SCALE);
}

static inline void bfx_ring_write(bfx_ring *r, float x)
{
    r->d[r->w] = bfx_to_i16(x);
    r->w = (r->w + 1) & r->mask;
}

/* Read 'delay' samples behind the most recent write (delay >= 1). */
static inline float bfx_ring_tap(const bfx_ring *r, float delay)
{
    float pos = (float)r->w - delay;
    int32_t i = (int32_t)floorf(pos);
    float f = pos - (float)i;
    float a = bfx_ring_at(r, (uint32_t)i);
    float b = bfx_ring_at(r, (uint32_t)(i + 1));
    return a + (b - a) * f;
}

/* ---- float delay line (short lines: chorus, reverb, pitch shift) ------ */

typedef struct bfx_fdelay {
    float *d;
    uint32_t mask;
    uint32_t w;
} bfx_fdelay;

static inline void bfx_fdelay_alloc(bfx_fdelay *l, bfx_arena *a, uint32_t min_len)
{
    uint32_t len = bfx_next_pow2(min_len < 2 ? 2 : min_len);
    l->d = (float *)bfx_alloc(a, len * sizeof(float));
    l->mask = len - 1;
    l->w = 0;
}

static inline void bfx_fdelay_clear(bfx_fdelay *l)
{
    if (l->d) memset(l->d, 0, (l->mask + 1) * sizeof(float));
    l->w = 0;
}

static inline void bfx_fdelay_write(bfx_fdelay *l, float x)
{
    l->d[l->w] = x;
    l->w = (l->w + 1) & l->mask;
}

static inline float bfx_fdelay_read(const bfx_fdelay *l, uint32_t delay)
{
    return l->d[(l->w - delay) & l->mask];
}

static inline float bfx_fdelay_tap(const bfx_fdelay *l, float delay)
{
    float pos = (float)l->w - delay;
    int32_t i = (int32_t)floorf(pos);
    float f = pos - (float)i;
    float a = l->d[(uint32_t)i & l->mask];
    float b = l->d[(uint32_t)(i + 1) & l->mask];
    return a + (b - a) * f;
}

/* ---- filters -------------------------------------------------------- */

static inline float bfx_onepole_coef(float hz, float sr)
{
    float c = 1.0f - expf(-BFX_TWO_PI * hz / sr);
    return bfx_clampf(c, 0.0f, 1.0f);
}

/* Topology-preserving state variable filter. */
typedef struct bfx_svf { float ic1, ic2; } bfx_svf;

typedef struct bfx_svf_out { float lp, bp, hp; } bfx_svf_out;

static inline bfx_svf_out bfx_svf_tick(bfx_svf *s, float x, float g, float k)
{
    float a1 = 1.0f / (1.0f + g * (g + k));
    float v3 = x - s->ic2;
    float v1 = a1 * s->ic1 + g * a1 * v3;
    float v2 = s->ic2 + g * v1;
    s->ic1 = 2.0f * v1 - s->ic1;
    s->ic2 = 2.0f * v2 - s->ic2;
    bfx_svf_out o = { v2, v1, x - k * v1 - v2 };
    return o;
}

static inline float bfx_svf_g(float hz, float sr)
{
    return tanf(BFX_PI * bfx_clampf(hz, 10.0f, sr * 0.45f) / sr);
}

#endif
