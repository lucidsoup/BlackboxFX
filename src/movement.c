/* MOVEMENT: LFO-driven modulation. Tremolo, chorus, vibrato, auto-filter. */
#include "fx.h"

#define CHORUS_MAX_MS 30.0f

void bfx_movement_alloc(bfx_movement *m, bfx_arena *a, float sr)
{
    uint32_t len = (uint32_t)(sr * CHORUS_MAX_MS * 0.001f) + 4u;
    bfx_fdelay_alloc(&m->dl, a, len);
    bfx_fdelay_alloc(&m->dr, a, len);
}

void bfx_movement_reset(bfx_movement *m)
{
    bfx_fdelay_clear(&m->dl);
    bfx_fdelay_clear(&m->dr);
    memset(&m->fl, 0, sizeof(m->fl));
    memset(&m->fr, 0, sizeof(m->fr));
    m->phase = 0.0f;
}

void bfx_movement_process(bfx_movement *m, float sr, float type, float rate,
                          float depth, float *l, float *r, int n)
{
    if (depth < 0.001f) return;

    int mode = (int)(bfx_clampf(type, 0.0f, 0.999f) * 4.0f);
    float hz = bfx_expmap(rate, 0.05f, 12.0f);
    float inc = hz / sr;
    float ms = sr * 0.001f;

    for (int i = 0; i < n; i++) {
        m->phase += inc;
        if (m->phase >= 1.0f) m->phase -= 1.0f;
        float lfo_l = bfx_sin01(m->phase);
        float lfo_r = bfx_sin01(m->phase + 0.25f);   /* quadrature for width */
        float xl = l[i], xr = r[i];

        switch (mode) {
        case 0: { /* tremolo, panning slightly at high depth */
            float gl = 1.0f - depth * (0.5f + 0.5f * lfo_l);
            float gr = 1.0f - depth * (0.5f + 0.5f * bfx_sin01(m->phase + 0.5f * depth));
            l[i] = xl * gl;
            r[i] = xr * gr;
            break;
        }
        case 1:   /* chorus: 50/50 blend with a swept 7 ms tap */
        case 2: { /* vibrato: tap only, shallower sweep */
            bfx_fdelay_write(&m->dl, xl);
            bfx_fdelay_write(&m->dr, xr);
            float sweep = (mode == 1) ? 5.0f : 3.0f;
            float dl = (8.0f + sweep * depth * lfo_l) * ms;
            float dr = (8.0f + sweep * depth * lfo_r) * ms;
            float wl = bfx_fdelay_tap(&m->dl, dl);
            float wr = bfx_fdelay_tap(&m->dr, dr);
            if (mode == 1) {
                l[i] = 0.7f * xl + 0.7f * wl;
                r[i] = 0.7f * xr + 0.7f * wr;
            } else {
                l[i] = wl;
                r[i] = wr;
            }
            break;
        }
        default: { /* auto-filter: resonant low-pass sweep */
            float base = 0.5f + 0.5f * lfo_l;
            float cut = bfx_expmap(1.0f - depth + depth * base, 150.0f, 8000.0f);
            float g = bfx_svf_g(cut, sr);
            float k = 0.6f;
            l[i] = bfx_svf_tick(&m->fl, xl, g, k).lp;
            r[i] = bfx_svf_tick(&m->fr, xr, g, k).lp;
            break;
        }
        }
    }
}
