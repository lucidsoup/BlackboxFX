/* SPACE: input diffusion into an 8-line feedback delay network with a
 * Hadamard mixing matrix, per-line damping and a slowly modulated pair of
 * lines. SHIMMER feeds an octave-up copy of the tail back into the input. */
#include "fx.h"

static const float k_line_ms[BFX_FDN] = {
    35.3f, 36.7f, 33.8f, 32.2f, 29.0f, 30.7f, 26.9f, 25.3f
};
static const float k_diff_ms[BFX_DIFFUSERS] = { 4.8f, 3.6f, 12.7f, 9.3f };

#define SHIM_WINDOW 2048u

void bfx_space_alloc(bfx_space *s, bfx_arena *a, float sr, float max_scale)
{
    s->max_scale = bfx_clampf(max_scale, 1.0f, 3.0f);
    for (int i = 0; i < BFX_FDN; i++) {
        s->base_len[i] = k_line_ms[i] * 0.001f * sr;
        bfx_fdelay_alloc(&s->line[i], a,
                         (uint32_t)(s->base_len[i] * s->max_scale) + 64u);
    }
    for (int i = 0; i < BFX_DIFFUSERS; i++) {
        s->diff_len[i] = (uint32_t)(k_diff_ms[i] * 0.001f * sr);
        bfx_fdelay_alloc(&s->diff[i], a, s->diff_len[i] + 1u);
    }
    bfx_fdelay_alloc(&s->shim, a, SHIM_WINDOW * 2u);
}

void bfx_space_reset(bfx_space *s)
{
    for (int i = 0; i < BFX_FDN; i++) {
        bfx_fdelay_clear(&s->line[i]);
        s->damp[i] = 0.0f;
    }
    for (int i = 0; i < BFX_DIFFUSERS; i++) bfx_fdelay_clear(&s->diff[i]);
    bfx_fdelay_clear(&s->shim);
    s->shim_phase = 0.0f;
    s->shim_lp = 0.0f;
    s->mod_phase = 0.0f;
}

static inline float allpass(bfx_fdelay *d, uint32_t len, float x, float g)
{
    float z = bfx_fdelay_read(d, len);
    float v = x + g * z;
    bfx_fdelay_write(d, v);
    return z - g * v;
}

static inline void hadamard8(float *v)
{
    for (int h = 1; h < 8; h <<= 1) {
        for (int i = 0; i < 8; i += h << 1) {
            for (int j = i; j < i + h; j++) {
                float a = v[j], b = v[j + h];
                v[j] = a + b;
                v[j + h] = a - b;
            }
        }
    }
    for (int i = 0; i < 8; i++) v[i] *= 0.35355339f;   /* 1/sqrt(8) */
}

/* Two-tap delay pitch shifter fixed at +12 semitones. */
static float shimmer_shift(bfx_space *s, float x)
{
    bfx_fdelay_write(&s->shim, x);
    const float win = (float)SHIM_WINDOW;
    s->shim_phase += 1.0f / win;              /* ratio 2: delay shrinks 1/sample */
    if (s->shim_phase >= 1.0f) s->shim_phase -= 1.0f;
    float p2 = s->shim_phase + 0.5f;
    if (p2 >= 1.0f) p2 -= 1.0f;
    float d1 = (1.0f - s->shim_phase) * win + 1.0f;
    float d2 = (1.0f - p2) * win + 1.0f;
    float g1 = bfx_sin01(s->shim_phase * 0.5f);
    float g2 = bfx_sin01(p2 * 0.5f);
    return bfx_fdelay_tap(&s->shim, d1) * g1 * g1
         + bfx_fdelay_tap(&s->shim, d2) * g2 * g2;
}

void bfx_space_process(bfx_space *s, float sr, float mix, float decay,
                       float damp, float shimmer, float *l, float *r, int n)
{
    if (mix < 0.001f) return;

    float scale = bfx_lerpf(0.6f, s->max_scale, decay);
    float rt60 = bfx_expmap(decay, 0.4f, 30.0f);
    float damp_c = bfx_onepole_coef(bfx_expmap(1.0f - damp, 1500.0f, 18000.0f), sr);
    float mod_inc = 0.37f / sr;
    float mod_depth = 0.0012f * sr;

    float len[BFX_FDN], gain[BFX_FDN];
    for (int i = 0; i < BFX_FDN; i++) {
        len[i] = s->base_len[i] * scale;
        /* per-pass gain that reaches -60 dB after rt60 seconds */
        gain[i] = powf(10.0f, -3.0f * len[i] / (sr * rt60));
        if (gain[i] > 0.9995f) gain[i] = 0.9995f;
    }
    /* Shimmer crossfades (never adds) an octave-up copy into one line's
     * recirculation, so the loop gain stays at or below the line's decay
     * and even maximum shimmer cannot run away. */
    float shim_amt = shimmer * 0.85f;

    for (int i = 0; i < n; i++) {
        s->mod_phase += mod_inc;
        if (s->mod_phase >= 1.0f) s->mod_phase -= 1.0f;
        float mod = mod_depth * (1.0f + bfx_sin01(s->mod_phase));

        float in = 0.5f * (l[i] + r[i]);
        float side = 0.5f * (l[i] - r[i]);
        for (int k = 0; k < BFX_DIFFUSERS; k++)
            in = allpass(&s->diff[k], s->diff_len[k], in, 0.6f);

        float v[BFX_FDN];
        for (int k = 0; k < BFX_FDN; k++) {
            float d = len[k] + ((k < 2) ? (k ? mod : mod_depth * 2.0f - mod) : 0.0f);
            float y = bfx_fdelay_tap(&s->line[k], d);
            s->damp[k] += damp_c * (y - s->damp[k]);
            v[k] = s->damp[k] * gain[k];
        }

        if (shim_amt > 0.001f) {
            float shifted = shimmer_shift(s, v[0]);
            /* soften the shifter's splice and the stacked octaves */
            s->shim_lp += 0.35f * (shifted - s->shim_lp);
            v[0] += shim_amt * (s->shim_lp - v[0]);
        }

        float out_l = v[0] + v[2] + v[4] + v[6];
        float out_r = v[1] + v[3] + v[5] + v[7];

        hadamard8(v);
        for (int k = 0; k < BFX_FDN; k++) {
            /* alternate the sign of the side feed to keep stereo input wide */
            float feed = in + ((k & 1) ? -side : side) * 0.5f;
            bfx_fdelay_write(&s->line[k], v[k] + feed * 0.5f);
        }

        l[i] += mix * 0.5f * out_l;
        r[i] += mix * 0.5f * out_r;
    }
}
