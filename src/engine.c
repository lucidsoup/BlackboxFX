/* Engine: parameter handling, smoothing, presets and the effect chain. */
#include "fx.h"

#define BLOCK 32

struct bfx_engine {
    bfx_config cfg;
    float target[BFX_PARAM_COUNT];
    float value[BFX_PARAM_COUNT];
    float smooth_c;

    bfx_character character;
    bfx_movement movement;
    bfx_cloud cloud;
    bfx_tape tape;
    bfx_space space;
    bfx_looper looper;

    float dry_l[BLOCK], dry_r[BLOCK];
};

typedef struct param_info {
    const char *name;
    float def;
    int stepped;   /* mode selectors and switches jump instead of gliding */
} param_info;

static const param_info k_params[BFX_PARAM_COUNT] = {
    [BFX_CHAR_TYPE]     = { "char_type",     0.0f,  1 },
    [BFX_CHAR_AMOUNT]   = { "char_amount",   0.0f,  0 },
    [BFX_CHAR_TONE]     = { "char_tone",     1.0f,  0 },
    [BFX_MOVE_TYPE]     = { "move_type",     0.3f,  1 },
    [BFX_MOVE_RATE]     = { "move_rate",     0.4f,  0 },
    [BFX_MOVE_DEPTH]    = { "move_depth",    0.0f,  0 },
    [BFX_CLOUD_MIX]     = { "cloud_mix",     0.0f,  0 },
    [BFX_CLOUD_SIZE]    = { "cloud_size",    0.5f,  0 },
    [BFX_CLOUD_DENSITY] = { "cloud_density", 0.4f,  0 },
    [BFX_CLOUD_PITCH]   = { "cloud_pitch",   0.0f,  1 },
    [BFX_CLOUD_SPREAD]  = { "cloud_spread",  0.3f,  0 },
    [BFX_CLOUD_REVERSE] = { "cloud_reverse", 0.0f,  0 },
    [BFX_CLOUD_FREEZE]  = { "cloud_freeze",  0.0f,  1 },
    [BFX_TAPE_MIX]      = { "tape_mix",      0.0f,  0 },
    [BFX_TAPE_TIME]     = { "tape_time",     0.5f,  0 },
    [BFX_TAPE_FEEDBACK] = { "tape_feedback", 0.4f,  0 },
    [BFX_TAPE_WOW]      = { "tape_wow",      0.2f,  0 },
    [BFX_TAPE_TONE]     = { "tape_tone",     0.6f,  0 },
    [BFX_SPACE_MIX]     = { "space_mix",     0.0f,  0 },
    [BFX_SPACE_DECAY]   = { "space_decay",   0.5f,  0 },
    [BFX_SPACE_DAMP]    = { "space_damp",    0.4f,  0 },
    [BFX_SPACE_SHIMMER] = { "space_shimmer", 0.0f,  0 },
    [BFX_LOOP_LEVEL]    = { "loop_level",    0.8f,  0 },
    [BFX_LOOP_PRE]      = { "loop_pre",      0.0f,  1 },
    [BFX_MIX]           = { "mix",           1.0f,  0 },
    [BFX_OUTPUT]        = { "output",        0.5f,  0 },
};

bfx_config bfx_default_config(void)
{
    bfx_config c = { 44100.0f, 6.0f, 2.5f, 60.0f, 3.0f };
    return c;
}

bfx_config bfx_device_config(void)
{
    bfx_config c = { 44100.0f, 1.4f, 1.4f, 2.5f, 1.5f };
    return c;
}

size_t bfx_engine_size(void) { return sizeof(bfx_engine); }

static void layout(bfx_engine *e, bfx_arena *a)
{
    float sr = e->cfg.sample_rate;
    bfx_movement_alloc(&e->movement, a, sr);
    bfx_cloud_alloc(&e->cloud, a, sr, e->cfg.cloud_seconds);
    bfx_tape_alloc(&e->tape, a, sr, e->cfg.tape_seconds);
    bfx_space_alloc(&e->space, a, sr, e->cfg.space_scale);
    bfx_looper_alloc(&e->looper, a, sr, e->cfg.loop_seconds);
}

size_t bfx_memory_required(const bfx_config *cfg)
{
    static bfx_engine scratch;          /* only pointer fields are written */
    bfx_arena a = { NULL, 0, 0, 0 };
    scratch.cfg = *cfg;
    layout(&scratch, &a);
    return a.used + 8u;                 /* slack for base alignment */
}

