/* Engine tests: memory contract, bypass, silence, stability, looper,
 * pitch, delay timing and determinism. Exit code is the failure count. */
#include "bfx.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SR 44100
static int failures = 0;

#define CHECK(cond, ...) do { \
    if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); \
                   printf(__VA_ARGS__); printf("\n"); } } while (0)

typedef struct rig {
    void *arena, *mem;
    bfx_engine *e;
} rig;

static rig make(const bfx_config *cfg)
{
    rig r;
    size_t need = bfx_memory_required(cfg);
    r.arena = malloc(need);
    r.mem = malloc(bfx_engine_size());
    r.e = bfx_init(r.mem, r.arena, need, cfg);
    return r;
}

static void drop(rig *r) { free(r->arena); free(r->mem); }

/* Every parameter at its quietest so the chain should be transparent. */
static void neutral(bfx_engine *e)
{
    bfx_set_param(e, BFX_CHAR_AMOUNT, 0.0f);
    bfx_set_param(e, BFX_CHAR_TONE, 1.0f);
    bfx_set_param(e, BFX_MOVE_DEPTH, 0.0f);
    bfx_set_param(e, BFX_CLOUD_MIX, 0.0f);
    bfx_set_param(e, BFX_TAPE_MIX, 0.0f);
    bfx_set_param(e, BFX_TAPE_FEEDBACK, 0.0f);
    bfx_set_param(e, BFX_SPACE_MIX, 0.0f);
    bfx_set_param(e, BFX_MIX, 1.0f);
    bfx_set_param(e, BFX_OUTPUT, 0.5f);
    bfx_reset(e);
}

static float goertzel(const float *x, int n, float hz)
{
    float w = 2.0f * 3.14159265f * hz / SR, c = 2.0f * cosf(w);
    float s1 = 0, s2 = 0;
    for (int i = 0; i < n; i++) {
        float s0 = x[i] + c * s1 - s2;
        s2 = s1; s1 = s0;
    }
    return s1 * s1 + s2 * s2 - c * s1 * s2;
}

static void test_memory(void)
{
    bfx_config cfg = bfx_device_config();
    size_t need = bfx_memory_required(&cfg);
    void *mem = malloc(bfx_engine_size());
    void *arena = malloc(need);
    CHECK(need > 0, "memory_required is zero");
    CHECK(need < 1200u * 1024u, "device config needs %zu bytes, budget is 1.2 MB", need);
    CHECK(bfx_init(mem, arena, need - 64, &cfg) == NULL, "init accepted a short arena");
    CHECK(bfx_init(mem, arena, need, &cfg) != NULL, "init rejected the exact arena");
    free(mem);
    free(arena);
}

static void test_bypass(void)
{
    bfx_config cfg = bfx_default_config();
    rig r = make(&cfg);
    neutral(r.e);
    int n = SR / 4;
    float *l = malloc(n * sizeof(float)), *rr = malloc(n * sizeof(float));
    float *ol = malloc(n * sizeof(float)), *or_ = malloc(n * sizeof(float));
    for (int i = 0; i < n; i++) {
        l[i] = 0.5f * sinf(i * 0.031f);
        rr[i] = 0.4f * sinf(i * 0.047f);
    }
    bfx_process(r.e, l, rr, ol, or_, n);
    float err = 0;
    for (int i = 0; i < n; i++) {
        err = fmaxf(err, fabsf(ol[i] - l[i]));
        err = fmaxf(err, fabsf(or_[i] - rr[i]));
    }
    CHECK(err < 1e-5f, "neutral chain is not transparent (max err %g)", err);
    free(l); free(rr); free(ol); free(or_); drop(&r);
}

