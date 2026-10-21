/* LOOPER: record, overdub, play, reverse and half-speed.
 * A short fade at the loop seam hides the splice click. */
#include "fx.h"

#define SEAM 256.0f

void bfx_looper_alloc(bfx_looper *lp, bfx_arena *a, float sr, float seconds)
{
    /* flat buffers of exactly the loop capacity; power-of-two rounding
     * would nearly double the largest allocation in the engine */
    lp->cap = (uint32_t)(sr * seconds);
    if (lp->cap < 2u) lp->cap = 2u;
    lp->bl.d = (int16_t *)bfx_alloc(a, lp->cap * sizeof(int16_t));
    lp->br.d = (int16_t *)bfx_alloc(a, lp->cap * sizeof(int16_t));
}

void bfx_looper_reset(bfx_looper *lp)
{
    if (lp->bl.d) memset(lp->bl.d, 0, lp->cap * sizeof(int16_t));
    if (lp->br.d) memset(lp->br.d, 0, lp->cap * sizeof(int16_t));
    lp->len = 0;
    lp->pos = 0.0f;
    lp->state = BFX_LOOPER_EMPTY;
    lp->reverse = 0;
    lp->half = 0;
}

void bfx_looper_cmd(bfx_looper *lp, int cmd)
{
    switch (cmd) {
    case BFX_LOOP_RECORD:
        switch (lp->state) {
        case BFX_LOOPER_EMPTY:
            lp->len = 0;
            lp->pos = 0.0f;
            lp->state = BFX_LOOPER_RECORDING;
            break;
        case BFX_LOOPER_RECORDING:
            lp->state = lp->len > 0 ? BFX_LOOPER_PLAYING : BFX_LOOPER_EMPTY;
            lp->pos = 0.0f;
            break;
        case BFX_LOOPER_PLAYING:
            lp->state = BFX_LOOPER_OVERDUBBING;
            break;
        case BFX_LOOPER_OVERDUBBING:
            lp->state = BFX_LOOPER_PLAYING;
            break;
        case BFX_LOOPER_STOPPED:
            lp->state = BFX_LOOPER_OVERDUBBING;
            lp->pos = 0.0f;
            break;
        }
        break;
    case BFX_LOOP_PLAY:
        if (lp->len > 0 && lp->state != BFX_LOOPER_RECORDING) {
            if (lp->state == BFX_LOOPER_STOPPED) lp->pos = 0.0f;
            lp->state = BFX_LOOPER_PLAYING;
        }
        break;
    case BFX_LOOP_STOP:
        if (lp->state == BFX_LOOPER_RECORDING)
            lp->state = lp->len > 0 ? BFX_LOOPER_STOPPED : BFX_LOOPER_EMPTY;
        else if (lp->len > 0)
            lp->state = BFX_LOOPER_STOPPED;
        break;
    case BFX_LOOP_CLEAR:
        bfx_looper_reset(lp);
        break;
    case BFX_LOOP_REVERSE:
        lp->reverse = !lp->reverse;
        break;
    case BFX_LOOP_HALF:
        lp->half = !lp->half;
        break;
    }
}

static float seam_gain(float pos, float len)
{
    float g = 1.0f;
    if (len > SEAM * 4.0f) {
        if (pos < SEAM) g = pos / SEAM;
        else if (len - pos < SEAM) g = (len - pos) / SEAM;
    }
    return g;
}

void bfx_looper_process(bfx_looper *lp, float level, const float *in_l,
                        const float *in_r, float *out_l, float *out_r, int n)
{
    if (!lp->bl.d) return;

    for (int i = 0; i < n; i++) {
        if (lp->state == BFX_LOOPER_RECORDING) {
            if (lp->len >= lp->cap) {           /* full: close the loop */
                lp->state = BFX_LOOPER_PLAYING;
                lp->pos = 0.0f;
            } else {
                lp->bl.d[lp->len] = bfx_to_i16(in_l[i]);
                lp->br.d[lp->len] = bfx_to_i16(in_r[i]);
                lp->len++;
                continue;
            }
        }
        if (lp->state != BFX_LOOPER_PLAYING && lp->state != BFX_LOOPER_OVERDUBBING)
            continue;

        float len = (float)lp->len;
        uint32_t i0 = (uint32_t)lp->pos;
        uint32_t i1 = (i0 + 1u < lp->len) ? i0 + 1u : 0u;
        float f = lp->pos - (float)i0;
        const float k = 1.0f / BFX_RING_SCALE;
        float a = lp->bl.d[i0] * k, b = lp->bl.d[i1] * k;
        float c = lp->br.d[i0] * k, d = lp->br.d[i1] * k;
        float g = seam_gain(lp->pos, len) * level;
        float pl = (a + (b - a) * f);
        float pr = (c + (d - c) * f);

        if (lp->state == BFX_LOOPER_OVERDUBBING) {
            lp->bl.d[i0] = bfx_to_i16(bfx_sat(pl + in_l[i]));
            lp->br.d[i0] = bfx_to_i16(bfx_sat(pr + in_r[i]));
        }

        out_l[i] += pl * g;
        out_r[i] += pr * g;

        float step = lp->half ? 0.5f : 1.0f;
        lp->pos += lp->reverse ? -step : step;
        if (lp->pos >= len) lp->pos -= len;
        if (lp->pos < 0.0f) lp->pos += len;
    }
}