bfx_engine *bfx_init(void *engine_mem, void *arena, size_t arena_bytes,
                     const bfx_config *cfg)
{
    if (!engine_mem || !arena || !cfg || cfg->sample_rate < 8000.0f) return NULL;
    bfx_engine *e = (bfx_engine *)engine_mem;
    memset(e, 0, sizeof(*e));
    e->cfg = *cfg;

    uintptr_t base = ((uintptr_t)arena + 7u) & ~(uintptr_t)7u;
    size_t skew = (size_t)(base - (uintptr_t)arena);
    if (arena_bytes < skew) return NULL;
    bfx_arena a = { (uint8_t *)base, 0, arena_bytes - skew, 0 };
    layout(e, &a);
    if (a.overflow) return NULL;

    e->smooth_c = bfx_onepole_coef(20.0f, cfg->sample_rate / (float)BLOCK);
    for (int p = 0; p < BFX_PARAM_COUNT; p++)
        e->target[p] = e->value[p] = k_params[p].def;
    bfx_reset(e);
    return e;
}

void bfx_reset(bfx_engine *e)
{
    bfx_character_reset(&e->character);
    bfx_movement_reset(&e->movement);
    bfx_cloud_reset(&e->cloud);
    bfx_tape_reset(&e->tape, e->cfg.sample_rate);
    bfx_space_reset(&e->space);
    bfx_looper_reset(&e->looper);
    for (int p = 0; p < BFX_PARAM_COUNT; p++) e->value[p] = e->target[p];
}

void bfx_set_param(bfx_engine *e, bfx_param p, float v)
{
    if ((unsigned)p >= BFX_PARAM_COUNT) return;
    v = bfx_clampf(v, 0.0f, 1.0f);
    e->target[p] = v;
    if (k_params[p].stepped) e->value[p] = v;
}

float bfx_get_param(const bfx_engine *e, bfx_param p)
{
    return ((unsigned)p < BFX_PARAM_COUNT) ? e->target[p] : 0.0f;
}

const char *bfx_param_name(bfx_param p)
{
    return ((unsigned)p < BFX_PARAM_COUNT) ? k_params[p].name : "?";
}

int bfx_param_from_name(const char *name)
{
    for (int p = 0; p < BFX_PARAM_COUNT; p++)
        if (strcmp(name, k_params[p].name) == 0) return p;
    return -1;
}

void bfx_loop(bfx_engine *e, bfx_loop_cmd cmd) { bfx_looper_cmd(&e->looper, (int)cmd); }

bfx_loop_state bfx_loop_state_get(const bfx_engine *e)
{
    return (bfx_loop_state)e->looper.state;
}

uint32_t bfx_loop_length(const bfx_engine *e) { return e->looper.len; }

/* Transparent below 0.9, then a smooth knee that never exceeds 1.0. */
static inline float limit(float x)
{
    float a = x < 0.0f ? -x : x;
    if (a <= 0.9f) return x;
    float over = (a - 0.9f) * 10.0f;
    float y = 0.9f + 0.1f * bfx_sat(over);
    if (y > 1.0f) y = 1.0f;
    return x < 0.0f ? -y : y;
}

