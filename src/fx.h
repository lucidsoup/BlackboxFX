/* Effect modules. Each processes a stereo block in place. Internal. */
#ifndef BFX_FX_H
#define BFX_FX_H

#include "bfx.h"
#include "dsp.h"

/* ---- CHARACTER: drive / fold / crush + tone ------------------------- */

typedef struct bfx_character {
    float tone_l, tone_r;
    float hold_l, hold_r, hold_phase;
    float dc_x_l, dc_y_l, dc_x_r, dc_y_r;
} bfx_character;

void bfx_character_reset(bfx_character *c);
void bfx_character_process(bfx_character *c, float sr, float type, float amount,
                           float tone, float *l, float *r, int n);

/* ---- MOVEMENT: tremolo / chorus / vibrato / auto-filter ------------- */

typedef struct bfx_movement {
    bfx_fdelay dl, dr;
    bfx_svf fl, fr;
    float phase;
} bfx_movement;

void bfx_movement_alloc(bfx_movement *m, bfx_arena *a, float sr);
void bfx_movement_reset(bfx_movement *m);
void bfx_movement_process(bfx_movement *m, float sr, float type, float rate,
                          float depth, float *l, float *r, int n);

/* ---- CLOUD: granular pitch/time engine ------------------------------ */

#define BFX_MAX_GRAINS 16

typedef struct bfx_grain {
    int active;
    int32_t ipos;      /* integer read index into the capture ring */
    float frac;
    float inc;         /* playback ratio; negative plays backwards */
    float age, len;
    float gl, gr;      /* constant-power pan gains */
} bfx_grain;

typedef struct bfx_cloud {
    bfx_ring bl, br;
    bfx_grain g[BFX_MAX_GRAINS];
    float countdown;
    bfx_rng rng;
} bfx_cloud;

void bfx_cloud_alloc(bfx_cloud *c, bfx_arena *a, float sr, float seconds);
void bfx_cloud_reset(bfx_cloud *c);
void bfx_cloud_process(bfx_cloud *c, float sr, float mix, float size,
                       float density, float pitch, float spread, float reverse,
                       int freeze, float *l, float *r, int n);

/* ---- TAPE: modulated, saturating stereo delay ----------------------- */

typedef struct bfx_tape {
    bfx_ring bl, br;
    float max_delay;
    float time_s;      /* slewed delay time in samples; slewing bends pitch */
    float lp_l, lp_r, hp_l, hp_r;
    float wow_phase, flutter_phase, drift, drift_target;
    bfx_rng rng;
} bfx_tape;

void bfx_tape_alloc(bfx_tape *t, bfx_arena *a, float sr, float seconds);
void bfx_tape_reset(bfx_tape *t, float sr);
void bfx_tape_process(bfx_tape *t, float sr, float mix, float time,
                      float feedback, float wow, float tone,
                      float *l, float *r, int n);

/* ---- SPACE: 8-line FDN reverb with shimmer -------------------------- */

#define BFX_FDN 8
#define BFX_DIFFUSERS 4

typedef struct bfx_space {
    bfx_fdelay line[BFX_FDN];
    float base_len[BFX_FDN];
    float damp[BFX_FDN];
    bfx_fdelay diff[BFX_DIFFUSERS];
    uint32_t diff_len[BFX_DIFFUSERS];
    bfx_fdelay shim;   /* octave-up pitch shifter buffer */
    float shim_phase;
    float shim_lp;
    float mod_phase;
    float max_scale;
} bfx_space;

void bfx_space_alloc(bfx_space *s, bfx_arena *a, float sr, float max_scale);
void bfx_space_reset(bfx_space *s);
void bfx_space_process(bfx_space *s, float sr, float mix, float decay,
                       float damp, float shimmer, float *l, float *r, int n);

/* ---- LOOPER --------------------------------------------------------- */

typedef struct bfx_looper {
    bfx_ring bl, br;   /* only .d is used: flat buffers of cap samples */
    uint32_t cap;
    uint32_t len;
    float pos;
    int state;
    int reverse;
    int half;
} bfx_looper;

void bfx_looper_alloc(bfx_looper *lp, bfx_arena *a, float sr, float seconds);
void bfx_looper_reset(bfx_looper *lp);
void bfx_looper_cmd(bfx_looper *lp, int cmd);
/* Records 'in' as configured and adds playback (times level) into out. */
void bfx_looper_process(bfx_looper *lp, float level, const float *in_l,
                        const float *in_r, float *out_l, float *out_r, int n);

#endif
