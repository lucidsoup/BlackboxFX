/*
 * BlackboxFX - multi-effects engine for the M-VAVE ANN BlackBox (and desktop).
 *
 * Signal flow (stereo):
 *
 *   in -> [looper, pre] -> CHARACTER -> MOVEMENT -> CLOUD (granular)
 *      -> TAPE (delay) -> SPACE (reverb + shimmer) -> [looper, post]
 *      -> dry/wet MIX -> OUTPUT -> limiter -> out
 *
 * The engine never calls malloc. The caller provides one memory arena sized
 * with bfx_memory_required(); all long buffers live there, stored as int16 to
 * halve memory on the device. Processing is float32.
 */
#ifndef BFX_H
#define BFX_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct bfx_config {
    float sample_rate;      /* the BlackBox runs at 44100 */
    float cloud_seconds;    /* granular capture buffer length */
    float tape_seconds;     /* maximum delay time */
    float loop_seconds;     /* maximum loop length */
    float space_scale;      /* maximum reverb size multiplier (1..3) */
} bfx_config;

/* Generous desktop defaults. */
bfx_config bfx_default_config(void);
/* Small footprint for the device (about 1.1 MB). The BlackBox's usable
 * RAM is not yet known; see docs/HARDWARE.md. */
bfx_config bfx_device_config(void);

typedef enum bfx_param {
    BFX_CHAR_TYPE,      /* 0 drive, 1/3 fold, 2/3 crush */
    BFX_CHAR_AMOUNT,
    BFX_CHAR_TONE,

    BFX_MOVE_TYPE,      /* tremolo, chorus, vibrato, auto-filter (quarters) */
    BFX_MOVE_RATE,
    BFX_MOVE_DEPTH,

    BFX_CLOUD_MIX,
    BFX_CLOUD_SIZE,     /* grain length 20 ms .. 500 ms */
    BFX_CLOUD_DENSITY,  /* overlapping grains 1 .. 8 */
    BFX_CLOUD_PITCH,    /* unison, +oct, -oct, +5th, harmonic, detune (sixths) */
    BFX_CLOUD_SPREAD,   /* how far back in time grains are picked from */
    BFX_CLOUD_REVERSE,  /* probability a grain plays backwards */
    BFX_CLOUD_FREEZE,   /* >= 0.5 holds the capture buffer */

    BFX_TAPE_MIX,
    BFX_TAPE_TIME,
    BFX_TAPE_FEEDBACK,  /* above ~0.9 it self-oscillates, saturating safely */
    BFX_TAPE_WOW,
    BFX_TAPE_TONE,

    BFX_SPACE_MIX,
    BFX_SPACE_DECAY,
    BFX_SPACE_DAMP,
    BFX_SPACE_SHIMMER,

    BFX_LOOP_LEVEL,
    BFX_LOOP_PRE,       /* >= 0.5 loops the dry input before the effects */

    BFX_MIX,            /* global dry/wet crossfade */
    BFX_OUTPUT,         /* output gain, 0.5 is unity */

    BFX_PARAM_COUNT
} bfx_param;

typedef enum bfx_loop_cmd {
    BFX_LOOP_RECORD,    /* empty: record; recording: close loop and play;
                           playing: overdub; overdubbing: back to play */
    BFX_LOOP_PLAY,
    BFX_LOOP_STOP,
    BFX_LOOP_CLEAR,
    BFX_LOOP_REVERSE,   /* toggle */
    BFX_LOOP_HALF,      /* toggle half speed */
} bfx_loop_cmd;

typedef enum bfx_loop_state {
    BFX_LOOPER_EMPTY,
    BFX_LOOPER_RECORDING,
    BFX_LOOPER_PLAYING,
    BFX_LOOPER_OVERDUBBING,
    BFX_LOOPER_STOPPED,
} bfx_loop_state;

typedef struct bfx_engine bfx_engine;

size_t bfx_engine_size(void);
size_t bfx_memory_required(const bfx_config *cfg);

/* engine_mem must hold bfx_engine_size() bytes, 8-byte aligned.
 * Returns NULL if arena_bytes is too small. */
bfx_engine *bfx_init(void *engine_mem, void *arena, size_t arena_bytes,
                     const bfx_config *cfg);

void bfx_reset(bfx_engine *e);

/* In-place is allowed (out == in). n may be any length. */
void bfx_process(bfx_engine *e, const float *in_l, const float *in_r,
                 float *out_l, float *out_r, int n);

void  bfx_set_param(bfx_engine *e, bfx_param p, float value01);
float bfx_get_param(const bfx_engine *e, bfx_param p);
const char *bfx_param_name(bfx_param p);
int   bfx_param_from_name(const char *name); /* -1 if unknown */

int         bfx_preset_count(void);
const char *bfx_preset_name(int index);
void        bfx_load_preset(bfx_engine *e, int index);

void           bfx_loop(bfx_engine *e, bfx_loop_cmd cmd);
bfx_loop_state bfx_loop_state_get(const bfx_engine *e);
uint32_t       bfx_loop_length(const bfx_engine *e);

#ifdef __cplusplus
}
#endif

#endif