static void process_block(bfx_engine *e, float *l, float *r, int n)
{
    const float *v = e->value;
    float sr = e->cfg.sample_rate;
    int loop_pre = v[BFX_LOOP_PRE] >= 0.5f;

    memcpy(e->dry_l, l, (size_t)n * sizeof(float));
    memcpy(e->dry_r, r, (size_t)n * sizeof(float));

    if (loop_pre)
        bfx_looper_process(&e->looper, v[BFX_LOOP_LEVEL], e->dry_l, e->dry_r, l, r, n);

    bfx_character_process(&e->character, sr, v[BFX_CHAR_TYPE], v[BFX_CHAR_AMOUNT],
                          v[BFX_CHAR_TONE], l, r, n);
    bfx_movement_process(&e->movement, sr, v[BFX_MOVE_TYPE], v[BFX_MOVE_RATE],
                         v[BFX_MOVE_DEPTH], l, r, n);
    bfx_cloud_process(&e->cloud, sr, v[BFX_CLOUD_MIX], v[BFX_CLOUD_SIZE],
                      v[BFX_CLOUD_DENSITY], v[BFX_CLOUD_PITCH], v[BFX_CLOUD_SPREAD],
                      v[BFX_CLOUD_REVERSE], v[BFX_CLOUD_FREEZE] >= 0.5f, l, r, n);
    bfx_tape_process(&e->tape, sr, v[BFX_TAPE_MIX], v[BFX_TAPE_TIME],
                     v[BFX_TAPE_FEEDBACK], v[BFX_TAPE_WOW], v[BFX_TAPE_TONE], l, r, n);
    bfx_space_process(&e->space, sr, v[BFX_SPACE_MIX], v[BFX_SPACE_DECAY],
                      v[BFX_SPACE_DAMP], v[BFX_SPACE_SHIMMER], l, r, n);

    float wet = v[BFX_MIX], dry = 1.0f - wet;
    float out = v[BFX_OUTPUT] * 2.0f;
    for (int i = 0; i < n; i++) {
        l[i] = dry * e->dry_l[i] + wet * l[i];
        r[i] = dry * e->dry_r[i] + wet * r[i];
    }

    /* post looper records and replays the finished sound */
    if (!loop_pre) {
        float rec_l[BLOCK], rec_r[BLOCK];
        memcpy(rec_l, l, (size_t)n * sizeof(float));
        memcpy(rec_r, r, (size_t)n * sizeof(float));
        bfx_looper_process(&e->looper, v[BFX_LOOP_LEVEL], rec_l, rec_r, l, r, n);
    }

    for (int i = 0; i < n; i++) {
        float yl = l[i] * out, yr = r[i] * out;
        /* NaN never leaves the engine */
        l[i] = (yl == yl) ? limit(yl) : 0.0f;
        r[i] = (yr == yr) ? limit(yr) : 0.0f;
    }
}

void bfx_process(bfx_engine *e, const float *in_l, const float *in_r,
                 float *out_l, float *out_r, int n)
{
    float l[BLOCK], r[BLOCK];
    for (int off = 0; off < n; off += BLOCK) {
        int m = (n - off < BLOCK) ? n - off : BLOCK;

        for (int p = 0; p < BFX_PARAM_COUNT; p++)
            e->value[p] += e->smooth_c * (e->target[p] - e->value[p]);

        memcpy(l, in_l + off, (size_t)m * sizeof(float));
        memcpy(r, in_r + off, (size_t)m * sizeof(float));
        process_block(e, l, r, m);
        memcpy(out_l + off, l, (size_t)m * sizeof(float));
        memcpy(out_r + off, r, (size_t)m * sizeof(float));
    }
}

/* ---- presets -------------------------------------------------------- */

typedef struct preset_value { int p; float v; } preset_value;
typedef struct preset {
    const char *name;
    preset_value v[16];
} preset;

#define END { -1, 0.0f }