static void test_silence_and_stability(void)
{
    bfx_config cfg = bfx_device_config();
    int n = SR * 2;
    float *l = malloc(n * sizeof(float)), *rr = malloc(n * sizeof(float));
    float *ol = malloc(n * sizeof(float)), *or_ = malloc(n * sizeof(float));
    for (int p = 0; p < bfx_preset_count(); p++) {
        rig r = make(&cfg);
        bfx_load_preset(r.e, p);
        bfx_reset(r.e);

        memset(l, 0, n * sizeof(float));
        memset(rr, 0, n * sizeof(float));
        bfx_process(r.e, l, rr, ol, or_, n);
        float peak = 0;
        for (int i = 0; i < n; i++) peak = fmaxf(peak, fmaxf(fabsf(ol[i]), fabsf(or_[i])));
        CHECK(peak < 1e-6f, "preset '%s' makes sound from silence (%g)", bfx_preset_name(p), peak);

        /* hammer it: loud noise with every feedback path wide open */
        bfx_set_param(r.e, BFX_TAPE_MIX, 1.0f);
        bfx_set_param(r.e, BFX_TAPE_FEEDBACK, 1.0f);
        bfx_set_param(r.e, BFX_SPACE_MIX, 1.0f);
        bfx_set_param(r.e, BFX_SPACE_DECAY, 1.0f);
        bfx_set_param(r.e, BFX_SPACE_SHIMMER, 1.0f);
        bfx_set_param(r.e, BFX_CLOUD_MIX, 1.0f);
        bfx_set_param(r.e, BFX_OUTPUT, 1.0f);
        unsigned s = 1u + (unsigned)p;
        for (int i = 0; i < n; i++) {
            s = s * 1664525u + 1013904223u;
            l[i] = rr[i] = ((float)(s >> 8) / 8388608.0f - 1.0f);
        }
        int bad = 0;
        for (int pass = 0; pass < 4; pass++) {
            if (pass == 2) { memset(l, 0, n * sizeof(float)); memset(rr, 0, n * sizeof(float)); }
            bfx_process(r.e, l, rr, ol, or_, n);
            for (int i = 0; i < n; i++)
                if (!(fabsf(ol[i]) <= 1.0f) || !(fabsf(or_[i]) <= 1.0f)) bad++;
        }
        CHECK(bad == 0, "preset '%s' produced %d out-of-range/NaN samples", bfx_preset_name(p), bad);
        drop(&r);
    }
    free(l); free(rr); free(ol); free(or_);
}

static float rms(const float *x, int n)
{
    double acc = 0;
    for (int i = 0; i < n; i++) acc += (double)x[i] * x[i];
    return (float)sqrt(acc / n);
}

/* After the input stops every preset's tail must die away. Case -1 is the
 * worst reverb setting: full decay with full shimmer. */
static void test_tails_decay(void)
{
    bfx_config cfg = bfx_device_config();
    int n = SR;
    float *l = malloc(n * sizeof(float)), *ol = malloc(n * sizeof(float)), *or_ = malloc(n * sizeof(float));
    for (int p = -1; p < bfx_preset_count(); p++) {
        rig r = make(&cfg);
        if (p >= 0) {
            bfx_load_preset(r.e, p);
        } else {
            bfx_set_param(r.e, BFX_SPACE_MIX, 1.0f);
            bfx_set_param(r.e, BFX_SPACE_DECAY, 1.0f);
            bfx_set_param(r.e, BFX_SPACE_SHIMMER, 1.0f);
        }
        bfx_reset(r.e);
        unsigned s = 99u;
        for (int i = 0; i < n; i++) {
            s = s * 1664525u + 1013904223u;
            l[i] = 0.3f * ((float)(s >> 8) / 8388608.0f - 1.0f);
        }
        bfx_process(r.e, l, l, ol, or_, n);
        memset(l, 0, n * sizeof(float));
        bfx_process(r.e, l, l, ol, or_, n);
        float first = rms(ol, n);
        for (int k = 0; k < 5; k++) bfx_process(r.e, l, l, ol, or_, n);
        float last = rms(ol, n);
        const char *name = p >= 0 ? bfx_preset_name(p) : "max decay + shimmer";
        CHECK(last < first * 0.5f, "'%s' tail does not decay (%g -> %g)", name, first, last);
        drop(&r);
    }
    free(l); free(ol); free(or_);
}

static void test_looper(void)
{
    bfx_config cfg = bfx_device_config();
    rig r = make(&cfg);
    neutral(r.e);
    bfx_set_param(r.e, BFX_LOOP_LEVEL, 1.0f);
    bfx_reset(r.e);

    int n = 4000;
    float in[4000], zero[4000] = { 0 }, ol[4000], or_[4000];
    for (int i = 0; i < n; i++) in[i] = 0.5f * sinf(i * 0.05f);

    bfx_loop(r.e, BFX_LOOP_RECORD);
    CHECK(bfx_loop_state_get(r.e) == BFX_LOOPER_RECORDING, "record did not start");
    bfx_process(r.e, in, in, ol, or_, n);
    bfx_loop(r.e, BFX_LOOP_RECORD);
    CHECK(bfx_loop_state_get(r.e) == BFX_LOOPER_PLAYING, "second press did not play");
    CHECK(bfx_loop_length(r.e) == (uint32_t)n, "loop length %u, expected %d", bfx_loop_length(r.e), n);

    bfx_process(r.e, zero, zero, ol, or_, n);
    float err = 0;
    for (int i = 300; i < n - 300; i++) err = fmaxf(err, fabsf(ol[i] - in[i]));
    CHECK(err < 1e-3f, "loop playback differs from recording (max err %g)", err);

    bfx_loop(r.e, BFX_LOOP_STOP);
    bfx_process(r.e, zero, zero, ol, or_, n);
    float peak = 0;
    for (int i = 0; i < n; i++) peak = fmaxf(peak, fabsf(ol[i]));
    CHECK(peak == 0.0f, "stopped looper still plays (%g)", peak);

    bfx_loop(r.e, BFX_LOOP_CLEAR);
    CHECK(bfx_loop_state_get(r.e) == BFX_LOOPER_EMPTY && bfx_loop_length(r.e) == 0, "clear failed");
    drop(&r);
}

