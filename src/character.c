/* CHARACTER: the colour stage. Drive, wavefold or bit/sample-rate crush,
 * followed by a tilt-style tone low-pass and a DC blocker. */
#include "fx.h"

void bfx_character_reset(bfx_character *c)
{
    memset(c, 0, sizeof(*c));
}

/* Triangle wavefolder: reflects anything past +-1 back into range. */
static float fold(float x)
{
    float t = (x + 1.0f) * 0.25f;          /* period of 4 in x -> 1 in t */
    t -= floorf(t);
    return (t < 0.5f) ? (4.0f * t - 1.0f) : (3.0f - 4.0f * t);
}

void bfx_character_process(bfx_character *c, float sr, float type, float amount,
                           float tone, float *l, float *r, int n)
{
    if (amount < 0.001f && tone > 0.999f) return;

    int mode = (int)(bfx_clampf(type, 0.0f, 0.999f) * 3.0f);
    float tone_c = bfx_onepole_coef(bfx_expmap(tone, 400.0f, 20000.0f), sr);
    float dc_r = 1.0f - BFX_TWO_PI * 20.0f / sr;

    float gain = 1.0f + amount * amount * 40.0f;
    float makeup = 1.0f / (1.0f + amount * 1.5f);
    float steps = powf(2.0f, 16.0f - amount * 12.0f) * 0.5f; /* 16 -> 4 bits */
    float hold_inc = 1.0f / (1.0f + amount * amount * 24.0f);

    for (int i = 0; i < n; i++) {
        float xl = l[i], xr = r[i];

        if (amount >= 0.001f) {
            switch (mode) {
            case 0:
                xl = bfx_sat(xl * gain) * makeup;
                xr = bfx_sat(xr * gain) * makeup;
                break;
            case 1: {
                float g = 1.0f + amount * 6.0f;
                xl = fold(xl * g) * makeup;
                xr = fold(xr * g) * makeup;
                break;
            }
            default:
                c->hold_phase += hold_inc;
                if (c->hold_phase >= 1.0f) {
                    c->hold_phase -= 1.0f;
                    c->hold_l = floorf(xl * steps + 0.5f) / steps;
                    c->hold_r = floorf(xr * steps + 0.5f) / steps;
                }
                xl = c->hold_l;
                xr = c->hold_r;
                break;
            }
        }

        c->tone_l += tone_c * (xl - c->tone_l);
        c->tone_r += tone_c * (xr - c->tone_r);
        xl = c->tone_l;
        xr = c->tone_r;

        /* fold and crush can add DC; drive is symmetric but keep it uniform */
        float yl = xl - c->dc_x_l + dc_r * c->dc_y_l;
        float yr = xr - c->dc_x_r + dc_r * c->dc_y_r;
        c->dc_x_l = xl; c->dc_y_l = yl;
        c->dc_x_r = xr; c->dc_y_r = yr;

        l[i] = yl;
        r[i] = yr;
    }
}