static const preset k_presets[] = {
    { "Clean Bloom", {
        { BFX_SPACE_MIX, 0.35f }, { BFX_SPACE_DECAY, 0.45f }, { BFX_SPACE_DAMP, 0.5f },
        END } },
    { "Haze Cloud", {
        { BFX_CLOUD_MIX, 0.8f }, { BFX_CLOUD_SIZE, 0.6f }, { BFX_CLOUD_DENSITY, 0.8f },
        { BFX_CLOUD_PITCH, 0.0f }, { BFX_CLOUD_SPREAD, 0.4f },
        { BFX_SPACE_MIX, 0.5f }, { BFX_SPACE_DECAY, 0.6f }, END } },
    { "Octave Mosaic", {
        { BFX_CLOUD_MIX, 0.7f }, { BFX_CLOUD_SIZE, 0.35f }, { BFX_CLOUD_DENSITY, 0.6f },
        { BFX_CLOUD_PITCH, 0.2f }, { BFX_CLOUD_REVERSE, 0.3f }, { BFX_CLOUD_SPREAD, 0.5f },
        { BFX_TAPE_MIX, 0.3f }, { BFX_TAPE_TIME, 0.55f }, { BFX_TAPE_FEEDBACK, 0.45f },
        { BFX_SPACE_MIX, 0.3f }, END } },
    { "Sub Tunnel", {
        { BFX_CLOUD_MIX, 0.8f }, { BFX_CLOUD_SIZE, 0.7f }, { BFX_CLOUD_DENSITY, 0.6f },
        { BFX_CLOUD_PITCH, 0.35f },
        { BFX_SPACE_MIX, 0.5f }, { BFX_SPACE_DECAY, 0.75f }, { BFX_SPACE_DAMP, 0.8f },
        END } },
    { "Shimmer Halo", {
        { BFX_SPACE_MIX, 0.7f }, { BFX_SPACE_DECAY, 0.85f }, { BFX_SPACE_DAMP, 0.3f },
        { BFX_SPACE_SHIMMER, 0.7f }, { BFX_MOVE_TYPE, 0.3f }, { BFX_MOVE_DEPTH, 0.2f },
        END } },
    { "Warped Tape", {
        { BFX_CHAR_TYPE, 0.0f }, { BFX_CHAR_AMOUNT, 0.35f }, { BFX_CHAR_TONE, 0.6f },
        { BFX_TAPE_MIX, 0.6f }, { BFX_TAPE_TIME, 0.6f }, { BFX_TAPE_FEEDBACK, 0.6f },
        { BFX_TAPE_WOW, 0.8f }, { BFX_TAPE_TONE, 0.4f }, { BFX_SPACE_MIX, 0.2f }, END } },
    { "Crushed Pulse", {
        { BFX_CHAR_TYPE, 0.8f }, { BFX_CHAR_AMOUNT, 0.5f }, { BFX_CHAR_TONE, 0.7f },
        { BFX_MOVE_TYPE, 0.1f }, { BFX_MOVE_RATE, 0.6f }, { BFX_MOVE_DEPTH, 0.8f },
        { BFX_TAPE_MIX, 0.4f }, { BFX_TAPE_TIME, 0.45f }, { BFX_TAPE_FEEDBACK, 0.5f },
        END } },
    { "Glitch Reverse", {
        { BFX_CLOUD_MIX, 0.9f }, { BFX_CLOUD_SIZE, 0.2f }, { BFX_CLOUD_DENSITY, 0.5f },
        { BFX_CLOUD_REVERSE, 1.0f }, { BFX_CLOUD_SPREAD, 0.7f }, { BFX_CLOUD_PITCH, 0.7f },
        { BFX_CHAR_TYPE, 0.8f }, { BFX_CHAR_AMOUNT, 0.25f }, { BFX_MIX, 0.8f }, END } },
    { "Frozen Choir", {
        { BFX_CLOUD_MIX, 1.0f }, { BFX_CLOUD_SIZE, 0.8f }, { BFX_CLOUD_DENSITY, 1.0f },
        { BFX_CLOUD_PITCH, 0.7f }, { BFX_CLOUD_SPREAD, 0.3f },
        { BFX_SPACE_MIX, 0.6f }, { BFX_SPACE_DECAY, 0.9f }, { BFX_SPACE_SHIMMER, 0.3f },
        END } },
    { "Wobble Chorus", {
        { BFX_MOVE_TYPE, 0.3f }, { BFX_MOVE_RATE, 0.35f }, { BFX_MOVE_DEPTH, 0.7f },
        { BFX_TAPE_MIX, 0.35f }, { BFX_TAPE_TIME, 0.4f }, { BFX_TAPE_WOW, 0.5f },
        { BFX_SPACE_MIX, 0.25f }, END } },
    { "Filter Drift", {
        { BFX_MOVE_TYPE, 0.9f }, { BFX_MOVE_RATE, 0.3f }, { BFX_MOVE_DEPTH, 0.7f },
        { BFX_CHAR_TYPE, 0.4f }, { BFX_CHAR_AMOUNT, 0.3f },
        { BFX_TAPE_MIX, 0.4f }, { BFX_TAPE_TIME, 0.65f }, { BFX_TAPE_FEEDBACK, 0.55f },
        { BFX_SPACE_MIX, 0.3f }, END } },
};

#define PRESET_COUNT ((int)(sizeof(k_presets) / sizeof(k_presets[0])))

int bfx_preset_count(void) { return PRESET_COUNT; }

const char *bfx_preset_name(int i)
{
    return (i >= 0 && i < PRESET_COUNT) ? k_presets[i].name : "?";
}

void bfx_load_preset(bfx_engine *e, int i)
{
    if (i < 0 || i >= PRESET_COUNT) return;
    for (int p = 0; p < BFX_PARAM_COUNT; p++) {
        if (p == BFX_LOOP_LEVEL || p == BFX_LOOP_PRE || p == BFX_OUTPUT) continue;
        bfx_set_param(e, (bfx_param)p, k_params[p].def);
    }
    for (int k = 0; k < 16 && k_presets[i].v[k].p >= 0; k++)
        bfx_set_param(e, (bfx_param)k_presets[i].v[k].p, k_presets[i].v[k].v);
}
