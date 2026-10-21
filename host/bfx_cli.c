/* bfx: render audio through the BlackboxFX engine offline.
 *
 *   bfx --list
 *   bfx -p 1 in.wav out.wav
 *   bfx -p "Shimmer Halo" --set space_mix=0.9 --tail 6 in.wav out.wav
 *   bfx --demo -p 2 out.wav            (built-in plucked guitar phrase)
 *   bfx --demo --render-all demo_dir   (every preset, one file each)
 */
#include "bfx.h"
#include "wav.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_SETS 64

static void usage(void)
{
    fprintf(stderr,
        "usage: bfx [options] in.wav out.wav\n"
        "       bfx --demo [options] out.wav\n"
        "       bfx --demo --render-all out_dir\n"
        "options:\n"
        "  -p, --preset N|NAME   start from a preset (see --list)\n"
        "  --set name=value      set a parameter 0..1 (repeatable)\n"
        "  --tail SECONDS        render extra time after the input ends (default 4)\n"
        "  --device              use the small on-device memory configuration\n"
        "  --list                list presets and parameters\n");
}

static void list(void)
{
    printf("presets:\n");
    for (int i = 0; i < bfx_preset_count(); i++) printf("  %2d  %s\n", i, bfx_preset_name(i));
    printf("parameters (0..1):\n");
    for (int p = 0; p < BFX_PARAM_COUNT; p++) printf("  %s\n", bfx_param_name((bfx_param)p));
    bfx_config desk = bfx_default_config(), dev = bfx_device_config();
    printf("memory: desktop %zu bytes, device %zu bytes\n",
           bfx_memory_required(&desk), bfx_memory_required(&dev));
}

static int find_preset(const char *s)
{
    char *end;
    long n = strtol(s, &end, 10);
    if (*s && !*end) return (n >= 0 && n < bfx_preset_count()) ? (int)n : -1;
    for (int i = 0; i < bfx_preset_count(); i++)
        if (!strcmp(s, bfx_preset_name(i))) return i;
    return -1;
}

/* Karplus-Strong plucks: a short arpeggio a guitarist might play. */
static void make_demo(float sr, wav_audio *a)
{
    static const float notes[] = { 52, 59, 64, 67, 71, 64, 67, 76, 0, 0, 57, 64, 69, 72 };
    const int count = (int)(sizeof(notes) / sizeof(notes[0]));
    const float step = 0.32f;
    uint32_t frames = (uint32_t)(sr * (step * count + 1.5f));
    a->l = calloc(frames, sizeof(float));
    a->r = calloc(frames, sizeof(float));
    a->frames = frames;
    a->sample_rate = (uint32_t)sr;
    uint32_t seed = 1234567u;
    float buf[4096];
    for (int k = 0; k < count; k++) {
        if (notes[k] <= 0.0f) continue;
        float hz = 440.0f * powf(2.0f, (notes[k] - 69.0f) / 12.0f);
        int n = (int)(sr / hz);
        if (n < 2 || n > 4096) continue;
        for (int i = 0; i < n; i++) {
            seed = seed * 1664525u + 1013904223u;
            buf[i] = ((float)(seed >> 9) / 4194304.0f - 1.0f) * 0.5f;
        }
        uint32_t start = (uint32_t)(k * step * sr);
        float prev = 0.0f;
        for (uint32_t i = 0; start + i < frames && i < (uint32_t)(sr * 2.5f); i++) {
            int j = (int)(i % (uint32_t)n);
            float y = buf[j];
            buf[j] = 0.996f * 0.5f * (y + prev);
            prev = y;
            a->l[start + i] += y * 0.6f;
        }
    }
    memcpy(a->r, a->l, frames * sizeof(float));
}

