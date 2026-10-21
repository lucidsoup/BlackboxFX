/* CLOUD: granular engine. The input is captured continuously into a ring;
 * overlapping Hann-windowed grains are spawned from points behind the write
 * head, each with its own pitch ratio, direction and stereo position. With
 * freeze on, capture stops and the cloud keeps re-reading the held audio. */
#include "fx.h"

#define MIN_LAG_MS 5.0f

void bfx_cloud_alloc(bfx_cloud *c, bfx_arena *a, float sr, float seconds)
{
    uint32_t len = (uint32_t)(sr * seconds);
    bfx_ring_alloc(&c->bl, a, len);
    bfx_ring_alloc(&c->br, a, len);
}

void bfx_cloud_reset(bfx_cloud *c)
{
    bfx_ring_clear(&c->bl);
    bfx_ring_clear(&c->br);
    memset(c->g, 0, sizeof(c->g));
    c->countdown = 0.0f;
    c->rng.s = 0x9E3779B9u;
}

static float pick_ratio(bfx_cloud *c, float pitch)
{
    static const float harmonic[] = { 0.5f, 1.0f, 1.0f, 1.4983f, 2.0f };
    int mode = (int)(bfx_clampf(pitch, 0.0f, 0.999f) * 6.0f);
    switch (mode) {
    case 0: return 1.0f;
    case 1: return 2.0f;
    case 2: return 0.5f;
    case 3: return 1.4983f;                       /* just fifth */
    case 4: return harmonic[bfx_rng_u32(&c->rng) % 5u];
    default: {                                    /* +-15 cent detune */
        float cents = (bfx_rng_f(&c->rng) * 2.0f - 1.0f) * 15.0f;
        return powf(2.0f, cents / 1200.0f);
    }
    }
}

static void spawn(bfx_cloud *c, float sr, float len, float pitch, float spread,
                  float reverse)
{
    bfx_grain *g = NULL;
    for (int i = 0; i < BFX_MAX_GRAINS; i++) {
        if (!c->g[i].active) { g = &c->g[i]; break; }
    }
    if (!g) return;

    float ratio = pick_ratio(c, pitch);
    int backwards = bfx_rng_f(&c->rng) < reverse;
    uint32_t cap = c->bl.mask + 1u;

    /* A forward grain faster than 1x gains on the write head by
     * len*(ratio-1) samples; start far enough back that it never crosses.
     * A backward grain walks into the past and can start close to it. */
    float lag = MIN_LAG_MS * 0.001f * sr;
    if (!backwards && ratio > 1.0f) lag += len * (ratio - 1.0f);
    /* the far end of the read (backwards travel, plus capture that keeps
     * overwriting the oldest audio while the grain plays) must stay valid */
    float room = (float)cap - lag - len * (ratio + 1.0f) - 2.0f;
    if (room < 0.0f) room = 0.0f;
    float reach = bfx_clampf(spread * spread * 4.0f * sr, 0.0f, room);
    lag += bfx_rng_f(&c->rng) * reach;

    g->active = 1;
    g->ipos = (int32_t)c->bl.w - (int32_t)lag;
    g->frac = 0.0f;
    g->inc = backwards ? -ratio : ratio;
    g->age = 0.0f;
    g->len = len;
    float pan = 0.5f + (bfx_rng_f(&c->rng) - 0.5f) * 0.8f;
    g->gl = bfx_sin01(0.25f * (1.0f - pan));   /* cos(pan*pi/2) */
    g->gr = bfx_sin01(0.25f * pan);
}

void bfx_cloud_process(bfx_cloud *c, float sr, float mix, float size,
                       float density, float pitch, float spread, float reverse,
                       int freeze, float *l, float *r, int n)
{
    int idle = mix < 0.001f;
    float len = bfx_expmap(size, 0.02f, 0.5f) * sr;
    float overlap = 1.0f + density * 7.0f;
    float interval = len / overlap;
    float norm = mix / sqrtf(overlap);

    for (int i = 0; i < n; i++) {
        if (!freeze) {
            bfx_ring_write(&c->bl, l[i]);
            bfx_ring_write(&c->br, r[i]);
        }
        if (idle) continue;

        c->countdown -= 1.0f;
        if (c->countdown <= 0.0f) {
            spawn(c, sr, len, pitch, spread, reverse);
            /* +-25% jitter keeps the cloud from buzzing at the spawn rate */
            c->countdown += interval * (0.75f + 0.5f * bfx_rng_f(&c->rng));
        }

        float wl = 0.0f, wr = 0.0f;
        for (int k = 0; k < BFX_MAX_GRAINS; k++) {
            bfx_grain *g = &c->g[k];
            if (!g->active) continue;

            float t = g->age / g->len;
            float w = bfx_sin01(t * 0.5f);
            w *= w;                                    /* Hann */

            uint32_t i0 = (uint32_t)g->ipos;
            float sl = bfx_ring_at(&c->bl, i0);
            float sr_ = bfx_ring_at(&c->br, i0);
            sl += (bfx_ring_at(&c->bl, i0 + 1u) - sl) * g->frac;
            sr_ += (bfx_ring_at(&c->br, i0 + 1u) - sr_) * g->frac;

            /* collapse to mono and re-pan so the cloud spreads wide */
            float m = 0.5f * (sl + sr_) * w;
            wl += m * g->gl;
            wr += m * g->gr;

            g->frac += g->inc;
            float whole = floorf(g->frac);
            g->ipos += (int32_t)whole;
            g->frac -= whole;
            g->age += 1.0f;
            if (g->age >= g->len) g->active = 0;
        }

        l[i] += wl * norm * 1.4f;
        r[i] += wr * norm * 1.4f;
    }

    if (idle) {
        memset(c->g, 0, sizeof(c->g));
        c->countdown = 0.0f;
    }
}