static void test_octave_up(void)
{
    bfx_config cfg = bfx_default_config();
    rig r = make(&cfg);
    neutral(r.e);
    bfx_set_param(r.e, BFX_CLOUD_MIX, 1.0f);
    bfx_set_param(r.e, BFX_CLOUD_PITCH, 0.2f);      /* +12 */
    bfx_set_param(r.e, BFX_CLOUD_SPREAD, 0.0f);
    bfx_set_param(r.e, BFX_CLOUD_DENSITY, 0.6f);
    bfx_set_param(r.e, BFX_MIX, 1.0f);
    bfx_reset(r.e);

    int n = SR;
    float *in = malloc(n * sizeof(float)), *ol = malloc(n * sizeof(float)), *or_ = malloc(n * sizeof(float));
    for (int i = 0; i < n; i++) in[i] = 0.3f * sinf(2.0f * 3.14159265f * 330.0f * i / SR);
    bfx_process(r.e, in, in, ol, or_, n);
    /* subtract the dry signal to look at the cloud alone */
    for (int i = 0; i < n; i++) ol[i] -= in[i];
    float e1 = goertzel(ol + SR / 2, SR / 2, 330.0f);
    float e2 = goertzel(ol + SR / 2, SR / 2, 660.0f);
    CHECK(e2 > 20.0f * e1, "octave-up cloud: 660 Hz energy %g vs 330 Hz %g", e2, e1);
    free(in); free(ol); free(or_); drop(&r);
}

static void test_delay_time(void)
{
    bfx_config cfg = bfx_default_config();
    rig r = make(&cfg);
    neutral(r.e);
    bfx_set_param(r.e, BFX_TAPE_MIX, 1.0f);
    bfx_set_param(r.e, BFX_TAPE_TIME, 0.5f);
    bfx_set_param(r.e, BFX_TAPE_WOW, 0.0f);
    bfx_set_param(r.e, BFX_TAPE_TONE, 1.0f);
    bfx_reset(r.e);

    /* tape_time 0.5 -> geometric mean of 20 ms and 2.5 s */
    float expect = sqrtf(0.02f * cfg.tape_seconds) * SR;
    int n = SR * 2;
    float *in = calloc(n, sizeof(float)), *ol = malloc(n * sizeof(float)), *or_ = malloc(n * sizeof(float));
    /* the slewed time settles first; then send a click */
    bfx_process(r.e, in, in, ol, or_, n);
    in[0] = 1.0f;
    bfx_process(r.e, in, in, ol, or_, n);
    int peak_at = 1;
    for (int i = 1; i < n; i++) if (fabsf(ol[i]) > fabsf(ol[peak_at])) peak_at = i;
    CHECK(fabsf(peak_at - expect) < 0.01f * expect + 4.0f,
          "echo at %d samples, expected ~%.0f", peak_at, expect);
    free(in); free(ol); free(or_); drop(&r);
}

static void test_determinism(void)
{
    bfx_config cfg = bfx_device_config();
    rig a = make(&cfg), b = make(&cfg);
    bfx_load_preset(a.e, 7); bfx_reset(a.e);
    bfx_load_preset(b.e, 7); bfx_reset(b.e);
    int n = SR / 2;
    float *in = malloc(n * sizeof(float));
    float *al = malloc(n * sizeof(float)), *ar = malloc(n * sizeof(float));
    float *bl = malloc(n * sizeof(float)), *br = malloc(n * sizeof(float));
    for (int i = 0; i < n; i++) in[i] = 0.4f * sinf(i * 0.02f) * sinf(i * 0.0003f);
    bfx_process(a.e, in, in, al, ar, n);
    /* odd block sizes must not change the result */
    for (int off = 0; off < n; off += 77) {
        int m = n - off < 77 ? n - off : 77;
        bfx_process(b.e, in + off, in + off, bl + off, br + off, m);
    }
    int diff = 0;
    for (int i = 0; i < n; i++) diff += (al[i] != bl[i]) || (ar[i] != br[i]);
    /* parameter smoothing runs per internal block, so only exact equality
     * of settled settings is guaranteed: presets were reset, so expect none */
    CHECK(diff == 0, "%d samples differ between block sizes", diff);
    free(in); free(al); free(ar); free(bl); free(br); drop(&a); drop(&b);
}

int main(void)
{
    test_memory();
    test_bypass();
    test_silence_and_stability();
    test_tails_decay();
    test_looper();
    test_octave_up();
    test_delay_time();
    test_determinism();
    if (failures) printf("%d check(s) failed\n", failures);
    else printf("all tests passed\n");
    return failures ? 1 : 0;
}