static int render(const wav_audio *in, int preset, char sets[][128], int nsets,
                  float tail, int device, const char *out_path)
{
    bfx_config cfg = device ? bfx_device_config() : bfx_default_config();
    cfg.sample_rate = (float)in->sample_rate;
    size_t need = bfx_memory_required(&cfg);
    void *arena = malloc(need);
    void *emem = malloc(bfx_engine_size());
    bfx_engine *e = bfx_init(emem, arena, need, &cfg);
    if (!e) { fprintf(stderr, "engine init failed\n"); free(arena); free(emem); return 1; }

    if (preset >= 0) bfx_load_preset(e, preset);
    for (int i = 0; i < nsets; i++) {
        char *eq = strchr(sets[i], '=');
        if (!eq) { fprintf(stderr, "bad --set '%s'\n", sets[i]); return 1; }
        *eq = 0;
        int p = bfx_param_from_name(sets[i]);
        if (p < 0) { fprintf(stderr, "unknown parameter '%s'\n", sets[i]); return 1; }
        bfx_set_param(e, (bfx_param)p, (float)atof(eq + 1));
        *eq = '=';
    }
    bfx_reset(e);   /* start from settled parameter values */

    uint32_t total = in->frames + (uint32_t)(tail * (float)in->sample_rate);
    float *ol = calloc(total, sizeof(float)), *or_ = calloc(total, sizeof(float));
    float *il = calloc(total, sizeof(float)), *ir = calloc(total, sizeof(float));
    memcpy(il, in->l, in->frames * sizeof(float));
    memcpy(ir, in->r, in->frames * sizeof(float));
    bfx_process(e, il, ir, ol, or_, (int)total);

    int rc = wav_write24(out_path, ol, or_, total, in->sample_rate);
    if (rc) fprintf(stderr, "cannot write %s\n", out_path);
    else printf("wrote %s (%s)\n", out_path, preset >= 0 ? bfx_preset_name(preset) : "custom");
    free(ol); free(or_); free(il); free(ir); free(arena); free(emem);
    return rc ? 1 : 0;
}

int main(int argc, char **argv)
{
    int demo = 0, all = 0, device = 0, preset = -1, nsets = 0;
    float tail = 4.0f;
    static char sets[MAX_SETS][128];
    const char *pos[2] = { NULL, NULL };
    int npos = 0;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "--list")) { list(); return 0; }
        else if (!strcmp(a, "--demo")) demo = 1;
        else if (!strcmp(a, "--render-all")) all = 1;
        else if (!strcmp(a, "--device")) device = 1;
        else if ((!strcmp(a, "-p") || !strcmp(a, "--preset")) && i + 1 < argc) {
            preset = find_preset(argv[++i]);
            if (preset < 0) { fprintf(stderr, "unknown preset '%s'\n", argv[i]); return 1; }
        } else if (!strcmp(a, "--set") && i + 1 < argc && nsets < MAX_SETS) {
            snprintf(sets[nsets++], sizeof(sets[0]), "%s", argv[++i]);
        } else if (!strcmp(a, "--tail") && i + 1 < argc) {
            tail = (float)atof(argv[++i]);
        } else if (a[0] == '-' && a[1]) { usage(); return 1; }
        else if (npos < 2) pos[npos++] = a;
        else { usage(); return 1; }
    }

    wav_audio in;
    const char *out;
    if (demo) {
        if (npos != 1) { usage(); return 1; }
        make_demo(44100.0f, &in);
        out = pos[0];
    } else {
        if (npos != 2 || all) { usage(); return 1; }
        char err[128];
        if (wav_read(pos[0], &in, err, sizeof(err))) {
            fprintf(stderr, "%s: %s\n", pos[0], err);
            return 1;
        }
        out = pos[1];
    }

    int rc = 0;
    if (all) {
        char path[1024];
        snprintf(path, sizeof(path), "%s/00_dry.wav", out);
        wav_write24(path, in.l, in.r, in.frames, in.sample_rate);
        for (int p = 0; p < bfx_preset_count() && !rc; p++) {
            snprintf(path, sizeof(path), "%s/%02d_%s.wav", out, p + 1, bfx_preset_name(p));
            for (char *c = strrchr(path, '/') + 1; *c; c++) if (*c == ' ') *c = '_';
            rc = render(&in, p, sets, nsets, tail, device, path);
        }
    } else {
        rc = render(&in, preset, sets, nsets, tail, device, out);
    }
    wav_free(&in);
    return rc;
}
