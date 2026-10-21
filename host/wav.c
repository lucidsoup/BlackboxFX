#include "wav.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

static int fail(char *err, int n, const char *msg)
{
    snprintf(err, (size_t)n, "%s", msg);
    return -1;
}

int wav_read(const char *path, wav_audio *out, char *err, int err_len)
{
    memset(out, 0, sizeof(*out));
    FILE *f = fopen(path, "rb");
    if (!f) return fail(err, err_len, "cannot open file");

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 44) { fclose(f); return fail(err, err_len, "file too small"); }
    uint8_t *buf = (uint8_t *)malloc((size_t)size);
    if (!buf || fread(buf, 1, (size_t)size, f) != (size_t)size) {
        fclose(f); free(buf); return fail(err, err_len, "read error");
    }
    fclose(f);

    if (memcmp(buf, "RIFF", 4) || memcmp(buf + 8, "WAVE", 4)) {
        free(buf); return fail(err, err_len, "not a RIFF/WAVE file");
    }

    uint16_t fmt = 0, ch = 0, bits = 0;
    uint32_t rate = 0;
    const uint8_t *data = NULL;
    uint32_t data_len = 0;
    long off = 12;
    while (off + 8 <= size) {
        uint32_t len = rd32(buf + off + 4);
        const uint8_t *body = buf + off + 8;
        if (off + 8 + (long)len > size) len = (uint32_t)(size - off - 8);
        if (!memcmp(buf + off, "fmt ", 4) && len >= 16) {
            fmt = rd16(body);
            ch = rd16(body + 2);
            rate = rd32(body + 4);
            bits = rd16(body + 14);
            if (fmt == 0xFFFE && len >= 26) fmt = rd16(body + 24); /* extensible */
        } else if (!memcmp(buf + off, "data", 4)) {
            data = body;
            data_len = len;
        }
        off += 8 + (long)len + (len & 1u);
    }

    if (!data || !ch) { free(buf); return fail(err, err_len, "missing fmt or data chunk"); }
    if (ch > 2) { free(buf); return fail(err, err_len, "only mono or stereo supported"); }
    int pcm = fmt == 1 && (bits == 16 || bits == 24 || bits == 32);
    int flt = fmt == 3 && bits == 32;
    if (!pcm && !flt) { free(buf); return fail(err, err_len, "unsupported sample format"); }

    uint32_t bps = bits / 8u;
    uint32_t frames = data_len / (bps * ch);
    out->l = (float *)malloc(frames * sizeof(float) + 1);
    out->r = (float *)malloc(frames * sizeof(float) + 1);
    if (!out->l || !out->r) { free(buf); wav_free(out); return fail(err, err_len, "out of memory"); }

    for (uint32_t i = 0; i < frames; i++) {
        for (uint32_t c = 0; c < ch; c++) {
            const uint8_t *p = data + (i * ch + c) * bps;
            float v;
            if (flt) {
                uint32_t u = rd32(p);
                memcpy(&v, &u, 4);
            } else if (bits == 16) {
                v = (float)(int16_t)rd16(p) / 32768.0f;
            } else if (bits == 24) {
                int32_t s = (int32_t)((uint32_t)p[0] << 8 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 24) >> 8;
                v = (float)s / 8388608.0f;
            } else {
                v = (float)(int32_t)rd32(p) / 2147483648.0f;
            }
            if (c == 0) out->l[i] = v;
            else out->r[i] = v;
        }
        if (ch == 1) out->r[i] = out->l[i];
    }
    out->frames = frames;
    out->sample_rate = rate;
    free(buf);
    return 0;
}

static void wr32(FILE *f, uint32_t v)
{
    uint8_t b[4] = { (uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24) };
    fwrite(b, 1, 4, f);
}

static void wr16(FILE *f, uint16_t v)
{
    uint8_t b[2] = { (uint8_t)v, (uint8_t)(v >> 8) };
    fwrite(b, 1, 2, f);
}

int wav_write24(const char *path, const float *l, const float *r,
                uint32_t frames, uint32_t sample_rate)
{
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    uint32_t data_len = frames * 6u;
    fwrite("RIFF", 1, 4, f); wr32(f, 36u + data_len); fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f); wr32(f, 16); wr16(f, 1); wr16(f, 2);
    wr32(f, sample_rate); wr32(f, sample_rate * 6u); wr16(f, 6); wr16(f, 24);
    fwrite("data", 1, 4, f); wr32(f, data_len);
    for (uint32_t i = 0; i < frames; i++) {
        const float *ch[2] = { l, r };
        for (int c = 0; c < 2; c++) {
            float v = ch[c][i];
            v = v > 1.0f ? 1.0f : (v < -1.0f ? -1.0f : v);
            int32_t s = (int32_t)(v * 8388607.0f);
            uint8_t b[3] = { (uint8_t)s, (uint8_t)(s >> 8), (uint8_t)(s >> 16) };
            fwrite(b, 1, 3, f);
        }
    }
    int ok = !ferror(f);
    fclose(f);
    return ok ? 0 : -1;
}

void wav_free(wav_audio *a)
{
    free(a->l);
    free(a->r);
    a->l = a->r = NULL;
    a->frames = 0;
}
