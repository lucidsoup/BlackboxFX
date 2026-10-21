/* TAPE: stereo delay with wow, flutter and random drift on the read head,
 * a saturating feedback loop, tone low-pass and a low-cut in the loop.
 * Delay-time changes are slewed, so turning TIME bends pitch like tape. */
#include "fx.h"

#define MIN_DELAY_S 0.02f

void bfx_tape_alloc(bfx_tape *t, bfx_arena *a, float sr, float seconds)
{
    /* extra room for the wow excursion and interpolation */
    uint32_t len = (uint32_t)(sr * seconds + sr * 0.05f) + 4u;
    bfx_ring_alloc(&t->bl, a, len);
    bfx_ring_alloc(&t->br, a, len);
    t->max_delay = sr * seconds;
}

void bfx_tape_reset(bfx_tape *t, float sr)
{
    bfx_ring_clear(&t->bl);
    bfx_ring_clear(&t->br);
    t->time_s = sr * 0.3f;
    t->lp_l = t->lp_r = t->hp_l = t->hp_r = 0.0f;
    t->wow_phase = t->flutter_phase = 0.0f;
    t->drift = t->drift_target = 0.0f;
    t->rng.s = 0x2545F491u;
}

void bfx_tape_process(bfx_tape *t, float sr, float mix, float time,
                      float feedback, float wow, float tone,
                      float *l, float *r, int n)
{
    float target = bfx_expmap(time, MIN_DELAY_S * sr, t->max_delay);
    float slew = bfx_onepole_coef(3.0f, sr);
    float fb = feedback * 1.1f;
    float lp_c = bfx_onepole_coef(bfx_expmap(tone, 800.0f, 14000.0f), sr);
    float hp_c = bfx_onepole_coef(80.0f, sr);
    float wow_inc = 0.7f / sr, flutter_inc = 6.3f / sr;
    float wow_depth = wow * wow * 0.006f * sr;       /* up to 6 ms */
    float flutter_depth = wow * 0.0004f * sr;
    float drift_c = bfx_onepole_coef(0.3f, sr);
    float cross = 0.25f;                             /* gentle ping-pong */

    for (int i = 0; i < n; i++) {
        t->time_s += slew * (target - t->time_s);

        t->wow_phase += wow_inc;
        if (t->wow_phase >= 1.0f) {
            t->wow_phase -= 1.0f;
            t->drift_target = (bfx_rng_f(&t->rng) * 2.0f - 1.0f) * wow_depth;
        }
        t->flutter_phase += flutter_inc;
        if (t->flutter_phase >= 1.0f) t->flutter_phase -= 1.0f;
        t->drift += drift_c * (t->drift_target - t->drift);

        float mod = wow_depth * (0.5f + 0.5f * bfx_sin01(t->wow_phase))
                  + flutter_depth * (0.5f + 0.5f * bfx_sin01(t->flutter_phase))
                  + 0.5f * t->drift;
        float d = bfx_clampf(t->time_s + mod, 2.0f, (float)t->bl.mask - 2.0f);

        float yl = bfx_ring_tap(&t->bl, d);
        float yr = bfx_ring_tap(&t->br, d);

        /* tone and low-cut inside the loop so repeats darken and thin */
        t->lp_l += lp_c * (yl - t->lp_l);
        t->lp_r += lp_c * (yr - t->lp_r);
        t->hp_l += hp_c * (t->lp_l - t->hp_l);
        t->hp_r += hp_c * (t->lp_r - t->hp_r);
        float el = t->lp_l - t->hp_l;
        float er = t->lp_r - t->hp_r;

        float fl = (1.0f - cross) * el + cross * er;
        float fr = (1.0f - cross) * er + cross * el;
        bfx_ring_write(&t->bl, bfx_sat(l[i] + fb * fl));
        bfx_ring_write(&t->br, bfx_sat(r[i] + fb * fr));

        l[i] += mix * el;
        r[i] += mix * er;
    }
}
