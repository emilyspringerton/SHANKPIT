#include "audio_wav.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t rd32(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

int audio_wav_load_stereo16(const char *path, int16_t **out_stereo) {
    *out_stereo = NULL;
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 44 || size > (32L << 20)) { fclose(f); return 0; }
    uint8_t *b = (uint8_t *)malloc((size_t)size);
    if (!b || fread(b, 1, (size_t)size, f) != (size_t)size) { free(b); fclose(f); return 0; }
    fclose(f);
    if (memcmp(b, "RIFF", 4) != 0 || memcmp(b + 8, "WAVE", 4) != 0) { free(b); return 0; }
    int channels = 0, bits = 0; uint32_t rate = 0;
    const uint8_t *data = NULL; uint32_t data_len = 0;
    long off = 12;
    while (off + 8 <= size) {
        uint32_t clen = rd32(b + off + 4);
        const uint8_t *body = b + off + 8;
        if ((long)clen > size - off - 8) clen = (uint32_t)(size - off - 8);
        if (memcmp(b + off, "fmt ", 4) == 0 && clen >= 16) {
            if (rd16(body) != 1) { free(b); return 0; }           /* PCM only */
            channels = rd16(body + 2); rate = rd32(body + 4); bits = rd16(body + 14);
        } else if (memcmp(b + off, "data", 4) == 0) {
            data = body; data_len = clen;
            break;
        }
        off += 8 + (long)clen + (clen & 1);
    }
    if (!data || bits != 16 || (channels != 1 && channels != 2) || rate != AUDIO_WAV_RATE) { free(b); return 0; }
    int frames = (int)(data_len / (uint32_t)(2 * channels));
    if (frames <= 0) { free(b); return 0; }
    int16_t *out = (int16_t *)malloc((size_t)frames * 2 * sizeof(int16_t));
    if (!out) { free(b); return 0; }
    for (int i = 0; i < frames; i++) {
        int16_t l = (int16_t)rd16(data + (size_t)i * 2 * channels);
        int16_t r = channels == 2 ? (int16_t)rd16(data + (size_t)i * 4 + 2) : l;
        out[i * 2] = l; out[i * 2 + 1] = r;
    }
    free(b);
    *out_stereo = out;
    return frames;
}

int audio_wav_seek_frame(unsigned int seek_ms, int frames) {
    long long f = (long long)seek_ms * AUDIO_WAV_RATE / 1000;
    if (f < 0) f = 0;
    if (f > frames) f = frames;
    return (int)f;
}
