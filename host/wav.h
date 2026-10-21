/* Minimal WAV reader/writer: PCM 16/24/32-bit and float32, mono or stereo. */
#ifndef BFX_WAV_H
#define BFX_WAV_H

#include <stdint.h>

typedef struct wav_audio {
    float *l, *r;       /* mono files are duplicated into both channels */
    uint32_t frames;
    uint32_t sample_rate;
} wav_audio;

/* Returns 0 on success; on failure fills err with a reason. */
int  wav_read(const char *path, wav_audio *out, char *err, int err_len);
/* Writes 24-bit stereo PCM, the BlackBox's native USB format. */
int  wav_write24(const char *path, const float *l, const float *r,
                 uint32_t frames, uint32_t sample_rate);
void wav_free(wav_audio *a);

#endif
